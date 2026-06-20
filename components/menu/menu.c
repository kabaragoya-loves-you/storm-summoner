#include "menu.h"
#include "menu_pages.h"
#include "menu_theme.h"
#include "ui.h"
#include "display_driver.h"
#include "event_bus.h"
#include "scene.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define TAG "MENU"

// Centralized callback debounce - prevents double-firing from LVGL events
#define MENU_CALLBACK_DEBOUNCE_MS 100
static uint32_t s_last_callback_time = 0;

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
  bool is_replace;  // For deferred replace_current
  bool pop_then_replace;  // Pop back_levels, then replace top (not push)
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
  int restore_focus_index;  // Clickable index after next page creation (-1 = default)
  int restore_focus_item_index;  // menu_items[] index (-1 = default)
} menu_state = {
  .initialized = false,
  .stack_depth = 0,
  .group = NULL,
  .encoder_indev = NULL,
  .has_pending_nav = false,
  .skip_focus_scroll = false,
  .restore_focus_index = -1,
  .restore_focus_item_index = -1
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
static lv_obj_t* find_menu_item_widget(lv_obj_t* cont, int item_index);
static void apply_menu_focus(lv_obj_t* cont, lv_obj_t* focus_target);
static void apply_initial_page_focus(lv_obj_t* cont);

// Pending scene change direction (set by event handler, consumed by LVGL timer)
static int s_pending_scene_direction = 0;  // +1 = next, -1 = previous

// Deferred scene change + index rebuild (runs in LVGL task context, off event_dispatch stack)
static void deferred_scene_change_cb(lv_timer_t* timer) {
  lv_timer_delete(timer);
  
  int direction = s_pending_scene_direction;
  s_pending_scene_direction = 0;
  
  esp_err_t ret = ESP_FAIL;
  if (direction > 0) {
    ret = scene_next();
    ESP_LOGI(TAG, "Index: Left button -> scene_next: %s", esp_err_to_name(ret));
  } else if (direction < 0) {
    ret = scene_previous();
    ESP_LOGI(TAG, "Index: Right button -> scene_previous: %s", esp_err_to_name(ret));
  }
  
  if (ret == ESP_OK) {
    if (inspect_scene_is_active()) {
      menu_replace_current("Inspect Scene", menu_page_inspect_scene_create);
    } else {
      menu_replace_current("Menu", menu_page_index_create);
    }
  }
}

// Handle button events for scene navigation on index page
static void menu_button_event_handler(const event_t* event, void* context) {
  (void)context;
  if (!event) return;
  
  if (!menu_is_top_level() && !inspect_scene_is_active()) return;
  
  // Only in multi-scene modes
  scene_mode_t mode = scene_get_mode();
  if (mode == SCENE_MODE_SINGLE) return;
  
  // Don't queue if a scene change is already pending
  if (s_pending_scene_direction != 0) return;
  
  switch (event->type) {
    case EVENT_BUTTON_L_PRESS:
      s_pending_scene_direction = 1;
      break;
    case EVENT_BUTTON_R_PRESS:
      s_pending_scene_direction = -1;
      break;
    default:
      return;
  }
  
  // Defer scene change + rebuild to LVGL task context to avoid stack overflow
  lv_timer_t* t = lv_timer_create(deferred_scene_change_cb, 10, NULL);
  if (t) {
    lv_timer_set_repeat_count(t, 1);
  } else {
    s_pending_scene_direction = 0;
  }
}

void menu_init(void) {
  if (menu_state.initialized) {
    ESP_LOGD(TAG, "Menu already initialized, reusing existing state");
    return;
  }

  memset(&menu_state, 0, sizeof(menu_state));
  menu_state.initialized = true;

  menu_theme_init();
  
  // Create group for encoder navigation
  menu_state.group = lv_group_create();
  lv_group_set_wrap(menu_state.group, false);
  
  // Subscribe to button events for scene navigation on index page
  event_bus_subscribe(EVENT_BUTTON_L_PRESS, menu_button_event_handler, NULL);
  event_bus_subscribe(EVENT_BUTTON_R_PRESS, menu_button_event_handler, NULL);
  
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
// Saves the clickable item index (not raw child index) for consistency with restore
static void save_focused_index(void) {
  if (menu_state.stack_depth <= 0) return;
  
  menu_stack_entry_t* entry = &menu_state.stack[menu_state.stack_depth - 1];
  if (!entry->container) return;
  
  lv_obj_t* focused = lv_group_get_focused(menu_state.group);
  if (!focused) return;
  
  // Find the clickable item index of the focused object
  uint32_t child_cnt = lv_obj_get_child_count(entry->container);
  int clickable_idx = 0;
  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t* child = lv_obj_get_child(entry->container, i);
    if (child == focused) {
      entry->focused_index = (int32_t)clickable_idx;
      ESP_LOGD(TAG, "Saved focused index: %ld (clickable)", (long)entry->focused_index);
      return;
    }
    if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
      clickable_idx++;
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

static lv_obj_t* find_menu_item_widget(lv_obj_t* cont, int item_index) {
  if (!cont || item_index < 0) {
    return NULL;
  }
  uint32_t child_cnt = lv_obj_get_child_count(cont);
  if ((uint32_t)item_index >= child_cnt) {
    return NULL;
  }
  return lv_obj_get_child(cont, (uint32_t)item_index);
}

static void apply_menu_focus(lv_obj_t* cont, lv_obj_t* focus_target) {
  if (!focus_target || !menu_state.group) {
    return;
  }
  menu_state.skip_focus_scroll = true;
  lv_obj_scroll_to_view(focus_target, LV_ANIM_OFF);
  lv_group_focus_obj(focus_target);
  menu_state.skip_focus_scroll = false;
  if (cont) {
    update_scroll_visuals(cont);
  }
}

static void apply_initial_page_focus(lv_obj_t* cont) {
  if (!cont || !menu_state.group) {
    return;
  }

  lv_obj_t* focus_target = NULL;
  if (menu_state.restore_focus_item_index >= 0) {
    focus_target = find_menu_item_widget(cont, menu_state.restore_focus_item_index);
    menu_state.restore_focus_item_index = -1;
  } else if (menu_state.restore_focus_index >= 0) {
    int clickable_count = 0;
    uint32_t child_cnt = lv_obj_get_child_count(cont);
    for (uint32_t i = 0; i < child_cnt; i++) {
      lv_obj_t* child = lv_obj_get_child(cont, i);
      if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
        if (clickable_count == menu_state.restore_focus_index) {
          focus_target = child;
          break;
        }
        clickable_count++;
      }
    }
    menu_state.restore_focus_index = -1;
  } else {
    uint32_t child_cnt = lv_obj_get_child_count(cont);
    for (uint32_t i = 0; i < child_cnt; i++) {
      lv_obj_t* child = lv_obj_get_child(cont, i);
      if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
        focus_target = child;
        break;
      }
    }
  }

  if (focus_target) {
    apply_menu_focus(cont, focus_target);
  } else {
    update_scroll_visuals(cont);
  }
}

static menu_item_kind_t menu_item_effective_kind(const menu_item_t* item) {
  if (!item) return MENU_ITEM_KIND_AUTO;
  int kind = (int)item->kind;
  if (kind < 0 || kind >= MENU_ITEM_KIND_COUNT) return MENU_ITEM_KIND_AUTO;
  return (menu_item_kind_t)kind;
}

static lv_color_t menu_item_text_color(const menu_item_t* item, bool is_readonly) {
  const menu_theme_palette_t* palette = menu_theme_get_palette();
  menu_item_kind_t kind = menu_item_effective_kind(item);
  if (is_readonly || kind == MENU_ITEM_KIND_DISPLAY || kind == MENU_ITEM_KIND_HEADING) {
    return palette->item_display;
  }
  switch (kind) {
    case MENU_ITEM_KIND_SUBMENU: return palette->item_submenu;
    case MENU_ITEM_KIND_ROLLER: return palette->item_roller;
    case MENU_ITEM_KIND_ACTION: return palette->item_action;
    default: return palette->item_auto;
  }
}

static void menu_apply_title_bar_style(lv_obj_t* title_bar, lv_obj_t* title_label,
    bool two_line_variant) {
  const menu_theme_palette_t* palette = menu_theme_get_palette();
  lv_obj_set_style_bg_color(title_bar, palette->title_bar_bg, 0);
  lv_obj_set_style_bg_grad_color(title_bar,
    two_line_variant ? palette->title_bar_grad_2line : palette->title_bar_grad, 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(title_bar, 0, 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_color(title_label, palette->title_text, 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
}

static void menu_apply_roller_style(lv_obj_t* roller) {
  const menu_theme_palette_t* palette = menu_theme_get_palette();
  lv_obj_set_style_bg_color(roller, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(roller, 0, 0);
  lv_obj_set_style_text_color(roller, palette->roller_text, 0);
  lv_obj_set_style_text_font(roller, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(roller, palette->roller_selected_bg, LV_PART_SELECTED);
  lv_obj_set_style_text_color(roller, palette->roller_selected_text, LV_PART_SELECTED);
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

  // Title bar height (taller to account for circular display clipping at top)
  const int title_bar_h = 32;
  
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, title);
  menu_apply_title_bar_style(title_bar, title_label, false);
  lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 8);
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

  const menu_theme_palette_t* palette = menu_theme_get_palette();

  // Create labels for menu items
  for (int i = 0; i < item_count; i++) {
    const char* item_label = items[i].label;
    bool is_divider = (item_label && strncmp(item_label, "---", 3) == 0);
    menu_item_kind_t kind = menu_item_effective_kind(&items[i]);
    bool is_heading = (kind == MENU_ITEM_KIND_HEADING);
    bool is_readonly = (items[i].callback == NULL && !is_divider && !is_heading);
    
    if (is_divider) {
      lv_obj_t* line = lv_obj_create(cont);
      lv_obj_set_size(line, lv_pct(80), 2);
      lv_obj_set_style_bg_color(line, palette->divider, 0);
      lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(line, 0, 0);
      lv_obj_set_style_radius(line, 1, 0);
      lv_obj_set_style_pad_all(line, 0, 0);
      lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
      // Dividers are not focusable or clickable
      continue;
    }

    if (is_heading) {
      lv_obj_t* label = lv_label_create(cont);
      lv_label_set_text(label, item_label);
      lv_obj_set_width(label, lv_pct(100));
      lv_obj_set_height(label, 22);
      lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
      lv_obj_set_style_text_color(label, menu_item_text_color(&items[i], true), 0);
      lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_pad_ver(label, 2, 0);
      // Headings are visible but not focusable or clickable
      continue;
    }
    
    lv_obj_t* label = lv_label_create(cont);
    lv_label_set_text(label, item_label);
    lv_obj_set_width(label, lv_pct(100));
    
    // Fixed height to prevent layout shifts when font size changes
    // 28px accommodates 20px font with padding
    lv_obj_set_height(label, 28);
    
    // Clip long text to prevent wrapping/overflow
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);

    // Label styling - color by kind (read-only stays grey)
    lv_obj_set_style_text_color(label, menu_item_text_color(&items[i], is_readonly), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(label, 4, 0);

    // All items are focusable for encoder navigation
    lv_obj_add_flag(label, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(label, focus_event_cb, LV_EVENT_FOCUSED, NULL);
    
    if (is_readonly) {
      lv_obj_set_user_data(label, (void*)&items[i]);
      if (menu_state.group) lv_group_add_obj(menu_state.group, label);
    } else {
      // Clickable items: in group and respond to enter
      lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
      // Store menu_item_t* on the object so we can retrieve from focused object
      lv_obj_set_user_data(label, (void*)&items[i]);
      lv_obj_add_event_cb(label, menu_item_event_cb, LV_EVENT_CLICKED, (void*)&items[i]);
      if (menu_state.group) lv_group_add_obj(menu_state.group, label);
    }
  }

  if (item_count > 0 && menu_state.group) {
    apply_initial_page_focus(cont);
  } else {
    update_scroll_visuals(cont);
  }

  // Debug: log what we created
  uint32_t created_children = lv_obj_get_child_count(cont);
  ESP_LOGD(TAG, "menu_create_page: title='%s', screen=%p, cont=%p, children=%u",
           title, (void*)screen, (void*)cont, (unsigned)created_children);

  return screen;
}

// Two-line variant of menu_create_page
// Labels can contain newlines for two-line display
lv_obj_t* menu_create_page_2line(const char* title, const menu_item_t* items, int item_count) {
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();
  
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  // Title bar height (taller to account for circular display clipping at top)
  const int title_bar_h = 32;
  
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, title);
  menu_apply_title_bar_style(title_bar, title_label, true);
  lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 8);
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

  // Add scroll event callback for visual effects
  lv_obj_add_event_cb(cont, scroll_event_cb, LV_EVENT_SCROLL, NULL);

  // Create labels for menu items
  for (int i = 0; i < item_count; i++) {
    const char* item_label = items[i].label;
    bool is_readonly = (items[i].callback == NULL);
    
    lv_obj_t* label = lv_label_create(cont);
    lv_label_set_text(label, item_label);
    lv_obj_set_width(label, lv_pct(100));
    
    // Taller fixed height for two-line items (accommodates 20px focused font)
    lv_obj_set_height(label, 56);
    
    // Clip text (newlines handled naturally)
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);

    // Label styling - color by kind (read-only stays grey)
    lv_obj_set_style_text_color(label, menu_item_text_color(&items[i], is_readonly), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(label, 4, 0);

    // All items are focusable for encoder navigation
    lv_obj_add_flag(label, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(label, focus_event_cb, LV_EVENT_FOCUSED, NULL);
    
    if (is_readonly) {
      lv_obj_set_user_data(label, (void*)&items[i]);
      if (menu_state.group) lv_group_add_obj(menu_state.group, label);
    } else {
      lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(label, (void*)&items[i]);
      lv_obj_add_event_cb(label, menu_item_event_cb, LV_EVENT_CLICKED, (void*)&items[i]);
      if (menu_state.group) lv_group_add_obj(menu_state.group, label);
    }
  }

  if (item_count > 0 && menu_state.group) {
    apply_initial_page_focus(cont);
  } else {
    update_scroll_visuals(cont);
  }

  ESP_LOGD(TAG, "menu_create_page_2line: title='%s', screen=%p, children=%u",
           title, (void*)screen, (unsigned)lv_obj_get_child_count(cont));

  return screen;
}

static void menu_item_event_cb(lv_event_t* e) {
  // With encoder navigation, the click fires on the focused object.
  // Use the focused object from the group to ensure we get the right item
  // (scroll snap animation might not have finished updating focus)
  lv_obj_t* focused = NULL;
  menu_item_t* item = NULL;
  
  if (menu_state.group) {
    focused = lv_group_get_focused(menu_state.group);
    if (focused) {
      // Get the user_data from the focused object's event callback
      // We stored the menu_item_t* when we added the callback
      item = (menu_item_t*)lv_obj_get_user_data(focused);
    }
  }
  
  // Fallback to event's user_data if group focus didn't work
  if (!item) {
    item = (menu_item_t*)lv_event_get_user_data(e);
  }

  if (!item) return;
  
  // Centralized debounce - prevent LVGL double-firing events
  uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
  if (now - s_last_callback_time < MENU_CALLBACK_DEBOUNCE_MS) {
    ESP_LOGD(TAG, "Debouncing menu callback: %s", item->label);
    return;
  }
  s_last_callback_time = now;
  
  // Don't fire callbacks if navigation is already pending
  if (menu_state.has_pending_nav) {
    ESP_LOGD(TAG, "Navigation pending, ignoring: %s", item->label);
    return;
  }

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

  ESP_LOGD(TAG, "Top-level menu created");
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
  ESP_LOGD(TAG, "Nav to %s: screen=%p, container=%p, children=%u", 
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

  ESP_LOGI(TAG, "Navigated to menu: %s (depth: %d)", 
    menu_name ? menu_name : "(unnamed)", menu_state.stack_depth);
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
      // Use async delete to let LVGL finish processing any pending events
      lv_obj_delete_async(s_pending_delete_screens[i]);
      s_pending_delete_screens[i] = NULL;
    }
  }
  s_pending_delete_count = 0;
}

static void deferred_nav_timer_cb(lv_timer_t* timer) {
  lv_timer_delete(timer);

  if (menu_state.has_pending_nav) {
    if (menu_state.pending_nav.is_replace && !menu_state.pending_nav.pop_then_replace) {
      // Deferred replace - call synchronous replace now (outside render cycle)
      menu_replace_current(menu_state.pending_nav.menu_name,
                          menu_state.pending_nav.builder);
      menu_state.has_pending_nav = false;
      return;
    }
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

      // If there's a forward navigation, push or replace the new page
      if (has_forward) {
        if (menu_state.pending_nav.pop_then_replace) {
          menu_replace_current(menu_state.pending_nav.menu_name,
            menu_state.pending_nav.builder);
        } else {
          menu_navigate_to_internal(menu_state.pending_nav.menu_name,
            menu_state.pending_nav.builder);
        }
        menu_flush_pending_deletes();
      }
    } else {
      menu_navigate_to_internal(menu_state.pending_nav.menu_name,
                               menu_state.pending_nav.builder);
    }
    
    // Apply restore focus if set
    if (menu_state.stack_depth > 0 &&
        (menu_state.restore_focus_item_index >= 0 || menu_state.restore_focus_index >= 0)) {
      menu_stack_entry_t* entry = &menu_state.stack[menu_state.stack_depth - 1];
      if (entry->container && menu_state.group) {
        lv_obj_t* focus_target = NULL;
        if (menu_state.restore_focus_item_index >= 0) {
          focus_target = find_menu_item_widget(entry->container,
            menu_state.restore_focus_item_index);
          menu_state.restore_focus_item_index = -1;
        } else {
          int focus_idx = menu_state.restore_focus_index;
          int clickable_count = 0;
          lv_obj_t* last_clickable = NULL;
          uint32_t child_cnt = lv_obj_get_child_count(entry->container);
          for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t* child = lv_obj_get_child(entry->container, i);
            if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
              last_clickable = child;
              if (clickable_count == focus_idx) {
                focus_target = child;
              }
              clickable_count++;
            }
          }
          if (!focus_target && last_clickable) {
            focus_target = last_clickable;
            ESP_LOGD(TAG, "Focus index %d clamped to last item", focus_idx);
          }
          menu_state.restore_focus_index = -1;
        }
        apply_menu_focus(entry->container, focus_target);
      } else {
        menu_state.restore_focus_item_index = -1;
        menu_state.restore_focus_index = -1;
      }
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
  menu_state.pending_nav.back_levels = 0;
  menu_state.pending_nav.is_replace = false;
  menu_state.has_pending_nav = true;
  
  // Create a one-shot timer to execute navigation in LVGL task context
  lv_timer_t* nav_timer = lv_timer_create(deferred_nav_timer_cb, 10, NULL);
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
    ESP_LOGD(TAG, "At top level, exiting Programming mode");
    ui_set_app_mode(APP_MODE_PERFORMANCE);
    return;
  }

  if (inspect_scene_is_active()) menu_page_inspect_scene_cleanup();

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
    lv_obj_delete_async(menu_state.stack[menu_state.stack_depth].screen);
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
      
      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(prev_cont, i);
        if (child) lv_group_add_obj(menu_state.group, child);
      }
      
      // Restore focus to saved clickable index
      int32_t focus_idx = entry->focused_index;
      if (entry->name && strcmp(entry->name, "CC Triggers") == 0)
        focus_idx = (int32_t)cc_triggers_focus_slot_get();

      lv_obj_t* focus_target = NULL;
      int clickable_count = 0;

      for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(prev_cont, i);
        if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
          if (clickable_count == focus_idx) {
            focus_target = child;
            break;
          }
          clickable_count++;
        }
      }
      
      // Fallback to first clickable if saved index not found
      if (!focus_target) {
        for (uint32_t i = 0; i < child_cnt; i++) {
          lv_obj_t* child = lv_obj_get_child(prev_cont, i);
          if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
            focus_target = child;
            break;
          }
        }
      }
      
      if (focus_target) {
        menu_state.skip_focus_scroll = true;
        lv_group_focus_obj(focus_target);
        lv_obj_scroll_to_view(focus_target, LV_ANIM_OFF);
        menu_state.skip_focus_scroll = false;
        
        update_scroll_visuals(prev_cont);
        
        ESP_LOGD(TAG, "Restored focus to clickable index: %ld", (long)focus_idx);
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

  // Special case: leaving Scene Name menu should rebuild the parent Scene page
  // to pick up any name changes that affect the Scene page title
  if (menu_state.stack_depth > 0) {
    const char* current_name = menu_state.stack[menu_state.stack_depth - 1].name;
    if (current_name && strcmp(current_name, "Scene Name") == 0) {
      menu_navigate_back_then_to(2, "Scene", menu_page_current_scene_create);
      return;
    }
    // Special case: leaving Scenes manager should rebuild the index page
    // to reflect any reordering (scene ordinal might have changed)
    if (current_name && strcmp(current_name, "Scenes") == 0 && menu_state.stack_depth == 2) {
      menu_navigate_back_then_to(2, "Menu", menu_page_index_create);
      return;
    }
  }

  menu_state.pending_nav.is_back = true;
  menu_state.pending_nav.back_levels = 1;
  menu_state.pending_nav.menu_name = NULL;
  menu_state.pending_nav.builder = NULL;
  menu_state.pending_nav.is_replace = false;
  menu_state.pending_nav.pop_then_replace = false;
  menu_state.has_pending_nav = true;

  // Create a one-shot timer to execute navigation in LVGL task context
  lv_timer_t* nav_timer = lv_timer_create(deferred_nav_timer_cb, 10, NULL);
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

  // Auto-capture focus from the page being replaced (if not manually set)
  // When doing back_then_to(2, ...), the page at stack_depth-2 is being replaced
  if (menu_state.restore_focus_index < 0 && levels >= 2 && 
      menu_state.stack_depth >= levels) {
    int target_depth = menu_state.stack_depth - levels;
    if (target_depth >= 0) {
      menu_stack_entry_t* target_entry = &menu_state.stack[target_depth];
      if (target_entry->focused_index >= 0) {
        menu_state.restore_focus_index = (int)target_entry->focused_index;
        ESP_LOGD(TAG, "Auto-captured restore focus: %d", 
                 menu_state.restore_focus_index);
      }
    }
  }

  menu_state.pending_nav.is_back = true;
  menu_state.pending_nav.back_levels = levels;
  menu_state.pending_nav.menu_name = menu_name;
  menu_state.pending_nav.builder = builder;
  menu_state.pending_nav.is_replace = false;
  menu_state.pending_nav.pop_then_replace = false;
  menu_state.has_pending_nav = true;

  // Create a one-shot timer to execute navigation in LVGL task context
  lv_timer_t* nav_timer = lv_timer_create(deferred_nav_timer_cb, 10, NULL);
  if (!nav_timer) {
    ESP_LOGE(TAG, "Failed to create back_then_to navigation timer");
    menu_state.has_pending_nav = false;
    return;
  }
  lv_timer_set_repeat_count(nav_timer, 1);
}

