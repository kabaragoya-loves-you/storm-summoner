#include "buttons.h"
#include "event_bus.h"
#include "screensaver.h"
#include "app_settings.h"
#include "io.h"
#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#define TAG "BUTTONS"
#define NVS_KEY_CHORD_WINDOW "btn_chord_ms"
#define NVS_KEY_DEBOUNCE "btn_debounce"
#define NVS_KEY_LONG_PRESS "btn_long_ms"
#define NVS_KEY_GLITCH_FILTER "btn_glitch"
#define NVS_KEY_GLITCH_WINDOW "btn_glitch_ns"

// Glitch filter modes
// Mode 0: No glitch filter (hysteresis only)
// Mode 1: Simple glitch filter (filters based on APB clock cycles - very short pulses)
// Mode 2: Flex glitch filter (configurable window in nanoseconds)
#define GLITCH_FILTER_MODE_NONE   0
#define GLITCH_FILTER_MODE_SIMPLE 1
#define GLITCH_FILTER_MODE_FLEX   2

// Flex glitch filter window limits (ESP32-P4 hardware constraint)
// The flex filter uses a counter clocked by the source clock.
// Maximum window depends on clock frequency and counter width.
// Practical range is roughly 100ns - 4000ns (0.1µs - 4µs)
#define GLITCH_FILTER_WINDOW_DEFAULT_NS 1000   // 1µs default for flex filter
#define GLITCH_FILTER_WINDOW_MIN_NS     100    // 100ns minimum
#define GLITCH_FILTER_WINDOW_MAX_NS     4000   // 4µs maximum (hardware limit)

// Button state tracking
typedef struct {
  bool pressed;
  uint32_t press_timestamp;
  uint32_t last_isr_timestamp;
  TimerHandle_t long_press_timer;
  TimerHandle_t chord_timer;
  bool long_press_fired;
} button_internal_state_t;

static button_internal_state_t g_button_left = {0};
static button_internal_state_t g_button_right = {0};
static bool g_both_pressed = false;
static bool g_initialized = false;
static uint16_t g_chord_window_ms = BUTTON_CHORD_WINDOW_MS_DEFAULT;
static uint16_t g_debounce_ms = BUTTON_DEBOUNCE_MS_DEFAULT;
static uint16_t g_long_press_ms = BUTTON_LONG_PRESS_MS_DEFAULT;
static uint8_t g_first_button_pressed = 0xFF; // 0xFF = none, 0 = left, 1 = right
static bool g_logging_enabled = false;

// Glitch filter state
static uint8_t g_glitch_filter_mode = GLITCH_FILTER_MODE_NONE;
static uint32_t g_glitch_filter_window_ns = GLITCH_FILTER_WINDOW_DEFAULT_NS;
static gpio_glitch_filter_handle_t g_filter_left = NULL;
static gpio_glitch_filter_handle_t g_filter_right = NULL;

// Forward declarations
static void button_l_isr_handler(void* arg);
static void button_r_isr_handler(void* arg);
static void button_process_press(uint8_t button_id);
static void button_process_release(uint8_t button_id);
static void button_long_press_timer_callback(TimerHandle_t xTimer);
static void button_chord_timer_callback(TimerHandle_t xTimer);
static esp_err_t apply_glitch_filter(void);

