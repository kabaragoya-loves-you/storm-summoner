#include "lvgl.h"
#include "ui.h"
#include "menu.h"
#include "menu_pages.h"
#include "display_driver.h"
#include "touch.h"
#include "touch_thresholds.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "SCREW_CAL_UI"

#define UPDATE_INTERVAL_MS 40
#define IDLE_SAMPLE_MS 2000
// Sample elevation in the EARLY window before the filter climb dominates.
// Hold bar still runs longer so the user gets a clear "keep holding" cue.
#define HOLD_EARLY_MS 200
#define HOLD_TARGET_MS 1200
#define HOLD_COUNT 4
#define TAP_COUNT 4
#define TAP_EARLY_MS 120
#define STEP_TIMEOUT_MS 10000
#define MIN_TOUCH_ELEV 200
#define TOUCH_DETECT_ELEV 80
#define RELEASE_SETTLE_MS 400
#define POST_SETTLE_TIMEOUT_MS 4000
#define POST_SETTLE_NEAR_PCT 8
#define POST_SETTLE_STABLE_MS 300

typedef enum {
  STEP_IDLE = 0,
  STEP_HOLD_WAIT,
  STEP_HOLD_ACTIVE,
  STEP_HOLD_RELEASE,
  STEP_TAP_WAIT,
  STEP_TAP_RELEASE,
  STEP_POST_SETTLE,
  STEP_DONE_OK,
  STEP_DONE_FAIL,
} screw_step_t;

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_title_label = NULL;
static lv_obj_t *g_instruction_label = NULL;
static lv_obj_t *g_progress_label = NULL;
static lv_obj_t *g_hold_bar = NULL;
static lv_timer_t *g_update_timer = NULL;

static bool g_running = false;
static screw_step_t g_step = STEP_IDLE;
static uint32_t g_step_start_ms = 0;
static uint32_t g_idle_sum = 0;
static uint32_t g_idle_count = 0;
static uint32_t g_idle_baseline = 0;
static uint32_t g_hold_elevs[HOLD_COUNT];
static int g_hold_index = 0;
static uint64_t g_hold_sample_sum = 0;
static uint32_t g_hold_sample_count = 0;
static uint32_t g_tap_elevs[TAP_COUNT];
static int g_tap_index = 0;
static uint64_t g_tap_sample_sum = 0;
static uint32_t g_tap_sample_count = 0;
static bool g_tap_early_done = false;
static bool g_bench_reset_done = false;
static uint32_t g_near_idle_since_ms = 0;
static bool g_pending_bench_reset = false;

static void wizard_update_cb(lv_timer_t *timer);
static void return_to_menu_cb(lv_timer_t *t);
static void finish_ok_begin_settle(void);
static void finish_success_ui(void);
static void finish_fail(const char *msg);
static void set_instruction(const char *text);
static void set_progress(const char *text);
static uint32_t now_ms(void);
static uint32_t median_u32(uint32_t *vals, int count);
static uint32_t min_u32(uint32_t *vals, int count);
static int elev_cmp(const void *a, const void *b);
static void request_bench_reset_when_idle(void);

static uint32_t now_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void set_instruction(const char *text) {
  if (g_instruction_label) lv_label_set_text(g_instruction_label, text);
}

static void set_progress(const char *text) {
  if (g_progress_label) lv_label_set_text(g_progress_label, text);
}

static int elev_cmp(const void *a, const void *b) {
  uint32_t va = *(const uint32_t *)a;
  uint32_t vb = *(const uint32_t *)b;
  return (va > vb) - (va < vb);
}

static uint32_t median_u32(uint32_t *vals, int count) {
  if (count <= 0) return 0;
  uint32_t tmp[HOLD_COUNT];
  if (count > HOLD_COUNT) count = HOLD_COUNT;
  memcpy(tmp, vals, sizeof(uint32_t) * (size_t)count);
  qsort(tmp, (size_t)count, sizeof(uint32_t), elev_cmp);
  return tmp[count / 2];
}

static uint32_t min_u32(uint32_t *vals, int count) {
  if (count <= 0) return 0;
  uint32_t m = vals[0];
  for (int i = 1; i < count; i++) {
    if (vals[i] < m) m = vals[i];
  }
  return m;
}

// Between guided presses, snap HW benchmark to idle so the next sample's
// (smooth - bench) is the real finger delta the active_thresh compares against.
static void request_bench_reset_when_idle(void) {
  g_pending_bench_reset = true;
}