void menu_set_restore_focus(int index) {
  menu_state.restore_focus_index = index;
  menu_state.restore_focus_item_index = -1;
  ESP_LOGD(TAG, "Set restore focus index: %d", index);
}

void menu_set_restore_focus_item(int item_index) {
  menu_state.restore_focus_item_index = item_index;
  menu_state.restore_focus_index = -1;
  ESP_LOGD(TAG, "Set restore focus item index: %d", item_index);
}

void menu_replace_current(const char* menu_name, menu_page_builder_t builder) {
  // Synchronous replacement - for use when already in LVGL context (callbacks)
  // This replaces the current page without going through deferred navigation
  
  if (menu_state.stack_depth < 1 || !builder) {
    ESP_LOGW(TAG, "Cannot replace current: invalid state");
    return;
  }
  
  ESP_LOGI(TAG, "Replacing current page with: %s", menu_name ? menu_name : "(unnamed)");
  
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
  if (inspect_scene_is_active()) inspect_scene_invalidate_scroll();
  lv_obj_t* old_screen = menu_state.stack[menu_state.stack_depth - 1].screen;
  
  // Decrement stack depth (pop current page)
  menu_state.stack_depth--;

  // Builders may call apply_initial_page_focus() and clear restore indices
  int saved_restore_focus = menu_state.restore_focus_index;
  int saved_restore_item = menu_state.restore_focus_item_index;

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
  // Track focus target for after screen load
  lv_obj_t* focus_target = NULL;
  
  if (saved_restore_focus >= 0 && menu_state.restore_focus_index < 0)
    menu_state.restore_focus_index = saved_restore_focus;
  if (saved_restore_item >= 0 && menu_state.restore_focus_item_index < 0)
    menu_state.restore_focus_item_index = saved_restore_item;

  if (menu_state.group && new_container) {
    uint32_t new_child_cnt = lv_obj_get_child_count(new_container);
    for (uint32_t i = 0; i < new_child_cnt; i++) {
      lv_obj_t* child = lv_obj_get_child(new_container, i);
      if (child) lv_group_add_obj(menu_state.group, child);
    }
    
    if (menu_state.restore_focus_item_index >= 0) {
      focus_target = find_menu_item_widget(new_container,
        menu_state.restore_focus_item_index);
      menu_state.restore_focus_item_index = -1;
    } else if (menu_state.restore_focus_index >= 0) {
      int clickable_count = 0;
      for (uint32_t i = 0; i < new_child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(new_container, i);
        if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
          if (clickable_count == menu_state.restore_focus_index) {
            focus_target = child;
            break;
          }
          clickable_count++;
        }
      }
      menu_state.restore_focus_index = -1;
    } else if (new_child_cnt > 0) {
      for (uint32_t i = 0; i < new_child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(new_container, i);
        if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
          focus_target = child;
          break;
        }
      }
    }
    apply_menu_focus(new_container, focus_target);
  }
  
  // Load new screen
  lv_screen_load(new_screen);
  
  // Scroll to focus target after screen is loaded
  if (focus_target) {
    lv_obj_scroll_to_view(focus_target, LV_ANIM_OFF);
  }
  
  // Delete old screen after loading new one (async to let events finish)
  if (old_screen) lv_obj_delete_async(old_screen);

  if (inspect_scene_is_active()) inspect_scene_rebind_input();
  
  // Reset debounce timer to prevent double-clicks after replacement
  s_last_callback_time = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
  
  ESP_LOGI(TAG, "Replaced current page with: %s", menu_name ? menu_name : "(unnamed)");
}

