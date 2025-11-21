#include "menu.h"
#include "ui.h"
#include "esp_log.h"
#include <string.h>

#define TAG "MENU"

// Menu navigation stack entry
typedef struct {
  lv_obj_t* screen;
  lv_obj_t* list;
  const char* name;
  menu_page_builder_t builder;
} menu_stack_entry_t;

// Deferred navigation context
typedef struct {
  const char* menu_name;
  menu_page_builder_t builder;
  bool is_back;
} deferred_nav_t;

// Menu state
static struct {
  bool initialized;
  menu_stack_entry_t stack[MAX_MENU_STACK];
  int stack_depth;
  lv_group_t* group;
  lv_indev_t* encoder_indev;
  deferred_nav_t pending_nav;
  bool has_pending_nav;
} menu_state = {
  .initialized = false,
  .stack_depth = 0,
  .group = NULL,
  .encoder_indev = NULL,
  .has_pending_nav = false
};

// Forward declarations
static void menu_item_event_cb(lv_event_t* e);
static void update_top_level_flag(void);
static lv_obj_t* find_list_in_screen(lv_obj_t* screen);
static void deferred_nav_timer_cb(lv_timer_t* timer);
static void menu_navigate_to_internal(const char* menu_name, menu_page_builder_t builder);
static void menu_navigate_back_internal(void);

void menu_init(void) {
  if (menu_state.initialized) {
    ESP_LOGD(TAG, "Menu already initialized, reusing existing state");
    return;
  }

  memset(&menu_state, 0, sizeof(menu_state));
  menu_state.initialized = true;
  
  // Create group for encoder navigation
  menu_state.group = lv_group_create();
  lv_group_set_wrap(menu_state.group, false);
  
  ESP_LOGI(TAG, "Menu system initialized");
}

static lv_obj_t* find_list_in_screen(lv_obj_t* screen) {
  if (!screen) return NULL;
  
  uint32_t child_cnt = lv_obj_get_child_count(screen);
  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t* child = lv_obj_get_child(screen, i);
    if (child && lv_obj_has_class(child, &lv_list_class)) return child;
  }
  return NULL;
}

lv_obj_t* menu_create_page(const char* title, const menu_item_t* items, int item_count) {
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, 128, 128);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  // Create title label (small, centered at top)
  lv_obj_t* title_label = lv_label_create(screen);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_10, 0);  // Small header
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 4);  // Position from top

  // Create list widget - positioned below title
  lv_obj_t* list = lv_list_create(screen);
  lv_obj_set_size(list, 100, 114);  // Size to fit remaining space
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 14);  // Position below title
  
  // Minimal styling - transparent, no padding, no borders
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 2, 0);  // Small gap between items
  lv_obj_set_style_text_color(list, lv_color_white(), 0);

  // Add menu items
  for (int i = 0; i < item_count && i < MAX_MENU_ITEMS; i++) {
    lv_obj_t* btn = lv_list_add_button(list, NULL, items[i].label);  // No icon, just text
    
    // Minimal button styling - white text on black, no background
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);  // Medium size for items
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_left(btn, 2, 0);
    lv_obj_set_style_pad_right(btn, 2, 0);
    lv_obj_set_style_pad_top(btn, (i == 0) ? 8 : 2, 0);
    lv_obj_set_style_pad_bottom(btn, 0, 0);
    // lv_obj_set_style_pad_all(btn, (i == 0) ? 10 : 2, 0);  // No padding on first item, minimal on rest
    lv_obj_set_style_text_align(btn, LV_TEXT_ALIGN_CENTER, 0);  // Center text
    
    // Focused state - just slightly brighter text, no background
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_10, LV_STATE_FOCUSED);  // Barely visible background
    
    // Store item index in user data
    lv_obj_set_user_data(btn, (void*)(intptr_t)i);
    
    // Add event callback
    lv_obj_add_event_cb(btn, menu_item_event_cb, LV_EVENT_CLICKED, (void*)&items[i]);
    
    // Add button to group for encoder navigation
    if (menu_state.group) lv_group_add_obj(menu_state.group, btn);
  }

  // Focus first item
  if (menu_state.group && item_count > 0) {
    lv_obj_t* first_btn = lv_obj_get_child(list, 0);
    if (first_btn) lv_group_focus_obj(first_btn);
  }

  return screen;
}

