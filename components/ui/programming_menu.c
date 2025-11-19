#include "programming_menu.h"
#include "ui.h"
#include "touchwheel.h"
#include "touchwheel_outputs.h"
#include "esp_log.h"
#include <string.h>

#define TAG "PROG_MENU"
#define MAX_MENU_STACK 8
#define MAX_MENU_ITEMS 16

// Menu navigation stack entry
typedef struct {
  lv_obj_t* screen;
  lv_obj_t* list;
  const char* name;
} menu_stack_entry_t;

// Menu state
static struct {
  bool initialized;
  menu_stack_entry_t stack[MAX_MENU_STACK];
  int stack_depth;
  lv_group_t* group;
  lv_indev_t* encoder_indev;
} menu_state = {
  .initialized = false,
  .stack_depth = 0,
  .group = NULL,
  .encoder_indev = NULL
};

// Forward declarations
static void create_top_level_menu(void);
static void menu_item_event_cb(lv_event_t* e);
static lv_obj_t* create_menu_screen(const char* title, const programming_menu_item_t* items, int item_count);
static void update_top_level_flag(void);

// Top-level menu items
static const programming_menu_item_t top_level_items[] = {
  { "Scenes", NULL, true },
  { "Device Config", NULL, true },
  { "Settings", NULL, true },
  { "About", NULL, true }
};

// Placeholder sub-menu items
static const programming_menu_item_t scenes_submenu_items[] = {
  { "Coming Soon", NULL, false }
};

static const programming_menu_item_t device_config_submenu_items[] = {
  { "Coming Soon", NULL, false }
};

static const programming_menu_item_t settings_submenu_items[] = {
  { "Coming Soon", NULL, false }
};

static const programming_menu_item_t about_submenu_items[] = {
  { "Coming Soon", NULL, false }
};

void programming_menu_init(void) {
  if (menu_state.initialized) {
    ESP_LOGW(TAG, "Programming menu already initialized");
    return;
  }

  memset(&menu_state, 0, sizeof(menu_state));
  menu_state.initialized = true;
  
  ESP_LOGI(TAG, "Programming menu initialized");
}

static lv_obj_t* create_menu_screen(const char* title, const programming_menu_item_t* items, int item_count) {
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
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 4);

  // Create list widget
  lv_obj_t* list = lv_list_create(screen);
  lv_obj_set_size(list, 120, 100);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_bg_color(list, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);

  // Add menu items
  for (int i = 0; i < item_count && i < MAX_MENU_ITEMS; i++) {
    lv_obj_t* btn = lv_list_add_button(list, LV_SYMBOL_FILE, items[i].label);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
    
    // Store item index in user data
    lv_obj_set_user_data(btn, (void*)(intptr_t)i);
    
    // Add event callback
    lv_obj_add_event_cb(btn, menu_item_event_cb, LV_EVENT_CLICKED, (void*)&items[i]);
    
    // Add button to group for encoder navigation
    if (menu_state.group) {
      lv_group_add_obj(menu_state.group, btn);
    }
  }

  // Focus first item
  if (menu_state.group && item_count > 0) {
    lv_obj_t* first_btn = lv_obj_get_child(list, 0);
    if (first_btn) {
      lv_group_focus_obj(first_btn);
    }
  }

  return screen;
}

static void menu_item_event_cb(lv_event_t* e) {
  LV_UNUSED(lv_event_get_target(e));
  programming_menu_item_t* item = (programming_menu_item_t*)lv_event_get_user_data(e);
  
  if (!item) return;

  ESP_LOGI(TAG, "Menu item selected: %s", item->label);

  if (item->has_submenu) {
    // Navigate to sub-menu based on label
    if (strcmp(item->label, "Scenes") == 0) {
      programming_menu_navigate_to("Scenes", scenes_submenu_items, 
                                   sizeof(scenes_submenu_items) / sizeof(scenes_submenu_items[0]));
    } else if (strcmp(item->label, "Device Config") == 0) {
      programming_menu_navigate_to("Device Config", device_config_submenu_items,
                                   sizeof(device_config_submenu_items) / sizeof(device_config_submenu_items[0]));
    } else if (strcmp(item->label, "Settings") == 0) {
      programming_menu_navigate_to("Settings", settings_submenu_items,
                                   sizeof(settings_submenu_items) / sizeof(settings_submenu_items[0]));
    } else if (strcmp(item->label, "About") == 0) {
      programming_menu_navigate_to("About", about_submenu_items,
                                   sizeof(about_submenu_items) / sizeof(about_submenu_items[0]));
    }
  } else if (item->callback) {
    // Execute callback
    item->callback();
  }
}

