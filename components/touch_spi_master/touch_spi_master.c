#include "touch_spi_master.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "io.h"
#include "task_priorities.h"
#include <string.h>

#define TAG "TOUCH_SPI_MASTER"

// SPI configuration
#define SPI_CLOCK_SPEED_HZ (4 * 1000 * 1000)  // 4 MHz for reliability
#define SPI_MODE 0  // CPOL=0, CPHA=0
#define SPI_QUEUE_SIZE 7

// Task configuration
#define TOUCH_SPI_MASTER_TASK_STACK_SIZE 4096
// Ensure polling interval is at least 1 FreeRTOS tick (minimum for xTaskDelayUntil)
// Take max of desired 2ms and actual tick period to guarantee > 0 ticks
#define TOUCH_SPI_MASTER_POLL_INTERVAL_MS (2 > portTICK_PERIOD_MS ? 2 : portTICK_PERIOD_MS)

// Module state
typedef struct {
  spi_device_handle_t spi_device;
  TaskHandle_t task_handle;
  SemaphoreHandle_t mutex;
  SemaphoreHandle_t int_semaphore;
  touch_spi_master_event_callback_t event_callback;
  bool initialized;
  uint32_t total_events;
  uint32_t overflow_events;
  volatile bool int_pending;
} touch_spi_master_state_t;

static touch_spi_master_state_t s_state = {0};

// Transaction buffers
static DRAM_ATTR uint8_t s_rx_buffer[TOUCH_SPI_MASTER_MAX_EVENTS_PER_TRANSFER + 1];
static DRAM_ATTR uint8_t s_tx_buffer[TOUCH_SPI_MASTER_MAX_EVENTS_PER_TRANSFER + 1];

#if USE_INTERRUPT_MODE
// GPIO ISR handler for INT pin
static void IRAM_ATTR gpio_isr_handler(void *arg) {
  BaseType_t higher_priority_task_woken = pdFALSE;
  
  // Set flag and give semaphore to wake task
  s_state.int_pending = true;
  xSemaphoreGiveFromISR(s_state.int_semaphore, &higher_priority_task_woken);
  
  if (higher_priority_task_woken) {
    portYIELD_FROM_ISR();
  }
}
#endif

// Perform SPI transaction to read events
static size_t spi_read_events(void) {
  esp_err_t ret;
  size_t event_count = 0;
  
  // Prepare transaction
  spi_transaction_t trans = {
    .length = (TOUCH_SPI_MASTER_MAX_EVENTS_PER_TRANSFER + 1) * 8,  // Length in bits
    .rx_buffer = s_rx_buffer,
    .tx_buffer = s_tx_buffer,
    .flags = 0
  };
  
  // Clear buffers
  memset(s_tx_buffer, 0, sizeof(s_tx_buffer));
  memset(s_rx_buffer, 0, sizeof(s_rx_buffer));
  
  // Perform SPI transaction
  ret = spi_device_transmit(s_state.spi_device, &trans);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(ret));
    return 0;
  }
  
  // Parse received data
  event_count = s_rx_buffer[0];  // First byte is count
  
  // Check for invalid/uninitialized data from slave
  if (event_count == 0xFF) {
    ESP_LOGD(TAG, "Slave returned uninitialized data (0xFF), skipping");
    return 0;
  }
  
  if (event_count > TOUCH_SPI_MASTER_MAX_EVENTS_PER_TRANSFER) {
    ESP_LOGW(TAG, "Invalid event count: %d (max: %d) - raw buffer: [0x%02X, 0x%02X, 0x%02X, 0x%02X]", 
             event_count, TOUCH_SPI_MASTER_MAX_EVENTS_PER_TRANSFER,
             s_rx_buffer[0], s_rx_buffer[1], s_rx_buffer[2], s_rx_buffer[3]);
    return 0;
  }
  
  // Only log raw bytes when there are actual events (reduce spam)
  if (event_count > 0) {
    ESP_LOGD(TAG, "=== SPI Raw Data (%d events) ===", event_count);
    for (int i = 0; i <= event_count; i++) {  // Only log count + actual events
      ESP_LOGD(TAG, "Raw byte %d: 0x%02X", i, s_rx_buffer[i]);
    }
    ESP_LOGD(TAG, "=== End Raw Data ===");
  }
  
  ESP_LOGD(TAG, "Received %d events", event_count);
  
  // Process each event
  for (size_t i = 0; i < event_count; i++) {
    uint8_t event = s_rx_buffer[i + 1];
    
    // Skip empty slots
    if (event == TOUCH_SPI_MASTER_EMPTY_SLOT) {
      ESP_LOGD(TAG, "Skipping empty slot at index %d (0x%02X)", i, event);
      continue;
    }
    
    // Handle overflow event
    if (event == TOUCH_SPI_MASTER_OVERFLOW_EVENT) {
      s_state.overflow_events++;
      ESP_LOGW(TAG, "Overflow event received");
      continue;
    }
    
    // Decode and dispatch regular event
    uint8_t pad_num = DECODE_PAD_NUM(event);
    bool is_pressed = IS_PRESSED(event);
    
    // Validate pad number
    if (pad_num >= 13) {  // We have 13 pads (0-12)
      ESP_LOGW(TAG, "Invalid pad number %d in event 0x%02X at index %d", pad_num, event, i);
      continue;
    }
    
    ESP_LOGD(TAG, "Event: Pad %d %s", pad_num, is_pressed ? "pressed" : "released");
    
    // Call registered callback
    if (s_state.event_callback) {
      s_state.event_callback(pad_num, is_pressed);
    }
    
    s_state.total_events++;
    ESP_LOGD(TAG, "Total events processed: %lu", s_state.total_events);
  }
  
  return event_count;
}

