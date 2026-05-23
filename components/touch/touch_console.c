#include "touch_console.h"
#include "touch.h"
#include "touchwheel.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <inttypes.h>

static const char* TAG = "touch_console";

static const char* registered_commands[] = {
  "calibrate", "reset", "debug", "query", "recover", "endless", "odometer", "bipolar", "stucktimeout", "idlecalib"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Test state for touchwheel commands
static struct {
  touchwheel_instance_t* instance;
  int current_value;
  bool active;
  TaskHandle_t task_handle;
  bool is_endless_mode;  // Track mode type for callback handling
  uint32_t cancel_press_time;  // When cancel pad was pressed (for hold detection)
} s_test_state = {NULL, 0, false, NULL, false, 0};

#define CONFIRM_PAD 8
#define CANCEL_PAD 12
#define CANCEL_HOLD_MS 300  // Must hold cancel pad this long to actually cancel

// Command: calibrate
static int cmd_calibrate(int argc, char **argv) {
  ESP_LOGI(TAG, "Starting touch calibration...");
  force_touch_calibration();
  ESP_LOGI(TAG, "Calibration complete");
  return 0;
}

// Command: reset
static int cmd_reset(int argc, char **argv) {
  ESP_LOGI(TAG, "Resetting stuck touch pads...");
  touch_reset_stuck_pads();
  ESP_LOGI(TAG, "Touch pads reset");
  return 0;
}

// Command: debug
static int cmd_debug(int argc, char **argv) {
  ESP_LOGI(TAG, "Enabling touch debug logging");
  touch_enable_debug_logging();
  return 0;
}

// Command: query
static int cmd_query(int argc, char **argv) {
  if (argc < 2) {
    ESP_LOGI(TAG, "Usage: query <pad_index>");
    ESP_LOGI(TAG, "  pad_index: 0-12 (logical pad number)");
    return 1;
  }
  
  int pad_index = atoi(argv[1]);
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) {
    ESP_LOGE(TAG, "Invalid pad index %d. Must be 0-%d", pad_index, MAX_TOUCH_PADS - 1);
    return 1;
  }
  
  touch_query_pad(pad_index);
  return 0;
}

// Command: recover
static int cmd_recover(int argc, char **argv) {
  if (argc < 2) {
    ESP_LOGI(TAG, "Usage: recover <pad_index>");
    ESP_LOGI(TAG, "  Clears quarantine (if any) and triggers a recovery on the pad.");
    ESP_LOGI(TAG, "  Use this to deliberately capture STAGE0/1/2 traces on a stuck pad.");
    return 1;
  }

  int pad_index = atoi(argv[1]);
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) {
    ESP_LOGE(TAG, "Invalid pad index %d. Must be 0-%d", pad_index, MAX_TOUCH_PADS - 1);
    return 1;
  }

  touch_force_recover_pad(pad_index);
  return 0;
}

// Callback for touchwheel value updates
static void test_value_callback(int value, void* user_data) {
  if (s_test_state.is_endless_mode) {
    // Endless mode returns deltas - accumulate
    s_test_state.current_value += value;
  } else {
    // Odometer/bipolar modes return absolute values - set directly
    s_test_state.current_value = value;
  }
  // Log removed - using detailed log in strategy binary instead
  ESP_LOGD(TAG, "Touchwheel value: %d (delta: %d)", s_test_state.current_value, value);
}