esp_err_t buttons_init(bool enable_logging) {
  if (g_initialized) {
    ESP_LOGW(TAG, "Already initialized");
    return ESP_OK;
  }

  g_logging_enabled = enable_logging;

  // Load chord window setting from NVS
  uint16_t chord_window;
  esp_err_t ret = app_settings_load_u16(NVS_KEY_CHORD_WINDOW, &chord_window);
  if (ret == ESP_OK) {
    if (chord_window <= BUTTON_CHORD_WINDOW_MS_MAX) {
      g_chord_window_ms = chord_window;
    } else {
      ESP_LOGW(TAG, "Invalid chord window in NVS: %u, using default: %u", chord_window, BUTTON_CHORD_WINDOW_MS_DEFAULT);
      g_chord_window_ms = BUTTON_CHORD_WINDOW_MS_DEFAULT;
      app_settings_save_u16(NVS_KEY_CHORD_WINDOW, g_chord_window_ms);
    }
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    app_settings_save_u16(NVS_KEY_CHORD_WINDOW, g_chord_window_ms);
  } else {
    ESP_LOGW(TAG, "Failed to load chord window: %s, using default: %u ms", esp_err_to_name(ret), g_chord_window_ms);
  }

  // Load debounce setting from NVS
  uint16_t debounce;
  ret = app_settings_load_u16(NVS_KEY_DEBOUNCE, &debounce);
  if (ret == ESP_OK) {
    if (debounce <= BUTTON_DEBOUNCE_MS_MAX) {
      g_debounce_ms = debounce;
    } else {
      ESP_LOGW(TAG, "Invalid debounce in NVS: %u, using default: %u", debounce, BUTTON_DEBOUNCE_MS_DEFAULT);
      g_debounce_ms = BUTTON_DEBOUNCE_MS_DEFAULT;
      app_settings_save_u16(NVS_KEY_DEBOUNCE, g_debounce_ms);
    }
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    app_settings_save_u16(NVS_KEY_DEBOUNCE, g_debounce_ms);
  } else {
    ESP_LOGW(TAG, "Failed to load debounce: %s, using default: %u ms", esp_err_to_name(ret), g_debounce_ms);
  }

  // Load long press threshold from NVS
  uint16_t long_press;
  ret = app_settings_load_u16(NVS_KEY_LONG_PRESS, &long_press);
  if (ret == ESP_OK) {
    if (long_press >= BUTTON_LONG_PRESS_MS_MIN && long_press <= BUTTON_LONG_PRESS_MS_MAX) {
      g_long_press_ms = long_press;
    } else {
      ESP_LOGW(TAG, "Invalid long press in NVS: %u, using default: %u", long_press, BUTTON_LONG_PRESS_MS_DEFAULT);
      g_long_press_ms = BUTTON_LONG_PRESS_MS_DEFAULT;
      app_settings_save_u16(NVS_KEY_LONG_PRESS, g_long_press_ms);
    }
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    app_settings_save_u16(NVS_KEY_LONG_PRESS, g_long_press_ms);
  } else {
    ESP_LOGW(TAG, "Failed to load long press: %s, using default: %u ms", esp_err_to_name(ret), g_long_press_ms);
  }

  // Load glitch filter mode from NVS
  uint8_t glitch_mode;
  ret = app_settings_load_u8(NVS_KEY_GLITCH_FILTER, &glitch_mode);
  if (ret == ESP_OK && glitch_mode <= GLITCH_FILTER_MODE_FLEX) {
    g_glitch_filter_mode = glitch_mode;
  }

  // Load glitch filter window from NVS
  uint32_t glitch_window;
  ret = app_settings_load_u32(NVS_KEY_GLITCH_WINDOW, &glitch_window);
  if (ret == ESP_OK && glitch_window >= GLITCH_FILTER_WINDOW_MIN_NS && 
      glitch_window <= GLITCH_FILTER_WINDOW_MAX_NS) {
    g_glitch_filter_window_ns = glitch_window;
  }

  // Configure GPIO pins
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_BUTTON_L) | (1ULL << PIN_BUTTON_R),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_ANYEDGE,  // Both edges for press and release detection
    .hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE  // Enable hysteresis for debouncing
  };
  
  ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(ret));
    return ret;
  }

  // Create long press timers
  g_button_left.long_press_timer = xTimerCreate(
    "ButtonLLongPress",
    pdMS_TO_TICKS(g_long_press_ms),
    pdFALSE,  // One-shot
    (void*)BUTTON_ID_LEFT,
    button_long_press_timer_callback
  );

  g_button_right.long_press_timer = xTimerCreate(
    "ButtonRLongPress",
    pdMS_TO_TICKS(g_long_press_ms),
    pdFALSE,  // One-shot
    (void*)BUTTON_ID_RIGHT,
    button_long_press_timer_callback
  );

  // Create chord detection timers
  g_button_left.chord_timer = xTimerCreate(
    "ButtonLChord",
    pdMS_TO_TICKS(g_chord_window_ms),
    pdFALSE,  // One-shot
    (void*)BUTTON_ID_LEFT,
    button_chord_timer_callback
  );

  g_button_right.chord_timer = xTimerCreate(
    "ButtonRChord",
    pdMS_TO_TICKS(g_chord_window_ms),
    pdFALSE,  // One-shot
    (void*)BUTTON_ID_RIGHT,
    button_chord_timer_callback
  );

  if (!g_button_left.long_press_timer || !g_button_right.long_press_timer ||
      !g_button_left.chord_timer || !g_button_right.chord_timer) {
    ESP_LOGE(TAG, "Failed to create timers");
    return ESP_ERR_NO_MEM;
  }

  // Add ISR handlers
  ret = gpio_isr_handler_add(PIN_BUTTON_L, button_l_isr_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add left button ISR: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = gpio_isr_handler_add(PIN_BUTTON_R, button_r_isr_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add right button ISR: %s", esp_err_to_name(ret));
    gpio_isr_handler_remove(PIN_BUTTON_L);
    return ret;
  }

  // Apply glitch filter if configured
  if (g_glitch_filter_mode != GLITCH_FILTER_MODE_NONE) {
    ret = apply_glitch_filter();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to apply glitch filter: %s", esp_err_to_name(ret));
      g_glitch_filter_mode = GLITCH_FILTER_MODE_NONE;
    }
  }

  g_initialized = true;
  
  const char* filter_mode_str = (g_glitch_filter_mode == GLITCH_FILTER_MODE_NONE) ? "none" :
    (g_glitch_filter_mode == GLITCH_FILTER_MODE_SIMPLE) ? "simple" : "flex";
  ESP_LOGI(TAG, "Buttons initialized (L: GPIO%d, R: GPIO%d, debounce: %ums, long: %ums, chord: %ums, glitch: %s)", 
    PIN_BUTTON_L, PIN_BUTTON_R, g_debounce_ms, g_long_press_ms, g_chord_window_ms, filter_mode_str);
  
  return ESP_OK;
}