static void create_top_level_menu(void) {
  // Clear any existing stack
  menu_state.stack_depth = 0;

  // Ensure group exists
  if (!menu_state.group) {
    menu_state.group = lv_group_create();
    lv_group_set_wrap(menu_state.group, false);
  } else {
    // Clear group of any existing objects
    lv_group_remove_all_objs(menu_state.group);
  }

  // Create top-level menu screen
  lv_obj_t* screen = create_menu_screen("Menu", top_level_items,
                                        sizeof(top_level_items) / sizeof(top_level_items[0]));

  // Find the list widget (it's the second child of screen: title_label, then list)
  lv_obj_t* list = NULL;
  lv_obj_t* child = lv_obj_get_child(screen, 0);
  int idx = 0;
  while (child) {
    if (lv_obj_has_class(child, &lv_list_class)) {
      list = child;
      break;
    }
    idx++;
    child = lv_obj_get_child(screen, idx);
  }

  // Push to stack
  menu_state.stack[0].screen = screen;
  menu_state.stack[0].list = list;
  menu_state.stack[0].name = "Menu";
  menu_state.stack_depth = 1;

  // Load screen
  lv_scr_load(screen);
  update_top_level_flag();

  ESP_LOGI(TAG, "Top-level menu created");
}

static void update_top_level_flag(void) {
  bool is_top_level = (menu_state.stack_depth == 1);
  ui_set_programming_top_level(is_top_level);
}

void programming_menu_create(void) {
  if (!menu_state.initialized) {
    programming_menu_init();
  }

  create_top_level_menu();
}

void programming_menu_navigate_to(const char* menu_name, const programming_menu_item_t* items, int item_count) {
  if (menu_state.stack_depth >= MAX_MENU_STACK) {
    ESP_LOGE(TAG, "Menu stack full, cannot navigate");
    return;
  }

  // Remove current menu items from group
  if (menu_state.group && menu_state.stack_depth > 0) {
    lv_obj_t* current_list = menu_state.stack[menu_state.stack_depth - 1].list;
    if (current_list) {
      // Remove all buttons from current list
      uint32_t child_cnt = lv_obj_get_child_cnt(current_list);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(current_list, i);
        if (child && lv_obj_has_class(child, &lv_list_button_class)) {
          lv_group_remove_obj(child);
        }
      }
    }
  }

  // Create new menu screen
  lv_obj_t* screen = create_menu_screen(menu_name, items, item_count);

  // Find the list widget
  lv_obj_t* list = NULL;
  lv_obj_t* child = lv_obj_get_child(screen, 0);
  int idx = 0;
  while (child) {
    if (lv_obj_has_class(child, &lv_list_class)) {
      list = child;
      break;
    }
    idx++;
    child = lv_obj_get_child(screen, idx);
  }

  // Push to stack
  menu_state.stack[menu_state.stack_depth].screen = screen;
  menu_state.stack[menu_state.stack_depth].list = list;
  menu_state.stack[menu_state.stack_depth].name = menu_name;
  menu_state.stack_depth++;

  // Load screen
  lv_scr_load(screen);
  update_top_level_flag();

  ESP_LOGI(TAG, "Navigated to menu: %s (depth: %d)", menu_name, menu_state.stack_depth);
}

void programming_menu_navigate_back(void) {
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
      // Remove all buttons from current list
      uint32_t child_cnt = lv_obj_get_child_cnt(current_list);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(current_list, i);
        if (child && lv_obj_has_class(child, &lv_list_button_class)) {
          lv_group_remove_obj(child);
        }
      }
    }
  }

  // Pop current screen from stack
  menu_state.stack_depth--;
  lv_obj_t* prev_screen = menu_state.stack[menu_state.stack_depth - 1].screen;

  // Delete current screen
  if (menu_state.stack[menu_state.stack_depth].screen) {
    lv_obj_del(menu_state.stack[menu_state.stack_depth].screen);
    menu_state.stack[menu_state.stack_depth].screen = NULL;
    menu_state.stack[menu_state.stack_depth].list = NULL;
  }

  // Restore previous menu items to group
  if (menu_state.group && menu_state.stack_depth > 0) {
    lv_obj_t* prev_list = menu_state.stack[menu_state.stack_depth - 1].list;
    if (prev_list) {
      // Add all buttons from previous list
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
        if (first_child) {
          lv_group_focus_obj(first_child);
        }
      }
    }
  }

  // Load previous screen
  lv_scr_load(prev_screen);
  update_top_level_flag();

  ESP_LOGI(TAG, "Navigated back (depth: %d)", menu_state.stack_depth);
}

void programming_menu_handle_enter(void) {
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

void programming_menu_handle_back(void) {
  programming_menu_navigate_back();
}

void programming_menu_cleanup(void) {
  // Delete all screens in stack
  for (int i = 0; i < menu_state.stack_depth; i++) {
    if (menu_state.stack[i].screen) {
      lv_obj_del(menu_state.stack[i].screen);
      menu_state.stack[i].screen = NULL;
      menu_state.stack[i].list = NULL;
    }
  }

  menu_state.stack_depth = 0;

  // Cleanup group (but don't delete, we'll reuse it)
  if (menu_state.group) {
    lv_group_remove_all_objs(menu_state.group);
  }

  ESP_LOGI(TAG, "Programming menu cleaned up");
}

bool programming_menu_is_top_level(void) {
  return (menu_state.stack_depth == 1);
}

// Get menu group for encoder attachment
lv_group_t* programming_menu_get_group(void) {
  return menu_state.group;
}

