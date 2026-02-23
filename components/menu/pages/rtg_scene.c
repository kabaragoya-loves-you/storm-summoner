#include "menu.h"
#include "menu_pages.h"
#include "rtg.h"
#include "scene.h"
#include "ui.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_RTG_SCENE"

// Forward declarations
lv_obj_t* menu_page_rtg_scene_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_RTG_ITEMS 11
static menu_item_t s_rtg_items[MAX_RTG_ITEMS];

static char s_enabled_label[LABEL_BUFFER_SETS][32];
static char s_mode_label[LABEL_BUFFER_SETS][32];
static char s_start_mode_label[LABEL_BUFFER_SETS][32];
static char s_rate_mode_label[LABEL_BUFFER_SETS][32];
static char s_rate_label[LABEL_BUFFER_SETS][32];
static char s_sync_mult_label[LABEL_BUFFER_SETS][32];
static char s_glide_label[LABEL_BUFFER_SETS][32];
static char s_probability_label[LABEL_BUFFER_SETS][32];
static char s_pattern_label[LABEL_BUFFER_SETS][32];

static bool s_callback_in_progress = false;

// ============================================================================
// Helper Functions
// ============================================================================

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static void persist_scene_changes(void) {
  if (ui_is_in_programming_mode()) {
    uint8_t scene_index = scene_get_current_index();
    scene_save_to_flash(scene_index);
  }
}

// ============================================================================
// Enabled Roller
// ============================================================================

static void enabled_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->rtg_config.enabled = (selected_index == 1);

  // Apply to RTG engine
  rtg_apply_config(&scene->rtg_config);

  persist_scene_changes();

  ESP_LOGI(TAG, "RTG %s", scene->rtg_config.enabled ? "enabled" : "disabled");

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* enabled_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = scene->rtg_config.enabled ? 1 : 0;
  return menu_create_roller_page("RTG", "Disabled\nEnabled", current, enabled_confirm_cb, NULL);
}

static void nav_to_enabled(void* user_data) {
  (void)user_data;
  menu_navigate_to("RTG", enabled_roller_create);
}

// ============================================================================
// Mode Roller
// ============================================================================

static void mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->rtg_config.mode = (selected_index == 0) ? RTG_MODE_CONTINUOUS : RTG_MODE_STEP;

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* mode_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = (scene->rtg_config.mode == RTG_MODE_CONTINUOUS) ? 0 : 1;
  return menu_create_roller_page("Mode", "Continuous\nStep", current, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", mode_roller_create);
}

// ============================================================================
// Start Mode Roller (Running / Paused / Follow Transport)
// ============================================================================

static void start_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  switch (selected_index) {
    case 0: scene->rtg_config.start_mode = RTG_START_RUNNING; break;
    case 1: scene->rtg_config.start_mode = RTG_START_PAUSED; break;
    case 2: scene->rtg_config.start_mode = RTG_START_TRANSPORT; break;
    default: scene->rtg_config.start_mode = RTG_START_RUNNING; break;
  }

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* start_mode_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = 0;
  switch (scene->rtg_config.start_mode) {
    case RTG_START_RUNNING: current = 0; break;
    case RTG_START_PAUSED: current = 1; break;
    case RTG_START_TRANSPORT: current = 2; break;
  }

  return menu_create_roller_page("Start Mode",
    "Running\nPaused\nFollow Transport", current, start_mode_confirm_cb, NULL);
}

static void nav_to_start_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Start Mode", start_mode_roller_create);
}

// ============================================================================
// Rate Mode Roller (Free / Sync)
// ============================================================================

static void rate_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->rtg_config.rate_mode = (selected_index == 0) ? RTG_RATE_MODE_FREE : RTG_RATE_MODE_SYNC;

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* rate_mode_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = (scene->rtg_config.rate_mode == RTG_RATE_MODE_FREE) ? 0 : 1;
  return menu_create_roller_page("Rate Mode", "Free (Hz)\nSync (BPM)", current, rate_mode_confirm_cb, NULL);
}

static void nav_to_rate_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Rate Mode", rate_mode_roller_create);
}

// ============================================================================
// Rate Roller (0.5 - 25.0 Hz)
// ============================================================================

