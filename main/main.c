#include "display.h"
#include "stars.h"
#include "touch.h"
#include "bump.h"
#include "haptic_manager.h"
#include "led.h"
#include "sensor.h"
#include "midi_out.h"
#include "midi_messages.h"
#include "midi_callbacks.h"
#include "tempo.h"
#include "elite.h"
#include "ui.h"
#include "app_settings.h"
#include "event_bus.h"
#include "screensaver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ads1015.h"
#include "expression.h"
#include "esp_heap_caps.h"
#include "performance.h"
#include "driver/gpio.h"
// #include "esp_wifi.h"
#include "io.h"
#include "task_monitor.h"
#include "transport.h"
#include "tempo.h"
#include "input_manager.h"
#include "cv.h"
#include "mcp4725.h"
#include "freertos/queue.h"

#define TAG "MAIN"

// Button GPIOs for CV range selection
#define BTN_10V_BIPOLAR  GPIO_NUM_11
#define BTN_5V_BIPOLAR   GPIO_NUM_12
#define BTN_10V_UNIPOLAR GPIO_NUM_13
#define BTN_5V_UNIPOLAR  GPIO_NUM_14
#define BTN_3V3_UNIPOLAR GPIO_NUM_15

static QueueHandle_t cv_range_queue = NULL;

// ISR handlers for CV range buttons - defer work to task context
static void IRAM_ATTR cv_range_button_isr(void* arg) {
  mcp4725_cv_range_t range = (mcp4725_cv_range_t)(uint32_t)arg;
  BaseType_t higher_prio_woken = pdFALSE;
  xQueueSendFromISR(cv_range_queue, &range, &higher_prio_woken);
  if (higher_prio_woken) portYIELD_FROM_ISR();
}

// Task to handle CV range changes
static void cv_range_task(void* arg) {
  mcp4725_cv_range_t range;
  while (1) {
    if (xQueueReceive(cv_range_queue, &range, portMAX_DELAY)) {
      ESP_LOGI(TAG, "Button pressed, switching to CV range mode %d", range);
      esp_err_t ret = mcp4725_set_cv_range(range);
      if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to set CV range: %s", esp_err_to_name(ret));
    }
  }
}

void app_main(void) {
  // esp_wifi_deinit();

  app_settings_init();
  
  event_bus_init();

  mcp4725_init();
  
  // display_init();
  
  // ui_init();
  // ui_set_draw_module(&buttons_module);
  
  // touch_init();
  // force_touch_calibration();
  
  // bump_init();
  // haptic_init();
  // led_init();
  // flicker_start();
  
  // midi_out_init();
  // midi_set_transmit_mode(MIDI_TRANSMIT_BOTH);
  // midi_callbacks_init();
  
  // ads1015_init();
  // expression_init();
  // expression_enable();
  
  // input_manager_init();
  // input_set_mode(INPUT_MODE_CV);
  // cv_set_mode(CV_MODE_LINEAR);               // Linear voltage to MIDI mapping
  // input_set_mode(INPUT_MODE_CLOCK_SYNC);  // Clock pulse detection
  // input_set_mode(INPUT_MODE_AUDIO);       // Future: audio analysis
  // cv_set_range(CV_RANGE_5V);
  
  // sensor_init();
  // als_enable();
  // ps_enable();

  // transport_init();
  // tempo_init();
  // tempo_set_source(CLOCK_SOURCE_INTERNAL);
  // tempo_start();

  // screensaver_init();
  // screensaver_set_mode(SCREENSAVER_MODE_STARFIELD);

  #if ENABLE_PERFORMANCE_MONITORING
  performance_init();
  #endif
  
  // task_monitor_init();
  // vTaskDelay(pdMS_TO_TICKS(3000));
  // task_monitor_print_heap_info();
  // task_monitor_print_report();

  // Create queue and task for CV range button handling
  cv_range_queue = xQueueCreate(5, sizeof(mcp4725_cv_range_t));
  xTaskCreate(cv_range_task, "cv_range", 4096, NULL, 5, NULL);

  // Configure CV range test buttons
  gpio_config_t io = {
    .pin_bit_mask = (1ULL << BTN_10V_BIPOLAR) | (1ULL << BTN_5V_BIPOLAR) | 
                    (1ULL << BTN_10V_UNIPOLAR) | (1ULL << BTN_5V_UNIPOLAR) | 
                    (1ULL << BTN_3V3_UNIPOLAR),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = 1,
    .pull_down_en = 0,
    .intr_type = GPIO_INTR_NEGEDGE,
    .hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE,
  };
  gpio_config(&io);

  // Install GPIO ISR service
  gpio_install_isr_service(0);

  // Hook up ISR handlers - pass range enum as argument
  gpio_isr_handler_add(BTN_10V_BIPOLAR, cv_range_button_isr, (void*)MCP4725_RANGE_10V_BIPOLAR);
  gpio_isr_handler_add(BTN_5V_BIPOLAR, cv_range_button_isr, (void*)MCP4725_RANGE_5V_BIPOLAR);
  gpio_isr_handler_add(BTN_10V_UNIPOLAR, cv_range_button_isr, (void*)MCP4725_RANGE_10V_UNIPOLAR);
  gpio_isr_handler_add(BTN_5V_UNIPOLAR, cv_range_button_isr, (void*)MCP4725_RANGE_5V_UNIPOLAR);
  gpio_isr_handler_add(BTN_3V3_UNIPOLAR, cv_range_button_isr, (void*)MCP4725_RANGE_3V3_UNIPOLAR);

  ESP_LOGI(TAG, "CV range test buttons configured on GPIO 2-6");
}