static void advance_hold_or_taps(void) {
  request_bench_reset_when_idle();
  g_hold_index++;
  if (g_hold_index < HOLD_COUNT) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Hold %d/%d", g_hold_index + 1, HOLD_COUNT);
    set_progress(buf);
    set_instruction("Hold screw");
    if (g_hold_bar) {
      lv_obj_remove_flag(g_hold_bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
    }
    g_step = STEP_HOLD_WAIT;
    g_step_start_ms = now_ms();
    g_hold_sample_sum = 0;
    g_hold_sample_count = 0;
    return;
  }

  g_tap_index = 0;
  set_progress("Tap 1/4");
  set_instruction("Tap screw");
  if (g_hold_bar) lv_obj_add_flag(g_hold_bar, LV_OBJ_FLAG_HIDDEN);
  g_step = STEP_TAP_WAIT;
  g_step_start_ms = now_ms();
  g_tap_sample_sum = 0;
  g_tap_sample_count = 0;
  g_tap_early_done = false;
}

static void finish_success_ui(void) {
  g_step = STEP_DONE_OK;
  g_running = false;
  if (g_update_timer) {
    lv_timer_delete(g_update_timer);
    g_update_timer = NULL;
  }
  touch_screw_calib_set_active(false);
  touch_clear_pressed_state(12);

  if (g_hold_bar) lv_obj_add_flag(g_hold_bar, LV_OBJ_FLAG_HIDDEN);
  set_instruction("Done!");
  set_progress(LV_SYMBOL_OK);
  if (g_instruction_label)
    lv_obj_set_style_text_color(g_instruction_label, lv_color_make(0, 255, 128), 0);

  lv_timer_t *t = lv_timer_create(return_to_menu_cb, 700, NULL);
  if (t) lv_timer_set_repeat_count(t, 1);
}

static void finish_ok_begin_settle(void) {
  // Taps are short → less filter climb. They drive the stored elevation.
  // Holds only prove sustained contact is possible (presence check).
  uint32_t hold_med = median_u32(g_hold_elevs, HOLD_COUNT);
  uint32_t hold_min = min_u32(g_hold_elevs, HOLD_COUNT);
  uint32_t tap_med = median_u32(g_tap_elevs, TAP_COUNT);
  uint32_t tap_min = min_u32(g_tap_elevs, TAP_COUNT);

  if (hold_min < MIN_TOUCH_ELEV) {
    finish_fail("Hold too weak");
    return;
  }

  uint32_t elev = tap_med;
  if (elev < MIN_TOUCH_ELEV) {
    // Taps missed; fall back to quietest early-hold sample
    elev = hold_min;
    ESP_LOGW(TAG, "Tap elev weak (%u); falling back to hold_min=%u",
      (unsigned)tap_med, (unsigned)hold_min);
  } else if (tap_min >= MIN_TOUCH_ELEV && tap_min < elev) {
    // Bias toward the quieter tap so one climbing tap doesn't dominate
    elev = (elev + tap_min) / 2;
  }

  if (elev < MIN_TOUCH_ELEV) {
    finish_fail("Signal too weak");
    return;
  }

  ESP_LOGI(TAG, "Screw calib elev: tap_med=%u tap_min=%u hold_med=%u hold_min=%u -> %u",
    (unsigned)tap_med, (unsigned)tap_min, (unsigned)hold_med, (unsigned)hold_min,
    (unsigned)elev);

  esp_err_t ret = touch_screw_calib_apply(g_idle_baseline, elev);
  if (ret != ESP_OK) {
    finish_fail("Save failed");
    return;
  }

  ESP_LOGI(TAG, "Screw calib applied; settling pad 12 (baseline=%u)",
    (unsigned)g_idle_baseline);

  set_instruction("Don't touch");
  set_progress("Settling...");
  if (g_hold_bar) lv_obj_add_flag(g_hold_bar, LV_OBJ_FLAG_HIDDEN);
  g_bench_reset_done = false;
  g_near_idle_since_ms = 0;
  g_step = STEP_POST_SETTLE;
  g_step_start_ms = now_ms();
}

