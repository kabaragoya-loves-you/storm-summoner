#include "gc9a01a_driver.h"
#include "display_driver.h"
#include "lvgl_stream.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"

#define TAG "GC9A01A"

// Debug flag for flush logging
#define DEBUG_FLUSH_MESSAGES 0

// Physical display dimensions
#define GC9A01A_PHYSICAL_WIDTH  240
#define GC9A01A_PHYSICAL_HEIGHT 240

// Viewport configuration (virtual display within physical)
// These define the visible area through the aperture
static int16_t viewport_offset_x = 15;   // Physical X where viewport starts (left edge)
static int16_t viewport_offset_y = 9;    // Physical Y where viewport starts (top edge)
static uint16_t viewport_width = 198;    // Visible width through aperture
static uint16_t viewport_height = 198;   // Visible height through aperture

// GC9A01A command definitions
#define GC9A01A_NOP       0x00
#define GC9A01A_SWRESET   0x01
#define GC9A01A_SLPIN     0x10
#define GC9A01A_SLPOUT    0x11
#define GC9A01A_PTLON     0x12
#define GC9A01A_NORON     0x13
#define GC9A01A_INVOFF    0x20
#define GC9A01A_INVON     0x21
#define GC9A01A_DISPOFF   0x28
#define GC9A01A_DISPON    0x29
#define GC9A01A_CASET     0x2A
#define GC9A01A_RASET     0x2B
#define GC9A01A_RAMWR     0x2C
#define GC9A01A_PTLAR     0x30
#define GC9A01A_VSCRDEF   0x33
#define GC9A01A_COLMOD    0x3A
#define GC9A01A_MADCTL    0x36
#define GC9A01A_VSCSAD    0x37
#define GC9A01A_TEON      0x35

// MADCTL bits
#define MADCTL_MY  0x80  // Row address order
#define MADCTL_MX  0x40  // Column address order
#define MADCTL_MV  0x20  // Row/Column exchange
#define MADCTL_ML  0x10  // Vertical refresh order
#define MADCTL_BGR 0x08  // BGR order (vs RGB)
#define MADCTL_MH  0x04  // Horizontal refresh order

static spi_device_handle_t spi;
static uint8_t *gc9a01a_line_buf = NULL;

// Forward declaration
static void gc9a01a_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

// Send a command byte
static void gc9a01a_cmd(uint8_t cmd) {
  gpio_set_level(PIN_DC, 0);  // Command mode
  spi_transaction_t t = {
    .length = 8,
    .tx_buffer = &cmd,
  };
  spi_device_polling_transmit(spi, &t);
}

// Send data bytes
static void gc9a01a_data(const uint8_t *data, size_t len) {
  if (len == 0) return;
  gpio_set_level(PIN_DC, 1);  // Data mode
  spi_transaction_t t = {
    .length = len * 8,
    .tx_buffer = data,
  };
  spi_device_polling_transmit(spi, &t);
}

// Send a single data byte
static void gc9a01a_data_byte(uint8_t val) {
  gc9a01a_data(&val, 1);
}

