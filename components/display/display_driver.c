#include "display_driver.h"
#include "ssd1327_driver.h"
#include "gc9a01a_driver.h"
#include "revision.h"
#include "esp_log.h"

#define TAG "DISP_DRV"

// Currently selected display driver
static const display_driver_t *s_active_driver = NULL;
static display_type_t s_display_type = DISPLAY_TYPE_UNKNOWN;

void display_driver_select(void) {
  hw_revision_t rev = revision_get();
  
  ESP_LOGI(TAG, "Selecting display driver for hardware revision: %s", revision_get_string());
  
  // Rev 2 uses GC9A01A, all others use SSD1327
  if (rev == HW_REV_2) {
    s_active_driver = &gc9a01a_driver;
    s_display_type = DISPLAY_TYPE_GC9A01A;
    ESP_LOGI(TAG, "Selected GC9A01A driver (viewport %dx%d @ offset %d,%d)", 
      gc9a01a_get_viewport_width(), gc9a01a_get_viewport_height(),
      gc9a01a_get_viewport_offset_x(), gc9a01a_get_viewport_offset_y());
  } else {
    s_active_driver = &ssd1327_driver;
    s_display_type = DISPLAY_TYPE_SSD1327;
    ESP_LOGI(TAG, "Selected SSD1327 driver (128x128 greyscale OLED)");
  }
}

const display_driver_t *display_driver_get(void) {
  return s_active_driver;
}

display_type_t display_driver_get_type(void) {
  return s_display_type;
}

uint16_t display_get_width(void) {
  if (!s_active_driver) return 128;  // Default fallback
  // GC9A01A uses viewport dimensions
  if (s_display_type == DISPLAY_TYPE_GC9A01A) {
    return gc9a01a_get_viewport_width();
  }
  return s_active_driver->width;
}

uint16_t display_get_height(void) {
  if (!s_active_driver) return 128;  // Default fallback
  // GC9A01A uses viewport dimensions
  if (s_display_type == DISPLAY_TYPE_GC9A01A) {
    return gc9a01a_get_viewport_height();
  }
  return s_active_driver->height;
}

lv_color_format_t display_get_color_format(void) {
  if (s_active_driver) return s_active_driver->color_format;
  return LV_COLOR_FORMAT_RGB565;  // Default fallback
}

bool display_is_circular(void) {
  // Both the SSD1327 OLED and GC9A01A IPS are round displays
  return true;
}

