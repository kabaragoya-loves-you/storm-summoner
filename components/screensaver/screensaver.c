#include "screensaver.h"
#include "app_settings.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui.h"
#include "stars.h"
#include "elite.h"
#include "nvs.h"

void screensaver_event_handler_init(void);

#define TAG "SCREENSAVER"

#define NVS_KEY_SS_ACTIVE "ss_active"
#define NVS_KEY_SS_DELAY  "ss_delay_sec"
#define NVS_KEY_SS_MODE   "ss_mode"

#define MAX_DELAY_SECONDS 3600 // 1 hour maximum

static bool g_screensaver_initialised = false;
static bool g_screensaver_enabled_in_settings = true;
static uint16_t g_screensaver_delay_seconds = 60;
static screensaver_mode_t g_selected_screensaver_mode = SCREENSAVER_MODE_STARFIELD;

static TimerHandle_t g_screensaver_activity_timer = NULL;
static bool g_screensaver_active = false;

static void screensaver_timer_callback(TimerHandle_t xTimer);

void screensaver_init(void) {
  esp_err_t ret;
  bool active_setting;
  
  // Load settings from NVS
  ret = app_settings_load_bool(NVS_KEY_SS_ACTIVE, &active_setting);
  if (ret == ESP_OK) {
    g_screensaver_enabled_in_settings = active_setting;
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "NVS key '%s' not found, saving default: true", NVS_KEY_SS_ACTIVE);
    g_screensaver_enabled_in_settings = true;
    app_settings_save_bool(NVS_KEY_SS_ACTIVE, g_screensaver_enabled_in_settings);
  } else {
    ESP_LOGE(TAG, "Failed to load '%s' from NVS: %s. Using default: true", NVS_KEY_SS_ACTIVE, esp_err_to_name(ret));
    g_screensaver_enabled_in_settings = true;
  }

  uint16_t delay_setting;
  ret = app_settings_load_u16(NVS_KEY_SS_DELAY, &delay_setting);
  if (ret == ESP_OK) {
    g_screensaver_delay_seconds = delay_setting;
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "NVS key '%s' not found, saving default: %u", NVS_KEY_SS_DELAY, g_screensaver_delay_seconds);
    app_settings_save_u16(NVS_KEY_SS_DELAY, g_screensaver_delay_seconds);
  } else {
    ESP_LOGE(TAG, "Failed to load '%s' from NVS: %s. Using default: %u", NVS_KEY_SS_DELAY, esp_err_to_name(ret), g_screensaver_delay_seconds);
  }

  uint16_t mode_setting_u16;
  ret = app_settings_load_u16(NVS_KEY_SS_MODE, &mode_setting_u16);
  if (ret == ESP_OK) {
    if (mode_setting_u16 == SCREENSAVER_MODE_STARFIELD || mode_setting_u16 == SCREENSAVER_MODE_ELITE) {
      g_selected_screensaver_mode = (screensaver_mode_t)mode_setting_u16;
    } else {
      ESP_LOGW(TAG, "Invalid NVS value for '%s': %u. Using default: %d", NVS_KEY_SS_MODE, mode_setting_u16, g_selected_screensaver_mode);
      app_settings_save_u16(NVS_KEY_SS_MODE, (uint16_t)g_selected_screensaver_mode);
    }
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "NVS key '%s' not found, saving default: %d", NVS_KEY_SS_MODE, g_selected_screensaver_mode);
    app_settings_save_u16(NVS_KEY_SS_MODE, (uint16_t)g_selected_screensaver_mode);
  } else {
    ESP_LOGE(TAG, "Failed to load '%s' from NVS: %s. Using default: %d", NVS_KEY_SS_MODE, esp_err_to_name(ret), g_selected_screensaver_mode);
  }

  if (g_screensaver_enabled_in_settings && g_screensaver_delay_seconds > 0) {
    g_screensaver_activity_timer = xTimerCreate(
      "ScreensaverTimer",
      pdMS_TO_TICKS(g_screensaver_delay_seconds * 1000),
      pdFALSE, // One-shot timer
      (void *)0,
      screensaver_timer_callback);

    if (g_screensaver_activity_timer == NULL) {
      ESP_LOGE(TAG, "Failed to create screensaver activity timer");
      return;
    }
    ESP_LOGI(TAG, "Activity timer created with %u seconds delay.", g_screensaver_delay_seconds);
  } else {
    ESP_LOGI(TAG, "Disabled in settings or delay is 0.");
  }
  
  g_screensaver_initialised = true;
  
  screensaver_event_handler_init();

  if (g_screensaver_enabled_in_settings && g_screensaver_delay_seconds > 0) {
    screensaver_enable();
  }
}