static void menu_item_event_cb(lv_event_t* e) {
  LV_UNUSED(lv_event_get_target(e));
  menu_item_t* item = (menu_item_t*)lv_event_get_user_data(e);
  
  if (!item) return;

  ESP_LOGD(TAG, "Menu item selected: %s", item->label);

  if (item->has_submenu && item->callback) {
    // Callback for submenu navigation
    item->callback();
  } else if (item->callback) {
    // Execute action callback
    item->callback();
  }
}

void menu_create(void) {
  if (!menu_state.initialized) menu_init();

  // Clear any existing stack
  menu_state.stack_depth = 0;

  // Clear group of any existing objects
  if (menu_state.group) lv_group_remove_all_objs(menu_state.group);

  // Create top-level menu by importing the index page
  extern lv_obj_t* menu_page_index_create(void);
  lv_obj_t* screen = menu_page_index_create();

  // Find the list widget
  lv_obj_t* list = find_list_in_screen(screen);

  // Push to stack
  menu_state.stack[0].screen = screen;
  menu_state.stack[0].list = list;
  menu_state.stack[0].name = "Menu";
  menu_state.stack[0].builder = NULL;  // Top level has no builder
  menu_state.stack_depth = 1;

  // Load screen
  lv_screen_load(screen);
  update_top_level_flag();

  ESP_LOGI(TAG, "Top-level menu created");
}

// Internal navigation function (called from LVGL task context)
static void menu_navigate_to_internal(const char* menu_name, menu_page_builder_t builder) {
  if (menu_state.stack_depth >= MAX_MENU_STACK) {
    ESP_LOGE(TAG, "Menu stack full, cannot navigate");
    return;
  }

  if (!builder) {
    ESP_LOGE(TAG, "NULL builder passed to menu_navigate_to");
    return;
  }

  // Remove current menu items from group
  if (menu_state.group && menu_state.stack_depth > 0) {
    lv_obj_t* current_list = menu_state.stack[menu_state.stack_depth - 1].list;
    if (current_list) {
      uint32_t child_cnt = lv_obj_get_child_cnt(current_list);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(current_list, i);
        if (child && lv_obj_has_class(child, &lv_list_button_class)) {
          lv_group_remove_obj(child);
        }
      }
    }
  }

  // Create new menu screen using builder
  lv_obj_t* screen = builder();
  if (!screen) {
    ESP_LOGE(TAG, "Builder failed to create screen");
    return;
  }

  // Find the list widget
  lv_obj_t* list = find_list_in_screen(screen);

  // Push to stack
  menu_state.stack[menu_state.stack_depth].screen = screen;
  menu_state.stack[menu_state.stack_depth].list = list;
  menu_state.stack[menu_state.stack_depth].name = menu_name;
  menu_state.stack[menu_state.stack_depth].builder = builder;
  menu_state.stack_depth++;

  // Load screen
  lv_screen_load(screen);
  update_top_level_flag();

  ESP_LOGD(TAG, "Navigated to menu: %s (depth: %d)", menu_name, menu_state.stack_depth);
}

// Deferred navigation timer callback (runs in LVGL task context)
static void deferred_nav_timer_cb(lv_timer_t* timer) {
  lv_timer_del(timer);
  
  if (menu_state.has_pending_nav) {
    if (menu_state.pending_nav.is_back) {
      menu_navigate_back_internal();
    } else {
      menu_navigate_to_internal(menu_state.pending_nav.menu_name, 
                               menu_state.pending_nav.builder);
    }
    menu_state.has_pending_nav = false;
  }
}

void menu_navigate_to(const char* menu_name, menu_page_builder_t builder) {
  // Defer to LVGL task context to avoid stack overflow
  if (menu_state.has_pending_nav) {
    ESP_LOGW(TAG, "Navigation already pending, dropping request");
    return;
  }
  
  menu_state.pending_nav.menu_name = menu_name;
  menu_state.pending_nav.builder = builder;
  menu_state.pending_nav.is_back = false;
  menu_state.has_pending_nav = true;
  
  // Create a one-shot timer to execute navigation in LVGL task context
  lv_timer_t* nav_timer = lv_timer_create(deferred_nav_timer_cb, 0, NULL);
  if (!nav_timer) {
    ESP_LOGE(TAG, "Failed to create navigation timer");
    menu_state.has_pending_nav = false;
    return;
  }
  lv_timer_set_repeat_count(nav_timer, 1);
}