void menu_rebuild_stack_entry(int depth, const char* menu_name,
  menu_page_builder_t builder, const char* focus_label) {
  if (!builder) {
    ESP_LOGW(TAG, "menu_rebuild_stack_entry: NULL builder");
    return;
  }
  if (depth < 0 || depth >= menu_state.stack_depth) {
    ESP_LOGW(TAG, "menu_rebuild_stack_entry: depth %d out of range (stack=%d)",
      depth, menu_state.stack_depth);
    return;
  }
  if (depth == menu_state.stack_depth - 1) {
    ESP_LOGW(TAG, "menu_rebuild_stack_entry: cannot rebuild visible page, use menu_replace_current");
    return;
  }

  lv_obj_t* new_screen = builder();
  if (!new_screen) {
    ESP_LOGE(TAG, "menu_rebuild_stack_entry: builder returned NULL");
    return;
  }

  lv_obj_t* new_container = find_container_in_screen(new_screen);

  int32_t focused_index = 0;
  if (focus_label && new_container) {
    uint32_t child_cnt = lv_obj_get_child_count(new_container);
    int clickable_idx = 0;
    for (uint32_t i = 0; i < child_cnt; i++) {
      lv_obj_t* child = lv_obj_get_child(new_container, i);
      if (!child || !lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) continue;
      menu_item_t* item = (menu_item_t*)lv_obj_get_user_data(child);
      if (item && item->label && strcmp(item->label, focus_label) == 0) {
        focused_index = clickable_idx;
        break;
      }
      clickable_idx++;
    }
  }

  lv_obj_t* old_screen = menu_state.stack[depth].screen;

  menu_state.stack[depth].screen = new_screen;
  menu_state.stack[depth].container = new_container;
  menu_state.stack[depth].name = menu_name;
  menu_state.stack[depth].builder = builder;
  menu_state.stack[depth].focused_index = focused_index;

  if (old_screen) lv_obj_delete_async(old_screen);

  ESP_LOGI(TAG, "Rebuilt stack entry at depth %d: %s (focus_idx=%ld)",
    depth, menu_name ? menu_name : "(unnamed)", (long)focused_index);
}