void screensaver_enable(void) {
  if (!g_screensaver_initialised) {
    ESP_LOGE(TAG, "Screensaver not initialized. Call screensaver_init() first.");
    return;
  }
  
  if (!g_screensaver_enabled_in_settings || g_screensaver_delay_seconds == 0) {
    ESP_LOGI(TAG, "Screensaver is disabled in settings or delay is 0.");
    return;
  }

  if (g_screensaver_activity_timer != NULL) {
    xTimerReset(g_screensaver_activity_timer, portMAX_DELAY);
    ESP_LOGD(TAG, "Screensaver timer started/reset.");
  }
}

void screensaver_disable(void) {
  if (!g_screensaver_initialised) return;

  if (g_screensaver_activity_timer != NULL) {
    xTimerStop(g_screensaver_activity_timer, portMAX_DELAY);
  }

  if (g_screensaver_active) {
    // Stop the active screensaver
    if (g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD) {
      starfield_stop();
    } else if (g_selected_screensaver_mode == SCREENSAVER_MODE_ELITE) {
      elite_stop();
    }
    g_screensaver_active = false;
    
    // Reclaim UI canvas buffer
    ui_reclaim_canvas_buffer();
    
    ESP_LOGI(TAG, "Screensaver stopped.");
  }
}

// LVGL timer for deferred screensaver stop
static void stop_screensaver_deferred(lv_timer_t *timer) {
  lv_timer_del(timer); // One-shot timer
  
  ESP_LOGI(TAG, "Stopping screensaver (deferred)");
  
  if (g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD) {
    starfield_stop();
  } else if (g_selected_screensaver_mode == SCREENSAVER_MODE_ELITE) {
    elite_stop();
  }
  
  g_screensaver_active = false;
  
  // Reclaim UI canvas buffer
  ui_reclaim_canvas_buffer();
}

void screensaver_notify_activity(void) {
  if (!g_screensaver_initialised || !g_screensaver_enabled_in_settings || g_screensaver_activity_timer == NULL) {
    return;
  }

  // If screensaver is active, stop it (deferred to LVGL context)
  if (g_screensaver_active) {
    ESP_LOGD(TAG, "Activity detected - scheduling screensaver stop");
    
    // Create a one-shot LVGL timer to stop the screensaver in the LVGL context
    lv_timer_t *deferred_timer = lv_timer_create(stop_screensaver_deferred, 0, NULL);
    if (deferred_timer) {
      lv_timer_set_repeat_count(deferred_timer, 1);
    }
  }

  // Always reset the inactivity timer
  xTimerReset(g_screensaver_activity_timer, portMAX_DELAY);
}

// LVGL timer for deferred screensaver start
static void start_screensaver_deferred(lv_timer_t *timer) {
  lv_timer_del(timer); // One-shot timer
  
  ESP_LOGI(TAG, "Starting screensaver (deferred)");
  
  // Start the selected screensaver
  if (g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD) {
    starfield_start();
  } else if (g_selected_screensaver_mode == SCREENSAVER_MODE_ELITE) {
    elite_start();
  }
  
  g_screensaver_active = true;
}

// FreeRTOS timer callback - now much simpler!
static void screensaver_timer_callback(TimerHandle_t xTimer) {
  if (g_screensaver_active) {
    ESP_LOGW(TAG, "Screensaver already active, ignoring timer");
    return;
  }

  ESP_LOGI(TAG, "Inactivity timeout - starting screensaver");
  
  // Release UI canvas buffer to free up memory for screensaver
  ui_release_canvas_buffer();
  
  // Create a one-shot LVGL timer to start the screensaver in the LVGL context
  lv_timer_t *deferred_timer = lv_timer_create(start_screensaver_deferred, 100, NULL);
  if (deferred_timer) {
    lv_timer_set_repeat_count(deferred_timer, 1);
  }
}

