#include "screensaver.h"
#include "app_settings.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "ui.h"
#include "stars.h" // For starfield functions
#include "elite.h" // For elite functions
#include "nvs.h"

void screensaver_event_handler_init(void);

#define TAG "SCREENSAVER"

#define NVS_KEY_SS_ACTIVE "ss_active"
#define NVS_KEY_SS_DELAY  "ss_delay_sec"
#define NVS_KEY_SS_MODE   "ss_mode"

#define MAX_DELAY_SECONDS 3600 // 1 hour maximum

static bool g_screensaver_initialised = false;
static bool g_screensaver_enabled_in_settings = true; // Default if NVS fails or key not found
static uint16_t g_screensaver_delay_seconds = 60;    // Default
static screensaver_mode_t g_selected_screensaver_mode = SCREENSAVER_MODE_STARFIELD; // Default

static app_mode_t g_previous_app_mode = APP_MODE_PERFORMANCE;
static TimerHandle_t g_screensaver_activity_timer = NULL;
static lv_timer_t *g_lvgl_screensaver_start_timer = NULL; // LVGL timer to start the actual screensaver
static lv_timer_t *g_lvgl_screensaver_stop_timer = NULL; // LVGL timer to stop the screensaver

static void screensaver_timer_callback(TimerHandle_t xTimer);
static void actual_screensaver_start_lvgl_cb(lv_timer_t *timer);
static void actual_screensaver_stop_lvgl_cb(lv_timer_t *timer);

void screensaver_init(void) {
  esp_err_t ret;
  bool active_setting;
  ret = app_settings_load_bool(NVS_KEY_SS_ACTIVE, &active_setting);
  if (ret == ESP_OK) {
    g_screensaver_enabled_in_settings = active_setting;
    // ESP_LOGI(TAG, "Loaded NVS setting: ss_active = %s", active_setting ? "true" : "false");
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "NVS key '%s' not found, saving default: true", NVS_KEY_SS_ACTIVE);
    g_screensaver_enabled_in_settings = true; // Default
    app_settings_save_bool(NVS_KEY_SS_ACTIVE, g_screensaver_enabled_in_settings);
  } else {
    ESP_LOGE(TAG, "Failed to load '%s' from NVS: %s. Using default: true", NVS_KEY_SS_ACTIVE, esp_err_to_name(ret));
    g_screensaver_enabled_in_settings = true; // Default on error
  }

  uint16_t delay_setting;
  ret = app_settings_load_u16(NVS_KEY_SS_DELAY, &delay_setting);
  if (ret == ESP_OK) {
    g_screensaver_delay_seconds = delay_setting;
    // ESP_LOGI(TAG, "Loaded NVS setting: ss_delay_sec = %u", delay_setting);
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "NVS key '%s' not found, saving default: %u", NVS_KEY_SS_DELAY, g_screensaver_delay_seconds);
    app_settings_save_u16(NVS_KEY_SS_DELAY, g_screensaver_delay_seconds);
  } else {
    ESP_LOGE(TAG, "Failed to load '%s' from NVS: %s. Using default: %u", NVS_KEY_SS_DELAY, esp_err_to_name(ret), g_screensaver_delay_seconds);
    // Keep default g_screensaver_delay_seconds on error
  }

  uint16_t mode_setting_u16; // NVS stores enums as underlying int type
  ret = app_settings_load_u16(NVS_KEY_SS_MODE, &mode_setting_u16);
  if (ret == ESP_OK) {
    if (mode_setting_u16 == SCREENSAVER_MODE_STARFIELD || mode_setting_u16 == SCREENSAVER_MODE_ELITE) {
      g_selected_screensaver_mode = (screensaver_mode_t)mode_setting_u16;
      // ESP_LOGI(TAG, "Loaded NVS setting: ss_mode = %d", mode_setting_u16);
    } else {
      ESP_LOGW(TAG, "Invalid NVS value for '%s': %u. Using default: %d", NVS_KEY_SS_MODE, mode_setting_u16, g_selected_screensaver_mode);
      app_settings_save_u16(NVS_KEY_SS_MODE, (uint16_t)g_selected_screensaver_mode); // Save valid default
    }
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "NVS key '%s' not found, saving default: %d", NVS_KEY_SS_MODE, g_selected_screensaver_mode);
    app_settings_save_u16(NVS_KEY_SS_MODE, (uint16_t)g_selected_screensaver_mode);
  } else {
    ESP_LOGE(TAG, "Failed to load '%s' from NVS: %s. Using default: %d", NVS_KEY_SS_MODE, esp_err_to_name(ret), g_selected_screensaver_mode);
    // Keep default g_selected_screensaver_mode on error
  }

  if (g_screensaver_enabled_in_settings) {
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
    ESP_LOGI(TAG, "Disabled in settings.");
  }
  
  g_screensaver_initialised = true;
  
  screensaver_event_handler_init();

  if (g_screensaver_enabled_in_settings) screensaver_enable();
}

