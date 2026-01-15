#include "display_driver.h"
#include "st7789v3_driver.h"
#include "esp_log.h"

#define TAG "DISP_DRV"

static const display_driver_t *s_active_driver = NULL;

void display_driver_select(void) {
  s_active_driver = &st7789v3_driver;
  ESP_LOGI(TAG, "Selected ST7789V3 driver (viewport %dx%d @ offset %d,%d)", 
    st7789v3_get_viewport_width(), st7789v3_get_viewport_height(),
    st7789v3_get_viewport_offset_x(), st7789v3_get_viewport_offset_y());
}

const display_driver_t *display_driver_get(void) {
  return s_active_driver;
}

uint16_t display_get_width(void) {
  return st7789v3_get_viewport_width();
}

uint16_t display_get_height(void) {
  return st7789v3_get_viewport_height();
}

lv_color_format_t display_get_color_format(void) {
  if (s_active_driver) return s_active_driver->color_format;
  return LV_COLOR_FORMAT_RGB565;
}

bool display_is_circular(void) {
  return true;  // ST7789V3 is viewed through a circular aperture
}
