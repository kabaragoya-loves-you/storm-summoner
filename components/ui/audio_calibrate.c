#include "lvgl.h"
#include "ui.h"
#include "cv.h"
#include "scene.h"
#include "menu.h"
#include "menu_pages.h"
#include "display_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

#define TAG "AUDIO_CAL_UI"

// Calibration duration in milliseconds
#define CALIBRATION_DURATION_MS 3000
#define UPDATE_INTERVAL_MS 20  // 50Hz update rate

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_title_label = NULL;
static lv_obj_t *g_countdown_label = NULL;
static lv_obj_t *g_amplitude_bar = NULL;
static lv_obj_t *g_peak_label = NULL;
static lv_obj_t *g_instruction_label = NULL;

// Calibration state - use lv_timer to stay in LVGL context
static lv_timer_t *g_update_timer = NULL;
static uint32_t g_start_time = 0;
static int16_t g_peak_amplitude = 0;
static int16_t g_current_amplitude = 0;
static int16_t g_half_range = 0;
static int16_t g_center = 0;
static bool g_calibrating = false;

// Forward declarations
static void calibration_update_cb(lv_timer_t* timer);
static void finish_calibration(void);
static void return_to_menu_cb(lv_timer_t* t);
void audio_calibrate_start(void);

static void audio_calibrate_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen == NULL) {
    uint16_t disp_w = display_get_width();
    uint16_t disp_h = display_get_height();
    
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, disp_w, disp_h);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_screen, 0, 0);
    lv_obj_set_style_pad_all(g_screen, 10, 0);
    
    // Title
    g_title_label = lv_label_create(g_screen);
    lv_label_set_text(g_title_label, "Audio Calibrate");
    lv_obj_set_style_text_color(g_title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_title_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // Countdown label
    g_countdown_label = lv_label_create(g_screen);
    lv_label_set_text(g_countdown_label, "3");
    lv_obj_set_style_text_color(g_countdown_label, lv_color_make(0, 255, 128), 0);
    lv_obj_set_style_text_font(g_countdown_label, &lv_font_montserrat_20, 0);
    lv_obj_align(g_countdown_label, LV_ALIGN_TOP_MID, 0, 45);
    
    // Amplitude bar (vertical)
    g_amplitude_bar = lv_bar_create(g_screen);
    lv_obj_set_size(g_amplitude_bar, 40, 80);
    lv_bar_set_range(g_amplitude_bar, 0, 100);
    lv_bar_set_value(g_amplitude_bar, 0, LV_ANIM_OFF);
    lv_obj_align(g_amplitude_bar, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(g_amplitude_bar, lv_color_make(40, 40, 40), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_amplitude_bar, lv_color_make(0, 200, 100), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_amplitude_bar, 4, 0);
    
    // Peak indicator label
    g_peak_label = lv_label_create(g_screen);
    lv_label_set_text(g_peak_label, "Peak: 0%");
    lv_obj_set_style_text_color(g_peak_label, lv_color_make(180, 180, 180), 0);
    lv_obj_set_style_text_font(g_peak_label, &lv_font_montserrat_12, 0);
    lv_obj_align(g_peak_label, LV_ALIGN_CENTER, 0, 60);
    
    // Instruction label
    g_instruction_label = lv_label_create(g_screen);
    lv_label_set_text(g_instruction_label, "Play LOUD!");
    lv_obj_set_style_text_color(g_instruction_label, lv_color_make(255, 200, 0), 0);
    lv_obj_set_style_text_font(g_instruction_label, &lv_font_montserrat_12, 0);
    lv_obj_align(g_instruction_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    ESP_LOGI(TAG, "Audio calibration screen created");
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
  
  // Start calibration after screen is loaded
  audio_calibrate_start();
}

UI_CREATE_DEFERRED_DRAW_FUNC(audio_calibrate, audio_calibrate_draw_deferred_cb)

static void audio_calibrate_teardown(void) {
  // Stop timer if running
  if (g_update_timer != NULL) {
    lv_timer_delete(g_update_timer);
    g_update_timer = NULL;
  }
  
  g_calibrating = false;
  
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_title_label = NULL;
    g_countdown_label = NULL;
    g_amplitude_bar = NULL;
    g_peak_label = NULL;
    g_instruction_label = NULL;
  }
  ESP_LOGD(TAG, "Audio calibrate module teardown");
}

static void audio_calibrate_init(void) {
  g_peak_amplitude = 0;
  g_current_amplitude = 0;
  g_calibrating = false;
  ESP_LOGI(TAG, "Audio calibrate module initialized");
}

void audio_calibrate_start(void) {
  if (!cv_is_audio_mode_active()) {
    ESP_LOGW(TAG, "Audio mode not active, cannot calibrate");
    return;
  }
  
  // Get calibration range info
  uint8_t scene_index = scene_get_current_index();
  audio_config_t* cfg = scene_get_audio_config(scene_index);
  if (!cfg) return;
  
  // Get center and half_range for the current audio range
  // These match cv.c's calibration values
  cv_range_t range = cfg->range;
  int16_t min_val, max_val;
  cv_get_calibration(range, &min_val, &max_val);
  g_center = (min_val + max_val) / 2;
  g_half_range = (max_val - min_val) / 2;
  
  g_peak_amplitude = 0;
  g_current_amplitude = 0;
  g_start_time = esp_timer_get_time() / 1000;
  g_calibrating = true;
  
  // Create LVGL timer (runs in LVGL context, safe for UI updates)
  g_update_timer = lv_timer_create(calibration_update_cb, UPDATE_INTERVAL_MS, NULL);
  
  ESP_LOGI(TAG, "Audio calibration started, center=%d, half_range=%d", g_center, g_half_range);
}

static void calibration_update_cb(lv_timer_t* timer) {
  (void)timer;
  
  if (!g_calibrating || !g_screen) return;
  
  uint32_t now = esp_timer_get_time() / 1000;
  uint32_t elapsed = now - g_start_time;
  
  // Read current amplitude
  int16_t raw = cv_read_raw_now();
  g_current_amplitude = abs(raw - g_center);
  
  if (g_current_amplitude > g_peak_amplitude) {
    g_peak_amplitude = g_current_amplitude;
  }
  
  // Update countdown
  int remaining_secs = (CALIBRATION_DURATION_MS - elapsed + 999) / 1000;
  if (remaining_secs < 0) remaining_secs = 0;
  
  // Schedule UI update on LVGL thread
  if (g_countdown_label) {
    static char countdown_buf[8];
    snprintf(countdown_buf, sizeof(countdown_buf), "%d", remaining_secs);
    lv_label_set_text(g_countdown_label, countdown_buf);
  }
  
  // Update amplitude bar - scale relative to peak for meaningful visual feedback
  // Use dynamic scaling: current amplitude relative to peak, filling more of the bar
  if (g_amplitude_bar) {
    int bar_value = 0;
    if (g_peak_amplitude > 10) {
      // Scale current amplitude relative to 85% of peak so bar uses more range
      // This makes playing at ~85% of peak fill the whole bar
      int scale = (g_peak_amplitude * 85) / 100;
      bar_value = (g_current_amplitude * 100) / scale;
      if (bar_value > 100) bar_value = 100;
    } else if (g_current_amplitude > 0) {
      // Before we have a meaningful peak, just show activity
      bar_value = 50;
    }
    lv_bar_set_value(g_amplitude_bar, bar_value, LV_ANIM_OFF);
    
    // Color based on amplitude (green -> yellow -> orange)
    lv_color_t bar_color;
    if (bar_value < 50) {
      bar_color = lv_color_make(0, 200, 100);  // Green
    } else if (bar_value < 80) {
      bar_color = lv_color_make(200, 200, 0);  // Yellow
    } else {
      bar_color = lv_color_make(255, 100, 0);  // Orange/Red (new peak!)
    }
    lv_obj_set_style_bg_color(g_amplitude_bar, bar_color, LV_PART_INDICATOR);
  }
  
  // Update peak label - show actual gain that will be applied
  if (g_peak_label && g_half_range > 0) {
    static char peak_buf[24];
    if (g_peak_amplitude > 5) {
      // Preview the gain we'll calculate
      float target_gain = 1.4f * (float)g_half_range / (float)g_peak_amplitude;
      snprintf(peak_buf, sizeof(peak_buf), "Gain: %.1fx", target_gain > 64.0f ? 64.0f : target_gain);
    } else {
      snprintf(peak_buf, sizeof(peak_buf), "Waiting...");
    }
    lv_label_set_text(g_peak_label, peak_buf);
  }
  
  // Check if calibration is complete
  if (elapsed >= CALIBRATION_DURATION_MS) {
    finish_calibration();
  }
}

static void finish_calibration(void) {
  g_calibrating = false;
  
  // Stop timer
  if (g_update_timer != NULL) {
    lv_timer_delete(g_update_timer);
    g_update_timer = NULL;
  }
  
  // Calculate and apply sensitivity
  uint8_t scene_index = scene_get_current_index();
  
  if (g_peak_amplitude < 5) {
    ESP_LOGW(TAG, "No significant audio detected. Using maximum sensitivity.");
    scene_set_audio_sensitivity(scene_index, 255);
    scene_set_audio_threshold(scene_index, 0);
  } else {
    // Same formula as cv_audio_calibrate but with our measured peak
    // Target 1.4 so peak amplitude maps to ~130 on the 0-127 scale
    // This ensures 127 is easily reachable with normal playing
    float target_gain = 1.4f * (float)g_half_range / (float)g_peak_amplitude;
    float sens_float = 255.0f * logf(target_gain * 4.0f) / logf(256.0f);
    
    if (sens_float < 0) sens_float = 0;
    if (sens_float > 255) sens_float = 255;
    
    uint8_t sensitivity = (uint8_t)(sens_float + 0.5f);
    float actual_gain = 0.25f * powf(256.0f, sensitivity / 255.0f);
    
    ESP_LOGI(TAG, "Calibration complete: peak=%d, sensitivity=%u (%.1fx gain)",
      g_peak_amplitude, (unsigned)sensitivity, actual_gain);
    
    scene_set_audio_sensitivity(scene_index, sensitivity);
    scene_set_audio_threshold(scene_index, 3);  // Small noise gate
  }
  
  // Update running config
  audio_config_t* cfg = scene_get_audio_config(scene_index);
  cv_update_audio_config(cfg);
  
  // Update UI to show complete
  if (g_instruction_label) {
    lv_label_set_text(g_instruction_label, "Done!");
    lv_obj_set_style_text_color(g_instruction_label, lv_color_make(0, 255, 128), 0);
  }
  if (g_countdown_label) {
    lv_label_set_text(g_countdown_label, LV_SYMBOL_OK);
  }
  
  // Return to menu after a brief delay
  lv_timer_create(return_to_menu_cb, 800, NULL);
}

static void return_to_menu_cb(lv_timer_t* t) {
  menu_set_restore_focus(0);  // Focus on Calibrate item
  menu_navigate_back_then_to(1, "Control Voltage", menu_page_cv_scene_create);
  lv_timer_delete(t);
}

ui_draw_module_t audio_calibrate_module = {
  .draw_func = audio_calibrate_draw,
  .teardown_func = audio_calibrate_teardown,
  .init_func = audio_calibrate_init,
  .name = "audio_calibrate",
  .title = "Audio Calibrate"
};