void screensaver_enable(void) {
  // ESP_LOGI(TAG, "screensaver_enable() called."); // Diagnostic log
  if (!g_screensaver_initialised) {
    ESP_LOGE(TAG, "Screensaver not initialized. Call screensaver_init() first.");
    return;
  }
  if (!g_screensaver_enabled_in_settings) {
    ESP_LOGI(TAG, "Screensaver is disabled in settings, not enabling.");
    return;
  }

  // Check if delay is 0 and fix it before enabling
  if (g_screensaver_delay_seconds == 0) {
    ESP_LOGW(TAG, "Screensaver delay is 0, setting to default 60 seconds before enabling.");
    g_screensaver_delay_seconds = 60;
    esp_err_t ret = app_settings_save_u16(NVS_KEY_SS_DELAY, g_screensaver_delay_seconds);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to save corrected delay to NVS: %s", esp_err_to_name(ret));
    }
    
    // Update timer period if timer exists
    if (g_screensaver_activity_timer != NULL) {
      TickType_t new_period = pdMS_TO_TICKS(g_screensaver_delay_seconds * 1000);
      if (xTimerChangePeriod(g_screensaver_activity_timer, new_period, portMAX_DELAY) != pdPASS) {
        ESP_LOGE(TAG, "Failed to update timer period to corrected delay!");
      }
    }
  }

  if (g_screensaver_activity_timer != NULL) {
    if (xTimerIsTimerActive(g_screensaver_activity_timer)) {
      // ESP_LOGI(TAG, "Screensaver activity timer is already active, resetting it.");
      xTimerReset(g_screensaver_activity_timer, portMAX_DELAY);
    } else {
      // ESP_LOGI(TAG, "Screensaver activity timer started.");
      xTimerStart(g_screensaver_activity_timer, portMAX_DELAY);
    }
  } else {
    ESP_LOGE(TAG, "Screensaver timer handle is NULL in enable, but should have been created if enabled in settings.");
  }
}

void screensaver_disable(void) {
  if (!g_screensaver_initialised) return;

  if (g_screensaver_activity_timer != NULL) {
    xTimerStop(g_screensaver_activity_timer, portMAX_DELAY);
    // ESP_LOGI(TAG, "Screensaver activity timer stopped.");
  }
  // If an LVGL stop timer was pending, remove it
  if (g_lvgl_screensaver_stop_timer != NULL) {
    lv_timer_del(g_lvgl_screensaver_stop_timer);
    g_lvgl_screensaver_stop_timer = NULL;
    // ESP_LOGI(TAG, "Pending LVGL screensaver stop timer deleted during disable.");
  }

  if (ui_get_app_mode() == APP_MODE_SCREENSAVER) {
    // ESP_LOGI(TAG, "Screensaver was active during disable, restoring previous mode: %d", g_previous_app_mode);
    // Directly stop visuals and resume UI here as disable might be called from non-LVGL context too
    if (g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD) {
      starfield_stop(); 
    } else if (g_selected_screensaver_mode == SCREENSAVER_MODE_ELITE) {
      elite_stop(); 
    }
    ui_graphics_resume();
    ui_set_app_mode(g_previous_app_mode);
  }
  // ESP_LOGI(TAG, "Screensaver disabled.");
}

