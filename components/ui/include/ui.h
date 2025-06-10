#ifndef UI_H
#define UI_H

#define SLICE_COUNT 8

typedef enum {
  APP_MODE_PERFORMANCE,
  APP_MODE_PROGRAMMING,
  APP_MODE_SCREENSAVER
} app_mode_t;

extern app_mode_t g_app_mode;
extern bool g_at_programming_top_level_menu;

void ui_init(void);
void draw_lizard(void);
void boundary_circle(void);
void pizza(void);
void pizza2(const bool slice_states[SLICE_COUNT]);

app_mode_t ui_get_app_mode(void);
void ui_set_app_mode(app_mode_t mode);
bool ui_is_programming_top_level(void);
void ui_set_programming_top_level(bool is_top_level);

void ui_graphics_suspend(void);
void ui_graphics_resume(void);

#endif // UI_H 