button_state_t buttons_get_state(void) {
  button_state_t state = {
    .left_pressed = g_button_left.pressed,
    .right_pressed = g_button_right.pressed,
    .both_pressed = g_both_pressed
  };
  return state;
}

// ISR handler for left button
static void IRAM_ATTR button_l_isr_handler(void* arg) {
  uint32_t now = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
  
  // Debounce check
  if (now - g_button_left.last_isr_timestamp < g_debounce_ms) {
    return;
  }
  g_button_left.last_isr_timestamp = now;

  // Read current state (active low)
  int level = gpio_get_level(PIN_BUTTON_L);
  
  if (level == 0) {  // Button pressed (active low)
    button_process_press(BUTTON_ID_LEFT);
  } else {  // Button released
    button_process_release(BUTTON_ID_LEFT);
  }
}

// ISR handler for right button
static void IRAM_ATTR button_r_isr_handler(void* arg) {
  uint32_t now = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
  
  // Debounce check
  if (now - g_button_right.last_isr_timestamp < g_debounce_ms) {
    return;
  }
  g_button_right.last_isr_timestamp = now;

  // Read current state (active low)
  int level = gpio_get_level(PIN_BUTTON_R);
  
  if (level == 0) {  // Button pressed (active low)
    button_process_press(BUTTON_ID_RIGHT);
  } else {  // Button released
    button_process_release(BUTTON_ID_RIGHT);
  }
}

