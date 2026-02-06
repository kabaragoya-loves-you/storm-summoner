#include "plasma.h"
#include "shared_canvas_buffer.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <math.h>
#include <string.h>

#define TAG "PLASMA"

// Animation timing
#define PLASMA_FRAME_MS 33  // ~30 FPS

// Auto-recovery
#define PLASMA_MAX_DRAW_FAILURES 5
static int g_consecutive_draw_failures = 0;

// Sine lookup table: 512 entries covering 0..2*PI
#define SIN_LUT_SIZE 512
#define SIN_LUT_MASK (SIN_LUT_SIZE - 1)
#define TWO_PI 6.283185307f
#define SIN_LUT_SCALE (SIN_LUT_SIZE / TWO_PI)

static float s_sin_lut[SIN_LUT_SIZE];
static bool s_lut_initialized = false;

// LVGL objects
static lv_obj_t *g_plasma_screen = NULL;
static lv_obj_t *g_plasma_canvas = NULL;
static lv_timer_t *g_animation_timer = NULL;
static lv_obj_t *g_previous_screen = NULL;
static bool g_skip_first_invalidate = false;

// Display dimensions
static uint16_t s_disp_width = 240;
static uint16_t s_disp_height = 240;

// Animation time accumulator
static float s_time = 0.0f;

// Previous-row RGB cache for specular finite differences (allocated in PSRAM)
static uint8_t *s_prev_row = NULL;  // 3 bytes per pixel (R, G, B)

// ---------------------------------------------------------------
// Sine LUT helpers
// ---------------------------------------------------------------

static void init_sin_lut(void) {
  if (s_lut_initialized) return;
  for (int i = 0; i < SIN_LUT_SIZE; i++) {
    s_sin_lut[i] = sinf((float)i * (TWO_PI / SIN_LUT_SIZE));
  }
  s_lut_initialized = true;
}

static inline float fast_sin(float angle) {
  float idx = angle * SIN_LUT_SCALE;
  int i = (int)idx;
  float frac = idx - (float)i;
  i &= SIN_LUT_MASK;
  int j = (i + 1) & SIN_LUT_MASK;
  return s_sin_lut[i] + frac * (s_sin_lut[j] - s_sin_lut[i]);
}

static inline float fast_cos(float angle) {
  return fast_sin(angle + 1.5707963f);  // + PI/2
}

// ---------------------------------------------------------------
// RGB565 packing
// ---------------------------------------------------------------

static inline uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) |
    ((uint16_t)(g & 0xFC) << 3) |
    (b >> 3);
}

// ---------------------------------------------------------------
// Clamp float 0..1 to uint8_t 0..255
// ---------------------------------------------------------------

static inline uint8_t clamp8(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 1.0f) return 255;
  return (uint8_t)(v * 255.0f);
}

// ---------------------------------------------------------------
// Plasma render + specular pass
// ---------------------------------------------------------------