// Rate values in Hz * 100 for precision
static const uint16_t s_rate_values[] = {
  50, 75, 100, 125, 150, 175, 200, 250, 300, 350, 400, 500,
  600, 700, 800, 900, 1000, 1250, 1500, 1750, 2000, 2500
};
#define NUM_RATE_VALUES (sizeof(s_rate_values) / sizeof(s_rate_values[0]))

static void rate_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  if (selected_index < NUM_RATE_VALUES) {
    scene->rtg_config.rate_hz_x100 = s_rate_values[selected_index];
    rtg_apply_config(&scene->rtg_config);
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* rate_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  // Build options string
  static char options[256];
  options[0] = '\0';
  size_t pos = 0;

  for (size_t i = 0; i < NUM_RATE_VALUES; i++) {
    if (i > 0) options[pos++] = '\n';
    float hz = s_rate_values[i] / 100.0f;
    if (hz < 1.0f) {
      pos += snprintf(options + pos, sizeof(options) - pos, "%.2f Hz", hz);
    } else if (hz < 10.0f) {
      pos += snprintf(options + pos, sizeof(options) - pos, "%.1f Hz", hz);
    } else {
      pos += snprintf(options + pos, sizeof(options) - pos, "%.0f Hz", hz);
    }
  }

  // Find closest matching index
  uint32_t current = 6;  // Default to 2.0 Hz
  uint16_t current_rate = scene->rtg_config.rate_hz_x100;
  for (size_t i = 0; i < NUM_RATE_VALUES; i++) {
    if (s_rate_values[i] >= current_rate) {
      // Check if previous value is closer
      if (i > 0) {
        uint16_t diff_prev = current_rate - s_rate_values[i - 1];
        uint16_t diff_curr = s_rate_values[i] - current_rate;
        current = (diff_prev <= diff_curr) ? (i - 1) : i;
      } else {
        current = i;
      }
      break;
    }
  }

  return menu_create_roller_page("Rate", options, current, rate_confirm_cb, NULL);
}

static void nav_to_rate(void* user_data) {
  (void)user_data;
  menu_navigate_to("Rate", rate_roller_create);
}

// ============================================================================
// Sync Multiplier Roller (only visible when rate_mode == SYNC)
// ============================================================================

// Musically useful multipliers (x1000 for precision)
static const uint16_t s_sync_mult_values[] = {
  125,   // 1/8 (0.125x) - every 8 beats
  167,   // 1/6 (0.167x) - triplet feel
  250,   // 1/4 (0.25x) - every 4 beats (1 bar)
  333,   // 1/3 (0.333x) - triplet
  500,   // 1/2 (0.5x) - every 2 beats
  667,   // 2/3 (0.667x) - dotted
  750,   // 3/4 (0.75x) - dotted
  1000,  // 1x - every beat (default)
  1500,  // 3/2 (1.5x) - dotted quarter
  2000,  // 2x - eighth notes
  3000,  // 3x - triplet eighths
  4000,  // 4x - sixteenth notes
  6000,  // 6x - triplet sixteenths
  8000   // 8x - 32nd notes
};
#define NUM_SYNC_MULT_VALUES (sizeof(s_sync_mult_values) / sizeof(s_sync_mult_values[0]))

// Display labels for the multiplier values
static const char* s_sync_mult_labels[] = {
  "1/8 (0.125x)",
  "1/6 (0.167x)",
  "1/4 (0.25x)",
  "1/3 (0.333x)",
  "1/2 (0.5x)",
  "2/3 (0.667x)",
  "3/4 (0.75x)",
  "1x",
  "3/2 (1.5x)",
  "2x",
  "3x",
  "4x",
  "6x",
  "8x"
};

static void sync_mult_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  if (selected_index < NUM_SYNC_MULT_VALUES) {
    scene->rtg_config.sync_mult_x1000 = s_sync_mult_values[selected_index];
    rtg_apply_config(&scene->rtg_config);
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* sync_mult_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  // Build options string
  static char options[384];
  options[0] = '\0';
  size_t pos = 0;

  for (size_t i = 0; i < NUM_SYNC_MULT_VALUES; i++) {
    if (i > 0) options[pos++] = '\n';
    pos += snprintf(options + pos, sizeof(options) - pos, "%s", s_sync_mult_labels[i]);
  }

  // Find closest matching index (default to 1x = index 7)
  uint32_t current = 7;
  uint16_t current_mult = scene->rtg_config.sync_mult_x1000;
  for (size_t i = 0; i < NUM_SYNC_MULT_VALUES; i++) {
    if (s_sync_mult_values[i] >= current_mult) {
      // Check if previous value is closer
      if (i > 0) {
        uint16_t diff_prev = current_mult - s_sync_mult_values[i - 1];
        uint16_t diff_curr = s_sync_mult_values[i] - current_mult;
        current = (diff_prev <= diff_curr) ? (i - 1) : i;
      } else {
        current = i;
      }
      break;
    }
  }

  return menu_create_roller_page("Multiplier", options, current, sync_mult_confirm_cb, NULL);
}

