#include "menu.h"
#include "menu_pages.h"
#include "action_summary.h"
#include "scene.h"
#include "event_bus.h"
#include "lfo.h"
#include "sample_hold.h"
#include "chalet_ny_8.h"
#include "esp_log.h"
#include "misc/lv_async.h"
#include <math.h>
#include <string.h>

#define TAG "INSPECT_SCENE"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Screen and widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_device_widget = NULL;
static lv_obj_t *g_text_label = NULL;
static lv_obj_t *g_status_widget = NULL;

// Inspect mode state
static bool g_inspect_active = false;
static bool g_subscribed = false;

// Auto-hide timer
static lv_timer_t *g_hide_timer = NULL;
#define HIDE_TIMEOUT_MS 1000

// Text buffer for assignment display
static char g_text_buf[256];

// Highlighted input tracking
typedef enum {
  HIGHLIGHT_NONE = 0,
  HIGHLIGHT_PAD_0,
  HIGHLIGHT_PAD_1,
  HIGHLIGHT_PAD_2,
  HIGHLIGHT_PAD_3,
  HIGHLIGHT_PAD_4,
  HIGHLIGHT_PAD_5,
  HIGHLIGHT_PAD_6,
  HIGHLIGHT_PAD_7,
  HIGHLIGHT_PAD_8,
  HIGHLIGHT_PAD_9,
  HIGHLIGHT_PAD_10,
  HIGHLIGHT_PAD_11,
  HIGHLIGHT_BUTTON_L,
  HIGHLIGHT_BUTTON_R,
  HIGHLIGHT_BUTTON_BOTH,
  HIGHLIGHT_BUMP,
  HIGHLIGHT_EXPRESSION,
  HIGHLIGHT_CV,
  HIGHLIGHT_PROXIMITY
} highlight_input_t;

static volatile highlight_input_t g_highlighted_input = HIGHLIGHT_NONE;

// Forward declarations
static void build_inspect_widgets(lv_obj_t *screen);
static void cleanup_screen(void);
static void subscribe_events(void);
static void unsubscribe_events(void);
static void update_text_label(void);
static void start_hide_timer(void);
static void cancel_hide_timer(void);
static void hide_timer_cb(lv_timer_t *timer);

bool inspect_scene_is_active(void) {
  return g_inspect_active;
}

// Timer helpers
static void hide_timer_cb(lv_timer_t *timer) {
  (void)timer;
  g_highlighted_input = HIGHLIGHT_NONE;
  update_text_label();
  if (g_device_widget) lv_obj_invalidate(g_device_widget);
  g_hide_timer = NULL;
}

static void start_hide_timer(void) {
  if (g_hide_timer) {
    lv_timer_reset(g_hide_timer);
  } else {
    g_hide_timer = lv_timer_create(hide_timer_cb, HIDE_TIMEOUT_MS, NULL);
    lv_timer_set_repeat_count(g_hide_timer, 1);
  }
}

static void cancel_hide_timer(void) {
  if (g_hide_timer) {
    lv_timer_delete(g_hide_timer);
    g_hide_timer = NULL;
  }
}