static void plasma_render_frame(void) {
  uint16_t *buf = (uint16_t *)shared_canvas_buffer_get();
  if (!buf) return;

  float t = s_time;

  // Per-frame constants
  float sin_t1 = fast_sin(0.148f - t);
  float cos_t2 = fast_cos(0.628f + t);
  float t_offset = t * 0.3f;
  float k_time_term = 2.4f * t;
  float w_time_term = -0.7f * t;

  float inv_w = 4.0f / (float)s_disp_width;
  float inv_h = 4.0f / (float)s_disp_height;

  // We need previous-pixel RGB for specular finite differences
  uint8_t prev_r = 0, prev_g = 0, prev_b = 0;
  bool has_prev_row = (s_prev_row != NULL);

  for (int y = 0; y < s_disp_height; y++) {
    float cy = (float)y * inv_h + t_offset;

    // k depends only on y and time -- precompute for the whole row
    float k = 0.1f + fast_cos(cy + sin_t1) + k_time_term;

    int row_offset = y * s_disp_width;
    prev_r = prev_g = prev_b = 0;

    for (int x = 0; x < s_disp_width; x++) {
      float cx = (float)x * inv_w + t_offset;

      float w = 0.9f + fast_sin(cx + cos_t2) + w_time_term;

      // Distance from center of normalized coord space
      float dx = cx - 2.0f;
      float dy = cy - 2.0f;
      float d = sqrtf(dx * dx + dy * dy);

      float s = 7.0f * fast_cos(d + w) * fast_sin(k + w);

      // Cosine color palette
      float rf = 0.5f + 0.5f * fast_cos(s + 0.2f);
      float gf = 0.5f + 0.5f * fast_cos(s + 0.5f);
      float bf = 0.5f + 0.5f * fast_cos(s + 0.9f);

      uint8_t r = clamp8(rf);
      uint8_t g = clamp8(gf);
      uint8_t b = clamp8(bf);

      // --- Specular enhancement (finite-difference approximation) ---
      // Compute color gradient magnitude from left neighbor and row above
      if (x > 0 || (has_prev_row && y > 0)) {
        // Horizontal difference (from previous pixel in this row)
        int dr_x = (int)r - (int)prev_r;
        int dg_x = (int)g - (int)prev_g;
        int db_x = (int)b - (int)prev_b;
        float grad_x = sqrtf((float)(dr_x * dr_x + dg_x * dg_x +
          db_x * db_x)) / 255.0f;

        float grad_y = 0.0f;
        if (has_prev_row && y > 0) {
          // Vertical difference (from pixel above, cached in prev_row)
          int idx3 = x * 3;
          int dr_y = (int)r - (int)s_prev_row[idx3];
          int dg_y = (int)g - (int)s_prev_row[idx3 + 1];
          int db_y = (int)b - (int)s_prev_row[idx3 + 2];
          grad_y = sqrtf((float)(dr_y * dr_y + dg_y * dg_y +
            db_y * db_y)) / 255.0f;
        }

        // Construct pseudo-normal and extract z component
        float nz = 0.5f / (float)s_disp_height;
        float len = sqrtf(grad_x * grad_x + grad_y * grad_y + nz * nz);
        float spec = (nz / len);
        spec = spec * spec;  // pow(., 2)

        // Apply warm tint + ambient
        float mult = spec + 0.75f;
        float tint_r = mult * 1.0f;
        float tint_g = mult * 0.7f;
        float tint_b = mult * 0.4f;

        // Blend: bias toward the original color so tint is subtle
        // mix = 0.35 tint influence, 0.65 original
        rf = rf * 0.65f + rf * tint_r * 0.35f;
        gf = gf * 0.65f + gf * tint_g * 0.35f;
        bf = bf * 0.65f + bf * tint_b * 0.35f;

        r = clamp8(rf);
        g = clamp8(gf);
        b = clamp8(bf);
      }

      // Cache this pixel for finite differences
      prev_r = r;
      prev_g = g;
      prev_b = b;

      if (has_prev_row) {
        int idx3 = x * 3;
        s_prev_row[idx3] = r;
        s_prev_row[idx3 + 1] = g;
        s_prev_row[idx3 + 2] = b;
      }

      buf[row_offset + x] = pack_rgb565(r, g, b);
    }
  }
}

// ---------------------------------------------------------------
// Animation callback
// ---------------------------------------------------------------

