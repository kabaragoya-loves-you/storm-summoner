#include "revision.h"
#include "io.h"
#include "adc_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "REVISION"

// Number of samples to average for stable reading
#define NUM_SAMPLES 10

// ADC threshold ranges for each revision (with hysteresis)
// Based on 10k top resistor, variable bottom resistor, 3.3V supply
// Using midpoints between expected values for thresholds
#define REV1_THRESHOLD_MAX  234   // Midpoint between Rev1 (71) and Rev2 (396)
#define REV2_THRESHOLD_MAX  590   // Midpoint between Rev2 (396) and Rev3 (785)
#define REV3_THRESHOLD_MAX  934   // Midpoint between Rev3 (785) and Rev4 (1083)
#define REV4_THRESHOLD_MAX  1278  // Midpoint between Rev4 (1083) and Rev5 (1473)

// Global state
static hw_revision_t s_hw_revision = HW_REV_UNKNOWN;
static uint16_t s_raw_adc_value = 0;
static bool s_initialized = false;

// Map ADC value to hardware revision
static hw_revision_t map_adc_to_revision(uint16_t adc_value) {
  if (adc_value < REV1_THRESHOLD_MAX) {
    return HW_REV_1;
  } else if (adc_value < REV2_THRESHOLD_MAX) {
    return HW_REV_2;
  } else if (adc_value < REV3_THRESHOLD_MAX) {
    return HW_REV_3;
  } else if (adc_value < REV4_THRESHOLD_MAX) {
    return HW_REV_4;
  } else {
    return HW_REV_5;
  }
}

esp_err_t revision_init(int force_revision) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Revision already initialized");
    return ESP_OK;
  }

  // Check for forced revision (valid range 1-5)
  if (force_revision >= HW_REV_1 && force_revision <= HW_REV_5) {
    s_hw_revision = (hw_revision_t)force_revision;
    s_raw_adc_value = 0;
    s_initialized = true;
    ESP_LOGI(TAG, "Hardware revision forced: %s", revision_get_string());
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Starting hardware revision detection");

  // Register our ADC channel with the manager
  esp_err_t ret = adc_manager_register_channel(REV_ADC_CHANNEL, ADC_ATTEN);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register ADC channel: %s", esp_err_to_name(ret));
    return ret;
  }

  // Let ADC settle
  vTaskDelay(pdMS_TO_TICKS(10));

  // Read and average multiple samples
  uint32_t sum = 0;
  int valid_samples = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {
    int raw = 0;
    ret = adc_manager_read(REV_ADC_CHANNEL, &raw);
    if (ret == ESP_OK) {
      sum += raw;
      valid_samples++;
    }
  }

  if (valid_samples > 0) {
    s_raw_adc_value = (uint16_t)(sum / valid_samples);
  } else {
    ESP_LOGE(TAG, "Failed to read revision ADC");
    return ESP_FAIL;
  }

  // Map to hardware revision
  s_hw_revision = map_adc_to_revision(s_raw_adc_value);

  s_initialized = true;

  ESP_LOGI(TAG, "Hardware revision detected: %s (raw ADC: %u)", revision_get_string(), s_raw_adc_value);
  ESP_LOGI(TAG, "Expected ADC ranges: Rev1:<234, Rev2:<590, Rev3:<934, Rev4:<1278, Rev5:>=1278");

  return ESP_OK;
}

hw_revision_t revision_get(void) {
  if (!s_initialized) {
    ESP_LOGW(TAG, "Revision not initialized, returning UNKNOWN");
    return HW_REV_UNKNOWN;
  }
  return s_hw_revision;
}

const char* revision_get_string(void) {
  switch (s_hw_revision) {
    case HW_REV_1: return "Rev 1";
    case HW_REV_2: return "Rev 2";
    case HW_REV_3: return "Rev 3";
    case HW_REV_4: return "Rev 4";
    case HW_REV_5: return "Rev 5";
    case HW_REV_UNKNOWN:
    default:
      return "Unknown";
  }
}

uint16_t revision_get_raw_adc(void) {
  return s_raw_adc_value;
}
