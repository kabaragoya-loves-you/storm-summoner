#include "lv_plasma.h"
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"

#define TAG "LV_PLASMA"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

//=============================================================================
// PLASMA WIDGET INTERNALS - Performance tuning
//=============================================================================

// Animation timing
#define PLASMA_ANIMATION_INTERVAL_MS  50    // Timer interval (lower = smoother, higher = faster)

// Main tendril glow effect
#define PLASMA_GLOW_PASSES            2     // Glow layers (1-4, fewer = faster)
#define PLASMA_GLOW_OUTER_WIDTH       5     // Outer glow line width
#define PLASMA_GLOW_OUTER_OPA         LV_OPA_40   // Outer glow opacity
#define PLASMA_CORE_WIDTH             2     // Core line width

// Branch tendrils
#define PLASMA_BRANCH_PASSES          1     // Branch passes (1-2)
#define PLASMA_BRANCH_WIDTH           2     // Branch line width
#define PLASMA_BRANCH_OPA             LV_OPA_60   // Branch opacity

//=============================================================================

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_plasma_draw_event_cb(lv_event_t * e);
static void lv_plasma_destructor_event_cb(lv_event_t * e);
static void animation_timer_cb(lv_timer_t * timer);
static float noise1d(float x);
static void draw_tendril(lv_layer_t * layer, lv_plasma_data_t * data, 
                         lv_plasma_tendril_t * tendril, int32_t cx, int32_t cy);
static void draw_branch(lv_layer_t * layer, lv_plasma_data_t * data,
                        int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                        float phase, int depth);

/**********************
 *  STATIC FUNCTIONS
 **********************/

// Simple 1D noise function for tendril displacement
static float noise1d(float x) {
  // Hash-based noise
  int xi = (int)floorf(x);
  float xf = x - xi;
  
  // Smooth interpolation
  float u = xf * xf * (3.0f - 2.0f * xf);
  
  // Hash values
  float h0 = sinf(xi * 127.1f) * 43758.5453f;
  h0 = h0 - floorf(h0);
  float h1 = sinf((xi + 1) * 127.1f) * 43758.5453f;
  h1 = h1 - floorf(h1);
  
  return h0 + (h1 - h0) * u;
}

// Draw a single tendril with glow effect
static void draw_tendril(lv_layer_t * layer, lv_plasma_data_t * data,
                         lv_plasma_tendril_t * tendril, int32_t cx, int32_t cy) {
  if (!tendril->active) return;
  
  int32_t tx = tendril->target_x;
  int32_t ty = tendril->target_y;
  
  // Calculate direction vector
  float dx = tx - cx;
  float dy = ty - cy;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) return;
  
  // Normalize and get perpendicular
  float nx = dx / len;
  float ny = dy / len;
  float px = -ny;  // Perpendicular
  float py = nx;
  
  // Generate path points with noise displacement
  int seg = data->segment_count;
  float *path_x = malloc((seg + 1) * sizeof(float));
  float *path_y = malloc((seg + 1) * sizeof(float));
  if (!path_x || !path_y) {
    free(path_x);
    free(path_y);
    return;
  }
  
  // First and last points are fixed
  path_x[0] = cx;
  path_y[0] = cy;
  path_x[seg] = tx;
  path_y[seg] = ty;
  
  // Intermediate points with noise displacement
  for (int i = 1; i < seg; i++) {
    float t = (float)i / seg;
    float base_x = cx + dx * t;
    float base_y = cy + dy * t;
    
    // Noise-based displacement (varies with time for animation)
    float noise_input = i * 0.5f + data->time * data->animation_speed + tendril->phase;
    float displacement = (noise1d(noise_input) - 0.5f) * 2.0f * data->displacement;
    
    // Taper displacement near endpoints
    float taper = 4.0f * t * (1.0f - t);  // Peaks at 0.5
    displacement *= taper;
    
    path_x[i] = base_x + px * displacement;
    path_y[i] = base_y + py * displacement;
  }
  
  // Draw glow passes (outer to inner) - configurable for performance
  typedef struct { int width; lv_opa_t opa; lv_color_t color; } pass_t;
  
#if PLASMA_GLOW_PASSES >= 4
  pass_t passes[] = {
    {PLASMA_GLOW_OUTER_WIDTH + 4, LV_OPA_20, data->glow_color},
    {PLASMA_GLOW_OUTER_WIDTH + 2, LV_OPA_30, data->glow_color},
    {PLASMA_GLOW_OUTER_WIDTH, PLASMA_GLOW_OUTER_OPA, data->glow_color},
    {PLASMA_CORE_WIDTH, LV_OPA_COVER, data->core_color}
  };