static void nav_to_sync_mult(void* user_data) {
  (void)user_data;
  menu_navigate_to("Multiplier", sync_mult_roller_create);
}

// ============================================================================
// Probability Roller
// ============================================================================

static void probability_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  // Index 0 = 10%, 1 = 20%, ..., 9 = 100%
  scene->rtg_config.probability = (uint8_t)((selected_index + 1) * 10);

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* probability_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint8_t prob = scene->rtg_config.probability;
  if (prob == 0) prob = 100;
  uint32_t current = (prob / 10) - 1;
  if (current > 9) current = 9;

  return menu_create_roller_page("Probability",
    "10%\n20%\n30%\n40%\n50%\n60%\n70%\n80%\n90%\n100%",
    current, probability_confirm_cb, NULL);
}

static void nav_to_probability(void* user_data) {
  (void)user_data;
  menu_navigate_to("Probability", probability_roller_create);
}

// ============================================================================
// Pattern Length Roller
// ============================================================================

static void pattern_length_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  // Index 0 = Off, 1 = 2, 2 = 3, ..., 7 = 8
  uint8_t new_length = (selected_index == 0) ? 0 : (uint8_t)(selected_index + 1);
  uint8_t old_length = scene->rtg_config.pattern_length;
  scene->rtg_config.pattern_length = new_length;

  // Preserve existing mask, enable any newly added steps
  if (new_length >= 2 && new_length > old_length) {
    for (int i = (old_length < 2 ? 0 : old_length); i < new_length; i++) {
      scene->rtg_config.pattern_mask |= (1 << i);
    }
  }

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* pattern_length_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint8_t length = scene->rtg_config.pattern_length;
  uint32_t current = (length < 2) ? 0 : (length - 1);

  return menu_create_roller_page("Pattern",
    "Off\n2\n3\n4\n5\n6\n7\n8",
    current, pattern_length_confirm_cb, NULL);
}

// ============================================================================
// Pattern Editor (toggle individual steps)
// ============================================================================

#define MAX_PATTERN_EDITOR_ITEMS 9  // 1 length + 8 steps
static menu_item_t s_pattern_editor_items[MAX_PATTERN_EDITOR_ITEMS];
static char s_pattern_length_item_label[32];
static char s_pattern_step_labels[8][16];

static lv_obj_t* pattern_editor_create(void);  // Forward declaration

// Custom back handler for pattern editor - saves and recreates RTG page
static bool pattern_editor_back_handler(void) {
  menu_set_custom_back_handler(NULL);
  persist_scene_changes();  // Save any step toggles made in editor
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
  return true;
}

static void pattern_step_toggle_cb(void* user_data) {
  scene_t* scene = scene_get_current();
  if (!scene) return;

  uint8_t step = (uint8_t)(uintptr_t)user_data;

  // Toggle the bit for this step
  scene->rtg_config.pattern_mask ^= (1 << step);

  // Lightweight update - full apply_config would overflow this task's stack
  rtg_set_pattern_mask(scene->rtg_config.pattern_mask);

  // Refresh the pattern editor page, preserving focus on the toggled step
  menu_set_restore_focus((int)step + 1);  // +1 because Length is at index 0
  menu_replace_current_deferred("Pattern", pattern_editor_create);
}

