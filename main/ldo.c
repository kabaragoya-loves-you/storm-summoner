#include "ldo.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"

#define TAG "LDO"

static esp_ldo_channel_handle_t s_ldo_vo4_handle = NULL;

esp_err_t ldo_init(void) {
  esp_ldo_channel_config_t ldo_config = {
    .chan_id = 4,
    .voltage_mv = 3300,
    .flags = {
      .adjustable = false,
      .owned_by_hw = false,
      .bypass = true,
    }
  };

  esp_err_t ret = esp_ldo_acquire_channel(&ldo_config, &s_ldo_vo4_handle);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "VO4 configured for 3.3V bypass mode");
  } else {
    ESP_LOGE(TAG, "Failed to configure VO4: %s", esp_err_to_name(ret));
  }
  return ret;
}