void screensaver_set_mode(screensaver_mode_t mode) {
  if (!g_screensaver_initialised) {
    ESP_LOGE(TAG, "Screensaver not initialized yet.");
    return;
  }

  if (mode != SCREENSAVER_MODE_STARFIELD && mode != SCREENSAVER_MODE_ELITE) {
    ESP_LOGE(TAG, "Invalid screensaver mode: %d", mode);
    return;
  }

  if (g_selected_screensaver_mode == mode) {
    ESP_LOGI(TAG, "Mode is already %s. No change needed.", 
      mode == SCREENSAVER_MODE_STARFIELD ? "Starfield" : "Elite");
    return;
  }

  // If screensaver is active, switch modes
  if (g_screensaver_active) {
    // Stop current mode
    if (g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD) {
      starfield_stop();
    } else if (g_selected_screensaver_mode == SCREENSAVER_MODE_ELITE) {
      elite_stop();
    }
    
    // Update mode
    g_selected_screensaver_mode = mode;
    
    // Ensure UI buffer is released (in case switching called before starting)
    ui_release_canvas_buffer();
    
    // Start new mode
    if (mode == SCREENSAVER_MODE_STARFIELD) {
      starfield_start();
    } else if (mode == SCREENSAVER_MODE_ELITE) {
      elite_start();
    }
  } else {
    // Just update the mode
    g_selected_screensaver_mode = mode;
  }

  ESP_LOGI(TAG, "Screensaver mode changed to %s", 
    mode == SCREENSAVER_MODE_STARFIELD ? "Starfield" : "Elite");

  // Save to NVS
  esp_err_t ret = app_settings_save_u16(NVS_KEY_SS_MODE, (uint16_t)mode);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save new mode to NVS: %s", esp_err_to_name(ret));
  }
}

void screensaver_set_delay(uint16_t delay_seconds) {
  if (!g_screensaver_initialised) {
    ESP_LOGE(TAG, "Screensaver not initialized yet.");
    return;
  }

  // Handle special case: 0 means disable screensaver
  if (delay_seconds == 0) {
    ESP_LOGI(TAG, "Delay set to 0, disabling screensaver.");
    g_screensaver_delay_seconds = 0;
    g_screensaver_enabled_in_settings = false;
    
    // Save both values to NVS
    app_settings_save_u16(NVS_KEY_SS_DELAY, delay_seconds);
    app_settings_save_bool(NVS_KEY_SS_ACTIVE, false);
    
    // Disable the screensaver
    screensaver_disable();
    return;
  }

  // Validate delay
  if (delay_seconds > MAX_DELAY_SECONDS) {
    ESP_LOGE(TAG, "Invalid delay: %u seconds (must be 1-%u)", delay_seconds, MAX_DELAY_SECONDS);
    return;
  }

  if (g_screensaver_delay_seconds == delay_seconds) {
    ESP_LOGI(TAG, "Delay is already %u seconds. No change needed.", delay_seconds);
    return;
  }

  g_screensaver_delay_seconds = delay_seconds;
  ESP_LOGI(TAG, "Screensaver delay changed to %u seconds.", delay_seconds);

  // Save to NVS
  esp_err_t ret = app_settings_save_u16(NVS_KEY_SS_DELAY, delay_seconds);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save new delay to NVS: %s", esp_err_to_name(ret));
  }

  // Update the timer period if it exists
  if (g_screensaver_activity_timer != NULL) {
    TickType_t new_period = pdMS_TO_TICKS(delay_seconds * 1000);
    if (xTimerChangePeriod(g_screensaver_activity_timer, new_period, portMAX_DELAY) == pdPASS) {
      ESP_LOGI(TAG, "Timer period updated to %u seconds.", delay_seconds);
      // Also make sure we're enabled if we weren't before
      if (!g_screensaver_enabled_in_settings) {
        g_screensaver_enabled_in_settings = true;
        app_settings_save_bool(NVS_KEY_SS_ACTIVE, true);
      }
    } else {
      ESP_LOGE(TAG, "Failed to update timer period!");
    }
  }
}