// Map highlight_input_t to summary_input_t
static summary_input_t highlight_to_summary_input(highlight_input_t h) {
  switch (h) {
    case HIGHLIGHT_PAD_0: return SUMMARY_INPUT_PAD_0;
    case HIGHLIGHT_PAD_1: return SUMMARY_INPUT_PAD_1;
    case HIGHLIGHT_PAD_2: return SUMMARY_INPUT_PAD_2;
    case HIGHLIGHT_PAD_3: return SUMMARY_INPUT_PAD_3;
    case HIGHLIGHT_PAD_4: return SUMMARY_INPUT_PAD_4;
    case HIGHLIGHT_PAD_5: return SUMMARY_INPUT_PAD_5;
    case HIGHLIGHT_PAD_6: return SUMMARY_INPUT_PAD_6;
    case HIGHLIGHT_PAD_7: return SUMMARY_INPUT_PAD_7;
    case HIGHLIGHT_PAD_8: return SUMMARY_INPUT_PAD_8;
    case HIGHLIGHT_PAD_9: return SUMMARY_INPUT_PAD_9;
    case HIGHLIGHT_PAD_10: return SUMMARY_INPUT_PAD_10;
    case HIGHLIGHT_PAD_11: return SUMMARY_INPUT_PAD_11;
    case HIGHLIGHT_BUTTON_L: return SUMMARY_INPUT_BUTTON_L;
    case HIGHLIGHT_BUTTON_R: return SUMMARY_INPUT_BUTTON_R;
    case HIGHLIGHT_BUTTON_BOTH: return SUMMARY_INPUT_BUTTON_BOTH;
    case HIGHLIGHT_BUMP: return SUMMARY_INPUT_BUMP;
    case HIGHLIGHT_EXPRESSION: return SUMMARY_INPUT_EXPRESSION;
    case HIGHLIGHT_CV: return SUMMARY_INPUT_CV;
    case HIGHLIGHT_PROXIMITY: return SUMMARY_INPUT_PROXIMITY;
    default: return SUMMARY_INPUT_UNKNOWN;
  }
}

// Update text label with current assignment
static void update_text_label(void) {
  if (!g_text_label) return;
  
  scene_t *scene = scene_get_current();
  if (!scene) {
    lv_label_set_text(g_text_label, "No scene");
    return;
  }
  
  highlight_input_t highlight = g_highlighted_input;
  
  if (highlight == HIGHLIGHT_NONE) {
    lv_label_set_text(g_text_label, "");
    return;
  }
  
  uint8_t scene_index = scene_get_current_index();
  bool is_pads_mode = scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS;
  
  action_summary_t summary;
  action_summary_init(&summary);
  action_summary_set_input(&summary, highlight_to_summary_input(highlight), is_pads_mode);
  
  if (highlight >= HIGHLIGHT_PAD_0 && highlight <= HIGHLIGHT_PAD_7) {
    int pad = highlight - HIGHLIGHT_PAD_0;
    if (is_pads_mode) {
      touchpad_mapping_t *mapping = &scene->touchpads[pad];
      if (mapping->enabled) {
        action_format_summary(&mapping->action, &summary, scene_index);
      } else {
        snprintf(summary.type_name, sizeof(summary.type_name), "Disabled");
      }
    } else {
      touchwheel_format_summary(scene, &summary);
    }
  } else if (highlight >= HIGHLIGHT_PAD_8 && highlight <= HIGHLIGHT_PAD_11) {
    int pad = highlight - HIGHLIGHT_PAD_0;
    touchpad_mapping_t *mapping = &scene->touchpads[pad];
    if (mapping->enabled) {
      action_format_summary(&mapping->action, &summary, scene_index);
    } else {
      snprintf(summary.type_name, sizeof(summary.type_name), "Disabled");
    }
  } else if (highlight == HIGHLIGHT_BUTTON_L) {
    action_format_summary(&scene->button_left, &summary, scene_index);
  } else if (highlight == HIGHLIGHT_BUTTON_R) {
    action_format_summary(&scene->button_right, &summary, scene_index);
  } else if (highlight == HIGHLIGHT_BUTTON_BOTH) {
    action_format_summary(&scene->button_both, &summary, scene_index);
  } else if (highlight == HIGHLIGHT_BUMP) {
    action_format_summary(&scene->bump, &summary, scene_index);
  } else if (highlight == HIGHLIGHT_EXPRESSION) {
    continuous_format_summary(&scene->expression, &summary, scene_index);
  } else if (highlight == HIGHLIGHT_CV) {
    continuous_format_summary(&scene->cv, &summary, scene_index);
  } else if (highlight == HIGHLIGHT_PROXIMITY) {
    continuous_format_summary(&scene->proximity, &summary, scene_index);
  } else {
    g_text_buf[0] = '\0';
    lv_label_set_text(g_text_label, "");
    return;
  }
  
  // Format with cyan input name (#00FFFF)
  action_summary_format_display(&summary, g_text_buf, sizeof(g_text_buf), 0x00FFFF);
  lv_label_set_recolor(g_text_label, true);
  lv_label_set_text(g_text_label, g_text_buf);
}

