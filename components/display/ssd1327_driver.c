#include "ssd1327_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"
#include "coordinate_map.h"
#include "../lvgl/src/display/lv_display_private.h"

#define TAG "SSD1327"

// Debug flag for flush logging - set to 0 to disable all flush debug messages
#define DEBUG_FLUSH_MESSAGES 0

static spi_device_handle_t spi;

#if ENABLE_SPI_DMA
// With DMA, buffer must be in DMA-capable memory with proper alignment
static uint8_t *ssd1327_buf = NULL;
#else
// Without DMA, can use regular static buffer
static uint8_t ssd1327_buf[128 * 128 / 2];
#endif

#if DISPLAY_OPTIMIZATION_MODE != 2
// is_pixel_visible is only needed for modes 0 and 1
IRAM_ATTR bool is_pixel_visible(int16_t x, int16_t y) {
  int16_t dx = x - 64;
  int16_t dy = y - 64;
  return (dx * dx + dy * dy) <= (64 * 64);
}
#endif



void ssd1327_init(void) {
  // Configure and perform display reset with robust sequence
  gpio_config_t reset_gpio_config = {
    .pin_bit_mask = (1ULL << PIN_RESET),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&reset_gpio_config);
  
  // Ensure reset pin starts high (inactive)
  gpio_set_level(PIN_RESET, 1);
  vTaskDelay(pdMS_TO_TICKS(20)); // Wait 20ms to ensure pin is stable
  
  // Perform reset sequence - multiple pulses for robustness
  for (int i = 0; i < 2; i++) {
    gpio_set_level(PIN_RESET, 0);  // Reset low (active)
    vTaskDelay(pdMS_TO_TICKS(20)); // Hold reset for 20ms
    gpio_set_level(PIN_RESET, 1);  // Reset high (inactive)
    vTaskDelay(pdMS_TO_TICKS(20)); // Wait 20ms between pulses
  }
  
  // Final longer wait for display to fully initialize
  vTaskDelay(pdMS_TO_TICKS(50)); // Wait 50ms for display to be completely ready

  spi_bus_config_t buscfg = {
    .miso_io_num = -1,
    .mosi_io_num = PIN_MOSI,
    .sclk_io_num = PIN_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 128 * 128 * 2,
  };
  
#if ENABLE_SPI_DMA
  // With DMA enabled, we can use hardware acceleration
  #if DEBUG_FLUSH_MESSAGES
  ESP_LOGI(TAG, "Initializing SPI with DMA support (MOSI: GPIO%d, CLK: GPIO%d)", PIN_MOSI, PIN_CLK);
  #endif
  esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI bus with DMA: %s", esp_err_to_name(ret));
    return;
  }
#else
  // Without DMA, use polling mode
  #if DEBUG_FLUSH_MESSAGES
  ESP_LOGI(TAG, "Initializing SPI without DMA (MOSI: GPIO%d, CLK: GPIO%d)", PIN_MOSI, PIN_CLK);
  #endif
  esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, 0);  // 0 = no DMA
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
    return;
  }
#endif

  spi_device_interface_config_t devcfg = {
#if ENABLE_SPI_DMA
    .clock_speed_hz = 23 * 1000 * 1000, // 23 MHz - maximum stable speed on breadboard
#else
    .clock_speed_hz = 20 * 1000 * 1000, // 20 MHz without DMA
#endif
    .mode = 0,
    .spics_io_num = -1,
    .queue_size = 7,
    .pre_cb = NULL,
    .post_cb = NULL,
    // Add timing parameters for better signal integrity
    .input_delay_ns = 0,    // No input delay (we don't read from display)
    .dummy_bits = 0,        // No dummy bits needed
    .flags = SPI_DEVICE_NO_DUMMY,  // Explicit no dummy cycles
  };
  
  ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
    return;
  }
  
  #if DEBUG_FLUSH_MESSAGES
  ESP_LOGI(TAG, "SPI initialized successfully - Clock: %d MHz, DMA: %s", devcfg.clock_speed_hz / 1000000, ENABLE_SPI_DMA ? "Enabled" : "Disabled");
  #endif

#if ENABLE_SPI_DMA
  // Allocate DMA-capable buffer for pixel data
  if (ssd1327_buf == NULL) {
    ssd1327_buf = (uint8_t *)heap_caps_malloc(128 * 128 / 2, MALLOC_CAP_DMA);
    if (ssd1327_buf == NULL) {
      ESP_LOGE(TAG, "Failed to allocate DMA buffer");
      return;
    }
    #if DEBUG_FLUSH_MESSAGES
    ESP_LOGI(TAG, "Allocated %d byte DMA buffer at %p", 128 * 128 / 2, ssd1327_buf);
    #endif
  }