// Callback when pattern length is changed from within the editor
static void pattern_length_editor_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  // Index 0 = Off, 1 = 2, 2 = 3, ..., 7 = 8
  uint8_t new_length = (selected_index == 0) ? 0 : (uint8_t)(selected_index + 1);
  uint8_t old_length = scene->rtg_config.pattern_length;
  scene->rtg_config.pattern_length = new_length;

  // Preserve existing mask, enable any newly added steps
  if (new_length >= 2 && new_length > old_length) {
    for (int i = (old_length < 2 ? 0 : old_length); i < new_length; i++) {
      scene->rtg_config.pattern_mask |= (1 << i);
    }
  }

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;

  // If pattern is now Off, go back to RTG page; otherwise refresh editor
  if (new_length < 2) {
    menu_set_custom_back_handler(NULL);
    menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
  } else {
    menu_navigate_back_then_to(2, "Pattern", pattern_editor_create);
  }
}

static lv_obj_t* pattern_length_editor_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint8_t length = scene->rtg_config.pattern_length;
  uint32_t current = (length < 2) ? 0 : (length - 1);

  return menu_create_roller_page("Length",
    "Off\n2\n3\n4\n5\n6\n7\n8",
    current, pattern_length_editor_confirm_cb, NULL);
}

static void nav_to_pattern_length_editor(void* user_data) {
  (void)user_data;
  menu_navigate_to("Length", pattern_length_editor_roller_create);
}

static lv_obj_t* pattern_editor_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return menu_create_page("Error", NULL, 0);

  // Set custom back handler to recreate RTG page with fresh pattern display
  menu_set_custom_back_handler(pattern_editor_back_handler);

  uint8_t length = scene->rtg_config.pattern_length;
  if (length < 2) length = 2;
  if (length > 8) length = 8;

  int idx = 0;

  // Length item at top
  snprintf(s_pattern_length_item_label, sizeof(s_pattern_length_item_label), "Length: %d", length);
  s_pattern_editor_items[idx++] = (menu_item_t){
    s_pattern_length_item_label, nav_to_pattern_length_editor, NULL, false
  };

  // Step toggle items
  for (int i = 0; i < length; i++) {
    bool enabled = (scene->rtg_config.pattern_mask >> i) & 1;
    snprintf(s_pattern_step_labels[i], sizeof(s_pattern_step_labels[i]),
      "Step %d: %s", i + 1, enabled ? "On" : "Off");
    s_pattern_editor_items[idx++] = (menu_item_t){
      s_pattern_step_labels[i], pattern_step_toggle_cb, (void*)(uintptr_t)i, false
    };
  }

  return menu_create_page("Pattern", s_pattern_editor_items, idx);
}

static void nav_to_pattern(void* user_data) {
  (void)user_data;
  scene_t* scene = scene_get_current();
  if (!scene) return;

  // If pattern is off, show length roller; otherwise show pattern editor
  if (scene->rtg_config.pattern_length < 2) {
    menu_navigate_to("Pattern", pattern_length_roller_create);
  } else {
    menu_navigate_to("Pattern", pattern_editor_create);
  }
}

// Get pattern display string (e.g., "X.X.X.X.")
static const char* get_pattern_display(uint8_t length, uint8_t mask) {
  static char buf[12];
  if (length < 2) return "Off";
  for (int i = 0; i < length && i < 8; i++) {
    buf[i] = (mask & (1 << i)) ? 'X' : '.';
  }
  buf[length] = '\0';
  return buf;
}

// ============================================================================
// Glide Roller
// ============================================================================

static void glide_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->rtg_config.glide = (selected_index == 1);

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* glide_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = scene->rtg_config.glide ? 1 : 0;
  return menu_create_roller_page("Glide", "Off\nOn", current, glide_confirm_cb, NULL);
}

static void nav_to_glide(void* user_data) {
  (void)user_data;
  menu_navigate_to("Glide", glide_roller_create);
}

// ============================================================================
// Main RTG Page
// ============================================================================

