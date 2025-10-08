#include "revision.h"
#include "io.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "REVISION"

// ESP32-P4 ADC configuration
#define REV_ADC_UNIT     ADC_UNIT_1
#define REV_ADC_CHANNEL  ADC_CHANNEL_3  // GPIO19
#define REV_ADC_ATTEN    ADC_ATTEN_DB_12
#define REV_ADC_BITWIDTH ADC_BITWIDTH_12

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

// Read ADC with averaging for stable result
static uint16_t read_revision_adc(adc_oneshot_unit_handle_t adc_handle) {
  uint32_t sum = 0;
  int valid_samples = 0;
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, REV_ADC_CHANNEL, &raw);
    if (ret == ESP_OK) {
      sum += raw;
      valid_samples++;
    }
  }
  
  if (valid_samples > 0) {
    return (uint16_t)(sum / valid_samples);
  }
  return 0;
}

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

esp_err_t revision_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Revision already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Starting hardware revision detection");
  
  // Configure ADC oneshot unit
  adc_oneshot_unit_init_cfg_t init_config = {
    .unit_id = REV_ADC_UNIT,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  
  adc_oneshot_unit_handle_t adc_handle = NULL;
  esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
    // If ADC unit already exists, default to Rev 1
    if (ret == ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG, "ADC unit may already be initialized by another component, defaulting to Rev 1");
      s_initialized = true;
      s_hw_revision = HW_REV_1;
      return ESP_OK;
    }
    return ret;
  }
  
  // Configure revision detection channel
  adc_oneshot_chan_cfg_t config = {
    .bitwidth = REV_ADC_BITWIDTH,
    .atten = REV_ADC_ATTEN,
  };
  
  ret = adc_oneshot_config_channel(adc_handle, REV_ADC_CHANNEL, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
    adc_oneshot_del_unit(adc_handle);
    return ret;
  }
  
  // Let ADC settle
  vTaskDelay(pdMS_TO_TICKS(10));
  
  // Read and average multiple samples
  s_raw_adc_value = read_revision_adc(adc_handle);
  
  // Map to hardware revision
  s_hw_revision = map_adc_to_revision(s_raw_adc_value);
  
  // CRITICAL: Clean up ADC immediately so other components can use it
  ret = adc_oneshot_del_unit(adc_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to delete ADC unit: %s (may cause issues for other ADC users)", esp_err_to_name(ret));
  }
  
  // Give ADC driver time to fully clean up
  vTaskDelay(pdMS_TO_TICKS(50));
  
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