// Process button press (called from ISR context)
static void button_process_press(uint8_t button_id) {
  uint32_t now = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
  button_internal_state_t* button = (button_id == BUTTON_ID_LEFT) ? &g_button_left : &g_button_right;
  button_internal_state_t* other_button = (button_id == BUTTON_ID_LEFT) ? &g_button_right : &g_button_left;
  
  button->pressed = true;
  button->press_timestamp = now;
  button->long_press_fired = false;

  BaseType_t higher_priority_woken = pdFALSE;

  // Check if the other button is already pressed (chord detected)
  if (other_button->pressed) {
    // Second button pressed - cancel the first button's chord timer
    xTimerStopFromISR(other_button->chord_timer, &higher_priority_woken);
    
    // Stop individual long press timers
    xTimerStopFromISR(g_button_left.long_press_timer, &higher_priority_woken);
    xTimerStopFromISR(g_button_right.long_press_timer, &higher_priority_woken);
    
    g_both_pressed = true;
    g_first_button_pressed = 0xFF; // Clear first button tracking
    
    // Post both-press event immediately
    event_t event = {
      .type = EVENT_BUTTON_BOTH_PRESS,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = now,
      .data.button.button_id = BUTTON_ID_BOTH,
      .data.button.duration_ms = 0
    };
    event_bus_post_from_isr(&event, &higher_priority_woken);
    
    if (g_logging_enabled) {
      ESP_EARLY_LOGI(TAG, "Both buttons pressed");
    }
  } else {
    // First button pressed - start chord detection timer if enabled
    g_first_button_pressed = button_id;
    
    if (g_chord_window_ms > 0) {
      // Start chord timer - single button event will fire when timer expires
      xTimerStartFromISR(button->chord_timer, &higher_priority_woken);
      if (g_logging_enabled) {
        ESP_EARLY_LOGD(TAG, "Button %s pressed, chord window started", 
          (button_id == BUTTON_ID_LEFT) ? "LEFT" : "RIGHT");
      }
    } else {
      // Chord detection disabled - fire single button event immediately
      event_t event = {
        .type = (button_id == BUTTON_ID_LEFT) ? EVENT_BUTTON_L_PRESS : EVENT_BUTTON_R_PRESS,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = now,
        .data.button.button_id = button_id,
        .data.button.duration_ms = 0
      };
      event_bus_post_from_isr(&event, &higher_priority_woken);
      
      // Start long press timer
      xTimerStartFromISR(button->long_press_timer, &higher_priority_woken);
      
      if (g_logging_enabled) ESP_EARLY_LOGI(TAG, "Button %s pressed", (button_id == BUTTON_ID_LEFT) ? "LEFT" : "RIGHT");
    }
  }

  // Notify screensaver of activity (ISR-safe version)
  screensaver_notify_activity_from_isr(&higher_priority_woken);
  
  portYIELD_FROM_ISR(higher_priority_woken);
}

// Process button release (called from ISR context)
static void button_process_release(uint8_t button_id) {
  button_internal_state_t* button = (button_id == BUTTON_ID_LEFT) ? &g_button_left : &g_button_right;
  button_internal_state_t* other_button = (button_id == BUTTON_ID_LEFT) ? &g_button_right : &g_button_left;
  
  button->pressed = false;
  
  // Stop timers
  BaseType_t higher_priority_woken = pdFALSE;
  xTimerStopFromISR(button->long_press_timer, &higher_priority_woken);
  xTimerStopFromISR(button->chord_timer, &higher_priority_woken);
  
  // Clear both-pressed state if it was set
  if (g_both_pressed) {
    g_both_pressed = false;
    g_first_button_pressed = 0xFF;
    // Don't log individual releases when both were pressed
  } else if (g_logging_enabled && !other_button->pressed) {
    // Only log single button release if the other button isn't pressed
    // (prevents logging when releasing second button after both were pressed)
    ESP_EARLY_LOGD(TAG, "Button %s released", (button_id == BUTTON_ID_LEFT) ? "LEFT" : "RIGHT");
  }
  
  portYIELD_FROM_ISR(higher_priority_woken);
}