void menu_replace_current_deferred(const char* menu_name, menu_page_builder_t builder) {
  // Deferred replacement - safe to call during LVGL event callbacks/rendering
  if (menu_state.stack_depth < 1 || !builder) {
    ESP_LOGW(TAG, "Cannot replace current (deferred): invalid state");
    return;
  }

  menu_state.pending_nav.menu_name = menu_name;
  menu_state.pending_nav.builder = builder;
  menu_state.pending_nav.is_back = false;
  menu_state.pending_nav.back_levels = 0;
  menu_state.pending_nav.is_replace = true;
  menu_state.pending_nav.pop_then_replace = false;
  menu_state.has_pending_nav = true;

  lv_timer_t* nav_timer = lv_timer_create(deferred_nav_timer_cb, 10, NULL);
  lv_timer_set_repeat_count(nav_timer, 1);
}

void menu_pop_then_replace_deferred(int levels, const char* menu_name,
  menu_page_builder_t builder) {
  if (menu_state.stack_depth < 1 || !builder || levels < 1) {
    ESP_LOGW(TAG, "Cannot pop-then-replace (deferred): invalid state");
    return;
  }
  if (menu_state.has_pending_nav) {
    ESP_LOGW(TAG, "Navigation already pending, dropping pop_then_replace request");
    return;
  }

  menu_state.pending_nav.menu_name = menu_name;
  menu_state.pending_nav.builder = builder;
  menu_state.pending_nav.is_back = true;
  menu_state.pending_nav.back_levels = levels;
  menu_state.pending_nav.is_replace = false;
  menu_state.pending_nav.pop_then_replace = true;
  menu_state.has_pending_nav = true;

  lv_timer_t* nav_timer = lv_timer_create(deferred_nav_timer_cb, 10, NULL);
  if (!nav_timer) {
    ESP_LOGW(TAG, "Failed to create pop_then_replace navigation timer");
    menu_state.has_pending_nav = false;
    return;
  }
  lv_timer_set_repeat_count(nav_timer, 1);
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
  if (inspect_scene_is_active()) menu_page_inspect_scene_cleanup();

  // Delete all screens in stack (async to let events finish)
  for (int i = 0; i < menu_state.stack_depth; i++) {
    if (menu_state.stack[i].screen) {
      lv_obj_delete_async(menu_state.stack[i].screen);
      menu_state.stack[i].screen = NULL;
      menu_state.stack[i].container = NULL;
    }
  }

  menu_state.stack_depth = 0;

  // Cleanup group
  if (menu_state.group) lv_group_remove_all_objs(menu_state.group);

  ESP_LOGD(TAG, "Menu cleaned up");
}

