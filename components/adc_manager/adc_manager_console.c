#include "adc_manager_console.h"
#include "adc_manager.h"
#include "io.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "adc_mgr_console";

static const char* registered_commands[] = {
  "info", "sample_ref"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  if (!adc_manager_is_initialized()) {
    ESP_LOGE(TAG, "ADC manager not initialized");
    return 1;
  }
  
  ESP_LOGI(TAG, "====== ADC Manager ======");
  ESP_LOGI(TAG, "Unit: ADC_UNIT_%d", ADC_UNIT == 0 ? 1 : 2);
  ESP_LOGI(TAG, "Bitwidth: %d-bit", ADC_BITWIDTH == ADC_BITWIDTH_12 ? 12 : ADC_BITWIDTH);
  ESP_LOGI(TAG, "Attenuation: %ddB", ADC_ATTEN == ADC_ATTEN_DB_12 ? 12 : ADC_ATTEN);
  ESP_LOGI(TAG, "Channels:");
  ESP_LOGI(TAG, "  CV: CH%d (GPIO%d)", CV_ADC_CHANNEL, 
           ADC_UNIT == ADC_UNIT_2 ? 49 : 16);
  ESP_LOGI(TAG, "  Expression: CH%d (GPIO%d)", EXP_ADC_CHANNEL,
           ADC_UNIT == ADC_UNIT_2 ? 50 : 17);
  ESP_LOGI(TAG, "  Reference: CH%d (GPIO%d)", REF_ADC_CHANNEL,
           ADC_UNIT == ADC_UNIT_2 ? 51 : 18);
  ESP_LOGI(TAG, "  CV Switch: CH%d (GPIO%d)", CV_SW_ADC_CHANNEL,
           ADC_UNIT == ADC_UNIT_2 ? 52 : 20);
  ESP_LOGI(TAG, "  Revision: CH%d (GPIO%d)", REV_ADC_CHANNEL,
           ADC_UNIT == ADC_UNIT_2 ? 53 : 19);
  ESP_LOGI(TAG, "========================");
  
  return 0;
}

// Command: sample_ref
static int cmd_sample_ref(int argc, char **argv) {
  if (!adc_manager_is_initialized()) {
    ESP_LOGE(TAG, "ADC manager not initialized");
    return 1;
  }
  
  ESP_LOGI(TAG, "Sampling REF_ADC_CHANNEL (VCC reference)...");
  
  // Take multiple samples and average for accuracy
  const int num_samples = 16;
  int32_t sum_mv = 0;
  int32_t sum_raw = 0;
  int successful_reads = 0;
  
  for (int i = 0; i < num_samples; i++) {
    int raw_value = 0;
    int voltage_mv = 0;
    
    esp_err_t ret_raw = adc_manager_read(REF_ADC_CHANNEL, &raw_value);
    esp_err_t ret_cal = adc_manager_read_calibrated(REF_ADC_CHANNEL, &voltage_mv);
    
    if (ret_raw == ESP_OK && ret_cal == ESP_OK) {
      sum_raw += raw_value;
      sum_mv += voltage_mv;
      successful_reads++;
    }
    vTaskDelay(pdMS_TO_TICKS(2));  // Small delay between samples
  }
  
  if (successful_reads == 0) {
    ESP_LOGE(TAG, "Failed to read reference ADC - all samples failed");
    return 1;
  }
  
  int avg_raw = sum_raw / successful_reads;
  int avg_mv = sum_mv / successful_reads;
  float avg_v = avg_mv / 1000.0f;
  
  ESP_LOGI(TAG, "Reference ADC Results:");
  ESP_LOGI(TAG, "  Samples: %d/%d successful", successful_reads, num_samples);
  ESP_LOGI(TAG, "  Raw value: %d (0-4095)", avg_raw);
  ESP_LOGI(TAG, "  Calibrated: %dmV (%.3fV)", avg_mv, avg_v);
  ESP_LOGI(TAG, "  Expected VCC range: 2.5V - 3.8V");
  
  if (avg_v < 2.5f || avg_v > 3.8f) {
    ESP_LOGW(TAG, "VCC reading outside expected range!");
  }
  
  return 0;
}

esp_err_t adc_manager_console_init(void) {
  ESP_LOGI(TAG, "Registering adc_manager commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show ADC manager status and channels",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // sample_ref command
  const esp_console_cmd_t sample_ref_cmd = {
    .command = "sample_ref",
    .help = "Sample reference ADC channel (VCC)",
    .hint = NULL,
    .func = &cmd_sample_ref,
  };
  esp_console_cmd_register(&sample_ref_cmd);
  
  return ESP_OK;
}

void adc_manager_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering adc_manager commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