// Long press timer callback
static void button_long_press_timer_callback(TimerHandle_t xTimer) {
  uint8_t button_id = (uint8_t)(uintptr_t)pvTimerGetTimerID(xTimer);
  button_internal_state_t* button = (button_id == BUTTON_ID_LEFT) ? &g_button_left : &g_button_right;
  
  // Only fire if button is still pressed and we haven't already fired
  if (button->pressed && !button->long_press_fired) {
    button->long_press_fired = true;
    
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t duration = now - button->press_timestamp;
    
    // Check if both are pressed for long press
    if (g_both_pressed) {
      event_t event = {
        .type = EVENT_BUTTON_BOTH_LONG_PRESS,
        .priority = EVENT_PRIORITY_HIGH,
        .timestamp = now,
        .data.button.button_id = BUTTON_ID_BOTH,
        .data.button.duration_ms = duration
      };
      event_bus_post(&event);
      
      if (g_logging_enabled) {
        ESP_LOGI(TAG, "Both buttons long press detected (%lu ms)", (unsigned long)duration);
      }
    } else {
      event_t event = {
        .type = (button_id == BUTTON_ID_LEFT) ? EVENT_BUTTON_L_LONG_PRESS : EVENT_BUTTON_R_LONG_PRESS,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = now,
        .data.button.button_id = button_id,
        .data.button.duration_ms = duration
      };
      event_bus_post(&event);
      
      if (g_logging_enabled) {
        ESP_LOGI(TAG, "Button %s long press detected (%lu ms)", 
          (button_id == BUTTON_ID_LEFT) ? "LEFT" : "RIGHT", (unsigned long)duration);
      }
    }
    
    // Notify screensaver of activity
    screensaver_notify_activity();
  }
}

// Chord timer callback - fires when chord window expires without second button press
static void button_chord_timer_callback(TimerHandle_t xTimer) {
  uint8_t button_id = (uint8_t)(uintptr_t)pvTimerGetTimerID(xTimer);
  button_internal_state_t* button = (button_id == BUTTON_ID_LEFT) ? &g_button_left : &g_button_right;
  
  // Only fire if button is still pressed and no chord was detected
  if (button->pressed && !g_both_pressed) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Fire single button press event
    event_t event = {
      .type = (button_id == BUTTON_ID_LEFT) ? EVENT_BUTTON_L_PRESS : EVENT_BUTTON_R_PRESS,
      .priority = EVENT_PRIORITY_NORMAL,
      .timestamp = now,
      .data.button.button_id = button_id,
      .data.button.duration_ms = 0
    };
    event_bus_post(&event);
    
    // Start long press timer now
    xTimerStart(button->long_press_timer, 0);
    
    if (g_logging_enabled) {
      ESP_LOGI(TAG, "Button %s pressed", 
        (button_id == BUTTON_ID_LEFT) ? "LEFT" : "RIGHT");
    }
  }
}

// Getter/setter functions
esp_err_t buttons_set_chord_window(uint16_t window_ms) {
  if (window_ms > BUTTON_CHORD_WINDOW_MS_MAX) {
    ESP_LOGE(TAG, "Invalid chord window: %u ms (max: %u)", window_ms, BUTTON_CHORD_WINDOW_MS_MAX);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_chord_window_ms = window_ms;
  
  // Update timer periods if timers exist
  if (g_button_left.chord_timer && g_button_right.chord_timer) {
    TickType_t new_period = pdMS_TO_TICKS(window_ms);
    xTimerChangePeriod(g_button_left.chord_timer, new_period, portMAX_DELAY);
    xTimerChangePeriod(g_button_right.chord_timer, new_period, portMAX_DELAY);
  }
  
  // Save to NVS
  esp_err_t ret = app_settings_save_u16(NVS_KEY_CHORD_WINDOW, window_ms);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to save chord window to NVS: %s", esp_err_to_name(ret));
  }
  
  ESP_LOGI(TAG, "Chord window set to %u ms", window_ms);
  return ESP_OK;
}

