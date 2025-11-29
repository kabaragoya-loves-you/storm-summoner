#ifndef UI_H
#define UI_H

#include "lvgl.h"

#define SLICE_COUNT 8

typedef enum {
  APP_MODE_PERFORMANCE,
  APP_MODE_PROGRAMMING,
  APP_MODE_SCREENSAVER
} app_mode_t;

typedef void (*ui_draw_func_t)(void);
typedef void (*ui_teardown_func_t)(void);
typedef void (*ui_init_func_t)(void);

typedef struct {
  ui_draw_func_t draw_func;
  ui_teardown_func_t teardown_func;
  ui_init_func_t init_func;
  const char* name;
} ui_draw_module_t;

#define UI_CREATE_DEFERRED_DRAW_FUNC(module_name, deferred_cb_func) \
static void module_name##_draw(void) { \
  lv_timer_t *deferred_timer = lv_timer_create(deferred_cb_func, 10, NULL); \
  if (deferred_timer != NULL) lv_timer_set_repeat_count(deferred_timer, 1); \
}

extern app_mode_t g_app_mode;
extern bool g_at_programming_top_level_menu;

extern ui_draw_module_t boundary_circle_module;

// Boundary circle calibration API
void boundary_circle_set_size(int32_t size);
void boundary_circle_set_left(int32_t x);
void boundary_circle_set_top(int32_t y);
int32_t boundary_circle_get_size(void);
int32_t boundary_circle_get_left(void);
int32_t boundary_circle_get_top(void);

extern ui_draw_module_t pizza_module;
extern ui_draw_module_t pizza2_module;
extern ui_draw_module_t draw_lizard_module;
extern ui_draw_module_t sphere_module;
extern ui_draw_module_t greyscale_test_module;
extern ui_draw_module_t buttons_module;
extern ui_draw_module_t template_module;

void ui_init(void);

void ui_set_draw_module(ui_draw_module_t* module);
ui_draw_module_t* ui_get_current_module(void);

app_mode_t ui_get_app_mode(void);
void ui_set_app_mode(app_mode_t mode);

static inline bool ui_is_in_screensaver_mode(void) {
  return ui_get_app_mode() == APP_MODE_SCREENSAVER;
}

static inline bool ui_is_in_performance_mode(void) {
  return ui_get_app_mode() == APP_MODE_PERFORMANCE;
}

static inline bool ui_is_in_programming_mode(void) {
  return ui_get_app_mode() == APP_MODE_PROGRAMMING;
}

bool ui_is_programming_top_level(void);
void ui_set_programming_top_level(bool is_top_level);

void ui_graphics_suspend(void);
void ui_graphics_resume(void);

// Canvas buffer management for memory optimization
// Returns true if release was initiated successfully, false if aborted
// post_release_cb: Optional callback to run after release completes (in LVGL context)
bool ui_release_canvas_buffer(void (*post_release_cb)(void));
void ui_reclaim_canvas_buffer(void);


// Touch state and configuration API
bool ui_touch_is_button_pressed(uint8_t pad_id);
uint32_t ui_get_button13_long_press_ms(void);
void ui_set_button13_long_press_ms(uint32_t value_ms);
// Rotary functions removed - now handled by touchwheel system

#endif // UI_H 