static void finish_fail(const char *msg) {
  ESP_LOGW(TAG, "Screw calib failed: %s", msg ? msg : "unknown");
  g_step = STEP_DONE_FAIL;
  g_running = false;
  if (g_update_timer) {
    lv_timer_delete(g_update_timer);
    g_update_timer = NULL;
  }
  touch_screw_calib_set_active(false);
  touch_clear_pressed_state(12);

  if (g_hold_bar) lv_obj_add_flag(g_hold_bar, LV_OBJ_FLAG_HIDDEN);
  set_instruction(msg ? msg : "Failed");
  set_progress(LV_SYMBOL_CLOSE);
  if (g_instruction_label)
    lv_obj_set_style_text_color(g_instruction_label, lv_color_make(255, 80, 80), 0);

  lv_timer_t *t = lv_timer_create(return_to_menu_cb, 1200, NULL);
  if (t) lv_timer_set_repeat_count(t, 1);
}

static void return_to_menu_cb(lv_timer_t *t) {
  menu_set_restore_focus(1);  // Screw Calibrate is index 1 after Calibrate
  menu_navigate_back_then_to(1, "Touch", menu_page_touch_create);
  lv_timer_delete(t);
}

static void wizard_start(void) {
  memset(g_hold_elevs, 0, sizeof(g_hold_elevs));
  memset(g_tap_elevs, 0, sizeof(g_tap_elevs));
  g_idle_sum = 0;
  g_idle_count = 0;
  g_idle_baseline = 0;
  g_hold_index = 0;
  g_hold_sample_sum = 0;
  g_hold_sample_count = 0;
  g_tap_index = 0;
  g_tap_sample_sum = 0;
  g_tap_sample_count = 0;
  g_tap_early_done = false;
  g_bench_reset_done = false;
  g_near_idle_since_ms = 0;
  g_pending_bench_reset = false;
  g_step = STEP_IDLE;
  g_step_start_ms = now_ms();
  g_running = true;

  touch_screw_calib_set_active(true);
  set_instruction("Don't touch");
  set_progress("Idle...");
  if (g_hold_bar) {
    lv_obj_add_flag(g_hold_bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
  }

  g_update_timer = lv_timer_create(wizard_update_cb, UPDATE_INTERVAL_MS, NULL);
  ESP_LOGI(TAG, "Screw calibration wizard started");
}

static void wizard_update_cb(lv_timer_t *timer) {
  (void)timer;
  if (!g_running || !g_screen) return;

  uint32_t smooth = 0;
  uint32_t bench = 0;
  if (touch_pad12_read_smooth_bench(&smooth, &bench) != ESP_OK) return;

  uint32_t elapsed = now_ms() - g_step_start_ms;

  // HW active_thresh compares smooth vs live benchmark. Measure that delta —
  // smooth-vs-idle_baseline was 25k+ even in the first samples (filter climb).
  int32_t hw_delta = (int32_t)smooth - (int32_t)bench;
  if (hw_delta < 0) hw_delta = 0;
  int32_t vs_idle = (g_idle_baseline > 0)
    ? ((int32_t)smooth - (int32_t)g_idle_baseline)
    : 0;
  bool touching = (hw_delta > (int32_t)TOUCH_DETECT_ELEV) ||
    (vs_idle > (int32_t)TOUCH_DETECT_ELEV);

  // Between presses: reset bench once we're near idle so the next edge is clean
  if (g_pending_bench_reset && g_idle_baseline > 0) {
    uint32_t slack = (g_idle_baseline * POST_SETTLE_NEAR_PCT) / 100;
    if (slack < 200) slack = 200;
    bool near = (smooth + slack >= g_idle_baseline) &&
      (smooth <= g_idle_baseline + slack);
    if (near && hw_delta < (int32_t)TOUCH_DETECT_ELEV) {
      if (touch_pad12_reset_benchmark() == ESP_OK)
        g_pending_bench_reset = false;
    }
  }

  switch (g_step) {
    case STEP_IDLE: {
      g_idle_sum += smooth;
      g_idle_count++;
      if (elapsed >= IDLE_SAMPLE_MS) {
        if (g_idle_count == 0) {
          finish_fail("No samples");
          return;
        }
        g_idle_baseline = g_idle_sum / g_idle_count;
        ESP_LOGI(TAG, "Idle baseline=%u (n=%u)",
          (unsigned)g_idle_baseline, (unsigned)g_idle_count);
        touch_pad12_reset_benchmark();
        char buf[32];
        snprintf(buf, sizeof(buf), "Hold 1/%d", HOLD_COUNT);
        set_progress(buf);
        set_instruction("Hold screw");
        if (g_hold_bar) {
          lv_obj_remove_flag(g_hold_bar, LV_OBJ_FLAG_HIDDEN);
          lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
        }
        g_hold_index = 0;
        g_hold_sample_sum = 0;
        g_hold_sample_count = 0;
        g_step = STEP_HOLD_WAIT;
        g_step_start_ms = now_ms();
      }
      break;
    }

    case STEP_HOLD_WAIT: {
      if (elapsed > STEP_TIMEOUT_MS) {
        finish_fail("Timed out");
        return;
      }
      if (touching) {
        g_hold_sample_sum = 0;
        g_hold_sample_count = 0;
        g_step = STEP_HOLD_ACTIVE;
        g_step_start_ms = now_ms();
      }
      break;
    }

    case STEP_HOLD_ACTIVE: {
      int bar = (int)((elapsed * 100) / HOLD_TARGET_MS);
      if (bar > 100) bar = 100;
      if (g_hold_bar) lv_bar_set_value(g_hold_bar, bar, LV_ANIM_OFF);

      if (!touching) {
        set_instruction("Hold screw");
        if (g_hold_bar) lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
        g_step = STEP_HOLD_WAIT;
        g_step_start_ms = now_ms();
        g_hold_sample_sum = 0;
        g_hold_sample_count = 0;
        break;
      }

      // Early HW delta only — what active_thresh actually sees at press edge
      if (elapsed < HOLD_EARLY_MS && hw_delta > 0) {
        g_hold_sample_sum += (uint32_t)hw_delta;
        g_hold_sample_count++;
      }

      if (elapsed >= HOLD_TARGET_MS) {
        uint32_t early = 0;
        if (g_hold_sample_count > 0)
          early = (uint32_t)(g_hold_sample_sum / g_hold_sample_count);
        g_hold_elevs[g_hold_index] = early;
        ESP_LOGI(TAG, "Hold %d hw_delta=%u (n=%u, vs_idle=%d)",
          g_hold_index + 1, (unsigned)early, (unsigned)g_hold_sample_count,
          (int)vs_idle);
        set_instruction("Release");
        if (g_hold_bar) lv_bar_set_value(g_hold_bar, 100, LV_ANIM_OFF);
        g_step = STEP_HOLD_RELEASE;
        g_step_start_ms = now_ms();
      }
      break;
    }

    case STEP_HOLD_RELEASE: {
      if (elapsed > STEP_TIMEOUT_MS) {
        finish_fail("Timed out");
        return;
      }
      if (!touching && elapsed >= RELEASE_SETTLE_MS)
        advance_hold_or_taps();
      break;
    }

    case STEP_TAP_WAIT: {
      if (elapsed > STEP_TIMEOUT_MS) {
        finish_fail("Timed out");
        return;
      }
      if (touching) {
        g_tap_sample_sum = (hw_delta > 0) ? (uint32_t)hw_delta : 0;
        g_tap_sample_count = (hw_delta > 0) ? 1 : 0;
        g_tap_early_done = false;
        g_step = STEP_TAP_RELEASE;
        g_step_start_ms = now_ms();
      }
      break;
    }

    case STEP_TAP_RELEASE: {
      if (elapsed > STEP_TIMEOUT_MS) {
        finish_fail("Timed out");
        return;
      }
      if (!g_tap_early_done && hw_delta > 0) {
        if (elapsed < TAP_EARLY_MS) {
          g_tap_sample_sum += (uint32_t)hw_delta;
          g_tap_sample_count++;
        } else {
          g_tap_early_done = true;
        }
      }
      if (!touching) {
        uint32_t early = 0;
        if (g_tap_sample_count > 0)
          early = (uint32_t)(g_tap_sample_sum / g_tap_sample_count);
        g_tap_elevs[g_tap_index] = early;
        ESP_LOGI(TAG, "Tap %d hw_delta=%u (n=%u)",
          g_tap_index + 1, (unsigned)early, (unsigned)g_tap_sample_count);
        g_tap_index++;
        request_bench_reset_when_idle();
        if (g_tap_index >= TAP_COUNT) {
          finish_ok_begin_settle();
          return;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "Tap %d/%d", g_tap_index + 1, TAP_COUNT);
        set_progress(buf);
        set_instruction("Tap screw");
        g_step = STEP_TAP_WAIT;
        g_step_start_ms = now_ms();
        g_tap_sample_sum = 0;
        g_tap_sample_count = 0;
        g_tap_early_done = false;
      }
      break;
    }

    case STEP_POST_SETTLE: {
      if (elapsed > POST_SETTLE_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Post-settle timed out (smooth=%u baseline=%u)",
          (unsigned)smooth, (unsigned)g_idle_baseline);
        finish_success_ui();
        return;
      }

      if (touching) {
        g_near_idle_since_ms = 0;
        g_bench_reset_done = false;
        set_instruction("Don't touch");
        break;
      }

      uint32_t slack = (g_idle_baseline * POST_SETTLE_NEAR_PCT) / 100;
      if (slack < 200) slack = 200;
      bool near = (smooth + slack >= g_idle_baseline) &&
        (smooth <= g_idle_baseline + slack);

      if (!near) {
        g_near_idle_since_ms = 0;
        break;
      }

      if (g_near_idle_since_ms == 0) g_near_idle_since_ms = now_ms();

      if (!g_bench_reset_done) {
        if (touch_pad12_reset_benchmark() == ESP_OK) {
          g_bench_reset_done = true;
          touch_update_known_good_benchmark(12, g_idle_baseline);
        }
        break;
      }

      if ((now_ms() - g_near_idle_since_ms) >= POST_SETTLE_STABLE_MS) {
        ESP_LOGI(TAG, "Pad 12 settled (smooth=%u delta=%d)",
          (unsigned)smooth, (int)hw_delta);
        finish_success_ui();
      }
      break;
    }

    default:
      break;
  }
}

