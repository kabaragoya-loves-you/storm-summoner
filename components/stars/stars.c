#include "stars.h"
#include <stdlib.h>
#include "esp_log.h"

#define TAG "stars"

// LVGL canvas buffer (16-bit color)
static lv_color_t starfield_buf[DISP_HOR_RES * DISP_VER_RES];

// Canvas object
static lv_obj_t *canvas = NULL;

// Star data
static Star stars[MAX_STARS];

static void init_star(int i);
static void init_stars(void);
static void update_and_draw_stars(void);
static void starfield_timer_cb(lv_timer_t *timer);

static void init_star(int i) {
  stars[i].x = (rand() % 200) - 100;
  stars[i].y = (rand() % 200) - 100;
  stars[i].z = (rand() % 400) + 100;
  stars[i].brightness = rand() & 0xFF;
}

static void init_stars(void) {
  for (int i = 0; i < MAX_STARS; i++) {
    init_star(i);
  }
}

static void update_and_draw_stars(void) {
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

  int center_x = DISP_HOR_RES / 2;
  int center_y = DISP_VER_RES / 2;

  for (int i = 0; i < MAX_STARS; i++) {
    stars[i].z -= 7;
    float depth = (float)stars[i].z;

    int x_proj = (int)((stars[i].x / depth) * 100.0f) + center_x;
    int y_proj = (int)((stars[i].y / depth) * 100.0f) + center_y;

    if (x_proj < 0 || x_proj >= DISP_HOR_RES ||
        y_proj < 0 || y_proj >= DISP_VER_RES ||
        stars[i].z < 1) {
      init_star(i);
      continue;
    }

    uint8_t gray_level_0_15 = (stars[i].brightness * 15) / 255;
    uint8_t scaled8 = gray_level_0_15 * 17;

    lv_color_t color = lv_color_make(scaled8, scaled8, scaled8);
    lv_canvas_set_px(canvas, x_proj, y_proj, color, LV_OPA_COVER);
  }

  lv_obj_invalidate(canvas);
}

static void starfield_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  update_and_draw_stars();
}

void create_starfield(void) {
  canvas = lv_canvas_create(lv_scr_act());

  lv_canvas_set_buffer(
    canvas,
    starfield_buf,
    DISP_HOR_RES,
    DISP_VER_RES,
    LV_COLOR_FORMAT_NATIVE
  );

  lv_obj_set_size(canvas, DISP_HOR_RES, DISP_VER_RES);
  lv_obj_center(canvas);

  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

  init_stars();

  lv_timer_create(starfield_timer_cb, 16, NULL);
}