uint16_t buttons_get_chord_window(void) {
  return g_chord_window_ms;
}

esp_err_t buttons_set_debounce(uint16_t debounce_ms) {
  if (debounce_ms > BUTTON_DEBOUNCE_MS_MAX) {
    ESP_LOGE(TAG, "Invalid debounce: %u ms (max: %u)", debounce_ms, BUTTON_DEBOUNCE_MS_MAX);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_debounce_ms = debounce_ms;
  
  // Save to NVS
  esp_err_t ret = app_settings_save_u16(NVS_KEY_DEBOUNCE, debounce_ms);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to save debounce to NVS: %s", esp_err_to_name(ret));
  }
  
  ESP_LOGI(TAG, "Debounce set to %u ms", debounce_ms);
  return ESP_OK;
}

uint16_t buttons_get_debounce(void) {
  return g_debounce_ms;
}

esp_err_t buttons_set_long_press_threshold(uint16_t long_press_ms) {
  if (long_press_ms < BUTTON_LONG_PRESS_MS_MIN || long_press_ms > BUTTON_LONG_PRESS_MS_MAX) {
    ESP_LOGE(TAG, "Invalid long press threshold: %u ms (range: %u-%u)", 
      long_press_ms, BUTTON_LONG_PRESS_MS_MIN, BUTTON_LONG_PRESS_MS_MAX);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_long_press_ms = long_press_ms;
  
  // Update timer periods if timers exist
  if (g_button_left.long_press_timer && g_button_right.long_press_timer) {
    TickType_t new_period = pdMS_TO_TICKS(long_press_ms);
    xTimerChangePeriod(g_button_left.long_press_timer, new_period, portMAX_DELAY);
    xTimerChangePeriod(g_button_right.long_press_timer, new_period, portMAX_DELAY);
  }
  
  // Save to NVS
  esp_err_t ret = app_settings_save_u16(NVS_KEY_LONG_PRESS, long_press_ms);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to save long press threshold to NVS: %s", esp_err_to_name(ret));
  }
  
  ESP_LOGI(TAG, "Long press threshold set to %u ms", long_press_ms);
  return ESP_OK;
}

uint16_t buttons_get_long_press_threshold(void) {
  return g_long_press_ms;
}

bool buttons_check_boot_right(void) {
  // Configure Right button GPIO as input with pull-up (no ISR)
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_BUTTON_R),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  
  // Read the button state (active low)
  int level = gpio_get_level(PIN_BUTTON_R);
  bool pressed = (level == 0);
  
  if (pressed) {
    ESP_LOGI(TAG, "Right button held at boot");
  }
  
  return pressed;
}

// Helper to remove existing glitch filters
static void remove_glitch_filters(void) {
  if (g_filter_left) {
    gpio_glitch_filter_disable(g_filter_left);
    gpio_del_glitch_filter(g_filter_left);
    g_filter_left = NULL;
  }
  if (g_filter_right) {
    gpio_glitch_filter_disable(g_filter_right);
    gpio_del_glitch_filter(g_filter_right);
    g_filter_right = NULL;
  }
}