// Draw a filled arc segment (pie slice)
static void draw_filled_arc(lv_layer_t *layer, int32_t cx, int32_t cy,
  int32_t outer_r, int32_t inner_r,
  float start_deg, float end_deg, lv_color_t color) {
  float start_rad = start_deg * M_PI / 180.0f;
  float end_rad = end_deg * M_PI / 180.0f;
  
  float arc_length = fabsf(end_rad - start_rad) * outer_r;
  int line_count = (int)(arc_length * 2);
  if (line_count < 16) line_count = 16;
  if (line_count > 48) line_count = 48;
  
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = color;
  line_dsc.opa = LV_OPA_COVER;
  line_dsc.width = 2;
  
  for (int i = 0; i <= line_count; i++) {
    float angle = start_rad + (end_rad - start_rad) * i / line_count;
    lv_point_precise_t p1, p2;
    p1.x = cx + cosf(angle) * inner_r;
    p1.y = cy + sinf(angle) * inner_r;
    p2.x = cx + cosf(angle) * outer_r;
    p2.y = cy + sinf(angle) * outer_r;
    line_dsc.p1 = p1;
    line_dsc.p2 = p2;
    lv_draw_line(layer, &line_dsc);
  }
}

// Custom draw callback for device widget
static void device_draw_event_cb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  
  // Center the device drawing within the widget
  int32_t cx = obj_coords.x1 + lv_area_get_width(&obj_coords) / 2;
  int32_t cy = obj_coords.y1 + lv_area_get_height(&obj_coords) / 2;
  int32_t outer_r = 60;
  int32_t inner_r = 20;
  
  scene_t *scene = scene_get_current();
  bool is_pads_mode = !scene || scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS;
  
  highlight_input_t highlight = g_highlighted_input;
  
  // Draw highlight fill first (behind the lines)
  if (highlight >= HIGHLIGHT_PAD_0 && highlight <= HIGHLIGHT_PAD_7) {
    if (is_pads_mode) {
      // Pads mode: draw filled slice for individual pad
      int slice = highlight - HIGHLIGHT_PAD_0;
      float slice_angle = 360.0f / 8;
      float start = slice * slice_angle - 90.0f;
      float end = start + slice_angle;
      draw_filled_arc(layer, cx, cy, outer_r, inner_r, start, end, lv_color_white());
    } else {
      // Wheel mode: fill entire wheel ring
      draw_filled_arc(layer, cx, cy, outer_r, inner_r, 0.0f, 360.0f, lv_color_white());
    }
  } else if (highlight >= HIGHLIGHT_PAD_8 && highlight <= HIGHLIGHT_PAD_11) {
    // Pads 8-11: draw as small filled circles around the main wheel
    int pad_idx = highlight - HIGHLIGHT_PAD_8;
    float angles[] = { -45.0f, 45.0f, 135.0f, -135.0f };
    float angle_rad = angles[pad_idx] * M_PI / 180.0f;
    int32_t px = cx + (int32_t)(cosf(angle_rad) * (outer_r + 12));
    int32_t py = cy + (int32_t)(sinf(angle_rad) * (outer_r + 12));
    
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = lv_color_white();
    arc_dsc.width = 6;
    arc_dsc.opa = LV_OPA_COVER;
    arc_dsc.center.x = px;
    arc_dsc.center.y = py;
    arc_dsc.radius = 6;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;
    lv_draw_arc(layer, &arc_dsc);
  } else if (highlight == HIGHLIGHT_BUTTON_L) {
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_white();
    rect_dsc.bg_opa = LV_OPA_COVER;
    lv_area_t btn_area = { cx - outer_r - 20, cy - 8, cx - outer_r - 4, cy + 8 };
    lv_draw_rect(layer, &rect_dsc, &btn_area);
  } else if (highlight == HIGHLIGHT_BUTTON_R) {
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_white();
    rect_dsc.bg_opa = LV_OPA_COVER;
    lv_area_t btn_area = { cx + outer_r + 4, cy - 8, cx + outer_r + 20, cy + 8 };
    lv_draw_rect(layer, &rect_dsc, &btn_area);
  } else if (highlight == HIGHLIGHT_BUTTON_BOTH) {
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_white();
    rect_dsc.bg_opa = LV_OPA_COVER;
    lv_area_t btn_l = { cx - outer_r - 20, cy - 8, cx - outer_r - 4, cy + 8 };
    lv_area_t btn_r = { cx + outer_r + 4, cy - 8, cx + outer_r + 20, cy + 8 };
    lv_draw_rect(layer, &rect_dsc, &btn_l);
    lv_draw_rect(layer, &rect_dsc, &btn_r);
  } else if (highlight == HIGHLIGHT_BUMP) {
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = lv_color_white();
    arc_dsc.width = inner_r - 2;
    arc_dsc.opa = LV_OPA_COVER;
    arc_dsc.center.x = cx;
    arc_dsc.center.y = cy;
    arc_dsc.radius = (inner_r - 2) / 2;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;
    lv_draw_arc(layer, &arc_dsc);
  } else if (highlight == HIGHLIGHT_EXPRESSION || highlight == HIGHLIGHT_CV ||
             highlight == HIGHLIGHT_PROXIMITY) {
    // Continuous inputs: highlight bottom area
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_white();
    rect_dsc.bg_opa = LV_OPA_60;
    int32_t y_base = cy + outer_r + 25;
    lv_area_t cont_area = { cx - 40, y_base, cx + 40, y_base + 12 };
    lv_draw_rect(layer, &rect_dsc, &cont_area);
  }
  
  // Draw device outline (lines)
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = lv_color_make(100, 100, 100);
  line_dsc.width = 1;
  line_dsc.opa = LV_OPA_COVER;
  
  // Draw 8 radial dividers only in pads mode (individual pad triggers)
  if (is_pads_mode) {
    for (int i = 0; i < 8; i++) {
      float angle = (i * 45.0f - 90.0f) * M_PI / 180.0f;
      lv_point_precise_t p1, p2;
      p1.x = cx + cosf(angle) * inner_r;
      p1.y = cy + sinf(angle) * inner_r;
      p2.x = cx + cosf(angle) * outer_r;
      p2.y = cy + sinf(angle) * outer_r;
      line_dsc.p1 = p1;
      line_dsc.p2 = p2;
      lv_draw_line(layer, &line_dsc);
    }
  }
  
  // Draw outer circle
  lv_draw_arc_dsc_t arc_dsc;
  lv_draw_arc_dsc_init(&arc_dsc);
  arc_dsc.color = lv_color_make(100, 100, 100);
  arc_dsc.width = 1;
  arc_dsc.opa = LV_OPA_COVER;
  arc_dsc.center.x = cx;
  arc_dsc.center.y = cy;
  arc_dsc.radius = outer_r;
  arc_dsc.start_angle = 0;
  arc_dsc.end_angle = 360;
  lv_draw_arc(layer, &arc_dsc);
  
  // Draw inner circle
  arc_dsc.radius = inner_r;
  lv_draw_arc(layer, &arc_dsc);
  
  // Draw pad 8-11 position indicators (small circles)
  arc_dsc.radius = 6;
  float pad_angles[] = { -45.0f, 45.0f, 135.0f, -135.0f };
  for (int i = 0; i < 4; i++) {
    float angle_rad = pad_angles[i] * M_PI / 180.0f;
    arc_dsc.center.x = cx + (int32_t)(cosf(angle_rad) * (outer_r + 12));
    arc_dsc.center.y = cy + (int32_t)(sinf(angle_rad) * (outer_r + 12));
    lv_draw_arc(layer, &arc_dsc);
  }
  
  // Draw button indicators (rectangles on sides)
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_color = lv_color_black();
  rect_dsc.bg_opa = LV_OPA_TRANSP;
  rect_dsc.border_color = lv_color_make(100, 100, 100);
  rect_dsc.border_width = 1;
  rect_dsc.border_opa = LV_OPA_COVER;
  
  lv_area_t btn_l = { cx - outer_r - 20, cy - 8, cx - outer_r - 4, cy + 8 };
  lv_area_t btn_r = { cx + outer_r + 4, cy - 8, cx + outer_r + 20, cy + 8 };
  lv_draw_rect(layer, &rect_dsc, &btn_l);
  lv_draw_rect(layer, &rect_dsc, &btn_r);
}

