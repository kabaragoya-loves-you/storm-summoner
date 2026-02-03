#include "lvgl.h"
#include "ui.h"
#include "name_edit.h"
#include "text_edit.h"
#include "scene.h"
#include "scene_name_gen.h"
#include "menu.h"
#include "menu_pages.h"
#include "display_driver.h"
#include "event_bus.h"
#include "esp_log.h"
#include <string.h>

#define TAG "TEXT_EDIT"

// Character set available in chalet_ny font
// Space first, then uppercase, lowercase, digits, and punctuation
static const char CHARSET[] =
  " ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789"
  ".,!?-_+@#";

#define CHARSET_LEN (sizeof(CHARSET) - 1)
#define VISIBLE_CHARS 7  // Number of characters visible in gallery

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_title_label = NULL;
static lv_obj_t *g_textarea = NULL;
static lv_obj_t *g_gallery_container = NULL;
static lv_obj_t *g_gallery_labels[VISIBLE_CHARS];
static lv_obj_t *g_selection_box = NULL;

// Flag to consume release events after exit is triggered
static bool g_exit_pending = false;

// Editor configuration (copied from text_edit_start)
static text_edit_config_t g_config;
static char g_edit_buffer[TEXT_EDIT_MAX_LEN + 1];
static uint8_t g_max_length = TEXT_EDIT_MAX_LEN;
static int g_gallery_index = 0;  // Current selected char in charset
static bool g_active = false;

// For backward compatibility with name_edit_set_name
static uint8_t g_scene_index = 0;
static bool g_is_scene_name_mode = false;

// Forward declarations
static void update_gallery_display(void);
static void insert_selected_char(void);
static void post_haptic_click(void);

