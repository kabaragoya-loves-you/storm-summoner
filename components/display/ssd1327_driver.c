#include "ssd1327_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>

#define PIN_CLK 34
#define PIN_MOSI 33
#define PIN_RESET 47
#define PIN_DC 26
#define PIN_CS 21

#define TAG "SSD1327"

static spi_device_handle_t spi;

void ssd1327_init(void) {
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
    .spics_io_num = PIN_CS,
    .queue_size = 7,
  };
  spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

  gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_RESET, GPIO_MODE_OUTPUT);

  gpio_set_level(PIN_RESET, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(PIN_RESET, 1);
  vTaskDelay(pdMS_TO_TICKS(10));

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
  int32_t width  = area->x2 - area->x1 + 1;
  int32_t height = area->y2 - area->y1 + 1;
  int32_t pixel_count = width * height;

  // This buffer will hold the 4-bit packed data (2 pixels / byte).
  // For a maximum area = full screen: 128 * 128 = 16384 pixels
  // We need 16384 / 2 = 8192 bytes.
  static uint8_t ssd1327_buf[128 * 128 / 2];

  // 1) Send column and row setup commands
  uint8_t ssd1327_flush_cmds[] = {
    0x15, (uint8_t)area->x1, (uint8_t)area->x2, // Set column address
    0x75, (uint8_t)area->y1, (uint8_t)area->y2, // Set row address
    0x5C // RAM write
  };
  gpio_set_level(PIN_DC, 0);
  spi_transaction_t commands = {0};
  commands.length    = sizeof(ssd1327_flush_cmds) * 8; // bits
  commands.tx_buffer = ssd1327_flush_cmds;
  spi_device_transmit(spi, &commands);

  // 2) Convert each LVGL pixel (16bpp = RGB565) to 4bpp, pack 2 per byte
  // px_map is 2 bytes per pixel in RGB565: (R5 G6 B5).
  // We'll do a quick luminance conversion. For example:
  //   r = (px >> 11) & 0x1F  => 0..31
  //   g = (px >> 5)  & 0x3F  => 0..63
  //   b = (px >> 0)  & 0x1F  => 0..31
  // Then map it into 0..15 grayscale.
  uint32_t out_index = 0;
  uint8_t high_nibble = 0;
  for (int32_t i = 0; i < pixel_count; i++) {
    uint16_t c = ((uint16_t *)px_map)[i];  // read 16-bit color
    uint8_t r = (c >> 11) & 0x1F;          // 5-bit red
    uint8_t g = (c >> 5)  & 0x3F;          // 6-bit green
    uint8_t b = (c >> 0)  & 0x1F;          // 5-bit blue

    // Scale them up to 8 bits just for the grayscale calc
    uint8_t R8 = r << 3;        // approximate range 0..248
    uint8_t G8 = g << 2;        // range 0..252
    uint8_t B8 = b << 3;        // range 0..248

    // Weighted grayscale (standard formula): 
    //   gray_8 = (0.299 * R + 0.587 * G + 0.114 * B).
    // We can approximate with integer math:
    uint16_t gray_16 = ((uint16_t)R8 * 77 +
                        (uint16_t)G8 * 151 +
                        (uint16_t)B8 * 28 ) >> 8; // 0..255

    // Convert 0..255 to 0..15
    uint8_t gray_4 = gray_16 >> 4; // 4-bit

    // Pack two pixels per byte. If i is even, store in high nibble, else low nibble.
    if ((i & 1) == 0) {
      // Even: store in the upper nibble
      high_nibble = (gray_4 & 0x0F) << 4;
    } else {
      // Odd: combine with high nibble and write out
      ssd1327_buf[out_index++] = high_nibble | (gray_4 & 0x0F);
    }
  }
  // If we had an odd number of pixels in this area, be sure to write the last nibble
  // (In typical use, area->width * area->height is even, but if you do partial updates,
  // you might need to handle that final nibble carefully.)

  // 3) Send the pixel data
  gpio_set_level(PIN_DC, 1);
  spi_transaction_t data = {0};
  // The total length in bits is "out_index * 8" if each out_index is one byte
  data.length    = out_index * 8; 
  data.tx_buffer = ssd1327_buf;
  spi_device_transmit(spi, &data);

  // 4) Tell LVGL we’re done
  lv_disp_flush_ready(disp);

  // ESP_LOGI(TAG, "Flushed area x[%d..%d], y[%d..%d]", (int)area->x1, (int)area->x2, (int)area->y1, (int)area->y2);
}