// Custom draw callback for status indicators widget
static void status_draw_event_cb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  
  scene_t *scene = scene_get_current();
  
  // Feature names and their enabled status
  static const char* labels[] = { "LFO1", "LFO2", "Prox", "Amb", "Tilt", "S+H" };
  bool enabled[6];
  
  if (scene) {
    enabled[0] = scene->lfo1_config.enabled || lfo_is_enabled(0);
    enabled[1] = scene->lfo2_config.enabled || lfo_is_enabled(1);
    enabled[2] = scene->proximity.enabled;
    enabled[3] = scene->als.enabled;
    enabled[4] = false;  // Tilt not implemented
    enabled[5] = sample_hold_is_enabled();
  } else {
    for (int i = 0; i < 6; i++) enabled[i] = false;
  }
  
  int32_t widget_width = lv_area_get_width(&obj_coords);
  int32_t cx = obj_coords.x1 + widget_width / 2;
  int32_t cy = obj_coords.y1 + 8;
  
  // Calculate spacing for 6 indicators
  int32_t spacing = widget_width / 6;
  int32_t start_x = cx - (spacing * 5) / 2;
  
  lv_color_t green = lv_color_make(0, 200, 0);
  lv_color_t gray = lv_color_make(80, 80, 80);
  
  for (int i = 0; i < 6; i++) {
    int32_t x = start_x + i * spacing;
    
    // Draw circle (filled green if enabled, outline gray if disabled)
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.center.x = x;
    arc_dsc.center.y = cy;
    arc_dsc.radius = 4;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;
    arc_dsc.opa = LV_OPA_COVER;
    
    if (enabled[i]) {
      arc_dsc.color = green;
      arc_dsc.width = 4;  // Filled
    } else {
      arc_dsc.color = gray;
      arc_dsc.width = 1;  // Outline only
    }
    
    lv_draw_arc(layer, &arc_dsc);
    
    // Draw label below the circle
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = enabled[i] ? green : gray;
    label_dsc.font = &chalet_ny_8;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    label_dsc.opa = LV_OPA_COVER;
    label_dsc.text = labels[i];
    
    lv_area_t label_area;
    label_area.x1 = x - 15;
    label_area.x2 = x + 15;
    label_area.y1 = cy + 6;
    label_area.y2 = cy + 18;
    
    lv_draw_label(layer, &label_dsc, &label_area);
  }
}

