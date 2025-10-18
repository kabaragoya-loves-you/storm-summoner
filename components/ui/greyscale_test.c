#include "lvgl.h"
#include "ui.h"
#include "esp_log.h"
#include <stdlib.h>

#define TAG "GREYSCALE_TEST"
#define SWATCH_SIZE 8
#define SWATCHES_PER_ROW 4
#define CANVAS_SIZE 64

extern lv_obj_t *canvas;

static lv_obj_t *test_canvas = NULL;
static lv_color_t *canvas_buf = NULL;

static void greyscale_test_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }

  // Only create widgets if they don't exist
  if (test_canvas == NULL) {
    // Allocate canvas buffer
    canvas_buf = malloc(CANVAS_SIZE * CANVAS_SIZE * sizeof(lv_color_t));
    if (!canvas_buf) {
      ESP_LOGE(TAG, "Failed to allocate canvas buffer");
      lv_timer_del(timer);
      return;
    }

    // Create canvas
    test_canvas = lv_canvas_create(lv_scr_act());
    lv_obj_remove_style_all(test_canvas);
    lv_obj_set_size(test_canvas, CANVAS_SIZE, CANVAS_SIZE);
    lv_obj_align(test_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(test_canvas, 0, 0);
    
    // Initialize canvas buffer to black
    for (int i = 0; i < CANVAS_SIZE * CANVAS_SIZE; i++) {
      canvas_buf[i] = lv_color_make(0, 0, 0);
    }
    
    lv_canvas_set_buffer(test_canvas, canvas_buf, CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_RGB565);
    
    // Set background to black
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    
    // Draw 16 greyscale swatches (0-15, mapped to 0-255)
    for (int i = 0; i < 16; i++) {
      // Calculate position in 4x4 grid
      int col = i % SWATCHES_PER_ROW;
      int row = i / SWATCHES_PER_ROW;
      
      int start_x = col * (SWATCH_SIZE + 2) + 8;  // 2px spacing, 8px margin
      int start_y = row * (SWATCH_SIZE + 2) + 8;
      
      // Map i (0-15) to greyscale value (0-255)
      uint8_t grey_value = (i * 255) / 15;
      lv_color_t swatch_color = lv_color_make(grey_value, grey_value, grey_value);
      
      // Draw swatch rectangle
      for (int y = 0; y < SWATCH_SIZE; y++) {
        for (int x = 0; x < SWATCH_SIZE; x++) {
          int px = start_x + x;
          int py = start_y + y;
          if (px >= 0 && px < CANVAS_SIZE && py >= 0 && py < CANVAS_SIZE) {
            lv_canvas_set_px(test_canvas, px, py, swatch_color, LV_OPA_COVER);
          }
        }
      }
      
      // Draw white border around each swatch for clarity
      lv_color_t border_color = lv_color_make(255, 255, 255);
      
      // Top and bottom borders
      for (int x = 0; x < SWATCH_SIZE; x++) {
        if (start_x + x < CANVAS_SIZE) {
          if (start_y - 1 >= 0)
            lv_canvas_set_px(test_canvas, start_x + x, start_y - 1, border_color, LV_OPA_COVER);
          if (start_y + SWATCH_SIZE < CANVAS_SIZE)
            lv_canvas_set_px(test_canvas, start_x + x, start_y + SWATCH_SIZE, border_color, LV_OPA_COVER);
        }
      }
      
      // Left and right borders  
      for (int y = 0; y < SWATCH_SIZE; y++) {
        if (start_y + y < CANVAS_SIZE) {
          if (start_x - 1 >= 0)
            lv_canvas_set_px(test_canvas, start_x - 1, start_y + y, border_color, LV_OPA_COVER);
          if (start_x + SWATCH_SIZE < CANVAS_SIZE)
            lv_canvas_set_px(test_canvas, start_x + SWATCH_SIZE, start_y + y, border_color, LV_OPA_COVER);
        }
      }
    }
    
    lv_obj_invalidate(test_canvas);
    
    ESP_LOGI(TAG, "Greyscale test display created - 16 swatches from black (0) to white (15)");
  }
  
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(greyscale_test, greyscale_test_draw_deferred_cb)

static void greyscale_test_teardown(void) {
  if (test_canvas) {
    lv_obj_del(test_canvas);
    test_canvas = NULL;
  }
  
  if (canvas_buf) {
    free(canvas_buf);
    canvas_buf = NULL;
  }
  
  ESP_LOGI(TAG, "Greyscale test teardown complete");
}

static void greyscale_test_init(void) {
  ESP_LOGI(TAG, "Greyscale test module initialized");
}

ui_draw_module_t greyscale_test_module = {
  .draw_func = greyscale_test_draw,
  .teardown_func = greyscale_test_teardown,
  .init_func = greyscale_test_init,
  .name = "greyscale_test"
}; 