static void screw_calibrate_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen == NULL) {
    uint16_t disp_w = display_get_width();
    uint16_t disp_h = display_get_height();

    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, disp_w, disp_h);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_screen, 0, 0);
    lv_obj_set_style_pad_all(g_screen, 10, 0);

    g_title_label = lv_label_create(g_screen);
    lv_label_set_text(g_title_label, "Screw Calibrate");
    lv_obj_set_style_text_color(g_title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_title_label, LV_ALIGN_TOP_MID, 0, 28);

    g_progress_label = lv_label_create(g_screen);
    lv_label_set_text(g_progress_label, "");
    lv_obj_set_style_text_color(g_progress_label, lv_color_make(0, 255, 128), 0);
    lv_obj_set_style_text_font(g_progress_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_progress_label, LV_ALIGN_TOP_MID, 0, 52);

    g_hold_bar = lv_bar_create(g_screen);
    lv_obj_set_size(g_hold_bar, 120, 18);
    lv_bar_set_range(g_hold_bar, 0, 100);
    lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
    lv_obj_align(g_hold_bar, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_color(g_hold_bar, lv_color_make(40, 40, 40), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_hold_bar, lv_color_make(0, 200, 100), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_hold_bar, 4, 0);
    lv_obj_add_flag(g_hold_bar, LV_OBJ_FLAG_HIDDEN);

    g_instruction_label = lv_label_create(g_screen);
    lv_label_set_text(g_instruction_label, "");
    lv_obj_set_style_text_color(g_instruction_label, lv_color_make(255, 200, 0), 0);
    lv_obj_set_style_text_font(g_instruction_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(g_instruction_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_instruction_label, LV_ALIGN_BOTTOM_MID, 0, -36);

    ESP_LOGI(TAG, "Screw calibration screen created");
  }

  lv_screen_load(g_screen);
  lv_timer_delete(timer);
  wizard_start();
}

UI_CREATE_DEFERRED_DRAW_FUNC(screw_calibrate, screw_calibrate_draw_deferred_cb)

static void screw_calibrate_teardown(void) {
  if (g_update_timer != NULL) {
    lv_timer_delete(g_update_timer);
    g_update_timer = NULL;
  }
  g_running = false;
  touch_screw_calib_set_active(false);

  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_title_label = NULL;
    g_instruction_label = NULL;
    g_progress_label = NULL;
    g_hold_bar = NULL;
  }
  ESP_LOGD(TAG, "Screw calibrate module teardown");
}

static void screw_calibrate_init(void) {
  g_running = false;
  g_step = STEP_IDLE;
  ESP_LOGI(TAG, "Screw calibrate module initialized");
}

ui_draw_module_t screw_calibrate_module = {
  .draw_func = screw_calibrate_draw,
  .teardown_func = screw_calibrate_teardown,
  .init_func = screw_calibrate_init,
  .name = "screw_calibrate",
  .title = "Screw Calibrate"
};