// Async callback to update display from event context
static void update_highlight_async(void* user_data) {
  highlight_input_t input = (highlight_input_t)(uintptr_t)user_data;
  g_highlighted_input = input;
  
  if (g_device_widget) {
    lv_obj_invalidate(g_device_widget);
  }
  
  update_text_label();
}

// Async callback for release events (starts hide timer)
static void start_hide_timer_async(void* user_data) {
  (void)user_data;
  start_hide_timer();
}

bool inspect_scene_handle_pad(uint8_t pad_id, bool pressed) {
  if (!g_inspect_active) return false;
  
  // Pad 12 is handled separately (back button) - don't consume it here
  if (pad_id == 12) return false;
  
  if (pressed) {
    // Cancel any pending hide timer on press
    cancel_hide_timer();
    
    ESP_LOGI(TAG, "Inspect pad %d pressed", pad_id);
    
    // Map pad_id to highlight enum
    highlight_input_t input = HIGHLIGHT_PAD_0 + pad_id;
    lv_async_call(update_highlight_async, (void*)(uintptr_t)input);
  } else {
    // Start hide timer on release
    lv_async_call(start_hide_timer_async, NULL);
  }
  
  return true;  // Consume the event
}

// Async callback for continuous inputs - update and restart timer
static void continuous_update_async(void* user_data) {
  highlight_input_t input = (highlight_input_t)(uintptr_t)user_data;
  g_highlighted_input = input;
  
  if (g_device_widget) lv_obj_invalidate(g_device_widget);
  update_text_label();
  
  // Restart hide timer on each value change
  start_hide_timer();
}