// Touch SPI master task
static void touch_spi_master_task(void *arg) {
  ESP_LOGI(TAG, "Touch SPI master task started");
  
#if USE_INTERRUPT_MODE
  ESP_LOGI(TAG, "Running in interrupt mode");
  
  while (1) {
    // Wait for interrupt semaphore
    if (xSemaphoreTake(s_state.int_semaphore, portMAX_DELAY) == pdTRUE) {
      // Clear pending flag
      s_state.int_pending = false;
      
      // Read events while INT is asserted (active low)
      while (gpio_get_level(PIN_TOUCH_INT) == 0) {
        size_t events_read = spi_read_events();
        if (events_read == 0) break;
      }
    }
  }
#else
  ESP_LOGI(TAG, "Running in polling mode");
  TickType_t last_wake_time = xTaskGetTickCount();
  
  while (1) {
    // Poll at fixed intervals
    vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TOUCH_SPI_MASTER_POLL_INTERVAL_MS));
    
    // Check if INT is asserted (active low) for efficiency
    if (gpio_get_level(PIN_TOUCH_INT) == 0) {
      spi_read_events();
    }
  }
#endif
}

// Public API implementation

esp_err_t touch_spi_master_init(void) {
  if (s_state.initialized) {
    ESP_LOGW(TAG, "Touch SPI master already initialized");
    return ESP_OK;
  }
  
  esp_err_t ret;
  
  // Create mutex
  s_state.mutex = xSemaphoreCreateMutex();
  if (s_state.mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_ERR_NO_MEM;
  }
  
#if USE_INTERRUPT_MODE
  // Create semaphore for interrupt mode
  s_state.int_semaphore = xSemaphoreCreateBinary();
  if (s_state.int_semaphore == NULL) {
    ESP_LOGE(TAG, "Failed to create interrupt semaphore");
    vSemaphoreDelete(s_state.mutex);
    return ESP_ERR_NO_MEM;
  }
#endif
  
  // Configure SPI bus
  spi_bus_config_t bus_cfg = {
    .miso_io_num = PIN_TOUCH_MISO,
    .mosi_io_num = -1,  // Not used for read-only
    .sclk_io_num = PIN_TOUCH_SCLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = TOUCH_SPI_MASTER_MAX_EVENTS_PER_TRANSFER + 1,
    .flags = SPICOMMON_BUSFLAG_MASTER,
    .intr_flags = 0
  };
  
  ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
    goto cleanup;
  }
  
  // Configure SPI device
  spi_device_interface_config_t dev_cfg = {
    .mode = SPI_MODE,
    .clock_speed_hz = SPI_CLOCK_SPEED_HZ,
    .spics_io_num = PIN_TOUCH_CS,
    .queue_size = SPI_QUEUE_SIZE,
    .flags = 0,
    .pre_cb = NULL,
    .post_cb = NULL
  };
  
  ret = spi_bus_add_device(SPI3_HOST, &dev_cfg, &s_state.spi_device);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
    spi_bus_free(SPI3_HOST);
    goto cleanup;
  }
  
