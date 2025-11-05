#include "led.h"
#include "io.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_random.h"
#include "esp_log.h"
#include "task_priorities.h"
#include "app_settings.h"

// Forward declaration for event handler init
void led_event_handler_init(void);

#define TAG "led"

// NVS keys
#define LED_ENABLED_KEY "led_enabled"
#define LED_MODE_KEY "led_mode"
#define LED_SUNDIAL_KEY "led_sundial"
#define LED_TEMPO_SYNC_KEY "led_tempo"

static TaskHandle_t flicker_task_handle = NULL;
static bool led_enabled = true;
static bool solid_on_mode = false;
static led_mode_t led_mode = LED_MODE_DAYLIGHT;
static bool sundial_mode = true;   // Default: sundial on for magical first experience
static bool tempo_sync_enabled = false;

// Sundial mode thresholds (ALS CC value 0-127)
#define ALS_DARK_THRESHOLD 32    // Below this = nighttime
#define ALS_LIGHT_THRESHOLD 64   // Above this = daylight
// Hysteresis prevents rapid switching

// Handle ALS events for sundial mode
static void als_event_handler(const event_t* event, void* context) {
  if (!sundial_mode) return;
  if (event->type != EVENT_SENSOR_ALS) return;
  
  uint8_t als_value = event->data.sensor.value;
  
  // Switch to nighttime if dark
  if (als_value < ALS_DARK_THRESHOLD && led_mode == LED_MODE_DAYLIGHT) {
    ESP_LOGI(TAG, "Sundial: Switching to nighttime mode (ALS=%d)", als_value);
    led_set_mode(LED_MODE_NIGHTTIME);
  }
  // Switch to daylight if bright
  else if (als_value > ALS_LIGHT_THRESHOLD && led_mode == LED_MODE_NIGHTTIME) {
    ESP_LOGI(TAG, "Sundial: Switching to daylight mode (ALS=%d)", als_value);
    led_set_mode(LED_MODE_DAYLIGHT);
  }
}

// Get the actual GPIO level based on mode (daylight vs nighttime inversion)
static int get_gpio_level_for_on(void) {
  return (led_mode == LED_MODE_DAYLIGHT) ? 1 : 0;  // Nighttime inverts
}

static int get_gpio_level_for_off(void) {
  return (led_mode == LED_MODE_DAYLIGHT) ? 0 : 1;  // Nighttime inverts
}

void led_set_on(void) {
  if (!led_enabled) return;
  solid_on_mode = true;
  gpio_set_level(PIN_LED, get_gpio_level_for_on());
}

void led_set_off(void) {
  solid_on_mode = false;
  gpio_set_level(PIN_LED, get_gpio_level_for_off());
}

void flash_led(uint32_t duration) {
  if (!led_enabled || solid_on_mode) return;
  
  // In daylight mode: turn on, then off
  // In nighttime mode: turn off, then on (inverted)
  gpio_set_level(PIN_LED, get_gpio_level_for_on());
  
  // For very short flashes, do synchronous
  if (duration < 10) {
    vTaskDelay(pdMS_TO_TICKS(duration));
    gpio_set_level(PIN_LED, get_gpio_level_for_off());
  }
  // For longer flashes, this is called from LED event handler task, so it's OK to block
  else if (duration < 60000) {  // Less than 1 minute
    vTaskDelay(pdMS_TO_TICKS(duration));
    if (!solid_on_mode) gpio_set_level(PIN_LED, get_gpio_level_for_off());
  }
  // Extremely long "flashes" are treated as solid on
  else {
    solid_on_mode = true;
  }
}

void flicker_task(void *pvParameters) {
  while (1) {
    // Ensure LED is in correct baseline state for current mode
    if (!solid_on_mode) {
      gpio_set_level(PIN_LED, (led_mode == LED_MODE_NIGHTTIME && led_enabled) ? 1 : 0);
    }
    
    // Random wait duration (30-120 seconds)
    int wait_duration = 30000 + (esp_random() % 90000);
    int burst_count = 1 + (esp_random() % 5);
    
    vTaskDelay(pdMS_TO_TICKS(wait_duration));

    // Burst of flashes
    for (int i = 0; i < burst_count; i++) {
      int flash_duration = 50 + (esp_random() % 250);
      
      // Flash: briefly invert the LED state
      // Daylight: off → on (flash) → off
      // Nighttime: on → off (dark burst) → on
      gpio_set_level(PIN_LED, get_gpio_level_for_on());
      vTaskDelay(pdMS_TO_TICKS(flash_duration));
      
      if (!solid_on_mode) {
        gpio_set_level(PIN_LED, get_gpio_level_for_off());
      }

      if (i < burst_count - 1) {
        int inter_burst = 50 + (esp_random() % 100);
        vTaskDelay(pdMS_TO_TICKS(inter_burst));
      }
    }
  }
}

