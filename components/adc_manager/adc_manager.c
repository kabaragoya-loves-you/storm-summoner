#include "adc_manager.h"
#include "esp_log.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "io.h"
#include <string.h>

#define TAG "ADC_MGR"

// Maximum number of channels to track
#define MAX_CHANNELS 10

// Channel registration tracking
typedef struct {
  adc_channel_t channel;
  adc_atten_t atten;
  adc_cali_handle_t cali_handle;
  bool registered;
  bool calibrated;
} channel_info_t;

// Global state
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static channel_info_t s_channels[MAX_CHANNELS] = {0};
static int s_channel_count = 0;

esp_err_t adc_manager_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "ADC manager already initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Create mutex for thread-safe operations
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_ERR_NO_MEM;
  }
  
  // Configure ADC unit
  adc_oneshot_unit_init_cfg_t init_config = {
    .unit_id = ADC_UNIT,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  
  esp_err_t ret = adc_oneshot_new_unit(&init_config, &s_adc_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize ADC unit %d: %s", ADC_UNIT, esp_err_to_name(ret));
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    return ret;
  }
  
  s_initialized = true;
  
  ESP_LOGI(TAG, "ADC manager initialized for unit %d, bitwidth %d", ADC_UNIT, ADC_BITWIDTH);
  
  return ESP_OK;
}

esp_err_t adc_manager_register_channel(adc_channel_t channel, adc_atten_t atten) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "ADC manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  if (s_channel_count >= MAX_CHANNELS) {
    ESP_LOGE(TAG, "Maximum number of channels (%d) already registered", MAX_CHANNELS);
    return ESP_ERR_NO_MEM;
  }
  
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  
  // Check if channel already registered
  for (int i = 0; i < s_channel_count; i++) {
    if (s_channels[i].channel == channel && s_channels[i].registered) {
      xSemaphoreGive(s_mutex);
      ESP_LOGD(TAG, "Channel %d already registered", channel);
      return ESP_OK;
    }
  }
  
  // Configure channel
  adc_oneshot_chan_cfg_t config = {
    .bitwidth = ADC_BITWIDTH,
    .atten = atten,
  };
  
  esp_err_t ret = adc_oneshot_config_channel(s_adc_handle, channel, &config);
  if (ret != ESP_OK) {
    xSemaphoreGive(s_mutex);
    ESP_LOGE(TAG, "Failed to configure channel %d: %s", channel, esp_err_to_name(ret));
    return ret;
  }
  
  // Create calibration scheme for this channel
  adc_cali_handle_t cali_handle = NULL;
  adc_cali_curve_fitting_config_t cali_config = {
    .unit_id = ADC_UNIT,
    .atten = atten,
    .bitwidth = ADC_BITWIDTH,
  };
  
  ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
  bool calibrated = (ret == ESP_OK);
  if (!calibrated) {
    ESP_LOGW(TAG, "Calibration scheme not available for channel %d: %s", channel, esp_err_to_name(ret));
  }
  
  // Track registration
  s_channels[s_channel_count].channel = channel;
  s_channels[s_channel_count].atten = atten;
  s_channels[s_channel_count].cali_handle = cali_handle;
  s_channels[s_channel_count].registered = true;
  s_channels[s_channel_count].calibrated = calibrated;
  s_channel_count++;
  
  xSemaphoreGive(s_mutex);
  
  ESP_LOGI(TAG, "Registered ADC channel %d with attenuation %d (calibration: %s)", 
    channel, atten, calibrated ? "enabled" : "unavailable");
  
  return ESP_OK;
}

esp_err_t adc_manager_read(adc_channel_t channel, int *raw_value) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "ADC manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  if (raw_value == NULL) {
    ESP_LOGE(TAG, "raw_value pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  
  // Verify channel is registered and perform read under mutex protection
  // The mutex prevents concurrent ADC reads which cause ESP_ERR_TIMEOUT
  bool found = false;
  esp_err_t ret;
  
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  
  for (int i = 0; i < s_channel_count; i++) {
    if (s_channels[i].channel == channel && s_channels[i].registered) {
      found = true;
      break;
    }
  }
  
  if (!found) {
    xSemaphoreGive(s_mutex);
    ESP_LOGE(TAG, "Channel %d not registered", channel);
    return ESP_ERR_NOT_FOUND;
  }
  
  // Read ADC while holding mutex to prevent concurrent access
  ret = adc_oneshot_read(s_adc_handle, channel, raw_value);
  
  xSemaphoreGive(s_mutex);
  
  if (ret != ESP_OK) {
    ESP_LOGD(TAG, "Failed to read channel %d: %s", channel, esp_err_to_name(ret));
    return ret;
  }
  
  return ESP_OK;
}

esp_err_t adc_manager_read_calibrated(adc_channel_t channel, int *voltage_mv) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "ADC manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  if (voltage_mv == NULL) {
    ESP_LOGE(TAG, "voltage_mv pointer is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  
  // Find channel and perform read under mutex protection
  adc_cali_handle_t cali_handle = NULL;
  bool found = false;
  int raw_value;
  esp_err_t ret;
  
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  
  for (int i = 0; i < s_channel_count; i++) {
    if (s_channels[i].channel == channel && s_channels[i].registered) {
      found = true;
      cali_handle = s_channels[i].cali_handle;
      break;
    }
  }
  
  if (!found) {
    xSemaphoreGive(s_mutex);
    ESP_LOGE(TAG, "Channel %d not registered", channel);
    return ESP_ERR_NOT_FOUND;
  }
  
  // Read ADC while holding mutex to prevent concurrent access
  ret = adc_oneshot_read(s_adc_handle, channel, &raw_value);
  
  xSemaphoreGive(s_mutex);
  
  if (ret != ESP_OK) {
    ESP_LOGD(TAG, "Failed to read channel %d: %s", channel, esp_err_to_name(ret));
    return ret;
  }
  
  // Apply calibration if available
  if (cali_handle != NULL) {
    ret = adc_cali_raw_to_voltage(cali_handle, raw_value, voltage_mv);
    if (ret != ESP_OK) {
      ESP_LOGD(TAG, "Failed to calibrate channel %d: %s", channel, esp_err_to_name(ret));
      // Fall back to linear approximation
      // Use 3300mV as practical upper bound (DB_12 nominal is 3100mV but extends higher)
      *voltage_mv = (raw_value * 3300) / 4095;
    }
  } else {
    // No calibration available, use linear approximation
    // DB_12 attenuation: nominal 0-3100mV, practical range extends to ~3300mV
    *voltage_mv = (raw_value * 3300) / 4095;
  }
  
  return ESP_OK;
}

bool adc_manager_is_initialized(void) {
  return s_initialized;
}

void adc_manager_deinit(void) {
  if (!s_initialized) {
    return;
  }
  
  // Delete calibration schemes
  for (int i = 0; i < s_channel_count; i++) {
    if (s_channels[i].calibrated && s_channels[i].cali_handle != NULL) {
      adc_cali_delete_scheme_curve_fitting(s_channels[i].cali_handle);
    }
  }
  
  if (s_adc_handle) {
    adc_oneshot_del_unit(s_adc_handle);
    s_adc_handle = NULL;
  }
  
  if (s_mutex) {
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
  }
  
  memset(s_channels, 0, sizeof(s_channels));
  s_channel_count = 0;
  s_initialized = false;
  
  ESP_LOGI(TAG, "ADC manager deinitialized");
}