#endif

  gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);

  uint8_t ssd1327_init_cmds[] = { // commands to initialize the SSD1327, borrowed from u8g2
    0xFD, 0x12, // Unlock display (if required)
    0xAE,       // Display off
    0xA8, 0x7F, // Set multiplex ratio
    0xA1, 0x00, // Set display start line
    0xA2, 0x00, // Set display offset
    0xA0, 0x51, // Remap configuration
    0xAB, 0x01, // Enable internal VDD regulator
    0x81, 0x53, // Set contrast
    0xB1, 0x51, // Set phase length
    0xB3, 0x01, // Set display clock divide ratio/oscillator frequency
    0xB9,       // Use linear lookup table
    0xBC, 0x08, // Set pre-charge voltage level
    0xBE, 0x07, // Set VCOMH voltage
    0xB6, 0x01, // Second precharge
    0xD5, 0x62, // Enable second precharge, internal VSL
    0xB5, 0x03, // Enable GPIO
    0xA4,       // Normal display mode
    0xAF        // Display on
  };

  // Always control D/C pin directly
  gpio_set_level(PIN_DC, 0);
  
  spi_transaction_t init;
  memset(&init, 0, sizeof(init));
  init.length = sizeof(ssd1327_init_cmds) * 8;
  init.tx_buffer = ssd1327_init_cmds;
  
#if ENABLE_SPI_DMA
  spi_device_polling_transmit(spi, &init);
#else
  spi_device_transmit(spi, &init);
#endif

  // Give display time to initialize after sending init commands
  vTaskDelay(pdMS_TO_TICKS(100));
  #if DEBUG_FLUSH_MESSAGES
  ESP_LOGI(TAG, "Display initialization complete");
  #endif
}

void ssd1327_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  static uint32_t flush_count = 0;
  flush_count++;
  
#if ENABLE_SPI_DMA
  // Ensure buffer is allocated
  if (ssd1327_buf == NULL) {
    ESP_LOGE(TAG, "DMA buffer not allocated!");
    lv_disp_flush_ready(disp);
    return;
  }