// Touch event handler for confirm/cancel pads
static void test_touch_event_handler(const event_t* event, void* context) {
  if (!s_test_state.active) return;
  
  uint8_t pad_id = event->data.touch.pad_id;
  
  if (event->type == EVENT_TOUCH_PRESS) {
    if (pad_id == CONFIRM_PAD) {
      ESP_LOGI(TAG, "=== CONFIRMED: Final value = %d ===", s_test_state.current_value);
      s_test_state.active = false;
    } else if (pad_id == CANCEL_PAD) {
      // Start tracking hold time for cancel
      s_test_state.cancel_press_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
  } else if (event->type == EVENT_TOUCH_RELEASE) {
    if (pad_id == CANCEL_PAD && s_test_state.cancel_press_time > 0) {
      uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      uint32_t hold_duration = now - s_test_state.cancel_press_time;
      s_test_state.cancel_press_time = 0;
      
      if (hold_duration >= CANCEL_HOLD_MS) {
        ESP_LOGI(TAG, "=== CANCELLED (held %"PRIu32"ms) ===", hold_duration);
        s_test_state.active = false;
      } else {
        ESP_LOGD(TAG, "Cancel ignored - hold too short (%"PRIu32"ms < %dms)", 
          hold_duration, CANCEL_HOLD_MS);
      }
    }
  }
}

// Test task that monitors for completion
static void test_monitor_task(void* pvParameters) {
  while (s_test_state.active) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  // Cleanup
  if (s_test_state.instance) {
    touch_unregister_touchwheel_instance(s_test_state.instance);
    touchwheel_destroy(s_test_state.instance);
    s_test_state.instance = NULL;
  }
  
  event_bus_unsubscribe(EVENT_TOUCH_PRESS, test_touch_event_handler);
  event_bus_unsubscribe(EVENT_TOUCH_RELEASE, test_touch_event_handler);
  
  s_test_state.task_handle = NULL;
  vTaskDelete(NULL);
}

// Command: endless
static int cmd_endless(int argc, char **argv) {
  if (s_test_state.active) {
    ESP_LOGE(TAG, "Test already running. Cancel with pad %d first.", CANCEL_PAD);
    return 1;
  }
  
  ESP_LOGI(TAG, "=== Endless Encoder Test ===");
  ESP_LOGI(TAG, "Use pads 0-7 to rotate (endless encoder)");
  ESP_LOGI(TAG, "Pad %d = Confirm, Pad %d = Cancel", CONFIRM_PAD, CANCEL_PAD);
  ESP_LOGI(TAG, "Starting value: 0");
  
  // Reset state
  s_test_state.current_value = 0;
  s_test_state.active = true;
  s_test_state.is_endless_mode = true;
  
  // Create endless mode touchwheel with callback output
  touchwheel_mode_processor_t* mode = touchwheel_mode_create_endless();
  touchwheel_output_t* output = touchwheel_output_callback_create(test_value_callback, NULL);
  
  if (!mode || !output) {
    ESP_LOGE(TAG, "Failed to create touchwheel components");
    if (mode) touchwheel_mode_destroy(mode);
    if (output) touchwheel_output_destroy(output);
    s_test_state.active = false;
    return 1;
  }
  
  s_test_state.instance = touchwheel_create(mode, output, 500);
  if (!s_test_state.instance) {
    ESP_LOGE(TAG, "Failed to create touchwheel instance");
    touchwheel_mode_destroy(mode);
    touchwheel_output_destroy(output);
    s_test_state.active = false;
    return 1;
  }
  
  // Register instance
  esp_err_t ret = touch_register_touchwheel_instance(s_test_state.instance);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register touchwheel instance");
    touchwheel_destroy(s_test_state.instance);
    s_test_state.instance = NULL;
    s_test_state.active = false;
    return 1;
  }
  
  // Subscribe to touch events for confirm/cancel
  event_bus_subscribe(EVENT_TOUCH_PRESS, test_touch_event_handler, NULL);
  event_bus_subscribe(EVENT_TOUCH_RELEASE, test_touch_event_handler, NULL);
  
  // Create monitor task
  xTaskCreate(test_monitor_task, "tw_test", 2048, NULL, 5, &s_test_state.task_handle);
  
  ESP_LOGI(TAG, "Test active. Rotate with pads 0-7, confirm with pad %d, cancel with pad %d", CONFIRM_PAD, CANCEL_PAD);
  
  return 0;
}

// Command: odometer
static int cmd_odometer(int argc, char **argv) {
  if (s_test_state.active) {
    ESP_LOGE(TAG, "Test already running. Cancel with pad %d first.", CANCEL_PAD);
    return 1;
  }
  
  ESP_LOGI(TAG, "=== Odometer Mode Test ===");
  ESP_LOGI(TAG, "Use pads 0-7 to rotate (0-100% range)");
  ESP_LOGI(TAG, "Pad %d = Confirm, Pad %d = Cancel", CONFIRM_PAD, CANCEL_PAD);
  ESP_LOGI(TAG, "Starting value: 50%%");
  
  s_test_state.current_value = 50;
  s_test_state.active = true;
  s_test_state.is_endless_mode = false;
  
  touchwheel_mode_processor_t* mode = touchwheel_mode_create_odometer();
  touchwheel_output_t* output = touchwheel_output_callback_create(test_value_callback, NULL);
  
  if (!mode || !output) {
    ESP_LOGE(TAG, "Failed to create touchwheel components");
    if (mode) touchwheel_mode_destroy(mode);
    if (output) touchwheel_output_destroy(output);
    s_test_state.active = false;
    return 1;
  }
  
  s_test_state.instance = touchwheel_create(mode, output, 500);
  if (!s_test_state.instance) {
    ESP_LOGE(TAG, "Failed to create touchwheel instance");
    touchwheel_mode_destroy(mode);
    touchwheel_output_destroy(output);
    s_test_state.active = false;
    return 1;
  }
  
  esp_err_t ret = touch_register_touchwheel_instance(s_test_state.instance);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register touchwheel instance");
    touchwheel_destroy(s_test_state.instance);
    s_test_state.instance = NULL;
    s_test_state.active = false;
    return 1;
  }
  
  event_bus_subscribe(EVENT_TOUCH_PRESS, test_touch_event_handler, NULL);
  event_bus_subscribe(EVENT_TOUCH_RELEASE, test_touch_event_handler, NULL);
  
  xTaskCreate(test_monitor_task, "tw_test", 2048, NULL, 5, &s_test_state.task_handle);
  
  ESP_LOGI(TAG, "Test active. Rotate with pads 0-7, confirm with pad %d, cancel with pad %d", CONFIRM_PAD, CANCEL_PAD);
  
  return 0;
}