void screensaver_notify_activity(void) {
  if (!g_screensaver_initialised || !g_screensaver_enabled_in_settings || g_screensaver_activity_timer == NULL) {
    return;
  }

  if (g_lvgl_screensaver_start_timer != NULL) {
    // ESP_LOGI(TAG, "SS_NOTIFY: Deleting pending LVGL screensaver START timer due to activity.");
    lv_timer_del(g_lvgl_screensaver_start_timer);
    g_lvgl_screensaver_start_timer = NULL;
  }
  // If an LVGL stop timer was already pending, no need to create another one
  if (g_lvgl_screensaver_stop_timer != NULL) {
      // ESP_LOGW(TAG, "SS_NOTIFY: Activity while LVGL stop timer already pending. Timer will proceed.");
      // We should still reset the main inactivity timer
      if (xTimerIsTimerActive(g_screensaver_activity_timer)) {
        xTimerReset(g_screensaver_activity_timer, portMAX_DELAY);
      } else {
        xTimerStart(g_screensaver_activity_timer, portMAX_DELAY);
      }
      return; 
  }

  if (ui_get_app_mode() == APP_MODE_SCREENSAVER) {
    // ESP_LOGI(TAG, "SS_NOTIFY: Activity in APP_MODE_SCREENSAVER. Scheduling LVGL timer to stop visuals.");
    
    // Create a one-shot LVGL timer to stop the screensaver visuals.
    g_lvgl_screensaver_stop_timer = lv_timer_create(actual_screensaver_stop_lvgl_cb, 10, NULL);
    if (g_lvgl_screensaver_stop_timer == NULL) {
      ESP_LOGE(TAG, "SS_NOTIFY: FAILED to create LVGL screensaver STOP timer! Critical error, UI might not restore.");
      // As a last resort, try direct (risky in this context if it was WDT source)
      // This path should ideally not be hit.
        if (g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD) starfield_stop();
        else if (g_selected_screensaver_mode == SCREENSAVER_MODE_ELITE) elite_stop();
        ui_graphics_resume();
        ui_set_app_mode(g_previous_app_mode);
    }
    // Always reset the main inactivity timer upon user activity that triggers stop sequence
    if (xTimerReset(g_screensaver_activity_timer, portMAX_DELAY) != pdPASS) {
        ESP_LOGE(TAG, "SS_NOTIFY: FAILED to reset main activity timer during stop sequence!");
    }

  } else {
    // ESP_LOGD(TAG, "SS_NOTIFY: Activity detected (not in screensaver mode). Timer reset/started.");
    if (xTimerIsTimerActive(g_screensaver_activity_timer)) {
      xTimerReset(g_screensaver_activity_timer, portMAX_DELAY);
    } else {
      xTimerStart(g_screensaver_activity_timer, portMAX_DELAY);
    }
  }
}

// FreeRTOS timer callback - runs in timer service task context
static void screensaver_timer_callback(TimerHandle_t xTimer) {
  // ESP_LOGI(TAG, "FREERTOS_CB: Screensaver inactivity timer expired.");

  if (g_lvgl_screensaver_start_timer != NULL || ui_get_app_mode() == APP_MODE_SCREENSAVER) {
    // ESP_LOGW(TAG, "FREERTOS_CB: Screensaver already active or start already pending. Ignoring.");
    return;
  }

  g_previous_app_mode = ui_get_app_mode();
  ui_set_app_mode(APP_MODE_SCREENSAVER);
  // ESP_LOGI(TAG, "FREERTOS_CB: Transitioning to APP_MODE_SCREENSAVER. Previous mode was: %d", g_previous_app_mode);

  ui_graphics_suspend();
  // ESP_LOGI(TAG, "FREERTOS_CB: Main UI graphics suspended.");

  // ESP_LOGI(TAG, "FREERTOS_CB: Scheduling LVGL timer to start screensaver visuals.");
  g_lvgl_screensaver_start_timer = lv_timer_create(actual_screensaver_start_lvgl_cb, 10, NULL);
  if (g_lvgl_screensaver_start_timer == NULL) {
      // ESP_LOGE(TAG, "FREERTOS_CB: Failed to create LVGL screensaver start timer! Attempting direct start (risky).");
      if (g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD) starfield_start();
      else if (g_selected_screensaver_mode == SCREENSAVER_MODE_ELITE) elite_start();
  } 
}