#elif PLASMA_GLOW_PASSES >= 3
  pass_t passes[] = {
    {PLASMA_GLOW_OUTER_WIDTH + 2, LV_OPA_30, data->glow_color},
    {PLASMA_GLOW_OUTER_WIDTH, PLASMA_GLOW_OUTER_OPA, data->glow_color},
    {PLASMA_CORE_WIDTH, LV_OPA_COVER, data->core_color}
  };
#elif PLASMA_GLOW_PASSES >= 2
  pass_t passes[] = {
    {PLASMA_GLOW_OUTER_WIDTH, PLASMA_GLOW_OUTER_OPA, data->glow_color},
    {PLASMA_CORE_WIDTH, LV_OPA_COVER, data->core_color}
  };
#else
  pass_t passes[] = {
    {PLASMA_CORE_WIDTH, LV_OPA_COVER, data->core_color}
  };
#endif
  int num_passes = sizeof(passes) / sizeof(passes[0]);
  
  for (int p = 0; p < num_passes; p++) {
    for (int i = 0; i < seg; i++) {
      lv_draw_line_dsc_t line_dsc;
      lv_draw_line_dsc_init(&line_dsc);
      line_dsc.color = passes[p].color;
      line_dsc.opa = passes[p].opa;
      line_dsc.width = passes[p].width;
      line_dsc.round_start = 1;
      line_dsc.round_end = 1;
      line_dsc.p1.x = (int32_t)path_x[i];
      line_dsc.p1.y = (int32_t)path_y[i];
      line_dsc.p2.x = (int32_t)path_x[i + 1];
      line_dsc.p2.y = (int32_t)path_y[i + 1];
      
      lv_draw_line(layer, &line_dsc);
    }
  }
  
  // Draw branches
  for (int b = 0; b < tendril->branch_count && b < 3; b++) {
    // Pick a point along the path for branch origin
    int branch_seg = 2 + (b * seg / 4);  // Spread branches along tendril
    if (branch_seg >= seg) branch_seg = seg - 1;
    
    float bx = path_x[branch_seg];
    float by = path_y[branch_seg];
    
    // Branch target: offset from main path
    float branch_phase = tendril->phase + b * 2.0f + data->time * 0.5f;
    float branch_angle = noise1d(branch_phase) * M_PI - M_PI / 2;
    float branch_len = len * 0.3f * (0.5f + noise1d(branch_phase + 10.0f) * 0.5f);
    
    // Calculate branch end relative to path direction at that point
    float local_dx = path_x[branch_seg + 1] - path_x[branch_seg];
    float local_dy = path_y[branch_seg + 1] - path_y[branch_seg];
    float local_len = sqrtf(local_dx * local_dx + local_dy * local_dy);
    if (local_len < 0.1f) local_len = 1.0f;
    
    float local_nx = local_dx / local_len;
    float local_ny = local_dy / local_len;
    
    // Rotate by branch angle
    float cos_a = cosf(branch_angle);
    float sin_a = sinf(branch_angle);
    float branch_dx = (local_nx * cos_a - local_ny * sin_a) * branch_len;
    float branch_dy = (local_nx * sin_a + local_ny * cos_a) * branch_len;
    
    draw_branch(layer, data, (int32_t)bx, (int32_t)by,
                (int32_t)(bx + branch_dx), (int32_t)(by + branch_dy),
                branch_phase, 0);
  }
  
  free(path_x);
  free(path_y);
}

// Draw a branch (smaller tendril)
static void draw_branch(lv_layer_t * layer, lv_plasma_data_t * data,
                        int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                        float phase, int depth) {
  if (depth > 1) return;  // Limit recursion
  
  float dx = x2 - x1;
  float dy = y2 - y1;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 5.0f) return;
  
  float nx = dx / len;
  float ny = dy / len;
  float px = -ny;
  float py = nx;
  
  // Fewer segments for branches
  int seg = 4;
  float path_x[5], path_y[5];
  
  path_x[0] = x1;
  path_y[0] = y1;
  path_x[seg] = x2;
  path_y[seg] = y2;
  
  for (int i = 1; i < seg; i++) {
    float t = (float)i / seg;
    float base_x = x1 + dx * t;
    float base_y = y1 + dy * t;
    
    float noise_input = i * 0.7f + data->time * data->animation_speed * 1.5f + phase;
    float displacement = (noise1d(noise_input) - 0.5f) * data->displacement * 0.6f;
    float taper = 4.0f * t * (1.0f - t);
    displacement *= taper;
    
    path_x[i] = base_x + px * displacement;
    path_y[i] = base_y + py * displacement;
  }
  
  // Branch glow passes - configurable
  typedef struct { int width; lv_opa_t opa; } bpass_t;
