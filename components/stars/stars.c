#include "stars.h"
#include <stdlib.h>
#include "esp_log.h"

#define TAG "stars"

// LVGL canvas buffer (16-bit color)
static lv_color_t starfield_buf[DISP_HOR_RES * DISP_VER_RES];

// Canvas object
static lv_obj_t *canvas = NULL;
static lv_timer_t *g_starfield_animation_timer = NULL;

// Star data
static Star stars[MAX_STARS];

static void init_star(int i);
static void init_stars(void);
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

static void starfield_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);

  if (!canvas) return; // Check if canvas exists

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

void create_starfield(void) {
  ESP_LOGI(TAG, "create_starfield() called (should be deprecated).");
  // For now, let's not call starfield_start() from here to prevent auto-start.
}

void starfield_start(void) {
  if (canvas != NULL) {
    ESP_LOGW(TAG, "Starfield already started.");
    return;
  }
  ESP_LOGI(TAG, "Starting starfield...");

  canvas = lv_canvas_create(lv_scr_act());
  if (!canvas) {
    ESP_LOGE(TAG, "Failed to create starfield canvas.");
    return;
  }

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

  if (g_starfield_animation_timer != NULL) {
    lv_timer_del(g_starfield_animation_timer);
    g_starfield_animation_timer = NULL;
  }
  g_starfield_animation_timer = lv_timer_create(starfield_timer_cb, 16, NULL);
  if (!g_starfield_animation_timer) {
    ESP_LOGE(TAG, "Failed to create starfield animation timer.");
    lv_obj_del(canvas);
    canvas = NULL;
    return;
  }
  ESP_LOGI(TAG, "Starfield started successfully.");
}

void starfield_stop(void) {
  ESP_LOGI(TAG, "Stopping starfield...");
  if (g_starfield_animation_timer != NULL) {
    lv_timer_del(g_starfield_animation_timer);
    g_starfield_animation_timer = NULL;
    ESP_LOGI(TAG, "Starfield animation timer deleted.");
  }
  if (canvas != NULL) {
    lv_obj_del(canvas);
    canvas = NULL;
    ESP_LOGI(TAG, "Starfield canvas deleted.");
  }
  ESP_LOGI(TAG, "Starfield stopped.");
}
