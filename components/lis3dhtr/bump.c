#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "i2c_common.h"
#include "app_settings.h"
#include "bump.h"
#include "event_bus.h"
#include "lis3dhtr_internal.h"
#include <string.h>

#define TAG "BUMP"

static bool s_logging_enabled = false;

#define NVS_KEY_BUMP_THRESHOLD   "bump_thresh"
#define NVS_KEY_BUMP_DEBOUNCE    "bump_debounce"
#define NVS_KEY_BUMP_INTENSITY   "bump_intensity"
#define NVS_KEY_BUMP_SENSITIVITY "bump_sens"
#define DEFAULT_BUMP_THRESHOLD         15
#define DEFAULT_BUMP_DEBOUNCE_MS       50
#define DEFAULT_BUMP_INTENSITY_MG      500
#define DEFAULT_BUMP_SENSITIVITY_LEVEL 5

#define BUMP_SENSITIVITY_MIN 1
#define BUMP_SENSITIVITY_MAX 10

typedef struct {
  uint8_t hw_threshold;
  uint32_t sw_threshold_mg;
} sensitivity_preset_t;

static const sensitivity_preset_t sensitivity_presets[] = {
  {3,  200},
  {5,  400},
  {8,  600},
  {10, 800},
  {13, 1000},
  {15, 1200},
  {18, 1400},
  {22, 1600},
  {25, 1800},
  {30, 2000},
};

static uint8_t  s_bump_threshold = DEFAULT_BUMP_THRESHOLD;
static uint32_t s_bump_debounce_ms = DEFAULT_BUMP_DEBOUNCE_MS;
static uint32_t s_bump_intensity_threshold_mg = DEFAULT_BUMP_INTENSITY_MG;
static uint8_t  s_bump_sensitivity_level = DEFAULT_BUMP_SENSITIVITY_LEVEL;
static volatile TickType_t s_last_bump_tick = 0;

void bump_handle_click(void) {
  i2c_master_dev_handle_t dev = lis3dhtr_get_dev();
  if (!dev) return;

  TickType_t now = xTaskGetTickCount();
  if ((now - s_last_bump_tick) * portTICK_PERIOD_MS < s_bump_debounce_ms) {
    uint8_t temp;
    i2c_common_read_reg(dev, LIS3DHTR_REG_INT1_SRC, &temp);
    return;
  }

  uint8_t int1_src = 0;
  i2c_common_read_reg(dev, LIS3DHTR_REG_INT1_SRC, &int1_src);

  uint8_t click_src = 0;
  esp_err_t ret = i2c_common_read_reg(dev, LIS3DHTR_REG_CLICK_SRC, &click_src);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read CLICK_SRC register: %s", esp_err_to_name(ret));
    return;
  }

  if (click_src == 0) {
    if (s_logging_enabled) {
      ESP_LOGD(TAG, "ISR triggered with empty CLICK_SRC (int1_src=0x%02X, possibly spurious)", int1_src);
    }
    return;
  }

  uint32_t magnitude = lis3dhtr_read_magnitude();

  if (s_logging_enabled) {
    ESP_LOGI(TAG, "Click detected (src: 0x%02X) - Magnitude: %u mg (threshold: %u mg)",
      click_src, (unsigned)magnitude, (unsigned)s_bump_intensity_threshold_mg);
  }

  if (magnitude < s_bump_intensity_threshold_mg) {
    if (s_logging_enabled) {
      ESP_LOGD(TAG, "Click ignored - magnitude %u mg below threshold %u mg",
        (unsigned)magnitude, (unsigned)s_bump_intensity_threshold_mg);
    }
    return;
  }

  s_last_bump_tick = now;

  event_t bump_event = {
    .type = EVENT_BUMP_DETECTED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.bump = {
      .intensity = magnitude,
      .duration_ms = 0
    }
  };
  esp_err_t post_ret = event_bus_post(&bump_event);
  if (s_logging_enabled) {
    ESP_LOGD(TAG, "Bump event posted (magnitude: %u mg) - Result: %s",
      (unsigned)magnitude, esp_err_to_name(post_ret));
  }
}