static void plasma_animation_cb(lv_timer_t *timer) {
  if (!g_plasma_canvas || !shared_canvas_buffer_is_valid()) return;

  if (!lv_canvas_get_buf(g_plasma_canvas)) {
    ESP_LOGW(TAG, "Canvas buffer not set yet, skipping draw");
    g_consecutive_draw_failures++;
    if (g_consecutive_draw_failures >= PLASMA_MAX_DRAW_FAILURES) {
      ESP_LOGE(TAG, "Too many draw failures, auto-recovering");
      event_t event = {
        .type = EVENT_UI_ACTION,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS
      };
      event_bus_post(&event);
    }
    return;
  }

  lv_obj_t *canvas_screen = lv_obj_get_screen(g_plasma_canvas);
  if (canvas_screen != lv_screen_active()) {
    ESP_LOGW(TAG, "Canvas not on active screen, skipping draw");
    g_consecutive_draw_failures++;
    if (g_consecutive_draw_failures >= PLASMA_MAX_DRAW_FAILURES) {
      ESP_LOGE(TAG, "Too many draw failures, auto-recovering");
      event_t event = {
        .type = EVENT_UI_ACTION,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS
      };
      event_bus_post(&event);
    }
    return;
  }

  g_consecutive_draw_failures = 0;

  // Advance time
  s_time += (float)PLASMA_FRAME_MS / 1000.0f;

  // Render the plasma frame directly into the shared buffer
  plasma_render_frame();

  if (g_skip_first_invalidate) {
    g_skip_first_invalidate = false;
    return;
  }

  if (g_plasma_canvas && lv_obj_is_valid(g_plasma_canvas))
    lv_obj_invalidate(g_plasma_canvas);
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

void plasma_start(void) {
  ESP_LOGD(TAG, "Starting plasma screensaver...");

  g_consecutive_draw_failures = 0;

  if (!shared_canvas_buffer_is_valid()) {
    ESP_LOGE(TAG, "Shared canvas buffer not available!");
    return;
  }

  // Initialize sine LUT on first use
  init_sin_lut();

  void *shared_buf = shared_canvas_buffer_get();
  s_disp_width = shared_canvas_buffer_get_width();
  s_disp_height = shared_canvas_buffer_get_height();

  ESP_LOGD(TAG, "Using shared canvas buffer at %p (%dx%d)",
    shared_buf, s_disp_width, s_disp_height);

  // Allocate previous-row cache in PSRAM for specular pass
  if (!s_prev_row) {
    s_prev_row = heap_caps_malloc(s_disp_width * 3,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_prev_row) {
      memset(s_prev_row, 0, s_disp_width * 3);
    } else {
      ESP_LOGW(TAG, "Could not allocate prev_row in PSRAM, "
        "specular disabled");
    }
  }

  g_previous_screen = lv_screen_active();

  if (!g_plasma_screen) {
    if (!lv_is_initialized()) {
      ESP_LOGE(TAG, "LVGL not initialized!");
      return;
    }

    g_plasma_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_plasma_screen, s_disp_width, s_disp_height);
    lv_obj_set_style_bg_color(g_plasma_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_plasma_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_plasma_screen, 0, 0);

    g_plasma_canvas = lv_canvas_create(g_plasma_screen);
    lv_obj_set_size(g_plasma_canvas, s_disp_width, s_disp_height);
    lv_obj_center(g_plasma_canvas);
  }

  if (g_plasma_canvas && shared_buf) {
    ESP_LOGD(TAG, "Attaching shared buffer to plasma canvas");
    shared_canvas_buffer_clear();
    lv_canvas_set_buffer(g_plasma_canvas, shared_buf,
      s_disp_width, s_disp_height,
      shared_canvas_buffer_get_format());
  }

  // Reset animation time
  s_time = 0.0f;

  lv_screen_load(g_plasma_screen);
  g_skip_first_invalidate = true;

  if (!g_animation_timer) {
    g_animation_timer = lv_timer_create(plasma_animation_cb,
      PLASMA_FRAME_MS, NULL);
  } else {
    lv_timer_resume(g_animation_timer);
  }

  ESP_LOGD(TAG, "Plasma screensaver started");
}

void plasma_stop(void) {
  ESP_LOGD(TAG, "Stopping plasma screensaver...");

  if (g_animation_timer) lv_timer_pause(g_animation_timer);

  if (g_previous_screen && lv_obj_is_valid(g_previous_screen)) {
    lv_screen_load(g_previous_screen);
    g_previous_screen = NULL;
  } else {
    ESP_LOGW(TAG, "Previous screen invalid or null, cannot restore");
  }

  ESP_LOGD(TAG, "Plasma screensaver stopped");
}

void plasma_cleanup(void) {
  ESP_LOGD(TAG, "Cleaning up plasma resources...");

  if (g_animation_timer) {
    lv_timer_delete(g_animation_timer);
    g_animation_timer = NULL;
  }

  if (g_plasma_screen) {
    lv_obj_delete(g_plasma_screen);
    g_plasma_screen = NULL;
    g_plasma_canvas = NULL;
  }

  if (s_prev_row) {
    heap_caps_free(s_prev_row);
    s_prev_row = NULL;
  }

  g_previous_screen = NULL;

  ESP_LOGD(TAG, "Plasma cleanup complete");
}