// Initialize the GC9A01A display
void gc9a01a_init(void) {
  ESP_LOGI(TAG, "Initializing GC9A01A 240x240 IPS display");

  // Configure reset pin
  gpio_config_t reset_gpio_config = {
    .pin_bit_mask = (1ULL << PIN_RESET),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&reset_gpio_config);

  // Configure D/C pin
  gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);

  // Hardware reset sequence
  gpio_set_level(PIN_RESET, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(PIN_RESET, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(PIN_RESET, 1);
  vTaskDelay(pdMS_TO_TICKS(120));

  // Initialize SPI bus
  spi_bus_config_t buscfg = {
    .miso_io_num = -1,
    .mosi_io_num = PIN_MOSI,
    .sclk_io_num = PIN_SCLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = GC9A01A_WIDTH * GC9A01A_HEIGHT * 3,  // RGB888
  };

  esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
    return;
  }

  spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 60 * 1000 * 1000,  // 60 MHz - GC9A01A maximum
    .mode = 0,
    .spics_io_num = -1,
    .queue_size = 7,
    .flags = SPI_DEVICE_NO_DUMMY,
  };

  ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
    return;
  }

  ESP_LOGI(TAG, "SPI initialized at 60 MHz with DMA");

  // Allocate line buffer for RGB888 data (3 bytes per pixel)
  size_t line_buf_size = GC9A01A_WIDTH * 3;
  gc9a01a_line_buf = heap_caps_malloc(line_buf_size, MALLOC_CAP_DMA);
  if (!gc9a01a_line_buf) {
    ESP_LOGE(TAG, "Failed to allocate line buffer");
    return;
  }

  // GC9A01A initialization sequence
  // Based on manufacturer recommendations and common implementations
  
  gc9a01a_cmd(0xEF);  // Inter register enable 2
  
  gc9a01a_cmd(0xEB);
  gc9a01a_data_byte(0x14);
  
  gc9a01a_cmd(0xFE);  // Inter register enable 1
  gc9a01a_cmd(0xEF);  // Inter register enable 2
  
  gc9a01a_cmd(0xEB);
  gc9a01a_data_byte(0x14);
  
  gc9a01a_cmd(0x84);
  gc9a01a_data_byte(0x40);
  
  gc9a01a_cmd(0x85);
  gc9a01a_data_byte(0xFF);
  
  gc9a01a_cmd(0x86);
  gc9a01a_data_byte(0xFF);
  
  gc9a01a_cmd(0x87);
  gc9a01a_data_byte(0xFF);
  
  gc9a01a_cmd(0x88);
  gc9a01a_data_byte(0x0A);
  
  gc9a01a_cmd(0x89);
  gc9a01a_data_byte(0x21);
  
  gc9a01a_cmd(0x8A);
  gc9a01a_data_byte(0x00);
  
  gc9a01a_cmd(0x8B);
  gc9a01a_data_byte(0x80);
  
  gc9a01a_cmd(0x8C);
  gc9a01a_data_byte(0x01);
  
  gc9a01a_cmd(0x8D);
  gc9a01a_data_byte(0x01);
  
  gc9a01a_cmd(0x8E);
  gc9a01a_data_byte(0xFF);
  
  gc9a01a_cmd(0x8F);
  gc9a01a_data_byte(0xFF);
  
  gc9a01a_cmd(0xB6);  // Display function control
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x00);
  
  gc9a01a_cmd(GC9A01A_MADCTL);  // Memory access control
  gc9a01a_data_byte(MADCTL_MX);  // RGB order (LVGL sends RGB, not BGR)
  
  gc9a01a_cmd(GC9A01A_COLMOD);  // Pixel format
  gc9a01a_data_byte(0x66);  // 18-bit color (RGB666) - closest to RGB888
  
  gc9a01a_cmd(0x90);
  gc9a01a_data_byte(0x08);
  gc9a01a_data_byte(0x08);
  gc9a01a_data_byte(0x08);
  gc9a01a_data_byte(0x08);
  
  gc9a01a_cmd(0xBD);
  gc9a01a_data_byte(0x06);
  
  gc9a01a_cmd(0xBC);
  gc9a01a_data_byte(0x00);
  
  gc9a01a_cmd(0xFF);
  gc9a01a_data_byte(0x60);
  gc9a01a_data_byte(0x01);
  gc9a01a_data_byte(0x04);
  
  gc9a01a_cmd(0xC3);  // Voltage VRHS
  gc9a01a_data_byte(0x13);
  
  gc9a01a_cmd(0xC4);  // Voltage VDV
  gc9a01a_data_byte(0x13);
  
  gc9a01a_cmd(0xC9);
  gc9a01a_data_byte(0x22);
  
  gc9a01a_cmd(0xBE);
  gc9a01a_data_byte(0x11);
  
  gc9a01a_cmd(0xE1);
  gc9a01a_data_byte(0x10);
  gc9a01a_data_byte(0x0E);
  
  gc9a01a_cmd(0xDF);
  gc9a01a_data_byte(0x21);
  gc9a01a_data_byte(0x0C);
  gc9a01a_data_byte(0x02);
  
  // Gamma settings
  gc9a01a_cmd(0xF0);
  gc9a01a_data_byte(0x45);
  gc9a01a_data_byte(0x09);
  gc9a01a_data_byte(0x08);
  gc9a01a_data_byte(0x08);
  gc9a01a_data_byte(0x26);
  gc9a01a_data_byte(0x2A);
  
  gc9a01a_cmd(0xF1);
  gc9a01a_data_byte(0x43);
  gc9a01a_data_byte(0x70);
  gc9a01a_data_byte(0x72);
  gc9a01a_data_byte(0x36);
  gc9a01a_data_byte(0x37);
  gc9a01a_data_byte(0x6F);
  
  gc9a01a_cmd(0xF2);
  gc9a01a_data_byte(0x45);
  gc9a01a_data_byte(0x09);
  gc9a01a_data_byte(0x08);
  gc9a01a_data_byte(0x08);
  gc9a01a_data_byte(0x26);
  gc9a01a_data_byte(0x2A);
  
  gc9a01a_cmd(0xF3);
  gc9a01a_data_byte(0x43);
  gc9a01a_data_byte(0x70);
  gc9a01a_data_byte(0x72);
  gc9a01a_data_byte(0x36);
  gc9a01a_data_byte(0x37);
  gc9a01a_data_byte(0x6F);
  
  gc9a01a_cmd(0xED);
  gc9a01a_data_byte(0x1B);
  gc9a01a_data_byte(0x0B);
  
  gc9a01a_cmd(0xAE);
  gc9a01a_data_byte(0x77);
  
  gc9a01a_cmd(0xCD);
  gc9a01a_data_byte(0x63);
  
  gc9a01a_cmd(0x70);
  gc9a01a_data_byte(0x07);
  gc9a01a_data_byte(0x07);
  gc9a01a_data_byte(0x04);
  gc9a01a_data_byte(0x0E);
  gc9a01a_data_byte(0x0F);
  gc9a01a_data_byte(0x09);
  gc9a01a_data_byte(0x07);
  gc9a01a_data_byte(0x08);
  gc9a01a_data_byte(0x03);
  
  gc9a01a_cmd(0xE8);
  gc9a01a_data_byte(0x34);
  
  gc9a01a_cmd(0x62);
  gc9a01a_data_byte(0x18);
  gc9a01a_data_byte(0x0D);
  gc9a01a_data_byte(0x71);
  gc9a01a_data_byte(0xED);
  gc9a01a_data_byte(0x70);
  gc9a01a_data_byte(0x70);
  gc9a01a_data_byte(0x18);
  gc9a01a_data_byte(0x0F);
  gc9a01a_data_byte(0x71);
  gc9a01a_data_byte(0xEF);
  gc9a01a_data_byte(0x70);
  gc9a01a_data_byte(0x70);
  
  gc9a01a_cmd(0x63);
  gc9a01a_data_byte(0x18);
  gc9a01a_data_byte(0x11);
  gc9a01a_data_byte(0x71);
  gc9a01a_data_byte(0xF1);
  gc9a01a_data_byte(0x70);
  gc9a01a_data_byte(0x70);
  gc9a01a_data_byte(0x18);
  gc9a01a_data_byte(0x13);
  gc9a01a_data_byte(0x71);
  gc9a01a_data_byte(0xF3);
  gc9a01a_data_byte(0x70);
  gc9a01a_data_byte(0x70);
  
  gc9a01a_cmd(0x64);
  gc9a01a_data_byte(0x28);
  gc9a01a_data_byte(0x29);
  gc9a01a_data_byte(0xF1);
  gc9a01a_data_byte(0x01);
  gc9a01a_data_byte(0xF1);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x07);
  
  gc9a01a_cmd(0x66);
  gc9a01a_data_byte(0x3C);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0xCD);
  gc9a01a_data_byte(0x67);
  gc9a01a_data_byte(0x45);
  gc9a01a_data_byte(0x45);
  gc9a01a_data_byte(0x10);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x00);
  
  gc9a01a_cmd(0x67);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x3C);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x01);
  gc9a01a_data_byte(0x54);
  gc9a01a_data_byte(0x10);
  gc9a01a_data_byte(0x32);
  gc9a01a_data_byte(0x98);
  
  gc9a01a_cmd(0x74);
  gc9a01a_data_byte(0x10);
  gc9a01a_data_byte(0x85);
  gc9a01a_data_byte(0x80);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x00);
  gc9a01a_data_byte(0x4E);
  gc9a01a_data_byte(0x00);
  
  gc9a01a_cmd(0x98);
  gc9a01a_data_byte(0x3E);
  gc9a01a_data_byte(0x07);
  
  gc9a01a_cmd(GC9A01A_TEON);  // Tearing effect line on
  
  gc9a01a_cmd(GC9A01A_INVON);  // Display inversion on (usually needed for IPS)
  
  gc9a01a_cmd(GC9A01A_SLPOUT);  // Exit sleep mode
  vTaskDelay(pdMS_TO_TICKS(120));
  
  // Fill entire physical display with black before showing
  // This prevents garbage pixels outside the viewport from being visible
  gc9a01a_set_addr_window(0, 0, GC9A01A_PHYSICAL_WIDTH - 1, GC9A01A_PHYSICAL_HEIGHT - 1);
  gpio_set_level(PIN_DC, 1);
  memset(gc9a01a_line_buf, 0, GC9A01A_PHYSICAL_WIDTH * 3);  // One line of black pixels
  for (int y = 0; y < GC9A01A_PHYSICAL_HEIGHT; y++) {
    spi_transaction_t t = { .length = GC9A01A_PHYSICAL_WIDTH * 3 * 8, .tx_buffer = gc9a01a_line_buf };
    spi_device_polling_transmit(spi, &t);
  }
  ESP_LOGI(TAG, "Display cleared to black");

  gc9a01a_cmd(GC9A01A_DISPON);  // Display on
  vTaskDelay(pdMS_TO_TICKS(50));  // Let power stabilize before heavy operations

  ESP_LOGI(TAG, "Display initialization complete");
}