#if PLASMA_BRANCH_PASSES >= 2
  bpass_t passes[] = {
    {PLASMA_BRANCH_WIDTH + 2, LV_OPA_30},
    {PLASMA_BRANCH_WIDTH, PLASMA_BRANCH_OPA}
  };
  int num_passes = 2;
#else
  bpass_t passes[] = {
    {PLASMA_BRANCH_WIDTH, PLASMA_BRANCH_OPA}
  };
  int num_passes = 1;
#endif
  
  for (int p = 0; p < num_passes; p++) {
    for (int i = 0; i < seg; i++) {
      lv_draw_line_dsc_t line_dsc;
      lv_draw_line_dsc_init(&line_dsc);
      line_dsc.color = data->glow_color;
      line_dsc.opa = passes[p].opa;
      line_dsc.width = passes[p].width;
      line_dsc.round_start = 1;
      line_dsc.round_end = 1;
      line_dsc.p1.x = (int32_t)path_x[i];
      line_dsc.p1.y = (int32_t)path_y[i];
      line_dsc.p2.x = (int32_t)path_x[i + 1];
      line_dsc.p2.y = (int32_t)path_y[i + 1];
      
      lv_draw_line(layer, &line_dsc);
    }
  }
}

/**********************
 *   WIDGET FUNCTIONS
 **********************/

lv_obj_t * lv_plasma_create(lv_obj_t * parent) {
  lv_obj_t * obj = lv_obj_create(parent);
  if (obj == NULL) return NULL;
  
  lv_plasma_data_t * plasma_data = malloc(sizeof(lv_plasma_data_t));
  if (plasma_data == NULL) {
    lv_obj_delete(obj);
    return NULL;
  }
  
  // Initialize defaults
  plasma_data->center_x = 0;
  plasma_data->center_y = 0;
  plasma_data->segment_count = LV_PLASMA_DEFAULT_SEGMENTS;
  plasma_data->displacement = 15.0f;
  plasma_data->animation_speed = 3.0f;
  plasma_data->time = 0.0f;
  plasma_data->core_color = lv_color_make(200, 150, 255);  // Light purple
  plasma_data->glow_color = lv_color_make(100, 50, 200);   // Deep purple
  plasma_data->animation_timer = NULL;
  plasma_data->auto_animate = true;
  plasma_data->state_cb = NULL;
  plasma_data->target_cb = NULL;
  plasma_data->cb_user_data = NULL;
  
  // Initialize all tendrils as inactive
  for (int i = 0; i < LV_PLASMA_MAX_TENDRILS; i++) {
    plasma_data->tendrils[i].active = false;
    plasma_data->tendrils[i].target_x = 0;
    plasma_data->tendrils[i].target_y = 0;
    plasma_data->tendrils[i].phase = (float)i * 1.7f;  // Different phase per tendril
    plasma_data->tendrils[i].branch_count = LV_PLASMA_DEFAULT_BRANCHES;
  }
  
  lv_obj_set_user_data(obj, plasma_data);
  
  // Widget styling
  lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_plasma_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(obj, lv_plasma_destructor_event_cb, LV_EVENT_DELETE, NULL);
  
  // Create animation timer
  if (plasma_data->auto_animate) {
    plasma_data->animation_timer = lv_timer_create(animation_timer_cb, PLASMA_ANIMATION_INTERVAL_MS, obj);
    if (plasma_data->animation_timer) {
      lv_timer_set_repeat_count(plasma_data->animation_timer, -1);
    }
  }
  
  ESP_LOGI(TAG, "Plasma globe widget created");
  return obj;
}

static void lv_plasma_destructor_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (plasma_data) {
    if (plasma_data->animation_timer) {
      lv_timer_delete(plasma_data->animation_timer);
    }
    free(plasma_data);
    lv_obj_set_user_data(obj, NULL);
  }
}