#if USE_INTERRUPT_MODE
  // Configure INT pin as input with interrupt
  gpio_config_t int_pin_config = {
    .pin_bit_mask = (1ULL << PIN_TOUCH_INT),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE  // Interrupt on falling edge
  };
  
  ret = gpio_config(&int_pin_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure INT pin: %s", esp_err_to_name(ret));
    spi_bus_remove_device(s_state.spi_device);
    spi_bus_free(SPI3_HOST);
    goto cleanup;
  }
  
  // Install GPIO ISR service
  ret = gpio_install_isr_service(0);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
    spi_bus_remove_device(s_state.spi_device);
    spi_bus_free(SPI3_HOST);
    goto cleanup;
  }
  
  // Add ISR handler
  ret = gpio_isr_handler_add(PIN_TOUCH_INT, gpio_isr_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
    spi_bus_remove_device(s_state.spi_device);
    spi_bus_free(SPI3_HOST);
    goto cleanup;
  }
#else
  // Configure INT pin as input for polling mode
  gpio_config_t int_pin_config = {
    .pin_bit_mask = (1ULL << PIN_TOUCH_INT),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&int_pin_config);
#endif
  
  // Create task
  BaseType_t task_ret = xTaskCreate(
    touch_spi_master_task,
    "touch_spi_master",
    TOUCH_SPI_MASTER_TASK_STACK_SIZE,
    NULL,
    TASK_PRIORITY_SPI,
    &s_state.task_handle
  );
  
  if (task_ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create touch SPI master task");
#if USE_INTERRUPT_MODE
    gpio_isr_handler_remove(PIN_TOUCH_INT);
#endif
    spi_bus_remove_device(s_state.spi_device);
    spi_bus_free(SPI3_HOST);
    goto cleanup;
  }
  
  s_state.initialized = true;
  ESP_LOGI(TAG, "Touch SPI master initialized (mode: %s)", 
           USE_INTERRUPT_MODE ? "interrupt" : "polling");
  return ESP_OK;
  
cleanup:
  if (s_state.mutex) {
    vSemaphoreDelete(s_state.mutex);
    s_state.mutex = NULL;
  }
#if USE_INTERRUPT_MODE
  if (s_state.int_semaphore) {
    vSemaphoreDelete(s_state.int_semaphore);
    s_state.int_semaphore = NULL;
  }
#endif
  return ret;
}

esp_err_t touch_spi_master_deinit(void) {
  if (!s_state.initialized) {
    return ESP_OK;
  }
  
  // Stop task
  if (s_state.task_handle) {
    vTaskDelete(s_state.task_handle);
    s_state.task_handle = NULL;
  }
  
#if USE_INTERRUPT_MODE
  // Remove ISR handler
  gpio_isr_handler_remove(PIN_TOUCH_INT);
#endif
  
  // Clean up SPI
  spi_bus_remove_device(s_state.spi_device);
  spi_bus_free(SPI3_HOST);
  
  // Clean up synchronization primitives
  if (s_state.mutex) {
    vSemaphoreDelete(s_state.mutex);
    s_state.mutex = NULL;
  }
#if USE_INTERRUPT_MODE
  if (s_state.int_semaphore) {
    vSemaphoreDelete(s_state.int_semaphore);
    s_state.int_semaphore = NULL;
  }
#endif
  
  // Reset state
  memset(&s_state, 0, sizeof(s_state));
  
  ESP_LOGI(TAG, "Touch SPI master deinitialized");
  return ESP_OK;
}

void touch_spi_master_register_event_callback(touch_spi_master_event_callback_t callback) {
  if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) == pdTRUE) {
    s_state.event_callback = callback;
    xSemaphoreGive(s_state.mutex);
    ESP_LOGI(TAG, "Event callback registered");
  }
}

void touch_spi_master_get_stats(uint32_t *total_events, uint32_t *overflow_events) {
  if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) == pdTRUE) {
    if (total_events) *total_events = s_state.total_events;
    if (overflow_events) *overflow_events = s_state.overflow_events;
    
    // Debug logging to track counter behavior
    ESP_LOGD(TAG, "Stats: total_events=%lu, overflow_events=%lu", 
             s_state.total_events, s_state.overflow_events);
    
    xSemaphoreGive(s_state.mutex);
  }
}

size_t touch_spi_master_poll_once(void) {
  if (!s_state.initialized) return 0;
  
  return spi_read_events();
} 