#include "menu.h"
#include "ui.h"
#include "display_driver.h"
#include "esp_log.h"
#include <string.h>

#define TAG "MENU"

// Menu navigation stack entry
typedef struct {
  lv_obj_t* screen;
  lv_obj_t* container;  // Scroll container (was list)
  const char* name;
  menu_page_builder_t builder;
  int32_t focused_index;  // Remember position when navigating away
} menu_stack_entry_t;

// Deferred navigation context
typedef struct {
  const char* menu_name;
  menu_page_builder_t builder;
  bool is_back;
  int back_levels;  // For multi-level back navigation
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
  bool skip_focus_scroll;  // Skip scroll in focus_event_cb during restore
} menu_state = {
  .initialized = false,
  .stack_depth = 0,
  .group = NULL,
  .encoder_indev = NULL,
  .has_pending_nav = false,
  .skip_focus_scroll = false
};

// Forward declarations
static void menu_item_event_cb(lv_event_t* e);
static void update_top_level_flag(void);
static lv_obj_t* find_container_in_screen(lv_obj_t* screen);
static void deferred_nav_timer_cb(lv_timer_t* timer);
static void menu_navigate_to_internal(const char* menu_name, menu_page_builder_t builder);
static void menu_navigate_back_internal(void);
static void scroll_event_cb(lv_event_t* e);
static void focus_event_cb(lv_event_t* e);
static void update_scroll_visuals(lv_obj_t* cont);
static void save_focused_index(void);

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

// Find the scroll container in a screen (first scrollable child)
static lv_obj_t* find_container_in_screen(lv_obj_t* screen) {
  if (!screen) return NULL;
  
  uint32_t child_cnt = lv_obj_get_child_count(screen);
  ESP_LOGD(TAG, "find_container: screen=%p has %u children", (void*)screen, (unsigned)child_cnt);
  
  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t* child = lv_obj_get_child(screen, i);
    if (!child) continue;
    bool scrollable = lv_obj_has_flag(child, LV_OBJ_FLAG_SCROLLABLE);
    uint32_t child_children = lv_obj_get_child_count(child);
    ESP_LOGD(TAG, "  child[%u]=%p scrollable=%d children=%u", 
             (unsigned)i, (void*)child, scrollable, (unsigned)child_children);
    if (scrollable) return child;
  }
  return NULL;
}

// Save the currently focused item index before navigating away
static void save_focused_index(void) {
  if (menu_state.stack_depth <= 0) return;
  
  menu_stack_entry_t* entry = &menu_state.stack[menu_state.stack_depth - 1];
  if (!entry->container) return;
  
  lv_obj_t* focused = lv_group_get_focused(menu_state.group);
  if (!focused) return;
  
  // Find the index of the focused object
  uint32_t child_cnt = lv_obj_get_child_count(entry->container);
  for (uint32_t i = 0; i < child_cnt; i++) {
    if (lv_obj_get_child(entry->container, i) == focused) {
      entry->focused_index = (int32_t)i;
      ESP_LOGD(TAG, "Saved focused index: %ld", (long)entry->focused_index);
      return;
    }
  }
}

