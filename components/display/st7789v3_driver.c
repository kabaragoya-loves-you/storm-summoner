#include "st7789v3_driver.h"
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

#define TAG "ST7789V3"

// Debug flag for flush logging
#define DEBUG_FLUSH_MESSAGES 0

// Physical display dimensions
#define ST7789V3_PHYSICAL_WIDTH  240
#define ST7789V3_PHYSICAL_HEIGHT 240

// SPI transmit chunk size in bytes. Must be a multiple of the ESP32-P4 64-byte
// L1 cache line so the SPI master never falls into its cache-alignment realloc
// path (heap_caps_aligned_alloc from the scarce MALLOC_CAP_DMA pool, which fails
// with ESP_ERR_NO_MEM under DMA-heap pressure). Pixels are streamed through this
// persistent buffer in 64-byte-aligned spans; kept small to avoid permanently
// consuming the very limited DMA-capable heap on this board.
#define ST7789V3_DMA_CHUNK 512

// Viewport configuration (virtual display within physical)
// ST7789V3 is centered in aperture, so no offset needed
static int16_t viewport_offset_x = 0;
static int16_t viewport_offset_y = 0;
static uint16_t viewport_width = 240;
static uint16_t viewport_height = 240;

// ST7789V3 command definitions
#define ST7789V3_NOP       0x00
#define ST7789V3_SWRESET   0x01
#define ST7789V3_SLPIN     0x10
#define ST7789V3_SLPOUT    0x11
#define ST7789V3_PTLON     0x12
#define ST7789V3_NORON     0x13
#define ST7789V3_INVOFF    0x20
#define ST7789V3_INVON     0x21
#define ST7789V3_DISPOFF   0x28
#define ST7789V3_DISPON    0x29
#define ST7789V3_CASET     0x2A
#define ST7789V3_RASET     0x2B
#define ST7789V3_RAMWR     0x2C
#define ST7789V3_PTLAR     0x30
#define ST7789V3_VSCRDEF   0x33
#define ST7789V3_COLMOD    0x3A
#define ST7789V3_MADCTL    0x36
#define ST7789V3_VSCSAD    0x37
#define ST7789V3_TEON      0x35

// ST7789V3-specific commands
#define ST7789V3_PORCTRL   0xB2  // Porch control
#define ST7789V3_GCTRL     0xB7  // Gate control
#define ST7789V3_VCOMS     0xBB  // VCOM setting
#define ST7789V3_LCMCTRL   0xC0  // LCM control
#define ST7789V3_VDVVRHEN  0xC2  // VDV and VRH command enable
#define ST7789V3_VRHS      0xC3  // VRH set
#define ST7789V3_VDVS      0xC4  // VDV set
#define ST7789V3_FRCTRL2   0xC6  // Frame rate control in normal mode
#define ST7789V3_PWCTRL1   0xD0  // Power control 1
#define ST7789V3_PVGAMCTRL 0xE0  // Positive voltage gamma control
#define ST7789V3_NVGAMCTRL 0xE1  // Negative voltage gamma control

// MADCTL bits
#define MADCTL_MY  0x80  // Row address order
#define MADCTL_MX  0x40  // Column address order
#define MADCTL_MV  0x20  // Row/Column exchange
#define MADCTL_ML  0x10  // Vertical refresh order
#define MADCTL_BGR 0x08  // BGR order (vs RGB)
#define MADCTL_MH  0x04  // Horizontal refresh order

static spi_device_handle_t spi = NULL;
static uint8_t *st7789v3_line_buf = NULL;


// Forward declaration
static void st7789v3_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

// Send a command byte
static void st7789v3_cmd(uint8_t cmd) {
  gpio_set_level(PIN_DC, 0);  // Command mode
  spi_transaction_t t = {
    .length = 8,
    .tx_buffer = &cmd,
  };
  spi_device_polling_transmit(spi, &t);
}