// Internal back navigation function (called from LVGL task context)
static void menu_navigate_back_internal(void) {
  if (menu_state.stack_depth <= 1) {
    // At top level, exit Programming mode
    ESP_LOGI(TAG, "At top level, exiting Programming mode");
    ui_set_app_mode(APP_MODE_PERFORMANCE);
    return;
  }

  // Remove current menu items from group
  if (menu_state.group && menu_state.stack_depth > 0) {
    lv_obj_t* current_list = menu_state.stack[menu_state.stack_depth - 1].list;
    if (current_list) {
      uint32_t child_cnt = lv_obj_get_child_cnt(current_list);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(current_list, i);
        if (child && lv_obj_has_class(child, &lv_list_button_class)) {
          lv_group_remove_obj(child);
        }
      }
    }
  }

  // CRITICAL: Load previous screen FIRST before deleting current
  // This prevents the "active screen was deleted" crash
  lv_obj_t* prev_screen = menu_state.stack[menu_state.stack_depth - 2].screen;
  lv_scr_load(prev_screen);

  // NOW it's safe to delete current screen (not active anymore)
  menu_state.stack_depth--;
  if (menu_state.stack[menu_state.stack_depth].screen) {
    lv_obj_delete(menu_state.stack[menu_state.stack_depth].screen);
    menu_state.stack[menu_state.stack_depth].screen = NULL;
    menu_state.stack[menu_state.stack_depth].list = NULL;
  }

  // Restore previous menu items to group
  if (menu_state.group && menu_state.stack_depth > 0) {
    lv_obj_t* prev_list = menu_state.stack[menu_state.stack_depth - 1].list;
    if (prev_list) {
      uint32_t child_cnt = lv_obj_get_child_cnt(prev_list);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(prev_list, i);
        if (child && lv_obj_has_class(child, &lv_list_button_class)) {
          lv_group_add_obj(menu_state.group, child);
        }
      }
      // Focus first item
      if (child_cnt > 0) {
        lv_obj_t* first_child = lv_obj_get_child(prev_list, 0);
        if (first_child) lv_group_focus_obj(first_child);
      }
    }
  }

  update_top_level_flag();

  ESP_LOGD(TAG, "Navigated back (depth: %d)", menu_state.stack_depth);
}

void menu_navigate_back(void) {
  // Defer to LVGL task context to avoid stack overflow
  if (menu_state.has_pending_nav) {
    ESP_LOGW(TAG, "Navigation already pending, dropping back request");
    return;
  }
  
  menu_state.pending_nav.is_back = true;
  menu_state.pending_nav.menu_name = NULL;
  menu_state.pending_nav.builder = NULL;
  menu_state.has_pending_nav = true;
  
  // Create a one-shot timer to execute navigation in LVGL task context
  lv_timer_t* nav_timer = lv_timer_create(deferred_nav_timer_cb, 0, NULL);
  if (!nav_timer) {
    ESP_LOGE(TAG, "Failed to create back navigation timer");
    menu_state.has_pending_nav = false;
    return;
  }
  lv_timer_set_repeat_count(nav_timer, 1);
}

void menu_handle_enter(void) {
  if (menu_state.stack_depth == 0 || !menu_state.group) return;

  // Get focused object (selected item)
  lv_obj_t* focused = lv_group_get_focused(menu_state.group);
  if (!focused) {
    // If no focused object, focus first item
    lv_obj_t* list = menu_state.stack[menu_state.stack_depth - 1].list;
    if (list) {
      lv_obj_t* first_child = lv_obj_get_child(list, 0);
      if (first_child) {
        lv_group_focus_obj(first_child);
        focused = first_child;
      }
    }
  }

  // If focused object is a button, trigger click event
  if (focused && lv_obj_has_class(focused, &lv_list_button_class)) {
    lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
  }
}

void menu_handle_back(void) {
  menu_navigate_back();
}

void menu_cleanup(void) {
  // Delete all screens in stack
  for (int i = 0; i < menu_state.stack_depth; i++) {
    if (menu_state.stack[i].screen) {
      lv_obj_delete(menu_state.stack[i].screen);
      menu_state.stack[i].screen = NULL;
      menu_state.stack[i].list = NULL;
    }
  }

  menu_state.stack_depth = 0;

  // Cleanup group
  if (menu_state.group) lv_group_remove_all_objs(menu_state.group);

  ESP_LOGI(TAG, "Menu cleaned up");
}