static void lv_plasma_draw_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (!plasma_data) return;
  
  lv_layer_t * layer = lv_event_get_layer(e);
  
  // Get object coordinates for center calculation
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  
  // Calculate actual center (relative to widget + offset)
  int32_t cx = obj_coords.x1 + plasma_data->center_x;
  int32_t cy = obj_coords.y1 + plasma_data->center_y;
  
  // Update tendril states from callback if set
  if (plasma_data->state_cb) {
    for (int i = 0; i < LV_PLASMA_MAX_TENDRILS; i++) {
      bool is_active = plasma_data->state_cb(i, plasma_data->cb_user_data);
      plasma_data->tendrils[i].active = is_active;
      
      // Update target position if active and target callback is set
      if (is_active && plasma_data->target_cb) {
        plasma_data->target_cb(i, &plasma_data->tendrils[i].target_x,
                               &plasma_data->tendrils[i].target_y,
                               plasma_data->cb_user_data);
      }
    }
  }
  
  // Draw each active tendril
  for (int i = 0; i < LV_PLASMA_MAX_TENDRILS; i++) {
    if (plasma_data->tendrils[i].active) {
      draw_tendril(layer, plasma_data, &plasma_data->tendrils[i], cx, cy);
    }
  }
}

static void animation_timer_cb(lv_timer_t * timer) {
  lv_obj_t * obj = (lv_obj_t *)lv_timer_get_user_data(timer);
  if (!obj) return;
  
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (!plasma_data) return;
  
  // Advance animation time
  plasma_data->time += 0.033f;  // ~30ms per frame
  
  // Always invalidate when using state callback (need to poll touch state)
  // Otherwise only invalidate if tendrils are active
  if (plasma_data->state_cb) {
    lv_obj_invalidate(obj);
  } else {
    bool any_active = false;
    for (int i = 0; i < LV_PLASMA_MAX_TENDRILS; i++) {
      if (plasma_data->tendrils[i].active) {
        any_active = true;
        break;
      }
    }
    if (any_active) {
      lv_obj_invalidate(obj);
    }
  }
}

/**********************
 *   SETTER FUNCTIONS
 **********************/

void lv_plasma_set_center(lv_obj_t * obj, int32_t x, int32_t y) {
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (plasma_data) {
    plasma_data->center_x = x;
    plasma_data->center_y = y;
    lv_obj_invalidate(obj);
  }
}

void lv_plasma_activate_tendril(lv_obj_t * obj, uint8_t index, int32_t target_x, int32_t target_y) {
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (plasma_data && index < LV_PLASMA_MAX_TENDRILS) {
    plasma_data->tendrils[index].active = true;
    plasma_data->tendrils[index].target_x = target_x;
    plasma_data->tendrils[index].target_y = target_y;
    // Randomize phase slightly on activation for variety
    plasma_data->tendrils[index].phase += 0.1f;
    lv_obj_invalidate(obj);
    ESP_LOGD(TAG, "Tendril %d activated at (%ld, %ld)", index, target_x, target_y);
  }
}

void lv_plasma_deactivate_tendril(lv_obj_t * obj, uint8_t index) {
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (plasma_data && index < LV_PLASMA_MAX_TENDRILS) {
    plasma_data->tendrils[index].active = false;
    lv_obj_invalidate(obj);
    ESP_LOGD(TAG, "Tendril %d deactivated", index);
  }
}

void lv_plasma_set_style(lv_obj_t * obj, uint8_t segments, float displacement) {
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (plasma_data) {
    plasma_data->segment_count = segments;
    plasma_data->displacement = displacement;
    lv_obj_invalidate(obj);
  }
}

void lv_plasma_set_colors(lv_obj_t * obj, lv_color_t core_color, lv_color_t glow_color) {
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (plasma_data) {
    plasma_data->core_color = core_color;
    plasma_data->glow_color = glow_color;
    lv_obj_invalidate(obj);
  }
}

void lv_plasma_set_animation_speed(lv_obj_t * obj, float speed) {
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (plasma_data) {
    plasma_data->animation_speed = speed;
  }
}

void lv_plasma_set_auto_animate(lv_obj_t * obj, bool enable) {
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (!plasma_data) return;
  
  if (enable && !plasma_data->animation_timer) {
    plasma_data->animation_timer = lv_timer_create(animation_timer_cb, PLASMA_ANIMATION_INTERVAL_MS, obj);
    if (plasma_data->animation_timer) {
      lv_timer_set_repeat_count(plasma_data->animation_timer, -1);
    }
  } else if (!enable && plasma_data->animation_timer) {
    lv_timer_delete(plasma_data->animation_timer);
    plasma_data->animation_timer = NULL;
  }
  
  plasma_data->auto_animate = enable;
}

void lv_plasma_set_state_cb(lv_obj_t * obj, lv_plasma_state_cb_t state_cb,
                            lv_plasma_target_cb_t target_cb, void* user_data) {
  lv_plasma_data_t * plasma_data = lv_obj_get_user_data(obj);
  if (plasma_data) {
    plasma_data->state_cb = state_cb;
    plasma_data->target_cb = target_cb;
    plasma_data->cb_user_data = user_data;
  }
}