void bump_init(bool enable_logging) {
  s_logging_enabled = enable_logging;

  uint32_t stored_val;
  if (app_settings_load_u32(NVS_KEY_BUMP_THRESHOLD, &stored_val) == ESP_OK) {
    s_bump_threshold = (uint8_t)stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_BUMP_THRESHOLD, DEFAULT_BUMP_THRESHOLD);
  }

  if (app_settings_load_u32(NVS_KEY_BUMP_DEBOUNCE, &stored_val) == ESP_OK) {
    s_bump_debounce_ms = stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_BUMP_DEBOUNCE, DEFAULT_BUMP_DEBOUNCE_MS);
  }

  if (app_settings_load_u32(NVS_KEY_BUMP_INTENSITY, &stored_val) == ESP_OK) {
    s_bump_intensity_threshold_mg = stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_BUMP_INTENSITY, DEFAULT_BUMP_INTENSITY_MG);
  }

  if (app_settings_load_u32(NVS_KEY_BUMP_SENSITIVITY, &stored_val) == ESP_OK) {
    s_bump_sensitivity_level = (uint8_t)stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_BUMP_SENSITIVITY, DEFAULT_BUMP_SENSITIVITY_LEVEL);
  }

  lis3dhtr_init();

  i2c_master_dev_handle_t dev = lis3dhtr_get_dev();
  if (dev) i2c_common_write_reg(dev, LIS3DHTR_REG_CLICK_THS, s_bump_threshold);

  ESP_LOGI(TAG, "LIS3DHTR bump init (threshold: %d, debounce: %u ms, intensity: %u mg)",
    s_bump_threshold, (unsigned)s_bump_debounce_ms, (unsigned)s_bump_intensity_threshold_mg);
}

uint8_t bump_get_threshold(void) {
  return s_bump_threshold;
}

void bump_set_threshold(uint8_t threshold) {
  s_bump_threshold = threshold;
  i2c_master_dev_handle_t dev = lis3dhtr_get_dev();
  if (dev) i2c_common_write_reg(dev, LIS3DHTR_REG_CLICK_THS, s_bump_threshold);
  app_settings_save_u32(NVS_KEY_BUMP_THRESHOLD, s_bump_threshold);
  ESP_LOGI(TAG, "Bump threshold set to %d", s_bump_threshold);
}

uint32_t bump_get_debounce(void) {
  return s_bump_debounce_ms;
}

void bump_set_debounce(uint32_t ms) {
  s_bump_debounce_ms = ms;
  app_settings_save_u32(NVS_KEY_BUMP_DEBOUNCE, ms);
  ESP_LOGI(TAG, "Bump debounce set to %u ms", (unsigned)ms);
}

uint32_t bump_get_intensity_threshold(void) {
  return s_bump_intensity_threshold_mg;
}

void bump_set_intensity_threshold(uint32_t threshold_mg) {
  s_bump_intensity_threshold_mg = threshold_mg;
  app_settings_save_u32(NVS_KEY_BUMP_INTENSITY, threshold_mg);
  ESP_LOGI(TAG, "Bump intensity threshold set to %u mg", (unsigned)threshold_mg);
}

uint8_t bump_get_sensitivity_level(void) {
  return s_bump_sensitivity_level;
}

void bump_set_sensitivity_level(uint8_t level) {
  if (level < BUMP_SENSITIVITY_MIN || level > BUMP_SENSITIVITY_MAX) {
    ESP_LOGW(TAG, "Invalid sensitivity level %d (must be %d-%d)", level, BUMP_SENSITIVITY_MIN, BUMP_SENSITIVITY_MAX);
    return;
  }

  s_bump_sensitivity_level = level;
  const sensitivity_preset_t* preset = &sensitivity_presets[level - 1];
  bump_set_threshold(preset->hw_threshold);
  bump_set_intensity_threshold(preset->sw_threshold_mg);
  app_settings_save_u32(NVS_KEY_BUMP_SENSITIVITY, level);

  ESP_LOGI(TAG, "Bump sensitivity set to level %d (hw_thresh=%d, sw_thresh=%u mg)",
    level, preset->hw_threshold, (unsigned)preset->sw_threshold_mg);
}