bool menu_is_top_level(void) {
  return (menu_state.stack_depth == 1);
}

bool menu_current_page_is(const char *name) {
  if (!name || menu_state.stack_depth < 1) return false;
  const char *current = menu_state.stack[menu_state.stack_depth - 1].name;
  return current && strcmp(current, name) == 0;
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

lv_obj_t* menu_get_current_container(void) {
  if (menu_state.stack_depth > 0) {
    return menu_state.stack[menu_state.stack_depth - 1].container;
  }
  return NULL;
}

void* menu_get_focused_item_user_data(void) {
  if (menu_state.stack_depth < 1 || !menu_state.group) return NULL;

  lv_obj_t* focused = lv_group_get_focused(menu_state.group);
  if (!focused) return NULL;

  menu_item_t* item = (menu_item_t*)lv_obj_get_user_data(focused);
  if (!item || !item->callback) return NULL;

  return item->user_data;
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

  // Title bar height (taller to account for circular display clipping at top)
  const int title_bar_h = 32;
  
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, title);
  menu_apply_title_bar_style(title_bar, title_label, false);
  lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 8);
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
  uint32_t create_time_ms;  // Time when roller was created (for debounce)
} s_roller_context = {0};

// Minimum time between roller creation and confirmation (ms)
#define ROLLER_DEBOUNCE_MS 100

