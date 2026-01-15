#include "display_console.h"
#include "display_driver.h"
#include "st7789v3_driver.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/ledc.h"
#include "argtable3/argtable3.h"
#include "io.h"
#include <stdlib.h>

static const char* TAG = "display_console";

// PWM configuration for backlight
#define BACKLIGHT_LEDC_TIMER    LEDC_TIMER_0
#define BACKLIGHT_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_DUTY_RES LEDC_TIMER_10_BIT  // 0-1023
#define BACKLIGHT_LEDC_FREQ     5000  // 5 kHz PWM

static bool s_pwm_initialized = false;
static uint8_t s_current_brightness = 70;  // Default 70% to reduce power draw

static const char* registered_commands[] = {
  "info",
  "brightness",
  "viewport"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Initialize backlight PWM
static esp_err_t backlight_pwm_init(void) {
  if (s_pwm_initialized) return ESP_OK;
  
  // Configure LEDC timer
  ledc_timer_config_t timer_conf = {
    .speed_mode = BACKLIGHT_LEDC_MODE,
    .timer_num = BACKLIGHT_LEDC_TIMER,
    .duty_resolution = BACKLIGHT_LEDC_DUTY_RES,
    .freq_hz = BACKLIGHT_LEDC_FREQ,
    .clk_cfg = LEDC_AUTO_CLK
  };
  esp_err_t ret = ledc_timer_config(&timer_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
    return ret;
  }
  
  // Configure LEDC channel
  ledc_channel_config_t channel_conf = {
    .speed_mode = BACKLIGHT_LEDC_MODE,
    .channel = BACKLIGHT_LEDC_CHANNEL,
    .timer_sel = BACKLIGHT_LEDC_TIMER,
    .intr_type = LEDC_INTR_DISABLE,
    .gpio_num = PIN_BACKLIGHT,
    .duty = 716,  // Start at 70% (716/1023)
    .hpoint = 0
  };
  ret = ledc_channel_config(&channel_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
    return ret;
  }
  
  s_pwm_initialized = true;
  ESP_LOGI(TAG, "Backlight PWM initialized on GPIO %d", PIN_BACKLIGHT);
  return ESP_OK;
}

// Set backlight brightness (0-100%)
esp_err_t display_set_brightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  
  if (!s_pwm_initialized) {
    esp_err_t ret = backlight_pwm_init();
    if (ret != ESP_OK) return ret;
  }
  
  // Convert percentage to duty cycle (0-1023)
  uint32_t duty = (percent * 1023) / 100;
  
  esp_err_t ret = ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
  if (ret != ESP_OK) return ret;
  
  ret = ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
  if (ret != ESP_OK) return ret;
  
  s_current_brightness = percent;
  return ESP_OK;
}

uint8_t display_get_brightness(void) {
  return s_current_brightness;
}

// Command: info
static int cmd_info(int argc, char **argv) {
  const display_driver_t *driver = display_driver_get();
  
  printf("====== DISPLAY ======\n");
  if (driver) {
    printf("Driver: %s\n", driver->name);
    printf("Resolution: %dx%d\n", driver->width, driver->height);
    printf("Color format: %s\n", 
           driver->color_format == LV_COLOR_FORMAT_RGB888 ? "RGB888 (24-bit)" :
           driver->color_format == LV_COLOR_FORMAT_RGB565 ? "RGB565 (16-bit)" :
           driver->color_format == LV_COLOR_FORMAT_RGB565_SWAPPED ? "RGB565 Swapped (16-bit)" : "Other");
  } else {
    printf("No display driver initialized\n");
  }
  printf("Brightness: %d%%\n", s_current_brightness);
  printf("Backlight GPIO: %d\n", PIN_BACKLIGHT);
  printf("=====================\n");
  
  return 0;
}

// Command: brightness <percent>
static struct {
  struct arg_int *percent;
  struct arg_end *end;
} brightness_args;

static int cmd_brightness(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &brightness_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, brightness_args.end, argv[0]);
    return 1;
  }
  
  if (brightness_args.percent->count == 0) {
    // No argument - show current brightness
    printf("Current brightness: %d%%\n", s_current_brightness);
    return 0;
  }
  
  int percent = brightness_args.percent->ival[0];
  if (percent < 0 || percent > 100) {
    printf("Error: Brightness must be 0-100%%\n");
    return 1;
  }
  
  esp_err_t ret = display_set_brightness((uint8_t)percent);
  if (ret != ESP_OK) {
    printf("Error: Failed to set brightness: %s\n", esp_err_to_name(ret));
    return 1;
  }
  
  printf("Brightness set to %d%%\n", percent);
  return 0;
}

// Command: viewport [offset_x offset_y width height]
static int cmd_viewport(int argc, char **argv) {
  if (argc < 5) {
    // Show current viewport
    printf("Viewport: offset=(%d,%d) size=%dx%d\n",
      st7789v3_get_viewport_offset_x(), st7789v3_get_viewport_offset_y(),
      st7789v3_get_viewport_width(), st7789v3_get_viewport_height());
    printf("Usage: viewport <offset_x> <offset_y> <width> <height>\n");
    return 0;
  }
  
  int16_t offset_x = atoi(argv[1]);
  int16_t offset_y = atoi(argv[2]);
  uint16_t width = atoi(argv[3]);
  uint16_t height = atoi(argv[4]);
  
  st7789v3_set_viewport(offset_x, offset_y, width, height);
  printf("Viewport set: offset=(%d,%d) size=%dx%d\n", offset_x, offset_y, width, height);
  printf("Note: Restart required for LVGL to use new dimensions\n");
  return 0;
}

esp_err_t display_console_init(void) {
  ESP_LOGI(TAG, "Registering display commands");
  
  // Initialize backlight PWM
  backlight_pwm_init();
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show display status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // brightness command
  brightness_args.percent = arg_int0(NULL, NULL, "<percent>", "Brightness 0-100%");
  brightness_args.end = arg_end(2);
  
  const esp_console_cmd_t brightness_cmd = {
    .command = "brightness",
    .help = "Get or set backlight brightness (0-100%)",
    .hint = NULL,
    .func = &cmd_brightness,
    .argtable = &brightness_args
  };
  esp_console_cmd_register(&brightness_cmd);
  
  // viewport command
  const esp_console_cmd_t viewport_cmd = {
    .command = "viewport",
    .help = "Get or set display viewport (offset_x offset_y width height)",
    .hint = NULL,
    .func = &cmd_viewport,
  };
  esp_console_cmd_register(&viewport_cmd);
  
  return ESP_OK;
}

void display_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering display commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}