// LVGL timer callback - runs in LVGL timer handler context
static void actual_screensaver_start_lvgl_cb(lv_timer_t *timer) {
  // ESP_LOGI(TAG, "LVGL_START_CB: actual_screensaver_start_lvgl_cb called.");

  if (g_lvgl_screensaver_start_timer == timer) { 
      lv_timer_del(g_lvgl_screensaver_start_timer);
      g_lvgl_screensaver_start_timer = NULL;
      // ESP_LOGI(TAG, "LVGL_START_CB: Self-deleted one-shot LVGL start timer.");
  }

  if (ui_get_app_mode() != APP_MODE_SCREENSAVER) {
    // ESP_LOGW(TAG, "LVGL_START_CB: App mode changed before screensaver could start. Aborting start.");
    // Ensure UI is consistent if this happens (though ui_graphics_suspend was already called)
    ui_graphics_resume(); 
    ui_set_app_mode(g_previous_app_mode); // Revert to original mode
    return;
  }

  if (g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD) {
    // ESP_LOGI(TAG, "LVGL_START_CB: Starting starfield screensaver.");
    starfield_start();
  } else if (g_selected_screensaver_mode == SCREENSAVER_MODE_ELITE) {
    // ESP_LOGI(TAG, "LVGL_START_CB: Starting elite screensaver.");
    elite_start();
  }
  // ESP_LOGI(TAG, "LVGL_START_CB: Screensaver visuals started.");
}

static void actual_screensaver_stop_lvgl_cb(lv_timer_t *timer) {
  // ESP_LOGI(TAG, "LVGL_STOP_CB: actual_screensaver_stop_lvgl_cb called.");

  if (g_lvgl_screensaver_stop_timer == timer) { 
    lv_timer_del(g_lvgl_screensaver_stop_timer);
    g_lvgl_screensaver_stop_timer = NULL;
    // ESP_LOGI(TAG, "LVGL_STOP_CB: Self-deleted one-shot LVGL stop timer.");
  }

  // Ensure we are still in screensaver mode; if not, something else might have handled it.
  // However, this callback is the designated handler for stopping now.
  if (ui_get_app_mode() != APP_MODE_SCREENSAVER) {
     // ESP_LOGW(TAG, "LVGL_STOP_CB: Not in screensaver mode anymore. Stop action might be redundant or late.");
     // Still, attempt to clean up resources if they were started and restore UI, just in case.
  }

  // ESP_LOGI(TAG, "LVGL_STOP_CB: Stopping active screensaver visuals (%s mode).", 
  //   g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD ? "Starfield" : "Elite");
  if (g_selected_screensaver_mode == SCREENSAVER_MODE_STARFIELD) {
    starfield_stop();
  } else if (g_selected_screensaver_mode == SCREENSAVER_MODE_ELITE) {
    elite_stop();
  }
  // ESP_LOGI(TAG, "LVGL_STOP_CB: Screensaver visuals stopped.");

  // ESP_LOGI(TAG, "LVGL_STOP_CB: Calling ui_graphics_resume().");
  ui_graphics_resume();
  // ESP_LOGI(TAG, "LVGL_STOP_CB: Returned from ui_graphics_resume().");
      
  // app_mode_t current_mode_before_change = ui_get_app_mode();
  ui_set_app_mode(g_previous_app_mode);
  // ESP_LOGI(TAG, "LVGL_STOP_CB: App mode changed from %d to %d (target previous: %d).", current_mode_before_change, ui_get_app_mode(), g_previous_app_mode);
  // ESP_LOGI(TAG, "LVGL_STOP_CB: Screensaver deactivation complete.");
}