// Send data bytes
static void st7789v3_data(const uint8_t *data, size_t len) {
  if (len == 0) return;
  gpio_set_level(PIN_DC, 1);  // Data mode
  spi_transaction_t t = {
    .length = len * 8,
    .tx_buffer = data,
  };
  spi_device_polling_transmit(spi, &t);
}

// Send a single data byte
static void st7789v3_data_byte(uint8_t val) {
  st7789v3_data(&val, 1);
}

// Initialize the ST7789V3 display
void st7789v3_init(void) {
  ESP_LOGI(TAG, "Initializing ST7789V3 240x240 IPS display");

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

  // Assert reset before SPI init - display will ignore any clock glitches while in reset
  gpio_set_level(PIN_RESET, 0);
  vTaskDelay(pdMS_TO_TICKS(10));

  // Initialize SPI bus (display is held in reset, so clock glitches are ignored)
  spi_bus_config_t buscfg = {
    .miso_io_num = -1,
    .mosi_io_num = PIN_MOSI,
    .sclk_io_num = PIN_SCLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = ST7789V3_WIDTH * ST7789V3_HEIGHT * 2,  // RGB565
  };

  esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
    return;
  }

  spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 80 * 1000 * 1000,  // 80 MHz - maximum speed
    .mode = 3,  // SPI mode 3 (CPOL=1, CPHA=1) - required for ST7789V3
    .spics_io_num = -1,
    .queue_size = 7,
    .flags = SPI_DEVICE_NO_DUMMY,
  };

  ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
    return;
  }

  ESP_LOGI(TAG, "SPI initialized at 80 MHz, mode 3, with DMA");

  // Now release reset - SPI is configured with correct clock polarity
  gpio_set_level(PIN_RESET, 1);
  vTaskDelay(pdMS_TO_TICKS(150)); // Wait for internal regulator (datasheet: 120ms min)

  // Persistent, 64-byte-aligned DMA scratch buffer. Transfers are sliced to this
  // size and kept cache-line aligned so the SPI master transmits directly from
  // it without allocating a temporary bounce buffer per transfer.
  st7789v3_line_buf = heap_caps_aligned_alloc(64, ST7789V3_DMA_CHUNK,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  if (!st7789v3_line_buf) {
    st7789v3_line_buf = heap_caps_aligned_alloc(64, ST7789V3_DMA_CHUNK, MALLOC_CAP_DMA);
  }
  if (!st7789v3_line_buf) {
    ESP_LOGE(TAG, "Failed to allocate DMA buffer");
    return;
  }

  // ST7789V3 initialization sequence (based on LovyanGFX Panel_ST7789P3)
  
  // Software reset
  st7789v3_cmd(ST7789V3_SWRESET);
  vTaskDelay(pdMS_TO_TICKS(150));

  // Exit sleep mode
  st7789v3_cmd(ST7789V3_SLPOUT);
  vTaskDelay(pdMS_TO_TICKS(120));

  // Memory access control (orientation)
  st7789v3_cmd(ST7789V3_MADCTL);
  st7789v3_data_byte(0x00);

  // Pixel format: 16-bit RGB565
  st7789v3_cmd(ST7789V3_COLMOD);
  st7789v3_data_byte(0x55);

  // Porch control (0xB2)
  st7789v3_cmd(ST7789V3_PORCTRL);
  st7789v3_data_byte(0x0C);
  st7789v3_data_byte(0x0C);
  st7789v3_data_byte(0x00);
  st7789v3_data_byte(0x33);
  st7789v3_data_byte(0x33);

  // Gate control (0xB7) - LovyanGFX uses 0x56
  st7789v3_cmd(ST7789V3_GCTRL);
  st7789v3_data_byte(0x56);

  // VCOM setting (0xBB) - LovyanGFX uses 0x1D
  st7789v3_cmd(ST7789V3_VCOMS);
  st7789v3_data_byte(0x1D);

  // LCM control (0xC0)
  st7789v3_cmd(ST7789V3_LCMCTRL);
  st7789v3_data_byte(0x2C);

  // VDV and VRH command enable (0xC2) - only 1 byte
  st7789v3_cmd(ST7789V3_VDVVRHEN);
  st7789v3_data_byte(0x01);

  // VRH set (0xC3) - LovyanGFX uses 0x0F
  st7789v3_cmd(ST7789V3_VRHS);
  st7789v3_data_byte(0x0F);

  // Frame rate control (0xC6)
  st7789v3_cmd(ST7789V3_FRCTRL2);
  st7789v3_data_byte(0x0F);

  // Power control 1 (0xD0) - LovyanGFX sends 0xA7 first, then 0xA4, 0xA1
  st7789v3_cmd(ST7789V3_PWCTRL1);
  st7789v3_data_byte(0xA7);
  st7789v3_cmd(ST7789V3_PWCTRL1);
  st7789v3_data_byte(0xA4);
  st7789v3_data_byte(0xA1);

  // ST7789P3-specific command 0xD6 with 0xA1
  st7789v3_cmd(0xD6);
  st7789v3_data_byte(0xA1);

  // Positive voltage gamma control (0xE0) - LovyanGFX values
  st7789v3_cmd(ST7789V3_PVGAMCTRL);
  st7789v3_data_byte(0xF0);
  st7789v3_data_byte(0x02);
  st7789v3_data_byte(0x07);
  st7789v3_data_byte(0x05);
  st7789v3_data_byte(0x06);
  st7789v3_data_byte(0x14);
  st7789v3_data_byte(0x2F);
  st7789v3_data_byte(0x54);
  st7789v3_data_byte(0x46);
  st7789v3_data_byte(0x38);
  st7789v3_data_byte(0x13);
  st7789v3_data_byte(0x11);
  st7789v3_data_byte(0x2E);
  st7789v3_data_byte(0x35);

  // Negative voltage gamma control (0xE1) - LovyanGFX values
  st7789v3_cmd(ST7789V3_NVGAMCTRL);
  st7789v3_data_byte(0xF0);
  st7789v3_data_byte(0x08);
  st7789v3_data_byte(0x0C);
  st7789v3_data_byte(0x0C);
  st7789v3_data_byte(0x09);
  st7789v3_data_byte(0x05);
  st7789v3_data_byte(0x2F);
  st7789v3_data_byte(0x43);
  st7789v3_data_byte(0x46);
  st7789v3_data_byte(0x36);
  st7789v3_data_byte(0x10);
  st7789v3_data_byte(0x12);
  st7789v3_data_byte(0x2C);
  st7789v3_data_byte(0x32);

  // Display inversion ON - required for correct colors on IPS panel
  st7789v3_cmd(ST7789V3_INVON);

  // Normal display mode on
  st7789v3_cmd(ST7789V3_NORON);
  vTaskDelay(pdMS_TO_TICKS(10));

  // Display on
  st7789v3_cmd(ST7789V3_DISPON);
  vTaskDelay(pdMS_TO_TICKS(100));

  // Clear display to black
  st7789v3_set_addr_window(0, 0, ST7789V3_PHYSICAL_WIDTH - 1, ST7789V3_PHYSICAL_HEIGHT - 1);
  gpio_set_level(PIN_DC, 1);
  memset(st7789v3_line_buf, 0, ST7789V3_PHYSICAL_WIDTH * 2);
  for (int y = 0; y < ST7789V3_PHYSICAL_HEIGHT; y++) {
    spi_transaction_t t = { .length = ST7789V3_PHYSICAL_WIDTH * 2 * 8, .tx_buffer = st7789v3_line_buf };
    spi_device_polling_transmit(spi, &t);
  }

  ESP_LOGI(TAG, "Display initialization complete");
}