bool menu_is_top_level(void) {
  return (menu_state.stack_depth == 1);
}

lv_group_t* menu_get_group(void) {
  return menu_state.group;
}

lv_obj_t* menu_get_current_screen(void) {
  if (menu_state.stack_depth > 0 && menu_state.stack[menu_state.stack_depth - 1].screen) {
    return menu_state.stack[menu_state.stack_depth - 1].screen;
  }
  return NULL;
}

static void update_top_level_flag(void) {
  bool is_top_level = (menu_state.stack_depth == 1);
  ui_set_programming_top_level(is_top_level);
}

// Info page builder context
typedef struct {
  char title[64];
  char info_text[512];
} info_page_context_t;

// Static context for info pages (single instance at a time)
static info_page_context_t s_info_context;

// Builder function for info pages
static lv_obj_t* info_page_builder(void) {
  return menu_create_info_page(s_info_context.title, s_info_context.info_text);
}

// Helper: Navigate to an info page (wrapper for menu_navigate_to)
void menu_navigate_to_info(const char* title, const char* info_text) {
  // Copy strings to context
  strncpy(s_info_context.title, title, sizeof(s_info_context.title) - 1);
  s_info_context.title[sizeof(s_info_context.title) - 1] = '\0';
  strncpy(s_info_context.info_text, info_text, sizeof(s_info_context.info_text) - 1);
  s_info_context.info_text[sizeof(s_info_context.info_text) - 1] = '\0';
  
  menu_navigate_to(s_info_context.title, info_page_builder);
}

// Helper: Create a scrollable info page with formatted text
lv_obj_t* menu_create_info_page(const char* title, const char* info_text) {
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, 128, 128);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  // Create title label
  lv_obj_t* title_label = lv_label_create(screen);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_10, 0);
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 4);

  // Create scrollable textarea for info
  lv_obj_t* textarea = lv_textarea_create(screen);
  lv_obj_set_size(textarea, 120, 110);
  lv_obj_align(textarea, LV_ALIGN_TOP_MID, 0, 18);
  lv_textarea_set_text(textarea, info_text);
  lv_textarea_set_placeholder_text(textarea, "");
  lv_obj_set_style_text_color(textarea, lv_color_white(), 0);
  lv_obj_set_style_text_font(textarea, &lv_font_montserrat_10, 0);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(textarea, 0, 0);
  lv_obj_set_scroll_dir(textarea, LV_DIR_VER);
  lv_textarea_set_cursor_click_pos(textarea, false);

  // Add to group for scrolling
  if (menu_state.group) lv_group_add_obj(menu_state.group, textarea);

  return screen;
}

// Helper: Create a page with action buttons
lv_obj_t* menu_create_action_page(const char* title, const menu_item_t* items, int item_count) {
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, 128, 128);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  // Create title label
  lv_obj_t* title_label = lv_label_create(screen);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_10, 0);
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 4);

  // Create list widget
  lv_obj_t* list = lv_list_create(screen);
  lv_obj_set_size(list, 100, 114);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 14);
  
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 2, 0);
  lv_obj_set_style_text_color(list, lv_color_white(), 0);

  // Add action buttons
  for (int i = 0; i < item_count && i < MAX_MENU_ITEMS; i++) {
    lv_obj_t* btn = lv_list_add_button(list, NULL, items[i].label);
    
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_left(btn, 2, 0);
    lv_obj_set_style_pad_right(btn, 2, 0);
    lv_obj_set_style_pad_top(btn, (i == 0) ? 8 : 2, 0);
    lv_obj_set_style_pad_bottom(btn, 0, 0);
    lv_obj_set_style_text_align(btn, LV_TEXT_ALIGN_CENTER, 0);
    
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_10, LV_STATE_FOCUSED);
    
    lv_obj_set_user_data(btn, (void*)(intptr_t)i);
    lv_obj_add_event_cb(btn, menu_item_event_cb, LV_EVENT_CLICKED, (void*)&items[i]);
    
    if (menu_state.group) lv_group_add_obj(menu_state.group, btn);
  }

  if (menu_state.group && item_count > 0) {
    lv_obj_t* first_btn = lv_obj_get_child(list, 0);
    if (first_btn) lv_group_focus_obj(first_btn);
  }

  return screen;
}

