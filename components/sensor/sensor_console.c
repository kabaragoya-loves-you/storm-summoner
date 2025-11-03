#include "sensor_console.h"
#include "sensor.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static const char* TAG = "sensor_console";

static const char* registered_commands[] = {
  "info", "calibrate_ps", "calibrate_als"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  uint16_t ps = get_ps();
  uint16_t als = get_als();
  uint16_t ps_min, ps_max, als_min, als_max;
  
  proximity_get_calibration(&ps_min, &ps_max);
  als_get_calibration(&als_min, &als_max);
  
  ESP_LOGI(TAG, "====== SENSOR ======");
  ESP_LOGI(TAG, "Proximity: %u (cal: %u-%u)", (unsigned)ps, (unsigned)ps_min, (unsigned)ps_max);
  ESP_LOGI(TAG, "ALS: %u (cal: %u-%u)", (unsigned)als, (unsigned)als_min, (unsigned)als_max);
  ESP_LOGI(TAG, "PS deadzone: %u", (unsigned)proximity_get_deadzone());
  ESP_LOGI(TAG, "ALS deadzone: %u", (unsigned)als_get_deadzone());
  ESP_LOGI(TAG, "====================");
  
  return 0;
}

// Command: calibrate_ps
static struct {
  struct arg_int *duration;
  struct arg_end *end;
} calibrate_ps_args;

static int cmd_calibrate_ps(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &calibrate_ps_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, calibrate_ps_args.end, argv[0]);
    return 1;
  }
  
  int duration = calibrate_ps_args.duration->ival[0];
  ESP_LOGI(TAG, "Calibrating proximity for %d ms (move hand from far to near)...", duration);
  
  esp_err_t ret = proximity_auto_calibrate(duration);
  if (ret == ESP_OK) {
    uint16_t min, max;
    proximity_get_calibration(&min, &max);
    ESP_LOGI(TAG, "Proximity calibrated: %u - %u", (unsigned)min, (unsigned)max);
  } else {
    ESP_LOGE(TAG, "Calibration failed");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: calibrate_als
static struct {
  struct arg_int *duration;
  struct arg_end *end;
} calibrate_als_args;

static int cmd_calibrate_als(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &calibrate_als_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, calibrate_als_args.end, argv[0]);
    return 1;
  }
  
  int duration = calibrate_als_args.duration->ival[0];
  ESP_LOGI(TAG, "Calibrating ALS for %d ms (cover and uncover sensor)...", duration);
  
  esp_err_t ret = als_auto_calibrate(duration);
  if (ret == ESP_OK) {
    uint16_t min, max;
    als_get_calibration(&min, &max);
    ESP_LOGI(TAG, "ALS calibrated: %u - %u", (unsigned)min, (unsigned)max);
  } else {
    ESP_LOGE(TAG, "Calibration failed");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

esp_err_t sensor_console_init(void) {
  ESP_LOGI(TAG, "Registering sensor commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show sensor readings",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // calibrate_ps command
  calibrate_ps_args.duration = arg_int1(NULL, NULL, "<ms>", "Calibration duration in ms");
  calibrate_ps_args.end = arg_end(2);
  
  const esp_console_cmd_t calibrate_ps_cmd = {
    .command = "calibrate_ps",
    .help = "Calibrate proximity sensor",
    .hint = NULL,
    .func = &cmd_calibrate_ps,
    .argtable = &calibrate_ps_args
  };
  esp_console_cmd_register(&calibrate_ps_cmd);
  
  // calibrate_als command
  calibrate_als_args.duration = arg_int1(NULL, NULL, "<ms>", "Calibration duration in ms");
  calibrate_als_args.end = arg_end(2);
  
  const esp_console_cmd_t calibrate_als_cmd = {
    .command = "calibrate_als",
    .help = "Calibrate ambient light sensor",
    .hint = NULL,
    .func = &cmd_calibrate_als,
    .argtable = &calibrate_als_args
  };
  esp_console_cmd_register(&calibrate_als_cmd);
  
  return ESP_OK;
}

void sensor_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering sensor commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