static void text_edit_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen == NULL) {
    uint16_t disp_w = display_get_width();
    uint16_t disp_h = display_get_height();

    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, disp_w, disp_h);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_screen, 0, 0);
    lv_obj_set_style_pad_all(g_screen, 8, 0);

    // Title (use config title or default)
    g_title_label = lv_label_create(g_screen);
    lv_label_set_text(g_title_label, g_config.title ? g_config.title : "Edit Text");
    lv_obj_set_style_text_color(g_title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_title_label, LV_ALIGN_TOP_MID, 0, 20);

    // Textarea (single line, configurable max length)
    g_textarea = lv_textarea_create(g_screen);
    lv_textarea_set_one_line(g_textarea, true);
    lv_textarea_set_max_length(g_textarea, g_max_length);
    lv_textarea_set_text(g_textarea, g_edit_buffer);
    lv_textarea_set_cursor_pos(g_textarea, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_set_width(g_textarea, disp_w - 40);
    lv_obj_align(g_textarea, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(g_textarea, lv_color_make(30, 30, 30), 0);
    lv_obj_set_style_text_color(g_textarea, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_textarea, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_color(g_textarea, lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_border_width(g_textarea, 1, 0);
    lv_obj_set_style_radius(g_textarea, 4, 0);
    lv_obj_set_style_pad_all(g_textarea, 6, 0);
    // Disable click-to-position cursor (we control cursor with pads)
    lv_textarea_set_cursor_click_pos(g_textarea, false);
    // Style the cursor to be visible
    lv_obj_set_style_anim_duration(g_textarea, 500, LV_PART_CURSOR);  // Blink rate
    lv_obj_set_style_bg_color(g_textarea, lv_color_make(255, 200, 0), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(g_textarea, LV_OPA_COVER, LV_PART_CURSOR);

    // Gallery container (horizontal character display)
    g_gallery_container = lv_obj_create(g_screen);
    lv_obj_set_size(g_gallery_container, disp_w - 20, 50);
    lv_obj_align(g_gallery_container, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(g_gallery_container, lv_color_make(20, 20, 20), 0);
    lv_obj_set_style_bg_opa(g_gallery_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_gallery_container, 0, 0);
    lv_obj_set_style_pad_all(g_gallery_container, 0, 0);
    lv_obj_set_flex_flow(g_gallery_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_gallery_container, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Create character labels for gallery
    int char_width = (disp_w - 40) / VISIBLE_CHARS;
    for (int i = 0; i < VISIBLE_CHARS; i++) {
      g_gallery_labels[i] = lv_label_create(g_gallery_container);
      lv_label_set_text(g_gallery_labels[i], "A");
      lv_obj_set_style_text_font(g_gallery_labels[i], &lv_font_montserrat_20, 0);
      lv_obj_set_width(g_gallery_labels[i], char_width);
      lv_obj_set_style_text_align(g_gallery_labels[i], LV_TEXT_ALIGN_CENTER, 0);

      // Center character is highlighted
      if (i == VISIBLE_CHARS / 2) {
        lv_obj_set_style_text_color(g_gallery_labels[i], lv_color_make(255, 200, 0), 0);
      } else {
        lv_obj_set_style_text_color(g_gallery_labels[i], lv_color_make(100, 100, 100), 0);
      }
    }

    // Selection box around center character
    g_selection_box = lv_obj_create(g_screen);
    lv_obj_set_size(g_selection_box, char_width + 4, 40);
    lv_obj_align_to(g_selection_box, g_gallery_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(g_selection_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(g_selection_box, lv_color_make(255, 200, 0), 0);
    lv_obj_set_style_border_width(g_selection_box, 2, 0);
    lv_obj_set_style_radius(g_selection_box, 4, 0);

    update_gallery_display();

    ESP_LOGI(TAG, "Name edit screen created");
  } else {
    // Screen already exists, just update content
    lv_textarea_set_text(g_textarea, g_edit_buffer);
    lv_textarea_set_cursor_pos(g_textarea, LV_TEXTAREA_CURSOR_LAST);
    update_gallery_display();
  }

  lv_screen_load(g_screen);
  lv_timer_delete(timer);
  g_active = true;
}

UI_CREATE_DEFERRED_DRAW_FUNC(text_edit, text_edit_draw_deferred_cb)

static void text_edit_teardown(void) {
  g_active = false;

  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_title_label = NULL;
    g_textarea = NULL;
    g_gallery_container = NULL;
    for (int i = 0; i < VISIBLE_CHARS; i++) {
      g_gallery_labels[i] = NULL;
    }
    g_selection_box = NULL;
  }
  
  // NOTE: Do NOT clear g_is_scene_name_mode or g_config here!
  // When re-entering the same module, the caller sets these BEFORE ui_set_draw_module,
  // but teardown runs AFTER that setup. Clearing here would wipe the new configuration.
  g_exit_pending = false;
  ESP_LOGD(TAG, "Text edit module teardown");
}

static void text_edit_init(void) {
  g_gallery_index = 1;  // Start at 'A' (space is at index 0)
  g_active = false;
  g_exit_pending = false;
  // NOTE: Do NOT reset g_is_scene_name_mode or g_config here - they're set by the
  // caller (name_edit_set_name or text_edit_start) BEFORE ui_set_draw_module is called
  ESP_LOGI(TAG, "Text edit module initialized");
}

// Generic text edit start function
void text_edit_start(const text_edit_config_t* config) {
  if (!config) return;
  
  // Copy configuration
  g_config = *config;
  g_is_scene_name_mode = false;
  
  // Set max length (clamp to TEXT_EDIT_MAX_LEN)
  g_max_length = config->max_length;
  if (g_max_length == 0 || g_max_length > TEXT_EDIT_MAX_LEN) {
    g_max_length = TEXT_EDIT_MAX_LEN;
  }
  
  // Copy initial text
  if (config->initial_text) {
    strncpy(g_edit_buffer, config->initial_text, g_max_length);
    g_edit_buffer[g_max_length] = '\0';
  } else {
    g_edit_buffer[0] = '\0';
  }
  
  // Reset gallery to 'A' (index 1, since space is at index 0)
  g_gallery_index = 1;
  
  // Activate the module
  ui_set_draw_module(&text_edit_module);
}

// Scene name editing convenience wrapper
void name_edit_set_name(const char* name, uint8_t scene_index) {
  g_scene_index = scene_index;
  g_is_scene_name_mode = true;
  g_max_length = SCENE_NAME_MAX_LEN;
  
  // Set up config for scene name mode
  g_config.title = "Edit Name";
  g_config.max_length = SCENE_NAME_MAX_LEN;
  g_config.on_confirm = NULL;  // We handle this specially
  g_config.on_cancel = NULL;
  g_config.user_data = NULL;

  if (name) {
    strncpy(g_edit_buffer, name, SCENE_NAME_MAX_LEN);
  } else {
    g_edit_buffer[0] = '\0';
  }
  g_edit_buffer[SCENE_NAME_MAX_LEN] = '\0';

  // Reset gallery to 'A' (index 1, since space is at index 0)
  g_gallery_index = 1;
}

static void update_gallery_display(void) {
  if (!g_gallery_labels[0]) return;

  int center = VISIBLE_CHARS / 2;

  for (int i = 0; i < VISIBLE_CHARS; i++) {
    int offset = i - center;
    int char_idx = g_gallery_index + offset;

    // Wrap around
    while (char_idx < 0) char_idx += CHARSET_LEN;
    while (char_idx >= (int)CHARSET_LEN) char_idx -= CHARSET_LEN;

    char buf[2] = { CHARSET[char_idx], '\0' };
    lv_label_set_text(g_gallery_labels[i], buf);
  }
}

static void insert_selected_char(void) {
  if (!g_textarea) return;

  // Check if we're at max length
  const char* current = lv_textarea_get_text(g_textarea);
  if (strlen(current) >= g_max_length) {
    ESP_LOGD(TAG, "Text at max length, cannot insert");
    return;
  }

  char c = CHARSET[g_gallery_index];
  lv_textarea_add_char(g_textarea, c);

  // Update buffer
  strncpy(g_edit_buffer, lv_textarea_get_text(g_textarea), g_max_length);
  g_edit_buffer[g_max_length] = '\0';
}

static void post_haptic_click(void) {
  event_t evt = {
    .type = EVENT_HAPTIC_REQUEST,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.haptic = { .pattern = HAPTIC_CLICK }
  };
  event_bus_post(&evt);
}

static void confirm_and_exit(void) {
  const char* final_text = g_textarea ? lv_textarea_get_text(g_textarea) : g_edit_buffer;
  
  if (g_is_scene_name_mode) {
    // Scene name mode: save to scene
    scene_set_name(g_scene_index, final_text);
    ESP_LOGI(TAG, "Saved scene name: %s", final_text);
  } else if (g_config.on_confirm) {
    // Generic mode: call confirm callback
    g_config.on_confirm(final_text, g_config.user_data);
  }
}

static void cancel_edit(void) {
  if (!g_is_scene_name_mode && g_config.on_cancel) {
    g_config.on_cancel(g_config.user_data);
  }
  ESP_LOGI(TAG, "Edit cancelled");
}

// Timer callback to return to menu (avoids LVGL render cycle issues)
static void return_to_menu_timer_cb(lv_timer_t* timer) {
  if (g_is_scene_name_mode) {
    // Scene name mode: return to Scene Name submenu (shows updated name)
    menu_navigate_back_then_to(1, "Scene Name", menu_page_scene_name_create);
  }
  // For generic mode, the callbacks handle navigation
  
  // Clear exit pending AFTER navigation to ensure any late release events are consumed
  g_exit_pending = false;
  lv_timer_delete(timer);
}

bool text_edit_handle_pad(uint8_t pad_id, bool pressed) {
  // Consume release events while exit is pending (prevents menu activation)
  if (g_exit_pending && !pressed) {
    return true;
  }
  
  if (!g_active) return false;

  // Only handle press events
  if (!pressed) return true;

  switch (pad_id) {
    case 8:  // Accept - confirm and exit
      g_active = false;  // Deactivate immediately to prevent duplicate handling
      g_exit_pending = true;  // Consume subsequent release events
      confirm_and_exit();
      post_haptic_click();
      lv_timer_create(return_to_menu_timer_cb, 100, NULL);
      return true;

    case 9:  // Insert selected character at cursor
      insert_selected_char();
      post_haptic_click();
      return true;

    case 10:  // Cursor forward (right), wrap at end
      if (g_textarea) {
        uint32_t pos = lv_textarea_get_cursor_pos(g_textarea);
        const char* text = lv_textarea_get_text(g_textarea);
        uint32_t len = strlen(text);
        if (pos >= len) {
          lv_textarea_set_cursor_pos(g_textarea, 0);
        } else {
          lv_textarea_cursor_right(g_textarea);
        }
        post_haptic_click();
      }
      return true;

    case 11:  // Backspace - delete character before cursor
      if (g_textarea) {
        lv_textarea_delete_char(g_textarea);
        strncpy(g_edit_buffer, lv_textarea_get_text(g_textarea), g_max_length);
        g_edit_buffer[g_max_length] = '\0';
        post_haptic_click();
      }
      return true;

    case 12:  // Cancel - don't save, just exit
      g_active = false;  // Deactivate immediately to prevent duplicate handling
      g_exit_pending = true;  // Consume subsequent release events
      cancel_edit();
      post_haptic_click();
      lv_timer_create(return_to_menu_timer_cb, 100, NULL);
      return true;

    default:
      // Wheel pads 0-7: consume but don't act (wheel scrolls gallery)
      if (pad_id < 8) {
        return true;  // Consume event but do nothing
      }
      return false;
  }
}

// Backward compatibility wrapper
bool name_edit_handle_pad(uint8_t pad_id, bool pressed) {
  return text_edit_handle_pad(pad_id, pressed);
}

// Async callback to update gallery (runs in LVGL task context)
static void update_gallery_async(void* user_data) {
  (void)user_data;
  if (g_active) {
    update_gallery_display();
  }
}

void text_edit_handle_wheel(int delta) {
  if (!g_active) return;

  g_gallery_index += delta;

  // Wrap around
  while (g_gallery_index < 0) g_gallery_index += CHARSET_LEN;
  while (g_gallery_index >= (int)CHARSET_LEN) g_gallery_index -= CHARSET_LEN;

  // Defer update to LVGL task context to avoid rendering conflicts
  lv_async_call(update_gallery_async, NULL);
}

// Backward compatibility wrapper
void name_edit_handle_wheel(int delta) {
  text_edit_handle_wheel(delta);
}

bool text_edit_is_active(void) {
  return g_active;
}

bool text_edit_exit_pending(void) {
  return g_exit_pending;
}

// Backward compatibility wrapper
bool name_edit_is_active(void) {
  return text_edit_is_active();
}

// The module can be accessed as either text_edit_module or name_edit_module
ui_draw_module_t text_edit_module = {
  .draw_func = text_edit_draw,
  .teardown_func = text_edit_teardown,
  .init_func = text_edit_init,
  .name = "text_edit"
};

// Alias for backward compatibility
ui_draw_module_t name_edit_module = {
  .draw_func = text_edit_draw,
  .teardown_func = text_edit_teardown,
  .init_func = text_edit_init,
  .name = "name_edit"
};
