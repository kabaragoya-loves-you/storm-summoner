#include "ssd1327_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"
#include "coordinate_map.h"
#include "../lvgl/src/display/lv_display_private.h"

#define TAG "SSD1327"

static spi_device_handle_t spi;

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
  spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

  spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 20 * 1000 * 1000, // 20 MHz
    .mode = 0,
    .spics_io_num = -1,
    .queue_size = 7,
  };
  spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

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

  gpio_set_level(PIN_DC, 0);
  spi_transaction_t init;
  memset(&init, 0, sizeof(init));
  init.length = sizeof(ssd1327_init_cmds) * 8;
  init.tx_buffer = ssd1327_init_cmds;
  spi_device_transmit(spi, &init);
}

void ssd1327_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  // This buffer will hold the 4-bit packed data (2 pixels / byte).
  static uint8_t ssd1327_buf[128 * 128 / 2];

  // 1) Send column and row setup commands
  uint8_t ssd1327_flush_cmds[] = {
    0x15, (uint8_t)area->x1, (uint8_t)area->x2,
    0x75, (uint8_t)area->y1, (uint8_t)area->y2,
    0x5C
  };
  gpio_set_level(PIN_DC, 0);
  spi_transaction_t commands = {0};
  commands.length    = sizeof(ssd1327_flush_cmds) * 8;
  commands.tx_buffer = ssd1327_flush_cmds;
  spi_device_transmit(spi, &commands);

  uint32_t out_index = 0;
  uint8_t high_nibble = 0;
  bool is_even_pixel = true;
  
  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);

  for (int32_t y = 0; y < h; y++) {
    for (int32_t x = 0; x < w; x++) {
      uint16_t c = 0; // Default to black

#if DISPLAY_OPTIMIZATION_MODE == 0 || DISPLAY_OPTIMIZATION_MODE == 1
      // Modes 0 & 1: Cull based on dynamic calculation
      if (is_pixel_visible(area->x1 + x, area->y1 + y)) {
          c = ((uint16_t *)px_map)[y * w + x];
      }
#elif DISPLAY_OPTIMIZATION_MODE == 2
      // Mode 2: Cull based on the coordinate map
      if (coordinate_map[(area->y1 + y) * SCREEN_WIDTH + (area->x1 + x)] != -1) {
          c = ((uint16_t *)px_map)[y * w + x];
      }
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
        ssd1327_buf[out_index++] = high_nibble | (gray_4 & 0x0F);
      }
      is_even_pixel = !is_even_pixel;
    }
  }

  if (!is_even_pixel) {
    ssd1327_buf[out_index++] = high_nibble;
  }

  // 3) Send the pixel data
  gpio_set_level(PIN_DC, 1);
  spi_transaction_t data = {0};
  data.length    = out_index * 8; 
  data.tx_buffer = ssd1327_buf;
  spi_device_transmit(spi, &data);

  lv_disp_flush_ready(disp);
}