void led_init(void) {
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_LED),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_conf);

  // Load settings from NVS
  bool saved_enabled;
  if (app_settings_load_bool(LED_ENABLED_KEY, &saved_enabled) == ESP_OK) {
    led_enabled = saved_enabled;
  }
  
  uint8_t mode_val;
  if (app_settings_load_u8(LED_MODE_KEY, &mode_val) == ESP_OK) {
    led_mode = (led_mode_t)mode_val;
  }
  
  bool saved_sundial;
  if (app_settings_load_bool(LED_SUNDIAL_KEY, &saved_sundial) == ESP_OK) {
    sundial_mode = saved_sundial;
  }
  
  bool saved_tempo_sync;
  if (app_settings_load_bool(LED_TEMPO_SYNC_KEY, &saved_tempo_sync) == ESP_OK) {
    tempo_sync_enabled = saved_tempo_sync;
  }
  
  // Set initial LED state based on mode
  gpio_set_level(PIN_LED, (led_mode == LED_MODE_NIGHTTIME && led_enabled) ? 1 : 0);

  ESP_LOGI(TAG, "UV LED initialized: enabled=%s, mode=%s, sundial=%s, tempo_sync=%s", 
           led_enabled ? "yes" : "no",
           led_mode == LED_MODE_DAYLIGHT ? "daylight" : "nighttime",
           sundial_mode ? "yes" : "no",
           tempo_sync_enabled ? "yes" : "no");
  
  if (sundial_mode) {
    ESP_LOGI(TAG, "Sundial mode enabled - will auto-switch based on ambient light");
  }
  
  // Subscribe to ALS events for sundial mode
  event_bus_subscribe(EVENT_SENSOR_ALS, als_event_handler, NULL);
  
  // Initialize event handler
  led_event_handler_init();
}

void flicker_start(void) {
  if (tempo_sync_enabled) {
    ESP_LOGW(TAG, "Cannot start flicker while tempo sync is active");
    return;
  }
  
  if (flicker_task_handle != NULL) {
    vTaskResume(flicker_task_handle);
    ESP_LOGI(TAG, "Flicker task resumed");
  } else {
    xTaskCreate(flicker_task, "flicker", 2048, NULL, TASK_PRIORITY_LED, &flicker_task_handle);
    ESP_LOGI(TAG, "Flicker task started");
  }
}

void flicker_stop(void) {
  if (flicker_task_handle) {
    vTaskSuspend(flicker_task_handle);
    // Return LED to default state for current mode
    gpio_set_level(PIN_LED, (led_mode == LED_MODE_NIGHTTIME && led_enabled) ? 1 : 0);
    ESP_LOGI(TAG, "Flicker task suspended");
  }
}

bool flicker_is_running(void) {
  return (flicker_task_handle != NULL && eTaskGetState(flicker_task_handle) != eSuspended);
}

void led_set_enabled(bool enabled) {
  led_enabled = enabled;
  esp_err_t ret = app_settings_save_bool(LED_ENABLED_KEY, enabled);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save LED enabled state: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "LED enabled state set to: %s", enabled ? "true" : "false");
  }
}

bool led_get_enabled(void) {
  return led_enabled;
}

esp_err_t led_set_mode(led_mode_t mode) {
  led_mode = mode;
  
  // Update LED state to match new mode
  // Even if flicker is running, set the baseline state
  // The flicker task will maintain it from there
  if (!solid_on_mode) {
    gpio_set_level(PIN_LED, (led_mode == LED_MODE_NIGHTTIME && led_enabled) ? 1 : 0);
    ESP_LOGD(TAG, "LED baseline set to: %s", (led_mode == LED_MODE_NIGHTTIME) ? "on (nighttime)" : "off (daylight)");
  }
  
  esp_err_t ret = app_settings_save_u8(LED_MODE_KEY, (uint8_t)mode);
  
  ESP_LOGI(TAG, "LED mode set to: %s", mode == LED_MODE_DAYLIGHT ? "daylight" : "nighttime");
  return ret;
}

led_mode_t led_get_mode(void) {
  return led_mode;
}

esp_err_t led_set_sundial_mode(bool enabled) {
  sundial_mode = enabled;
  
  esp_err_t ret = app_settings_save_bool(LED_SUNDIAL_KEY, enabled);
  
  ESP_LOGI(TAG, "Sundial mode %s", enabled ? "enabled" : "disabled");
  if (enabled) {
    ESP_LOGI(TAG, "Will auto-switch day/night based on ambient light");
    ESP_LOGI(TAG, "Dark threshold: %d, Light threshold: %d", ALS_DARK_THRESHOLD, ALS_LIGHT_THRESHOLD);
  }
  
  return ret;
}

bool led_get_sundial_mode(void) {
  return sundial_mode;
}

esp_err_t led_tempo_sync_enable(void) {
  if (flicker_is_running()) {
    flicker_stop();
    ESP_LOGI(TAG, "Stopped flicker to enable tempo sync");
  }
  
  tempo_sync_enabled = true;
  app_settings_save_bool(LED_TEMPO_SYNC_KEY, true);
  
  ESP_LOGI(TAG, "LED tempo sync enabled");
  // TODO: Subscribe to EVENT_BEAT to flash on tempo
  return ESP_OK;
}

esp_err_t led_tempo_sync_disable(void) {
  tempo_sync_enabled = false;
  app_settings_save_bool(LED_TEMPO_SYNC_KEY, false);
  
  // Return LED to default state
  gpio_set_level(PIN_LED, (led_mode == LED_MODE_NIGHTTIME && led_enabled) ? 1 : 0);
  
  ESP_LOGI(TAG, "LED tempo sync disabled");
  // TODO: Unsubscribe from EVENT_BEAT
  return ESP_OK;
}

bool led_tempo_sync_is_enabled(void) {
  return tempo_sync_enabled;
}