// Button event handler (no release events, so start timer immediately)
static void button_event_handler(const event_t* event, void* context) {
  (void)context;
  if (!g_inspect_active) return;
  
  highlight_input_t input = HIGHLIGHT_NONE;
  
  switch (event->type) {
    case EVENT_BUTTON_L_PRESS:
      input = HIGHLIGHT_BUTTON_L;
      break;
    case EVENT_BUTTON_R_PRESS:
      input = HIGHLIGHT_BUTTON_R;
      break;
    case EVENT_BUTTON_BOTH_PRESS:
      input = HIGHLIGHT_BUTTON_BOTH;
      break;
    default:
      return;
  }
  
  // Use continuous_update_async since buttons have no release event
  lv_async_call(continuous_update_async, (void*)(uintptr_t)input);
}

// Bump event handler (no release, so start timer immediately)
static void bump_event_handler(const event_t* event, void* context) {
  (void)context;
  if (!g_inspect_active) return;
  
  ESP_LOGI(TAG, "Inspect bump detected");
  // Use continuous_update_async which updates display and starts timer
  lv_async_call(continuous_update_async, (void*)(uintptr_t)HIGHLIGHT_BUMP);
}

// Expression pedal value handler
static void expression_event_handler(const event_t* event, void* context) {
  (void)context;
  if (!g_inspect_active) return;
  
  lv_async_call(continuous_update_async, (void*)(uintptr_t)HIGHLIGHT_EXPRESSION);
}

// CV input value handler
static void cv_event_handler(const event_t* event, void* context) {
  (void)context;
  if (!g_inspect_active) return;
  
  lv_async_call(continuous_update_async, (void*)(uintptr_t)HIGHLIGHT_CV);
}

// Proximity sensor handler
static void proximity_event_handler(const event_t* event, void* context) {
  (void)context;
  if (!g_inspect_active) return;
  
  lv_async_call(continuous_update_async, (void*)(uintptr_t)HIGHLIGHT_PROXIMITY);
}

static void subscribe_events(void) {
  if (g_subscribed) return;
  
  event_bus_subscribe(EVENT_BUTTON_L_PRESS, button_event_handler, NULL);
  event_bus_subscribe(EVENT_BUTTON_R_PRESS, button_event_handler, NULL);
  event_bus_subscribe(EVENT_BUTTON_BOTH_PRESS, button_event_handler, NULL);
  event_bus_subscribe(EVENT_BUMP_DETECTED, bump_event_handler, NULL);
  event_bus_subscribe(EVENT_EXPRESSION_VALUE, expression_event_handler, NULL);
  event_bus_subscribe(EVENT_CV_VALUE, cv_event_handler, NULL);
  event_bus_subscribe(EVENT_SENSOR_PROXIMITY, proximity_event_handler, NULL);
  
  g_subscribed = true;
  ESP_LOGI(TAG, "Subscribed to inspect events");
}