#endif
  
  #if DEBUG_FLUSH_MESSAGES
  // Log every 100th flush to avoid spam
  if (flush_count % 100 == 0) {
    ESP_LOGI(TAG, "Flush #%lu: area (%d,%d)-(%d,%d)", (unsigned long)flush_count, (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
  }
  #endif
  
  // Validate area bounds
  if (area->x1 < 0 || area->y1 < 0 || area->x2 >= 128 || area->y2 >= 128 || 
      area->x1 > area->x2 || area->y1 > area->y2) {
    ESP_LOGE(TAG, "Invalid area bounds: (%d,%d)-(%d,%d)", (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
    lv_disp_flush_ready(disp);
    return;
  }
  
  #if DEBUG_FLUSH_MESSAGES
  // Debug first few flushes
  if (flush_count <= 10) {
    ESP_LOGI(TAG, "Flush #%lu: buffer=%p", (unsigned long)flush_count, ssd1327_buf);
  }
  #endif
  
  // Early exit for completely invisible areas
  // Check if the area is entirely outside the circle
  int32_t center_x = 64;
  int32_t center_y = 64;
  int32_t radius = 64;
  
  // Calculate closest point in rectangle to circle center
  int32_t closest_x = (area->x1 > center_x) ? area->x1 : ((area->x2 < center_x) ? area->x2 : center_x);
  int32_t closest_y = (area->y1 > center_y) ? area->y1 : ((area->y2 < center_y) ? area->y2 : center_y);
  
  int32_t dx = closest_x - center_x;
  int32_t dy = closest_y - center_y;
  
  // If closest point is outside circle, entire area is invisible
  if ((dx * dx + dy * dy) > (radius * radius)) {
    lv_disp_flush_ready(disp);
    return;
  }
  

  
  // 1) Send column and row setup commands
  uint8_t ssd1327_flush_cmds[] = {
    0x15, (uint8_t)area->x1, (uint8_t)area->x2,
    0x75, (uint8_t)area->y1, (uint8_t)area->y2,
    0x5C
  };
  
  // Always control D/C pin directly, even with DMA
  gpio_set_level(PIN_DC, 0);  // Command mode
  
  spi_transaction_t commands = {0};
  commands.length    = sizeof(ssd1327_flush_cmds) * 8;
  commands.tx_buffer = ssd1327_flush_cmds;
  
#if ENABLE_SPI_DMA
  spi_device_polling_transmit(spi, &commands);
#else
  spi_device_transmit(spi, &commands);
#endif



  uint32_t out_index = 0;
  uint8_t high_nibble = 0;
  bool is_even_pixel = true;
  
  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);
  
  // Pre-calculate values for optimization
  const int32_t area_x1 = area->x1;
  const int32_t area_y1 = area->y1;
  const int32_t radius_squared = 64 * 64;

  for (int32_t y = 0; y < h; y++) {
    int32_t abs_y = area_y1 + y;
    
    // Row-wise early exit optimization for modes 0 and 1
    #if DISPLAY_OPTIMIZATION_MODE == 0 || DISPLAY_OPTIMIZATION_MODE == 1
    int32_t dy = abs_y - 64;
    if ((dy * dy) > radius_squared) {
      // Entire row is outside circle, output black pixels
      for (int32_t x = 0; x < w; x++) {
        if (is_even_pixel) {
          high_nibble = 0;
        } else {
          if (out_index < (128 * 128 / 2)) {
            ssd1327_buf[out_index++] = high_nibble;
          } else {
            ESP_LOGE(TAG, "Buffer overflow prevented at index %lu", (unsigned long)out_index);
          }
        }
        is_even_pixel = !is_even_pixel;
      }
      continue;
    }
    #endif
    
    // Process pixels in this row
    for (int32_t x = 0; x < w; x++) {
      uint16_t c = 0; // Default to black

#if DISPLAY_OPTIMIZATION_MODE == 0 || DISPLAY_OPTIMIZATION_MODE == 1
      // Modes 0 & 1: Cull based on dynamic calculation
      if (is_pixel_visible(area_x1 + x, abs_y)) c = ((uint16_t *)px_map)[y * w + x];
#elif DISPLAY_OPTIMIZATION_MODE == 2
      // Mode 2: Cull based on the coordinate map
      int32_t map_index = abs_y * SCREEN_WIDTH + area_x1 + x;
      if (coordinate_map[map_index] != -1) {
          c = ((uint16_t *)px_map)[y * w + x];
      }
#elif DISPLAY_OPTIMIZATION_MODE == 3
      // Mode 3: No culling here - circular display handler does it
      c = ((uint16_t *)px_map)[y * w + x];
#endif

      // --- Common color conversion and packing logic ---
      uint8_t r = (c >> 11) & 0x1F;
      uint8_t g = (c >> 5)  & 0x3F;
      uint8_t b = (c >> 0)  & 0x1F;
      uint8_t R8 = r << 3;
      uint8_t G8 = g << 2;
      uint8_t B8 = b << 3;
      uint16_t gray_16 = ((uint16_t)R8 * 77 + (uint16_t)G8 * 151 + (uint16_t)B8 * 28) >> 8;
      uint8_t gray_4 = gray_16 >> 4;

      if (is_even_pixel) {
        high_nibble = (gray_4 & 0x0F) << 4;
      } else {
        if (out_index < (128 * 128 / 2)) {
          ssd1327_buf[out_index++] = high_nibble | (gray_4 & 0x0F);
        } else {
          ESP_LOGE(TAG, "Buffer overflow prevented at index %lu", (unsigned long)out_index);
        }
      }
      is_even_pixel = !is_even_pixel;
    }
  }

  if (!is_even_pixel) {
    if (out_index < (128 * 128 / 2)) {
      ssd1327_buf[out_index++] = high_nibble;
    } else {
      ESP_LOGE(TAG, "Buffer overflow prevented at index %lu", (unsigned long)out_index);
    }
  }

  #if DEBUG_FLUSH_MESSAGES
  // Debug first few flushes
  if (flush_count <= 10) {
    ESP_LOGI(TAG, "Flush #%lu: sending %lu bytes to display", (unsigned long)flush_count, (unsigned long)out_index);
    
    // Calculate simple checksum
    uint32_t checksum = 0;
    for (int i = 0; i < out_index && i < 256; i++) checksum += ssd1327_buf[i];
    ESP_LOGI(TAG, "Flush #%lu: first 256 bytes checksum = %lu", (unsigned long)flush_count, (unsigned long)checksum);
  }
  #endif
  
  // Validate output size
  if (out_index > (128 * 128 / 2)) {
    ESP_LOGE(TAG, "Output index %lu exceeds buffer size!", (unsigned long)out_index);
    out_index = 128 * 128 / 2;
  }

  // 3) Send the pixel data
  gpio_set_level(PIN_DC, 1);  // Data mode
  
#if ENABLE_SPI_DMA
  // With DMA, can send entire buffer at once
  spi_transaction_t data = {0};
  data.length    = out_index * 8; 
  data.tx_buffer = ssd1327_buf;
  
  esp_err_t ret = spi_device_polling_transmit(spi, &data);
  if (ret != ESP_OK) ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(ret));
  
  #if DEBUG_FLUSH_MESSAGES
  if (flush_count <= 10) ESP_LOGI(TAG, "Flush #%lu completed successfully", (unsigned long)flush_count);
  #endif
#else
  // Without DMA, must send in smaller chunks (max 64 bytes per transaction)
  const size_t max_chunk_size = 64;  // Conservative limit for non-DMA transfers
  size_t bytes_sent = 0;
  
  while (bytes_sent < out_index) {
    size_t chunk_size = (out_index - bytes_sent) > max_chunk_size ? max_chunk_size : (out_index - bytes_sent);
    
    spi_transaction_t data = {0};
    data.length = chunk_size * 8;
    data.tx_buffer = &ssd1327_buf[bytes_sent];
    
    esp_err_t ret = spi_device_transmit(spi, &data);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "SPI transmit failed at offset %zu: %s", bytes_sent, esp_err_to_name(ret));
      break;
    }
    
    bytes_sent += chunk_size;
  }
  
  #if DEBUG_FLUSH_MESSAGES
  if (flush_count <= 10) ESP_LOGI(TAG, "Flush #%lu: sent %zu bytes in %zu chunks", (unsigned long)flush_count, bytes_sent, (bytes_sent + max_chunk_size - 1) / max_chunk_size);
  #endif
#endif

  lv_disp_flush_ready(disp);
}