lv_obj_t* menu_page_rtg_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    return menu_create_page("RTG", NULL, 0);
  }

  int buf = get_next_buffer_set();
  int idx = 0;

  // Enabled
  snprintf(s_enabled_label[buf], sizeof(s_enabled_label[buf]),
    "RTG: %s", scene->rtg_config.enabled ? "Enabled" : "Disabled");
  s_rtg_items[idx++] = (menu_item_t){ s_enabled_label[buf], nav_to_enabled, NULL, false };

  // Only show other options when RTG is enabled
  if (scene->rtg_config.enabled) {
    // Mode
    const char* mode_str = (scene->rtg_config.mode == RTG_MODE_CONTINUOUS) ? "Continuous" : "Step";
    snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Mode: %s", mode_str);
    s_rtg_items[idx++] = (menu_item_t){ s_mode_label[buf], nav_to_mode, NULL, false };

    // Start Mode and Rate settings only apply to Continuous mode
    if (scene->rtg_config.mode == RTG_MODE_CONTINUOUS) {
      // Start Mode (Running / Paused / Follow Transport)
      const char* start_mode_str;
      switch (scene->rtg_config.start_mode) {
        case RTG_START_RUNNING: start_mode_str = "Running"; break;
        case RTG_START_PAUSED: start_mode_str = "Paused"; break;
        case RTG_START_TRANSPORT: start_mode_str = "Follow Transport"; break;
        default: start_mode_str = "Running"; break;
      }
      snprintf(s_start_mode_label[buf], sizeof(s_start_mode_label[buf]), "Start: %s", start_mode_str);
      s_rtg_items[idx++] = (menu_item_t){ s_start_mode_label[buf], nav_to_start_mode, NULL, false };

      // Rate Mode (Free Hz / Sync BPM)
      const char* rate_mode_str = (scene->rtg_config.rate_mode == RTG_RATE_MODE_SYNC) ? "Sync (BPM)" : "Free (Hz)";
      snprintf(s_rate_mode_label[buf], sizeof(s_rate_mode_label[buf]), "Rate: %s", rate_mode_str);
      s_rtg_items[idx++] = (menu_item_t){ s_rate_mode_label[buf], nav_to_rate_mode, NULL, false };

      // Hz Rate (only show if in Free mode) or Sync Multiplier (only show if in Sync mode)
      if (scene->rtg_config.rate_mode == RTG_RATE_MODE_FREE) {
        float rate_hz = scene->rtg_config.rate_hz_x100 / 100.0f;
        if (rate_hz < 1.0f) {
          snprintf(s_rate_label[buf], sizeof(s_rate_label[buf]), "Hz: %.2f", rate_hz);
        } else if (rate_hz < 10.0f) {
          snprintf(s_rate_label[buf], sizeof(s_rate_label[buf]), "Hz: %.1f", rate_hz);
        } else {
          snprintf(s_rate_label[buf], sizeof(s_rate_label[buf]), "Hz: %.0f", rate_hz);
        }
        s_rtg_items[idx++] = (menu_item_t){ s_rate_label[buf], nav_to_rate, NULL, false };
      } else {
        // Find the label for the current multiplier
        uint16_t mult = scene->rtg_config.sync_mult_x1000;
        const char* mult_label = "1x";
        for (size_t i = 0; i < NUM_SYNC_MULT_VALUES; i++) {
          if (s_sync_mult_values[i] == mult) {
            mult_label = s_sync_mult_labels[i];
            break;
          }
        }
        snprintf(s_sync_mult_label[buf], sizeof(s_sync_mult_label[buf]), "Mult: %s", mult_label);
        s_rtg_items[idx++] = (menu_item_t){ s_sync_mult_label[buf], nav_to_sync_mult, NULL, false };
      }

      // Probability (only in continuous mode)
      uint8_t prob = scene->rtg_config.probability;
      if (prob == 0) prob = 100;
      snprintf(s_probability_label[buf], sizeof(s_probability_label[buf]), "Prob: %d%%", prob);
      s_rtg_items[idx++] = (menu_item_t){ s_probability_label[buf], nav_to_probability, NULL, false };

      // Pattern (only in continuous mode)
      const char* pattern_display = get_pattern_display(
        scene->rtg_config.pattern_length, scene->rtg_config.pattern_mask);
      snprintf(s_pattern_label[buf], sizeof(s_pattern_label[buf]), "Pattern: %s", pattern_display);
      s_rtg_items[idx++] = (menu_item_t){ s_pattern_label[buf], nav_to_pattern, NULL, false };
    }

    // Glide
    snprintf(s_glide_label[buf], sizeof(s_glide_label[buf]),
      "Glide: %s", scene->rtg_config.glide ? "On" : "Off");
    s_rtg_items[idx++] = (menu_item_t){ s_glide_label[buf], nav_to_glide, NULL, false };
  }

  return menu_create_page("RTG", s_rtg_items, idx);
}