// Function to change the active screensaver mode
void screensaver_set_mode(screensaver_mode_t mode) {
  if (!g_screensaver_initialised) {
    ESP_LOGE(TAG, "screensaver_set_mode: Screensaver not initialized yet.");
    return;
  }

  // Validate mode
  if (mode != SCREENSAVER_MODE_STARFIELD && mode != SCREENSAVER_MODE_ELITE) {
    ESP_LOGE(TAG, "screensaver_set_mode: Invalid screensaver mode: %d", mode);
    return;
  }

  if (g_selected_screensaver_mode == mode) {
    ESP_LOGI(TAG, "screensaver_set_mode: Mode is already %s. No change needed.", 
      mode == SCREENSAVER_MODE_STARFIELD ? "Starfield" : "Elite");
    return;
  }

  g_selected_screensaver_mode = mode;
  ESP_LOGI(TAG, "screensaver_set_mode: Screensaver mode changed to %s locally.", 
    mode == SCREENSAVER_MODE_STARFIELD ? "Starfield" : "Elite");

  esp_err_t ret = app_settings_save_u16(NVS_KEY_SS_MODE, (uint16_t)mode);
  if (ret == ESP_OK) {
    // ESP_LOGI(TAG, "screensaver_set_mode: Successfully saved new mode (%d) to NVS.", mode);
  } else {
    ESP_LOGE(TAG, "screensaver_set_mode: Failed to save new mode to NVS! Error: %s", esp_err_to_name(ret));
    // Decide if we should revert g_selected_screensaver_mode or not. For now, keep it changed locally.
  }
}

// Function to change the screensaver delay
void screensaver_set_delay(uint16_t delay_seconds) {
  if (!g_screensaver_initialised) {
    ESP_LOGE(TAG, "screensaver_set_delay: Screensaver not initialized yet.");
    return;
  }

  // Handle special case: 0 means disable screensaver
  if (delay_seconds == 0) {
    ESP_LOGI(TAG, "screensaver_set_delay: Delay set to 0, disabling screensaver.");
    g_screensaver_delay_seconds = 0;
    g_screensaver_enabled_in_settings = false;
    
    // Save both values to NVS
    esp_err_t ret1 = app_settings_save_u16(NVS_KEY_SS_DELAY, delay_seconds);
    esp_err_t ret2 = app_settings_save_bool(NVS_KEY_SS_ACTIVE, false);
    
    if (ret1 != ESP_OK) {
      ESP_LOGE(TAG, "screensaver_set_delay: Failed to save delay to NVS: %s", esp_err_to_name(ret1));
    }
    if (ret2 != ESP_OK) {
      ESP_LOGE(TAG, "screensaver_set_delay: Failed to save active setting to NVS: %s", esp_err_to_name(ret2));
    }
    
    // Disable the screensaver
    screensaver_disable();
    return;
  }

  // Validate delay (allow reasonable range)
  if (delay_seconds > MAX_DELAY_SECONDS) {
    ESP_LOGE(TAG, "screensaver_set_delay: Invalid delay: %u seconds (must be 1-%u)", delay_seconds, MAX_DELAY_SECONDS);
    return;
  }

  if (g_screensaver_delay_seconds == delay_seconds) {
    ESP_LOGI(TAG, "screensaver_set_delay: Delay is already %u seconds. No change needed.", delay_seconds);
    return;
  }

  g_screensaver_delay_seconds = delay_seconds;
  ESP_LOGI(TAG, "screensaver_set_delay: Screensaver delay changed to %u seconds locally.", delay_seconds);

  // Save to NVS
  esp_err_t ret = app_settings_save_u16(NVS_KEY_SS_DELAY, delay_seconds);
  if (ret == ESP_OK) {
    // ESP_LOGI(TAG, "screensaver_set_delay: Successfully saved new delay (%u) to NVS.", delay_seconds);
  } else {
    ESP_LOGE(TAG, "screensaver_set_delay: Failed to save new delay to NVS! Error: %s", esp_err_to_name(ret));
  }

  // Update the timer period if it exists and is enabled
  if (g_screensaver_enabled_in_settings && g_screensaver_activity_timer != NULL) {
    TickType_t new_period = pdMS_TO_TICKS(delay_seconds * 1000);
    if (xTimerChangePeriod(g_screensaver_activity_timer, new_period, portMAX_DELAY) == pdPASS) {
      ESP_LOGI(TAG, "screensaver_set_delay: Timer period updated to %u seconds.", delay_seconds);
    } else {
      ESP_LOGE(TAG, "screensaver_set_delay: Failed to update timer period!");
    }
  }
}