// Update visual styling for scroll container (translation, opacity, font size)
static void update_scroll_visuals(lv_obj_t* cont) {
  if (!cont) return;

  lv_area_t cont_a;
  lv_obj_get_coords(cont, &cont_a);
  int32_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;
  int32_t r = lv_obj_get_height(cont) * 7 / 10;

  int32_t child_cnt = (int32_t)lv_obj_get_child_count(cont);

  // First pass: find the active item (closest to center)
  int32_t active_idx = 0;
  int32_t min_dist = INT32_MAX;

  for (int32_t i = 0; i < child_cnt; i++) {
    lv_obj_t* child = lv_obj_get_child(cont, i);
    lv_area_t child_a;
    lv_obj_get_coords(child, &child_a);
    int32_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;
    int32_t dist = LV_ABS(child_y_center - cont_y_center);

    if (dist < min_dist) {
      min_dist = dist;
      active_idx = i;
    }
  }

  // Second pass: apply styling
  for (int32_t i = 0; i < child_cnt; i++) {
    lv_obj_t* child = lv_obj_get_child(cont, i);
    lv_area_t child_a;
    lv_obj_get_coords(child, &child_a);

    int32_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;
    int32_t diff_y = LV_ABS(child_y_center - cont_y_center);

    // Calculate x translation for circular effect
    int32_t x;
    if (diff_y >= r) {
      x = r;
    } else {
      uint32_t x_sqr = r * r - diff_y * diff_y;
      lv_sqrt_res_t res;
      lv_sqrt(x_sqr, &res, 0x8000);
      x = r - res.i;
    }
    lv_obj_set_style_translate_x(child, x, 0);

    // Set opacity based on index distance from active item
    int32_t idx_dist = LV_ABS(i - active_idx);
    lv_opa_t opa;
    switch (idx_dist) {
      case 0: opa = LV_OPA_COVER; break;            // 100%
      case 1: opa = (LV_OPA_COVER * 3) / 4; break;  // 75%
      case 2: opa = LV_OPA_50; break;               // 50%
      default: opa = LV_OPA_COVER / 4; break;       // 25%
    }
    lv_obj_set_style_opa(child, opa, 0);

    // Active item gets larger font
    if (i == active_idx) {
      lv_obj_set_style_text_font(child, &lv_font_montserrat_20, 0);
    } else {
      lv_obj_set_style_text_font(child, &lv_font_montserrat_14, 0);
    }
  }
}

// Scroll event callback - update visuals
static void scroll_event_cb(lv_event_t* e) {
  if (menu_state.skip_focus_scroll) return;
  
  lv_obj_t* cont = lv_event_get_target_obj(e);
  if (!cont) return;

  update_scroll_visuals(cont);
}

// Focus handler - scroll focused item to center
static void focus_event_cb(lv_event_t* e) {
  if (menu_state.skip_focus_scroll) return;
  
  lv_obj_t* obj = lv_event_get_target_obj(e);
  lv_obj_t* parent = lv_obj_get_parent(obj);
  if (parent) {
    lv_obj_scroll_to_view(obj, LV_ANIM_ON);
  }
}