static void unsubscribe_events(void) {
  if (!g_subscribed) return;
  
  event_bus_unsubscribe(EVENT_BUTTON_L_PRESS, button_event_handler);
  event_bus_unsubscribe(EVENT_BUTTON_R_PRESS, button_event_handler);
  event_bus_unsubscribe(EVENT_BUTTON_BOTH_PRESS, button_event_handler);
  event_bus_unsubscribe(EVENT_BUMP_DETECTED, bump_event_handler);
  event_bus_unsubscribe(EVENT_EXPRESSION_VALUE, expression_event_handler);
  event_bus_unsubscribe(EVENT_CV_VALUE, cv_event_handler);
  event_bus_unsubscribe(EVENT_SENSOR_PROXIMITY, proximity_event_handler);
  
  g_subscribed = false;
  ESP_LOGI(TAG, "Unsubscribed from inspect events");
}

// Build inspect widgets on the provided screen
static void build_inspect_widgets(lv_obj_t *screen) {
  g_screen = screen;
  
  // Create custom device widget - centered in upper portion
  g_device_widget = lv_obj_create(screen);
  lv_obj_remove_style_all(g_device_widget);  // Remove defaults first
  lv_obj_set_size(g_device_widget, 200, 170);
  lv_obj_align(g_device_widget, LV_ALIGN_TOP_MID, 0, 15);
  lv_obj_set_style_bg_opa(g_device_widget, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(g_device_widget, LV_OBJ_FLAG_SCROLLABLE);
  
  // Add custom draw callback
  lv_obj_add_event_cb(g_device_widget, device_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  
  // Create text label for assignment display - centered on screen
  g_text_label = lv_label_create(screen);
  lv_obj_set_width(g_text_label, 200);
  lv_obj_align(g_text_label, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_style_text_align(g_text_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(g_text_label, lv_color_make(0, 255, 0), 0);
  lv_label_set_text(g_text_label, "");
  
  // Create status indicators widget at bottom - centered
  g_status_widget = lv_obj_create(screen);
  lv_obj_remove_style_all(g_status_widget);  // Remove defaults first
  lv_obj_set_size(g_status_widget, 220, 30);
  lv_obj_align(g_status_widget, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_opa(g_status_widget, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(g_status_widget, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g_status_widget, status_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  
  // Subscribe to events
  subscribe_events();
  
  // Mark inspect mode as active
  g_inspect_active = true;
  g_highlighted_input = HIGHLIGHT_NONE;
  
  ESP_LOGI(TAG, "Inspect scene screen created");
}

static void cleanup_screen(void) {
  g_inspect_active = false;
  g_highlighted_input = HIGHLIGHT_NONE;
  
  // Cancel hide timer if active
  cancel_hide_timer();
  
  // Unsubscribe from events
  unsubscribe_events();
  
  // Don't delete the screen - it's owned by the menu system
  // Just clear our references
  g_screen = NULL;
  g_device_widget = NULL;
  g_text_label = NULL;
  g_status_widget = NULL;
  
  ESP_LOGD(TAG, "Inspect scene cleanup complete");
}

// Custom back handler for inspect mode
static bool inspect_back_handler(void) {
  if (!g_inspect_active) return false;
  
  // Mark as inactive and unsubscribe from events
  // Don't delete the screen here - let the menu system handle the transition
  g_inspect_active = false;
  g_highlighted_input = HIGHLIGHT_NONE;
  cancel_hide_timer();
  unsubscribe_events();
  menu_set_custom_back_handler(NULL);
  
  // Let menu system handle the actual back navigation
  return false;
}

lv_obj_t* menu_page_inspect_scene_create(void) {
  ESP_LOGD(TAG, "Creating inspect scene page");
  
  // Set up custom back handler
  menu_set_custom_back_handler(inspect_back_handler);
  
  // Create the screen that the menu system will manage
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_remove_style_all(screen);  // Remove all default styles (padding, borders)
  lv_obj_set_size(screen, 240, 240);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);  // Ensure no padding
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling
  
  // Build our widgets directly on this screen
  build_inspect_widgets(screen);
  
  return screen;
}

void menu_page_inspect_scene_cleanup(void) {
  cleanup_screen();
  menu_set_custom_back_handler(NULL);
}