// Apply glitch filter based on current mode
static esp_err_t apply_glitch_filter(void) {
  esp_err_t ret = ESP_OK;
  
  // First remove any existing filters
  remove_glitch_filters();
  
  if (g_glitch_filter_mode == GLITCH_FILTER_MODE_NONE) {
    ESP_LOGI(TAG, "Glitch filter disabled");
    return ESP_OK;
  }
  
  if (g_glitch_filter_mode == GLITCH_FILTER_MODE_SIMPLE) {
    // Simple glitch filter - uses APB clock cycles
    gpio_pin_glitch_filter_config_t cfg_l = {
      .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
      .gpio_num = PIN_BUTTON_L,
    };
    gpio_pin_glitch_filter_config_t cfg_r = {
      .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
      .gpio_num = PIN_BUTTON_R,
    };
    
    ret = gpio_new_pin_glitch_filter(&cfg_l, &g_filter_left);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create simple glitch filter for left button: %s", esp_err_to_name(ret));
      return ret;
    }
    
    ret = gpio_new_pin_glitch_filter(&cfg_r, &g_filter_right);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create simple glitch filter for right button: %s", esp_err_to_name(ret));
      gpio_del_glitch_filter(g_filter_left);
      g_filter_left = NULL;
      return ret;
    }
    
    gpio_glitch_filter_enable(g_filter_left);
    gpio_glitch_filter_enable(g_filter_right);
    
    ESP_LOGI(TAG, "Simple glitch filter enabled");
  } else if (g_glitch_filter_mode == GLITCH_FILTER_MODE_FLEX) {
    // Flex glitch filter - configurable window
    gpio_flex_glitch_filter_config_t cfg_l = {
      .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
      .gpio_num = PIN_BUTTON_L,
      .window_thres_ns = g_glitch_filter_window_ns,
      .window_width_ns = g_glitch_filter_window_ns,
    };
    gpio_flex_glitch_filter_config_t cfg_r = {
      .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
      .gpio_num = PIN_BUTTON_R,
      .window_thres_ns = g_glitch_filter_window_ns,
      .window_width_ns = g_glitch_filter_window_ns,
    };
    
    ret = gpio_new_flex_glitch_filter(&cfg_l, &g_filter_left);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create flex glitch filter for left button: %s", esp_err_to_name(ret));
      return ret;
    }
    
    ret = gpio_new_flex_glitch_filter(&cfg_r, &g_filter_right);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create flex glitch filter for right button: %s", esp_err_to_name(ret));
      gpio_del_glitch_filter(g_filter_left);
      g_filter_left = NULL;
      return ret;
    }
    
    gpio_glitch_filter_enable(g_filter_left);
    gpio_glitch_filter_enable(g_filter_right);
    
    ESP_LOGI(TAG, "Flex glitch filter enabled (window: %lu ns)", (unsigned long)g_glitch_filter_window_ns);
  }
  
  return ESP_OK;
}

uint8_t buttons_get_glitch_filter_mode(void) {
  return g_glitch_filter_mode;
}

uint32_t buttons_get_glitch_filter_window_ns(void) {
  return g_glitch_filter_window_ns;
}

esp_err_t buttons_set_glitch_filter(uint8_t mode, uint32_t window_ns) {
  if (mode > GLITCH_FILTER_MODE_FLEX) {
    ESP_LOGE(TAG, "Invalid glitch filter mode: %u (0=none, 1=simple, 2=flex)", mode);
    return ESP_ERR_INVALID_ARG;
  }
  
  if (mode == GLITCH_FILTER_MODE_FLEX && 
      (window_ns < GLITCH_FILTER_WINDOW_MIN_NS || window_ns > GLITCH_FILTER_WINDOW_MAX_NS)) {
    ESP_LOGE(TAG, "Invalid glitch filter window: %lu ns (range: %d-%d)", 
      (unsigned long)window_ns, GLITCH_FILTER_WINDOW_MIN_NS, GLITCH_FILTER_WINDOW_MAX_NS);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_glitch_filter_mode = mode;
  if (mode == GLITCH_FILTER_MODE_FLEX) {
    g_glitch_filter_window_ns = window_ns;
  }
  
  // Apply the new filter settings
  esp_err_t ret = apply_glitch_filter();
  if (ret != ESP_OK) {
    return ret;
  }
  
  // Save to NVS
  app_settings_save_u8(NVS_KEY_GLITCH_FILTER, mode);
  if (mode == GLITCH_FILTER_MODE_FLEX) {
    app_settings_save_u32(NVS_KEY_GLITCH_WINDOW, window_ns);
  }
  
  return ESP_OK;
}