// Roller click event handler
static void roller_click_cb(lv_event_t* e) {
  // Debounce: ignore confirmations that happen too soon after creation
  uint32_t now = lv_tick_get();
  if (now - s_roller_context.create_time_ms < ROLLER_DEBOUNCE_MS) {
    ESP_LOGW(TAG, "Roller confirmation ignored (debounce: %lums)", 
             (unsigned long)(now - s_roller_context.create_time_ms));
    return;
  }
  
  lv_obj_t* roller = lv_event_get_target(e);
  uint32_t selected = lv_roller_get_selected(roller);

  ESP_LOGD(TAG, "Roller confirmed: index=%lu", (unsigned long)selected);

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
  s_roller_context.create_time_ms = lv_tick_get();

  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  // Title bar (taller to account for circular display clipping at top)
  const int title_bar_h = 27;
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, title);
  menu_apply_title_bar_style(title_bar, title_label, false);
  lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 8);

  // Create roller centered below title
  lv_obj_t* roller = lv_roller_create(screen);
  lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller, 3);
  lv_roller_set_selected(roller, initial_index, LV_ANIM_OFF);
  lv_obj_align(roller, LV_ALIGN_CENTER, 0, 10);

  menu_apply_roller_style(roller);
  
  // Add click event for confirmation
  lv_obj_add_event_cb(roller, roller_click_cb, LV_EVENT_CLICKED, NULL);
  
  // Add roller to menu group for encoder navigation
  if (menu_state.group) {
    lv_group_add_obj(menu_state.group, roller);
    lv_group_focus_obj(roller);
    // Put group in editing mode so encoder scrolls the roller
    lv_group_set_editing(menu_state.group, true);
  }
  
  ESP_LOGD(TAG, "Roller page created: %s, initial=%lu", title, (unsigned long)initial_index);
  
  return screen;
}