// Set the address window for pixel write
static void st7789v3_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  st7789v3_cmd(ST7789V3_CASET);  // Column address set
  uint8_t data[4] = {
    (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
    (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
  };
  st7789v3_data(data, 4);

  st7789v3_cmd(ST7789V3_RASET);  // Row address set
  data[0] = (uint8_t)(y0 >> 8);
  data[1] = (uint8_t)(y0 & 0xFF);
  data[2] = (uint8_t)(y1 >> 8);
  data[3] = (uint8_t)(y1 & 0xFF);
  st7789v3_data(data, 4);

  st7789v3_cmd(ST7789V3_RAMWR);  // Memory write
}

// Viewport getters/setters
void st7789v3_set_viewport(int16_t offset_x, int16_t offset_y, uint16_t width, uint16_t height) {
  viewport_offset_x = offset_x;
  viewport_offset_y = offset_y;
  viewport_width = width;
  viewport_height = height;
  ESP_LOGI(TAG, "Viewport set: offset=(%d,%d) size=%dx%d", offset_x, offset_y, width, height);
}

uint16_t st7789v3_get_viewport_width(void) { return viewport_width; }
uint16_t st7789v3_get_viewport_height(void) { return viewport_height; }
int16_t st7789v3_get_viewport_offset_x(void) { return viewport_offset_x; }
int16_t st7789v3_get_viewport_offset_y(void) { return viewport_offset_y; }

// Flush pixels to the display
void st7789v3_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  if (!st7789v3_line_buf) {
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
  st7789v3_set_addr_window(
    area->x1 + viewport_offset_x, 
    area->y1 + viewport_offset_y, 
    area->x2 + viewport_offset_x, 
    area->y2 + viewport_offset_y
  );

  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);

  // px_map is a contiguous, row-major RGB565 block (2 bytes/pixel). Stream it
  // through the persistent aligned DMA buffer. Every transfer is a multiple of
  // the 64-byte cache line EXCEPT a final sub-cache-line remainder. The aligned
  // transfers go straight out with no bounce allocation; the remainder is always
  // < 64 bytes, so the SPI master's fallback allocation is rounded up to exactly
  // 64 bytes -- small enough to satisfy even when the DMA pool is nearly drained.
  gpio_set_level(PIN_DC, 1);  // Data mode

  uint16_t *src16 = (uint16_t *)px_map;
  size_t total_bytes = (size_t)w * (size_t)h * 2;
  size_t sent = 0;

  while (sent < total_bytes) {
    size_t remaining = total_bytes - sent;
    size_t xfer_bytes;
    if (remaining >= 64) {
      // Largest 64-aligned span we can do this pass (capped at the chunk size).
      xfer_bytes = remaining < ST7789V3_DMA_CHUNK ? (remaining & ~(size_t)63) : ST7789V3_DMA_CHUNK;
    } else {
      xfer_bytes = remaining;  // final < 64-byte remainder
    }

    // Byte-swap into the DMA buffer: ST7789V3 wants big-endian RGB565, while
    // ESP32/LVGL produce little-endian.
    size_t px_off = sent / 2;
    size_t npx = xfer_bytes / 2;
    uint16_t *dst16 = (uint16_t *)st7789v3_line_buf;
    for (size_t i = 0; i < npx; i++) {
      dst16[i] = __builtin_bswap16(src16[px_off + i]);
    }

    spi_transaction_t t = {
      .length = xfer_bytes * 8,
      .tx_buffer = st7789v3_line_buf,
    };

    esp_err_t ret = spi_device_polling_transmit(spi, &t);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(ret));
      break;
    }

    sent += xfer_bytes;
  }

  lv_display_flush_ready(disp);
}

// Display driver interface implementation
const display_driver_t st7789v3_driver = {
  .name = "ST7789V3",
  .width = 240,
  .height = 240,
  .color_format = LV_COLOR_FORMAT_RGB565,
  .init = st7789v3_init,
  .flush = st7789v3_flush,
};