lv_obj_t* menu_create_page(const char* title, const menu_item_t* items, int item_count) {
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();
  
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  // Title bar height
  const int title_bar_h = 22;
  
  // Create title bar container with woody brown gradient
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(title_bar, lv_color_make(101, 67, 33), 0);  // Dark woody brown
  lv_obj_set_style_bg_grad_color(title_bar, lv_color_make(139, 90, 43), 0);  // Lighter brown
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(title_bar, 0, 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  
  // Create title label inside title bar
  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_make(255, 248, 220), 0);  // Cornsilk/cream
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_center(title_label);
  lv_obj_remove_flag(title_label, LV_OBJ_FLAG_SCROLLABLE);

  // Create scrollable container with flex layout
  lv_obj_t* cont = lv_obj_create(screen);
  lv_obj_set_size(cont, disp_w, disp_h - title_bar_h);
  lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Container styling - black background
  lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 4, 0);
  lv_obj_set_style_pad_row(cont, 6, 0);

  // Enable flex column layout
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Circular clipping for the curved effect
  lv_obj_set_style_radius(cont, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_clip_corner(cont, true, 0);

  // Scroll settings
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(cont, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

  // Add scroll event callback
  lv_obj_add_event_cb(cont, scroll_event_cb, LV_EVENT_SCROLL, NULL);

  // Create labels for menu items
  for (int i = 0; i < item_count && i < MAX_MENU_ITEMS; i++) {
    const char* item_label = items[i].label;
    bool is_divider = (item_label && strncmp(item_label, "---", 3) == 0);
    bool is_readonly = (items[i].callback == NULL && !is_divider);
    
    if (is_divider) {
      // Create a horizontal line divider
      lv_obj_t* line = lv_obj_create(cont);
      lv_obj_set_size(line, lv_pct(80), 2);
      lv_obj_set_style_bg_color(line, lv_color_make(80, 80, 80), 0);
      lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(line, 0, 0);
      lv_obj_set_style_radius(line, 1, 0);
      lv_obj_set_style_pad_all(line, 0, 0);
      lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
      // Dividers are not focusable or clickable
      continue;
    }
    
    lv_obj_t* label = lv_label_create(cont);
    lv_label_set_text(label, item_label);
    lv_obj_set_width(label, lv_pct(100));

    // Label styling - dimmer color for read-only items
    if (is_readonly) {
      lv_obj_set_style_text_color(label, lv_color_make(160, 160, 160), 0);
    } else {
      lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(label, 4, 0);

    // All items are focusable for encoder navigation
    lv_obj_add_flag(label, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(label, focus_event_cb, LV_EVENT_FOCUSED, NULL);
    
    if (is_readonly) {
      // Read-only items: in group for scrolling, but not clickable
      if (menu_state.group) lv_group_add_obj(menu_state.group, label);
    } else {
      // Clickable items: in group and respond to enter
      lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(label, menu_item_event_cb, LV_EVENT_CLICKED, (void*)&items[i]);
      if (menu_state.group) lv_group_add_obj(menu_state.group, label);
    }
  }

  // Scroll to center on first focusable item
  if (item_count > 0 && menu_state.group) {
    // Find the first clickable child (first focusable item)
    uint32_t child_cnt = lv_obj_get_child_count(cont);
    for (uint32_t i = 0; i < child_cnt; i++) {
      lv_obj_t* child = lv_obj_get_child(cont, i);
      if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
        menu_state.skip_focus_scroll = true;
        lv_obj_scroll_to_view(child, LV_ANIM_OFF);
        lv_group_focus_obj(child);
        menu_state.skip_focus_scroll = false;
        break;
      }
    }
  }

  // Initial visual update
  update_scroll_visuals(cont);

  // Debug: log what we created
  uint32_t created_children = lv_obj_get_child_count(cont);
  ESP_LOGD(TAG, "menu_create_page: title='%s', screen=%p, cont=%p, children=%u",
           title, (void*)screen, (void*)cont, (unsigned)created_children);

  return screen;
}

static void menu_item_event_cb(lv_event_t* e) {
  LV_UNUSED(lv_event_get_target(e));
  menu_item_t* item = (menu_item_t*)lv_event_get_user_data(e);

  if (!item) return;

  ESP_LOGD(TAG, "Menu item selected: %s", item->label);

  if (item->callback) {
    item->callback(item->user_data);
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

  // Find the container widget
  lv_obj_t* container = find_container_in_screen(screen);

  // Push to stack
  menu_state.stack[0].screen = screen;
  menu_state.stack[0].container = container;
  menu_state.stack[0].name = "Menu";
  menu_state.stack[0].builder = NULL;
  menu_state.stack[0].focused_index = 0;
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

  // Save the focused index before navigating away
  save_focused_index();

  // Remove current menu items from group
  if (menu_state.group && menu_state.stack_depth > 0) {
    lv_obj_t* current_cont = menu_state.stack[menu_state.stack_depth - 1].container;
    if (current_cont) {
      uint32_t child_cnt = lv_obj_get_child_count(current_cont);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(current_cont, i);
        if (child) lv_group_remove_obj(child);
      }
    }
  }

  // Create new menu screen using builder
  lv_obj_t* screen = builder();
  if (!screen) {
    ESP_LOGE(TAG, "Builder failed to create screen");
    return;
  }

  // Find the container widget
  lv_obj_t* container = find_container_in_screen(screen);
  
  // Debug: log what we're storing
  uint32_t cont_children = container ? lv_obj_get_child_count(container) : 0;
  ESP_LOGI(TAG, "Nav to %s: screen=%p, container=%p, children=%u", 
           menu_name, (void*)screen, (void*)container, (unsigned)cont_children);

  // Push to stack
  menu_state.stack[menu_state.stack_depth].screen = screen;
  menu_state.stack[menu_state.stack_depth].container = container;
  menu_state.stack[menu_state.stack_depth].name = menu_name;
  menu_state.stack[menu_state.stack_depth].builder = builder;
  menu_state.stack[menu_state.stack_depth].focused_index = 0;
  menu_state.stack_depth++;

  // Load screen
  lv_screen_load(screen);
  update_top_level_flag();

  ESP_LOGI(TAG, "Navigated to menu: %s (depth: %d)", menu_name, menu_state.stack_depth);
}

// Deferred navigation timer callback (runs in LVGL task context)
// Storage for screens pending deletion (to avoid deleting active screen)
#define MAX_PENDING_DELETES 4
static lv_obj_t* s_pending_delete_screens[MAX_PENDING_DELETES];
static int s_pending_delete_count = 0;

// Internal function to pop current page without triggering top-level exit
// Does NOT delete the screen immediately - caller must call menu_flush_pending_deletes()
static void menu_pop_current_page(void) {
  if (menu_state.stack_depth < 1) return;
  
  // Remove current page from group
  if (menu_state.group) {
    lv_obj_t* current_cont = menu_state.stack[menu_state.stack_depth - 1].container;
    if (current_cont) {
      uint32_t child_cnt = lv_obj_get_child_count(current_cont);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(current_cont, i);
        if (child) lv_group_remove_obj(child);
      }
    }
  }
  
  // Queue screen for deletion (don't delete yet - might be active)
  lv_obj_t* screen = menu_state.stack[menu_state.stack_depth - 1].screen;
  if (screen && s_pending_delete_count < MAX_PENDING_DELETES) {
    s_pending_delete_screens[s_pending_delete_count++] = screen;
  }
  
  menu_state.stack[menu_state.stack_depth - 1].screen = NULL;
  menu_state.stack[menu_state.stack_depth - 1].container = NULL;
  menu_state.stack_depth--;
}

// Delete all screens that were queued for deletion
static void menu_flush_pending_deletes(void) {
  for (int i = 0; i < s_pending_delete_count; i++) {
    if (s_pending_delete_screens[i]) {
      lv_obj_delete(s_pending_delete_screens[i]);
      s_pending_delete_screens[i] = NULL;
    }
  }
  s_pending_delete_count = 0;
}

static void deferred_nav_timer_cb(lv_timer_t* timer) {
  lv_timer_delete(timer);

  if (menu_state.has_pending_nav) {
    if (menu_state.pending_nav.is_back) {
      // Handle multi-level back navigation
      int levels = menu_state.pending_nav.back_levels;
      if (levels <= 0) levels = 1;

      bool has_forward = (menu_state.pending_nav.builder != NULL);

      for (int i = 0; i < levels && menu_state.stack_depth > 0; i++) {
        if (has_forward) {
          // If we have a forward nav pending, pop without triggering exit
          // We'll push a new page, so we won't actually be at top level
          menu_pop_current_page();
        } else {
          // Pure back navigation - use normal back which handles exit
          menu_navigate_back_internal();
        }
      }

      // If there's a forward navigation, push the new page ON TOP of where we landed
      // (back_then_to means: go back N levels, then navigate TO a new page)
      if (has_forward) {
        // Flush pending deletes before creating new page
        menu_flush_pending_deletes();
        
        // Push the new page on top of current stack
        menu_navigate_to_internal(menu_state.pending_nav.menu_name,
                                 menu_state.pending_nav.builder);
      }
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

  // Reset editing mode (info pages set this to true for scrolling)
  if (menu_state.group) {
    lv_group_set_editing(menu_state.group, false);
  }

  // Remove current menu items from group
  if (menu_state.group && menu_state.stack_depth > 0) {
    lv_obj_t* current_cont = menu_state.stack[menu_state.stack_depth - 1].container;
    if (current_cont) {
      uint32_t child_cnt = lv_obj_get_child_count(current_cont);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(current_cont, i);
        if (child) lv_group_remove_obj(child);
      }
    }
  }

  // CRITICAL: Load previous screen FIRST before deleting current
  lv_obj_t* prev_screen = menu_state.stack[menu_state.stack_depth - 2].screen;
  lv_screen_load(prev_screen);

  // NOW it's safe to delete current screen (not active anymore)
  menu_state.stack_depth--;
  if (menu_state.stack[menu_state.stack_depth].screen) {
    lv_obj_delete(menu_state.stack[menu_state.stack_depth].screen);
    menu_state.stack[menu_state.stack_depth].screen = NULL;
    menu_state.stack[menu_state.stack_depth].container = NULL;
  }

  // Restore previous menu items to group
  if (menu_state.group && menu_state.stack_depth > 0) {
    menu_stack_entry_t* entry = &menu_state.stack[menu_state.stack_depth - 1];
    lv_obj_t* prev_cont = entry->container;
    
    // Debug: log what we're restoring to
    ESP_LOGD(TAG, "Back nav to %s: container=%p", entry->name ? entry->name : "NULL", (void*)prev_cont);
    
    if (prev_cont) {
      uint32_t child_cnt = lv_obj_get_child_count(prev_cont);
      
      // Debug: log scroll position BEFORE we try to reset
      int32_t scroll_y_before = lv_obj_get_scroll_y(prev_cont);
      ESP_LOGD(TAG, "Back nav: child_cnt=%u, scroll_y_before=%ld", (unsigned)child_cnt, (long)scroll_y_before);
      
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(prev_cont, i);
        if (child) lv_group_add_obj(menu_state.group, child);
      }
      
      // Restore focus to saved position
      int32_t focus_idx = entry->focused_index;
      lv_obj_t* focus_target = NULL;
      
      if (focus_idx >= 0 && focus_idx < (int32_t)child_cnt) {
        focus_target = lv_obj_get_child(prev_cont, focus_idx);
      }
      if (!focus_target && child_cnt > 0) {
        focus_target = lv_obj_get_child(prev_cont, 0);
      }
      
      if (focus_target) {
        menu_state.skip_focus_scroll = true;
        lv_group_focus_obj(focus_target);
        lv_obj_scroll_to_view(focus_target, LV_ANIM_OFF);
        menu_state.skip_focus_scroll = false;
        
        update_scroll_visuals(prev_cont);
        
        ESP_LOGD(TAG, "Restored focus to index: %ld", (long)focus_idx);
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
  menu_state.pending_nav.back_levels = 1;
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

void menu_navigate_back_then_to(int levels, const char* menu_name,
  menu_page_builder_t builder) {
  // Defer to LVGL task context to avoid stack overflow
  if (menu_state.has_pending_nav) {
    ESP_LOGW(TAG, "Navigation already pending, dropping back_then_to request");
    return;
  }

  menu_state.pending_nav.is_back = true;
  menu_state.pending_nav.back_levels = levels;
  menu_state.pending_nav.menu_name = menu_name;
  menu_state.pending_nav.builder = builder;
  menu_state.has_pending_nav = true;

  // Create a one-shot timer to execute navigation in LVGL task context
  lv_timer_t* nav_timer = lv_timer_create(deferred_nav_timer_cb, 0, NULL);
  if (!nav_timer) {
    ESP_LOGE(TAG, "Failed to create back_then_to navigation timer");
    menu_state.has_pending_nav = false;
    return;
  }
  lv_timer_set_repeat_count(nav_timer, 1);
}

void menu_replace_current(const char* menu_name, menu_page_builder_t builder) {
  // Synchronous replacement - for use when already in LVGL context (callbacks)
  // This replaces the current page without going through deferred navigation
  
  if (menu_state.stack_depth < 1 || !builder) {
    ESP_LOGW(TAG, "Cannot replace current: invalid state");
    return;
  }
  
  ESP_LOGI(TAG, "Replacing current page with: %s", menu_name);
  
  // Remove current page from group
  if (menu_state.group) {
    lv_obj_t* current_cont = menu_state.stack[menu_state.stack_depth - 1].container;
    if (current_cont) {
      uint32_t child_cnt = lv_obj_get_child_count(current_cont);
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(current_cont, i);
        if (child) lv_group_remove_obj(child);
      }
    }
  }
  
  // Delete current screen
  lv_obj_t* old_screen = menu_state.stack[menu_state.stack_depth - 1].screen;
  
  // Decrement stack depth (pop current page)
  menu_state.stack_depth--;
  
  // Build new page
  lv_obj_t* new_screen = builder();
  if (!new_screen) {
    ESP_LOGE(TAG, "Failed to create replacement page");
    // Try to restore old screen
    menu_state.stack_depth++;
    lv_screen_load(old_screen);
    return;
  }
  
  // Find container in new screen
  lv_obj_t* new_container = NULL;
  uint32_t child_cnt = lv_obj_get_child_count(new_screen);
  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t* child = lv_obj_get_child(new_screen, i);
    if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_SCROLLABLE)) {
      new_container = child;
      break;
    }
  }
  
  // Push new page onto stack
  menu_state.stack[menu_state.stack_depth].screen = new_screen;
  menu_state.stack[menu_state.stack_depth].container = new_container;
  menu_state.stack[menu_state.stack_depth].name = menu_name;
  menu_state.stack[menu_state.stack_depth].builder = builder;
  menu_state.stack[menu_state.stack_depth].focused_index = 0;
  menu_state.stack_depth++;
  
  // Add new items to group
  if (menu_state.group && new_container) {
    uint32_t new_child_cnt = lv_obj_get_child_count(new_container);
    for (uint32_t i = 0; i < new_child_cnt; i++) {
      lv_obj_t* child = lv_obj_get_child(new_container, i);
      if (child) lv_group_add_obj(menu_state.group, child);
    }
    // Focus first item
    if (new_child_cnt > 0) {
      lv_obj_t* first = lv_obj_get_child(new_container, 0);
      if (first) lv_group_focus_obj(first);
    }
  }
  
  // Load new screen
  lv_screen_load(new_screen);
  
  // Delete old screen after loading new one
  if (old_screen) lv_obj_delete(old_screen);
  
  ESP_LOGI(TAG, "Replaced current page with: %s", menu_name);
}

bool menu_handle_enter(void) {
  if (menu_state.stack_depth == 0 || !menu_state.group) return false;

  // Get focused object (selected item)
  lv_obj_t* focused = lv_group_get_focused(menu_state.group);
  if (!focused) {
    // If no focused object, focus first item
    if (menu_state.stack_depth > 0) {
      lv_obj_t* cont = menu_state.stack[menu_state.stack_depth - 1].container;
      if (cont) {
        lv_obj_t* first_child = lv_obj_get_child(cont, 0);
        if (first_child) {
          lv_group_focus_obj(first_child);
          focused = first_child;
        }
      }
    }
  }

  // Only trigger click if the object is clickable (not a read-only label)
  if (focused && lv_obj_has_flag(focused, LV_OBJ_FLAG_CLICKABLE)) {
    lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
    return true;
  }
  
  return false;
}

// Custom back handler for special pages
static menu_custom_back_cb_t s_custom_back_handler = NULL;

void menu_set_custom_back_handler(menu_custom_back_cb_t handler) {
  s_custom_back_handler = handler;
}

void menu_handle_back(void) {
  // Check for custom back handler first
  if (s_custom_back_handler && s_custom_back_handler()) {
    return;  // Handled by custom handler
  }
  menu_navigate_back();
}

void menu_cleanup(void) {
  // Delete all screens in stack
  for (int i = 0; i < menu_state.stack_depth; i++) {
    if (menu_state.stack[i].screen) {
      lv_obj_delete(menu_state.stack[i].screen);
      menu_state.stack[i].screen = NULL;
      menu_state.stack[i].container = NULL;
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
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();
  
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  // Title bar height
  const int title_bar_h = 22;
  
  // Create title bar container with woody brown gradient
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(title_bar, lv_color_make(101, 67, 33), 0);  // Dark woody brown
  lv_obj_set_style_bg_grad_color(title_bar, lv_color_make(139, 90, 43), 0);  // Lighter brown
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(title_bar, 0, 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  
  // Create title label inside title bar
  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_make(255, 248, 220), 0);  // Cornsilk/cream
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_center(title_label);
  lv_obj_remove_flag(title_label, LV_OBJ_FLAG_SCROLLABLE);

  // Margins for content
  const int left_margin = 20;
  const int top_margin = 4;

  // Create scrollable label (simpler than textarea, better for read-only)
  lv_obj_t* scroll_cont = lv_obj_create(screen);
  lv_obj_set_size(scroll_cont, disp_w - 4, disp_h - title_bar_h - 4);
  lv_obj_align(scroll_cont, LV_ALIGN_TOP_LEFT, 2, title_bar_h + 2);
  lv_obj_set_style_bg_opa(scroll_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scroll_cont, 0, 0);
  lv_obj_set_style_pad_left(scroll_cont, left_margin, 0);
  lv_obj_set_style_pad_top(scroll_cont, top_margin, 0);
  lv_obj_set_style_pad_right(scroll_cont, 4, 0);
  lv_obj_set_style_pad_bottom(scroll_cont, 4, 0);
  lv_obj_set_scroll_dir(scroll_cont, LV_DIR_VER);
  lv_obj_add_flag(scroll_cont, LV_OBJ_FLAG_SCROLLABLE);
  // Remove focus border styling
  lv_obj_set_style_border_width(scroll_cont, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(scroll_cont, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(scroll_cont, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(scroll_cont, 0, LV_STATE_FOCUS_KEY);
  
  // Create label inside scroll container
  lv_obj_t* label = lv_label_create(scroll_cont);
  lv_label_set_text(label, info_text);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_width(label, disp_w - left_margin - 12);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

  // Add container to group and set edit mode for encoder scrolling
  // In edit mode, encoder rotation scrolls the focused object
  if (menu_state.group) {
    lv_group_add_obj(menu_state.group, scroll_cont);
    lv_group_focus_obj(scroll_cont);
    lv_group_set_editing(menu_state.group, true);
  }

  return screen;
}

// Helper: Create a page with action buttons (uses same style as menu pages)
lv_obj_t* menu_create_action_page(const char* title, const menu_item_t* items, int item_count) {
  // Action pages use the same style as regular menu pages
  return menu_create_page(title, items, item_count);
}

// Static context for roller page callback
static struct {
  menu_roller_confirm_cb_t confirm_cb;
  void* user_data;
} s_roller_context = {0};

// Roller click event handler
static void roller_click_cb(lv_event_t* e) {
  lv_obj_t* roller = lv_event_get_target(e);
  uint32_t selected = lv_roller_get_selected(roller);

  ESP_LOGI(TAG, "Roller confirmed: index=%lu", (unsigned long)selected);

  // Call the confirm callback - callback is responsible for navigation
  if (s_roller_context.confirm_cb) {
    s_roller_context.confirm_cb(selected, s_roller_context.user_data);
  } else {
    // No callback, just navigate back
    menu_navigate_back();
  }
}

lv_obj_t* menu_create_roller_page(const char* title, const char* options,
  uint32_t initial_index, menu_roller_confirm_cb_t confirm_cb, void* user_data) {
  
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();
  
  // Store callback context
  s_roller_context.confirm_cb = confirm_cb;
  s_roller_context.user_data = user_data;
  
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  // Title bar
  const int title_bar_h = 22;
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(title_bar, lv_color_make(101, 67, 33), 0);
  lv_obj_set_style_bg_grad_color(title_bar, lv_color_make(139, 90, 43), 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(title_bar, 0, 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_make(255, 248, 220), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_center(title_label);

  // Create roller centered below title
  lv_obj_t* roller = lv_roller_create(screen);
  lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller, 3);
  lv_roller_set_selected(roller, initial_index, LV_ANIM_OFF);
  lv_obj_align(roller, LV_ALIGN_CENTER, 0, 10);
  
  // Roller styling
  lv_obj_set_style_bg_color(roller, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(roller, 0, 0);
  lv_obj_set_style_text_color(roller, lv_color_make(160, 160, 160), 0);
  lv_obj_set_style_text_font(roller, &lv_font_montserrat_14, 0);
  
  // Selected item styling
  lv_obj_set_style_bg_color(roller, lv_color_make(60, 60, 60), LV_PART_SELECTED);
  lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_SELECTED);
  
  // Add click event for confirmation
  lv_obj_add_event_cb(roller, roller_click_cb, LV_EVENT_CLICKED, NULL);
  
  // Add roller to menu group for encoder navigation
  if (menu_state.group) {
    lv_group_add_obj(menu_state.group, roller);
    lv_group_focus_obj(roller);
    // Put group in editing mode so encoder scrolls the roller
    lv_group_set_editing(menu_state.group, true);
  }
  
  ESP_LOGI(TAG, "Roller page created: %s, initial=%lu", title, (unsigned long)initial_index);
  
  return screen;
}