// ============================================================================
// Fractional BPM editor (two-phase single roller)
// ============================================================================

typedef enum {
  BPM_EDITOR_PHASE_WHOLE = 0,
  BPM_EDITOR_PHASE_TENTHS,
} bpm_editor_phase_t;

static struct {
  menu_bpm_editor_cfg_t cfg;
  bpm_editor_phase_t phase;
  uint16_t frozen_whole;
  lv_obj_t* roller;
  char whole_options[2048];
  char tenths_options[128];
  uint32_t create_time_ms;
  menu_custom_back_cb_t saved_back_handler;
  bool active;
} s_bpm_editor = {0};

static void bpm_editor_build_whole_options(char* buf, size_t cap,
  const menu_bpm_editor_cfg_t* cfg) {
  size_t pos = 0;
  buf[0] = '\0';
  if (cfg->prefix_options && cfg->prefix_count > 0) {
    int n = snprintf(buf, cap, "%s", cfg->prefix_options);
    if (n > 0 && (size_t)n < cap) pos = (size_t)n;
  }
  for (uint16_t w = cfg->min_whole; w <= cfg->max_whole && pos < cap; w++) {
    int n = snprintf(buf + pos, cap - pos, "%s%u",
      pos > 0 ? "\n" : "", (unsigned)w);
    if (n <= 0 || (size_t)n >= cap - pos) break;
    pos += (size_t)n;
  }
}

static void bpm_editor_build_tenths_options(char* buf, size_t cap, uint16_t whole) {
  size_t pos = 0;
  buf[0] = '\0';
  for (uint8_t t = 0; t < 10 && pos < cap; t++) {
    int n = snprintf(buf + pos, cap - pos, "%s%u.%u",
      t > 0 ? "\n" : "", (unsigned)whole, (unsigned)t);
    if (n <= 0 || (size_t)n >= cap - pos) break;
    pos += (size_t)n;
  }
}

static uint32_t bpm_editor_whole_index_from_bpm(const menu_bpm_editor_cfg_t* cfg) {
  uint16_t bpm = cfg->initial_bpm_x10;
  if (cfg->prefix_count > 0 && cfg->prefix_values) {
    for (uint8_t i = 0; i < cfg->prefix_count; i++) {
      if (bpm == cfg->prefix_values[i]) return i;
    }
  }
  uint16_t whole = bpm / 10;
  if (whole < cfg->min_whole) whole = cfg->min_whole;
  if (whole > cfg->max_whole) whole = cfg->max_whole;
  return (uint32_t)(cfg->prefix_count + (whole - cfg->min_whole));
}

static void bpm_editor_enter_tenths_phase(uint8_t initial_tenth) {
  menu_bpm_editor_cfg_t* cfg = &s_bpm_editor.cfg;
  s_bpm_editor.phase = BPM_EDITOR_PHASE_TENTHS;
  bpm_editor_build_tenths_options(s_bpm_editor.tenths_options,
    sizeof(s_bpm_editor.tenths_options), s_bpm_editor.frozen_whole);
  lv_roller_set_options(s_bpm_editor.roller, s_bpm_editor.tenths_options,
    LV_ROLLER_MODE_NORMAL);
  if (initial_tenth > 9) initial_tenth = 0;
  lv_roller_set_selected(s_bpm_editor.roller, initial_tenth, LV_ANIM_OFF);
  (void)cfg;
}