// Set the address window for pixel write
static void gc9a01a_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  gc9a01a_cmd(GC9A01A_CASET);  // Column address set
  uint8_t data[4] = {
    (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
    (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
  };
  gc9a01a_data(data, 4);

  gc9a01a_cmd(GC9A01A_RASET);  // Row address set
  data[0] = (uint8_t)(y0 >> 8);
  data[1] = (uint8_t)(y0 & 0xFF);
  data[2] = (uint8_t)(y1 >> 8);
  data[3] = (uint8_t)(y1 & 0xFF);
  gc9a01a_data(data, 4);

  gc9a01a_cmd(GC9A01A_RAMWR);  // Memory write
}

// Viewport getters/setters
void gc9a01a_set_viewport(int16_t offset_x, int16_t offset_y, uint16_t width, uint16_t height) {
  viewport_offset_x = offset_x;
  viewport_offset_y = offset_y;
  viewport_width = width;
  viewport_height = height;
  ESP_LOGI(TAG, "Viewport set: offset=(%d,%d) size=%dx%d", offset_x, offset_y, width, height);
}

uint16_t gc9a01a_get_viewport_width(void) { return viewport_width; }
uint16_t gc9a01a_get_viewport_height(void) { return viewport_height; }
int16_t gc9a01a_get_viewport_offset_x(void) { return viewport_offset_x; }
int16_t gc9a01a_get_viewport_offset_y(void) { return viewport_offset_y; }

// Flush pixels to the display
void gc9a01a_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  static uint32_t flush_count = 0;
  flush_count++;

  if (!gc9a01a_line_buf) {
    ESP_LOGE(TAG, "Line buffer not allocated!");
    lv_display_flush_ready(disp);
    return;
  }

  // Validate area bounds against viewport
  if (area->x1 < 0 || area->y1 < 0 || area->x2 >= viewport_width || area->y2 >= viewport_height ||
      area->x1 > area->x2 || area->y1 > area->y2) {
    ESP_LOGE(TAG, "Invalid area bounds: (%d,%d)-(%d,%d)", 
             (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
    lv_display_flush_ready(disp);
    return;
  }

  #if DEBUG_FLUSH_MESSAGES
  if (flush_count % 100 == 0) {
    ESP_LOGI(TAG, "Flush #%lu: area (%d,%d)-(%d,%d) -> phys (%d,%d)-(%d,%d)", 
      (unsigned long)flush_count, 
      (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
      (int)(area->x1 + viewport_offset_x), (int)(area->y1 + viewport_offset_y),
      (int)(area->x2 + viewport_offset_x), (int)(area->y2 + viewport_offset_y));
  }
  #endif

  // Mirror to USB stream if active (do this before SPI transfer to avoid delay)
  if (lvgl_stream_is_active()) {
    lvgl_stream_queue_flush(area, px_map);
  }

  // Set the address window with viewport offset applied
  gc9a01a_set_addr_window(
    area->x1 + viewport_offset_x, 
    area->y1 + viewport_offset_y, 
    area->x2 + viewport_offset_x, 
    area->y2 + viewport_offset_y
  );

  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);

  // The px_map is in RGB888 format (3 bytes per pixel)
  // Send data line by line for better DMA handling
  gpio_set_level(PIN_DC, 1);  // Data mode

  size_t line_size = w * 3;  // RGB888 = 3 bytes per pixel
  uint8_t *src = px_map;

  for (int32_t y = 0; y < h; y++) {
    // Copy line to DMA buffer
    memcpy(gc9a01a_line_buf, src, line_size);
    
    spi_transaction_t t = {
      .length = line_size * 8,
      .tx_buffer = gc9a01a_line_buf,
    };
    
    esp_err_t ret = spi_device_polling_transmit(spi, &t);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(ret));
      break;
    }
    
    src += line_size;
  }

  lv_display_flush_ready(disp);
}

// Display driver interface implementation
const display_driver_t gc9a01a_driver = {
  .name = "GC9A01A",
  .width = 198,  // Default viewport width (will be overridden by get functions)
  .height = 198, // Default viewport height
  .color_format = LV_COLOR_FORMAT_RGB888,
  .init = gc9a01a_init,
  .flush = gc9a01a_flush,
};