// Command: bipolar
static int cmd_bipolar(int argc, char **argv) {
  if (s_test_state.active) {
    ESP_LOGE(TAG, "Test already running. Cancel with pad %d first.", CANCEL_PAD);
    return 1;
  }
  
  ESP_LOGI(TAG, "=== Bipolar Mode Test ===");
  ESP_LOGI(TAG, "Use pads 0-7 to rotate (-100 to +100 range)");
  ESP_LOGI(TAG, "Pad %d = Confirm, Pad %d = Cancel", CONFIRM_PAD, CANCEL_PAD);
  ESP_LOGI(TAG, "Starting value: 0");
  
  s_test_state.current_value = 0;
  s_test_state.active = true;
  s_test_state.is_endless_mode = false;
  
  touchwheel_mode_processor_t* mode = touchwheel_mode_create_bipolar();
  touchwheel_output_t* output = touchwheel_output_callback_create(test_value_callback, NULL);
  
  if (!mode || !output) {
    ESP_LOGE(TAG, "Failed to create touchwheel components");
    if (mode) touchwheel_mode_destroy(mode);
    if (output) touchwheel_output_destroy(output);
    s_test_state.active = false;
    return 1;
  }
  
  s_test_state.instance = touchwheel_create(mode, output, 500);
  if (!s_test_state.instance) {
    ESP_LOGE(TAG, "Failed to create touchwheel instance");
    touchwheel_mode_destroy(mode);
    touchwheel_output_destroy(output);
    s_test_state.active = false;
    return 1;
  }
  
  esp_err_t ret = touch_register_touchwheel_instance(s_test_state.instance);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register touchwheel instance");
    touchwheel_destroy(s_test_state.instance);
    s_test_state.instance = NULL;
    s_test_state.active = false;
    return 1;
  }
  
  event_bus_subscribe(EVENT_TOUCH_PRESS, test_touch_event_handler, NULL);
  event_bus_subscribe(EVENT_TOUCH_RELEASE, test_touch_event_handler, NULL);
  
  xTaskCreate(test_monitor_task, "tw_test", 2048, NULL, 5, &s_test_state.task_handle);
  
  ESP_LOGI(TAG, "Test active. Rotate with pads 0-7, confirm with pad %d, cancel with pad %d", CONFIRM_PAD, CANCEL_PAD);
  
  return 0;
}

// Command: stucktimeout
static int cmd_stucktimeout(int argc, char **argv) {
  if (argc < 2) {
    // Show current value
    uint32_t current = touch_get_stuck_timeout_ms();
    if (current == 0) {
      ESP_LOGI(TAG, "Stuck touch detection: DISABLED");
    } else {
      ESP_LOGI(TAG, "Stuck touch timeout: %"PRIu32" ms (%"PRIu32" seconds)", 
        current, current / 1000);
    }
    ESP_LOGI(TAG, "Usage: stucktimeout <ms>  (set timeout, 0 to disable)");
    return 0;
  }
  
  uint32_t timeout_ms = (uint32_t)atoi(argv[1]);
  touch_set_stuck_timeout_ms(timeout_ms);
  
  if (timeout_ms == 0) {
    ESP_LOGI(TAG, "Stuck touch detection DISABLED");
  } else {
    ESP_LOGI(TAG, "Stuck touch timeout set to %"PRIu32" ms (%"PRIu32" seconds)", 
      timeout_ms, timeout_ms / 1000);
  }
  return 0;
}