static void bpm_editor_revert_to_whole(void) {
  menu_bpm_editor_cfg_t* cfg = &s_bpm_editor.cfg;
  s_bpm_editor.phase = BPM_EDITOR_PHASE_WHOLE;
  bpm_editor_build_whole_options(s_bpm_editor.whole_options,
    sizeof(s_bpm_editor.whole_options), cfg);
  lv_roller_set_options(s_bpm_editor.roller, s_bpm_editor.whole_options,
    LV_ROLLER_MODE_NORMAL);
  uint32_t idx = (uint32_t)(cfg->prefix_count +
    (s_bpm_editor.frozen_whole - cfg->min_whole));
  lv_roller_set_selected(s_bpm_editor.roller, idx, LV_ANIM_OFF);
}

static void bpm_editor_finish(void) {
  menu_custom_back_cb_t restore = s_bpm_editor.saved_back_handler;
  s_bpm_editor.active = false;
  s_bpm_editor.roller = NULL;
  s_bpm_editor.saved_back_handler = NULL;
  menu_set_custom_back_handler(restore);
}

static bool bpm_editor_back_handler(void) {
  if (!s_bpm_editor.active) return false;
  if (s_bpm_editor.phase == BPM_EDITOR_PHASE_TENTHS) {
    bpm_editor_revert_to_whole();
    return true;
  }
  bpm_editor_finish();
  return false;
}

static void bpm_editor_click_cb(lv_event_t* e) {
  uint32_t now = lv_tick_get();
  if (now - s_bpm_editor.create_time_ms < ROLLER_DEBOUNCE_MS) {
    ESP_LOGW(TAG, "BPM editor confirmation ignored (debounce: %lums)",
      (unsigned long)(now - s_bpm_editor.create_time_ms));
    return;
  }

  lv_obj_t* roller = lv_event_get_target(e);
  uint32_t selected = lv_roller_get_selected(roller);
  menu_bpm_editor_cfg_t* cfg = &s_bpm_editor.cfg;

  if (s_bpm_editor.phase == BPM_EDITOR_PHASE_WHOLE) {
    if (cfg->prefix_count > 0 && cfg->prefix_values &&
        selected < cfg->prefix_count) {
      uint16_t value = cfg->prefix_values[selected];
      menu_bpm_commit_cb_t commit = cfg->commit;
      void* user_data = cfg->user_data;
      bpm_editor_finish();
      if (commit) commit(value, user_data);
      return;
    }
    uint16_t whole = (uint16_t)(cfg->min_whole + (selected - cfg->prefix_count));
    if (cfg->allow_fractional) {
      s_bpm_editor.frozen_whole = whole;
      uint8_t tenth = 0;
      if (cfg->initial_bpm_x10 / 10 == whole)
        tenth = (uint8_t)(cfg->initial_bpm_x10 % 10);
      bpm_editor_enter_tenths_phase(tenth);
      return;
    }
    menu_bpm_commit_cb_t commit = cfg->commit;
    void* user_data = cfg->user_data;
    bpm_editor_finish();
    if (commit) commit((uint16_t)(whole * 10), user_data);
    return;
  }

  menu_bpm_commit_cb_t commit = cfg->commit;
  void* user_data = cfg->user_data;
  uint16_t bpm_x10 = (uint16_t)(s_bpm_editor.frozen_whole * 10 + selected);
  bpm_editor_finish();
  if (commit) commit(bpm_x10, user_data);
}

lv_obj_t* menu_create_bpm_editor_page(const menu_bpm_editor_cfg_t* cfg) {
  if (!cfg || !cfg->commit) return NULL;

  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();

  memset(&s_bpm_editor, 0, sizeof(s_bpm_editor));
  s_bpm_editor.cfg = *cfg;
  s_bpm_editor.phase = BPM_EDITOR_PHASE_WHOLE;
  s_bpm_editor.active = true;
  s_bpm_editor.create_time_ms = lv_tick_get();

  bpm_editor_build_whole_options(s_bpm_editor.whole_options,
    sizeof(s_bpm_editor.whole_options), cfg);
  uint32_t initial_index = bpm_editor_whole_index_from_bpm(cfg);

  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  const int title_bar_h = 27;
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, cfg->title ? cfg->title : "BPM");
  menu_apply_title_bar_style(title_bar, title_label, false);
  lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 8);

  lv_obj_t* roller = lv_roller_create(screen);
  s_bpm_editor.roller = roller;
  lv_roller_set_options(roller, s_bpm_editor.whole_options, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller, 3);
  lv_roller_set_selected(roller, initial_index, LV_ANIM_OFF);
  lv_obj_align(roller, LV_ALIGN_CENTER, 0, 10);
  menu_apply_roller_style(roller);
  lv_obj_add_event_cb(roller, bpm_editor_click_cb, LV_EVENT_CLICKED, NULL);

  if (menu_state.group) {
    lv_group_add_obj(menu_state.group, roller);
    lv_group_focus_obj(roller);
    lv_group_set_editing(menu_state.group, true);
  }

  s_bpm_editor.saved_back_handler = s_custom_back_handler;
  menu_set_custom_back_handler(bpm_editor_back_handler);

  ESP_LOGD(TAG, "BPM editor created: %s, initial=%lu",
    cfg->title ? cfg->title : "BPM", (unsigned long)initial_index);

  return screen;
}
