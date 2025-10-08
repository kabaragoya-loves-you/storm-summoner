#include "adc_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "ADC_MGR"

// Maximum number of channels to track
#define MAX_CHANNELS 10

// Channel registration tracking
typedef struct {
  adc_channel_t channel;
  bool registered;
} channel_info_t;

// Global state
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_unit_t s_adc_unit = ADC_UNIT_1;
static adc_bitwidth_t s_bitwidth = ADC_BITWIDTH_12;
static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static channel_info_t s_channels[MAX_CHANNELS] = {0};
static int s_channel_count = 0;

esp_err_t adc_manager_init(adc_unit_t unit, adc_bitwidth_t bitwidth) {
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
    .unit_id = unit,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  
  esp_err_t ret = adc_oneshot_new_unit(&init_config, &s_adc_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize ADC unit %d: %s", unit, esp_err_to_name(ret));
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    return ret;
  }
  
  s_adc_unit = unit;
  s_bitwidth = bitwidth;
  s_initialized = true;
  
  ESP_LOGI(TAG, "ADC manager initialized for unit %d, bitwidth %d", unit, bitwidth);
  
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
    .bitwidth = s_bitwidth,
    .atten = atten,
  };
  
  esp_err_t ret = adc_oneshot_config_channel(s_adc_handle, channel, &config);
  if (ret != ESP_OK) {
    xSemaphoreGive(s_mutex);
    ESP_LOGE(TAG, "Failed to configure channel %d: %s", channel, esp_err_to_name(ret));
    return ret;
  }
  
  // Track registration
  s_channels[s_channel_count].channel = channel;
  s_channels[s_channel_count].registered = true;
  s_channel_count++;
  
  xSemaphoreGive(s_mutex);
  
  ESP_LOGI(TAG, "Registered ADC channel %d with attenuation %d", channel, atten);
  
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
  
  // Verify channel is registered
  bool found = false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  for (int i = 0; i < s_channel_count; i++) {
    if (s_channels[i].channel == channel && s_channels[i].registered) {
      found = true;
      break;
    }
  }
  xSemaphoreGive(s_mutex);
  
  if (!found) {
    ESP_LOGE(TAG, "Channel %d not registered", channel);
    return ESP_ERR_NOT_FOUND;
  }
  
  // Read ADC (oneshot read is internally thread-safe in ESP-IDF)
  esp_err_t ret = adc_oneshot_read(s_adc_handle, channel, raw_value);
  if (ret != ESP_OK) {
    ESP_LOGD(TAG, "Failed to read channel %d: %s", channel, esp_err_to_name(ret));
    return ret;
  }
  
  return ESP_OK;
}

adc_unit_t adc_manager_get_unit(void) {
  return s_adc_unit;
}

bool adc_manager_is_initialized(void) {
  return s_initialized;
}

void adc_manager_deinit(void) {
  if (!s_initialized) {
    return;
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