// Command: idlecalib
static int cmd_idlecalib(int argc, char **argv) {
  if (argc < 2) {
    // Show current value and status
    uint32_t interval = touch_get_idle_calibration_interval_ms();
    uint32_t last_touch = touch_get_last_touch_time_ms();
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    if (interval == 0) {
      ESP_LOGI(TAG, "Idle calibration: DISABLED");
    } else {
      ESP_LOGI(TAG, "Idle calibration: recalibrate after %"PRIu32" min without touch",
        interval / 60000);
    }
    
    uint32_t touch_idle = now - last_touch;
    ESP_LOGI(TAG, "  Time since last touch: %"PRIu32" sec", touch_idle / 1000);
    
    if (interval > 0) {
      if (touch_idle >= interval) {
        ESP_LOGI(TAG, "  Status: Will calibrate on next health check cycle");
      } else {
        uint32_t remaining = (interval - touch_idle) / 1000;
        uint32_t min = remaining / 60;
        uint32_t sec = remaining % 60;
        ESP_LOGI(TAG, "  Status: Next idle calibration in %"PRIu32"m %"PRIu32"s (if no touch)",
          min, sec);
      }
    }
    
    ESP_LOGI(TAG, "Usage: idlecalib <minutes>  (set interval, 0 to disable)");
    return 0;
  }
  
  uint32_t minutes = (uint32_t)atoi(argv[1]);
  uint32_t interval_ms = minutes * 60000;
  touch_set_idle_calibration_interval_ms(interval_ms);
  
  if (interval_ms == 0) {
    ESP_LOGI(TAG, "Idle calibration DISABLED");
  } else {
    ESP_LOGI(TAG, "Idle calibration: will recalibrate after %"PRIu32" min without touch", minutes);
  }
  return 0;
}

esp_err_t touch_console_init(void) {
  ESP_LOGI(TAG, "Registering touch commands");
  
  // calibrate command
  const esp_console_cmd_t calibrate_cmd = {
    .command = "calibrate",
    .help = "Calibrate touch sensors",
    .hint = NULL,
    .func = &cmd_calibrate,
  };
  esp_console_cmd_register(&calibrate_cmd);
  
  // reset command
  const esp_console_cmd_t reset_cmd = {
    .command = "reset",
    .help = "Reset stuck touch pads",
    .hint = NULL,
    .func = &cmd_reset,
  };
  esp_console_cmd_register(&reset_cmd);
  
  // debug command
  const esp_console_cmd_t debug_cmd = {
    .command = "debug",
    .help = "Enable debug logging",
    .hint = NULL,
    .func = &cmd_debug,
  };
  esp_console_cmd_register(&debug_cmd);
  
  // query command
  const esp_console_cmd_t query_cmd = {
    .command = "query",
    .help = "Query detailed info for a specific touch pad",
    .hint = "<pad_index>",
    .func = &cmd_query,
  };
  esp_console_cmd_register(&query_cmd);

  // recover command
  const esp_console_cmd_t recover_cmd = {
    .command = "recover",
    .help = "Clear quarantine on a pad and trigger recovery (captures STAGE0/1/2)",
    .hint = "<pad_index>",
    .func = &cmd_recover,
  };
  esp_console_cmd_register(&recover_cmd);
  
  // endless command
  const esp_console_cmd_t endless_cmd = {
    .command = "endless",
    .help = "Test endless encoder mode (pads 0-7 rotate, pad 8 confirm, pad 12 cancel)",
    .hint = NULL,
    .func = &cmd_endless,
  };
  esp_console_cmd_register(&endless_cmd);
  
  // odometer command
  const esp_console_cmd_t odometer_cmd = {
    .command = "odometer",
    .help = "Test odometer mode 0-100% (pads 0-7 rotate, pad 8 confirm, pad 12 cancel)",
    .hint = NULL,
    .func = &cmd_odometer,
  };
  esp_console_cmd_register(&odometer_cmd);
  
  // bipolar command
  const esp_console_cmd_t bipolar_cmd = {
    .command = "bipolar",
    .help = "Test bipolar mode -100 to +100 (pads 0-7 rotate, pad 8 confirm, pad 12 cancel)",
    .hint = NULL,
    .func = &cmd_bipolar,
  };
  esp_console_cmd_register(&bipolar_cmd);
  
  // stucktimeout command
  const esp_console_cmd_t stucktimeout_cmd = {
    .command = "stucktimeout",
    .help = "Get/set stuck touch timeout in ms (0 to disable, default 10000)",
    .hint = "[ms]",
    .func = &cmd_stucktimeout,
  };
  esp_console_cmd_register(&stucktimeout_cmd);
  
  // idlecalib command
  const esp_console_cmd_t idlecalib_cmd = {
    .command = "idlecalib",
    .help = "Get/set idle calibration interval in minutes (0 to disable, default 15)",
    .hint = "[minutes]",
    .func = &cmd_idlecalib,
  };
  esp_console_cmd_register(&idlecalib_cmd);
  
  return ESP_OK;
}

void touch_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering touch commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

