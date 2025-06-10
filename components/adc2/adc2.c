#include "adc2.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define TAG "ADC2"

static adc_oneshot_unit_handle_t adc_handle = NULL;

adc_oneshot_unit_handle_t adc2_handle(void) {
  esp_err_t err;
  if (adc_handle != NULL) return adc_handle;
  
  adc_oneshot_unit_init_cfg_t unit_config = {
    .unit_id = ADC_UNIT,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  err = adc_oneshot_new_unit(&unit_config, &adc_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %d", err);
    return NULL;
  }
  return adc_handle;
}
