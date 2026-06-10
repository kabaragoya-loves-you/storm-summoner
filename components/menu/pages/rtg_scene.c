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

#define MAX_RTG_ITEMS 16
static menu_item_t s_rtg_items[MAX_RTG_ITEMS];

static char s_enabled_label[LABEL_BUFFER_SETS][32];
static char s_generator_label[LABEL_BUFFER_SETS][32];
static char s_mode_label[LABEL_BUFFER_SETS][32];
static char s_start_mode_label[LABEL_BUFFER_SETS][32];
static char s_rate_mode_label[LABEL_BUFFER_SETS][32];
static char s_rate_label[LABEL_BUFFER_SETS][32];
static char s_sync_mult_label[LABEL_BUFFER_SETS][32];
static char s_glide_label[LABEL_BUFFER_SETS][32];
static char s_direction_label[LABEL_BUFFER_SETS][32];
static char s_layout_label[LABEL_BUFFER_SETS][32];
static char s_fade_label[LABEL_BUFFER_SETS][32];
static char s_style_label[LABEL_BUFFER_SETS][32];
static char s_wide_semis_label[LABEL_BUFFER_SETS][32];
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
  return menu_create_roller_page("Rate Mode", "Time\nDivision", current, rate_mode_confirm_cb, NULL);
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
// Division Roller (only visible when rate_mode == SYNC)
// ============================================================================

static const char* rtg_division_display(lfo_note_division_t division) {
  switch (division) {
    case LFO_DIVISION_16_BARS: return "16 Bars";
    case LFO_DIVISION_12_BARS: return "12 Bars";
    case LFO_DIVISION_8_BARS: return "8 Bars";
    case LFO_DIVISION_4_BARS: return "4 Bars";
    case LFO_DIVISION_2_BARS: return "2 Bars";
    case LFO_DIVISION_1_BAR: return "1 Bar";
    case LFO_DIVISION_HALF: return "1/2 Note";
    case LFO_DIVISION_QUARTER: return "1/4 Note";
    case LFO_DIVISION_EIGHTH: return "1/8 Note";
    case LFO_DIVISION_SIXTEENTH: return "1/16 Note";
    case LFO_DIVISION_32ND: return "1/32 Note";
    default: return "1/4 Note";
  }
}

static void division_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  if (selected_index < LFO_DIVISION_MAX) {
    scene->rtg_config.division = (lfo_note_division_t)selected_index;
    rtg_apply_config(&scene->rtg_config);
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* division_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = (uint32_t)scene->rtg_config.division;
  return menu_create_roller_page("Divider",
    "16 Bars\n12 Bars\n8 Bars\n4 Bars\n2 Bars\n1 Bar\n1/2 Note\n1/4 Note\n1/8 Note\n1/16 Note\n1/32 Note",
    current, division_confirm_cb, NULL);
}

static void nav_to_division(void* user_data) {
  (void)user_data;
  menu_navigate_to("Divider", division_roller_create);
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

static lv_obj_t* pattern_editor_create(void);  // Forward declaration

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

  if (new_length < 2) {
    // Pattern disabled - go back to RTG
    menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
  } else {
    // Pattern enabled - go directly to pattern editor
    // Stack: RTG -> Pattern_roller, replace with RTG -> Pattern_editor
    menu_navigate_back_then_to(1, "Pattern", pattern_editor_create);
  }
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
    s_pattern_length_item_label, nav_to_pattern_length_editor, NULL, false,
    MENU_ITEM_KIND_ROLLER
  };

  // Step toggle items
  for (int i = 0; i < length; i++) {
    bool enabled = (scene->rtg_config.pattern_mask >> i) & 1;
    snprintf(s_pattern_step_labels[i], sizeof(s_pattern_step_labels[i]),
      "Step %d: %s", i + 1, enabled ? "On" : "Off");
    s_pattern_editor_items[idx++] = (menu_item_t){
      s_pattern_step_labels[i], pattern_step_toggle_cb, (void*)(uintptr_t)i, false,
      MENU_ITEM_KIND_ACTION
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
// Generator Roller (Random / Shepard)
// ============================================================================

static void generator_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->rtg_config.generator =
    (selected_index == 1) ? RTG_GEN_SHEPARD : RTG_GEN_RANDOM;

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* generator_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = (scene->rtg_config.generator == RTG_GEN_SHEPARD) ? 1 : 0;
  return menu_create_roller_page("Generator", "Random\nShepard",
    current, generator_confirm_cb, NULL);
}

static void nav_to_generator(void* user_data) {
  (void)user_data;
  menu_navigate_to("Generator", generator_roller_create);
}

// ============================================================================
// Shepard Direction Roller (Rising / Falling)
// ============================================================================

static void direction_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->rtg_config.shepard_direction =
    (selected_index == 1) ? SHEPARD_DIR_FALLING : SHEPARD_DIR_RISING;

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* direction_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current =
    (scene->rtg_config.shepard_direction == SHEPARD_DIR_FALLING) ? 1 : 0;
  return menu_create_roller_page("Direction", "Rising\nFalling",
    current, direction_confirm_cb, NULL);
}

static void nav_to_direction(void* user_data) {
  (void)user_data;
  menu_navigate_to("Direction", direction_roller_create);
}

// ============================================================================
// Shepard Voice Layout Roller (Single Channel / Multi-Channel)
// ============================================================================

static void layout_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->rtg_config.shepard_layout =
    (selected_index == 1) ? SHEPARD_LAYOUT_MULTI : SHEPARD_LAYOUT_SINGLE;

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* layout_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current =
    (scene->rtg_config.shepard_layout == SHEPARD_LAYOUT_MULTI) ? 1 : 0;
  return menu_create_roller_page("Layout",
    "Single Channel\nMulti-Channel", current, layout_confirm_cb, NULL);
}

static void nav_to_layout(void* user_data) {
  (void)user_data;
  menu_navigate_to("Layout", layout_roller_create);
}

// ============================================================================
// Shepard Voice Fade Roller (None / CC11 / Poly Aftertouch)
// ============================================================================

static void fade_confirm_cb(uint32_t selected_index, void* user_data) {
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
    case 1: scene->rtg_config.shepard_fade = SHEPARD_FADE_CC11; break;
    case 2: scene->rtg_config.shepard_fade = SHEPARD_FADE_POLY_AT; break;
    default: scene->rtg_config.shepard_fade = SHEPARD_FADE_NONE; break;
  }

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* fade_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = 0;
  switch (scene->rtg_config.shepard_fade) {
    case SHEPARD_FADE_NONE:    current = 0; break;
    case SHEPARD_FADE_CC11:    current = 1; break;
    case SHEPARD_FADE_POLY_AT: current = 2; break;
  }

  return menu_create_roller_page("Fade",
    "None\nCC11 Expression\nPoly Aftertouch",
    current, fade_confirm_cb, NULL);
}

static void nav_to_fade(void* user_data) {
  (void)user_data;
  menu_navigate_to("Fade", fade_roller_create);
}

// ============================================================================
// Shepard Smoothness Style Roller (Stream / Wide / Crossfade)
// ============================================================================

static void style_confirm_cb(uint32_t selected_index, void* user_data) {
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
    case 1: scene->rtg_config.shepard_style = SHEPARD_STYLE_WIDE; break;
    case 2: scene->rtg_config.shepard_style = SHEPARD_STYLE_CROSSFADE; break;
    default: scene->rtg_config.shepard_style = SHEPARD_STYLE_STREAM; break;
  }

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* style_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = 0;
  switch (scene->rtg_config.shepard_style) {
    case SHEPARD_STYLE_STREAM:    current = 0; break;
    case SHEPARD_STYLE_WIDE:      current = 1; break;
    case SHEPARD_STYLE_CROSSFADE: current = 2; break;
  }

  return menu_create_roller_page("Style",
    "Stream\nWide\nCrossfade", current, style_confirm_cb, NULL);
}

static void nav_to_style(void* user_data) {
  (void)user_data;
  menu_navigate_to("Style", style_roller_create);
}

// ============================================================================
// Shepard Wide Retrigger Spacing Roller (2 / 3 / 4 semitones)
// ============================================================================

static void wide_semis_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  uint8_t semis = (uint8_t)(selected_index + 2);  // 0 -> 2, 1 -> 3, 2 -> 4
  if (semis < 2) semis = 2;
  if (semis > 4) semis = 4;
  scene->rtg_config.shepard_wide_semis = semis;

  rtg_apply_config(&scene->rtg_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "RTG", menu_page_rtg_scene_create);
}

static lv_obj_t* wide_semis_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint8_t semis = scene->rtg_config.shepard_wide_semis;
  if (semis < 2) semis = 2;
  if (semis > 4) semis = 4;
  uint32_t current = (uint32_t)(semis - 2);

  return menu_create_roller_page("Retrigger",
    "2 semis\n3 semis\n4 semis", current, wide_semis_confirm_cb, NULL);
}

static void nav_to_wide_semis(void* user_data) {
  (void)user_data;
  menu_navigate_to("Retrigger", wide_semis_roller_create);
}

// ============================================================================
// Smooth Roller (Shepard mode label for the existing glide field)
// ============================================================================

static void smooth_confirm_cb(uint32_t selected_index, void* user_data) {
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

static lv_obj_t* smooth_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = scene->rtg_config.glide ? 1 : 0;
  return menu_create_roller_page("Smooth", "Off\nOn",
    current, smooth_confirm_cb, NULL);
}

static void nav_to_smooth(void* user_data) {
  (void)user_data;
  menu_navigate_to("Smooth", smooth_roller_create);
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
  s_rtg_items[idx++] = (menu_item_t){ s_enabled_label[buf], nav_to_enabled, NULL, false, MENU_ITEM_KIND_ROLLER };

  // Only show other options when RTG is enabled
  if (!scene->rtg_config.enabled) {
    return menu_create_page("RTG", s_rtg_items, idx);
  }

  {
    bool is_shepard = (scene->rtg_config.generator == RTG_GEN_SHEPARD);

    snprintf(s_generator_label[buf], sizeof(s_generator_label[buf]),
      "Generator\n%s", is_shepard ? "Shepard" : "Random");
    s_rtg_items[idx++] = (menu_item_t){
      s_generator_label[buf], nav_to_generator, NULL, false, MENU_ITEM_KIND_ROLLER
    };

    // Mode
    const char* mode_str = (scene->rtg_config.mode == RTG_MODE_CONTINUOUS) ? "Continuous" : "Step";
    snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Mode: %s", mode_str);
    s_rtg_items[idx++] = (menu_item_t){ s_mode_label[buf], nav_to_mode, NULL, false, MENU_ITEM_KIND_ROLLER };

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
      s_rtg_items[idx++] = (menu_item_t){ s_start_mode_label[buf], nav_to_start_mode, NULL, false, MENU_ITEM_KIND_ROLLER };

      // Rate Mode (Free Hz / Sync BPM)
      const char* rate_mode_str = (scene->rtg_config.rate_mode == RTG_RATE_MODE_SYNC) ? "Division" : "Time";
      snprintf(s_rate_mode_label[buf], sizeof(s_rate_mode_label[buf]), "Rate: %s", rate_mode_str);
      s_rtg_items[idx++] = (menu_item_t){ s_rate_mode_label[buf], nav_to_rate_mode, NULL, false, MENU_ITEM_KIND_ROLLER };

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
        s_rtg_items[idx++] = (menu_item_t){ s_rate_label[buf], nav_to_rate, NULL, false, MENU_ITEM_KIND_ROLLER };
      } else {
        // Find the label for the current multiplier
        snprintf(s_sync_mult_label[buf], sizeof(s_sync_mult_label[buf]), "Divider: %s",
          rtg_division_display(scene->rtg_config.division));
        s_rtg_items[idx++] = (menu_item_t){ s_sync_mult_label[buf], nav_to_division, NULL, false, MENU_ITEM_KIND_ROLLER };
      }

      // Probability (only in continuous mode) - applies to both generators
      uint8_t prob = scene->rtg_config.probability;
      if (prob == 0) prob = 100;
      snprintf(s_probability_label[buf], sizeof(s_probability_label[buf]), "Prob: %d%%", prob);
      s_rtg_items[idx++] = (menu_item_t){ s_probability_label[buf], nav_to_probability, NULL, false, MENU_ITEM_KIND_ROLLER };

      // Pattern (only in continuous mode) - applies to both generators
      const char* pattern_display = get_pattern_display(
        scene->rtg_config.pattern_length, scene->rtg_config.pattern_mask);
      snprintf(s_pattern_label[buf], sizeof(s_pattern_label[buf]), "Pattern: %s", pattern_display);
      s_rtg_items[idx++] = (menu_item_t){ s_pattern_label[buf], nav_to_pattern, NULL, false, MENU_ITEM_KIND_ROLLER };
    }

    if (is_shepard) {
      // Direction (Rising / Falling)
      const char* dir_str =
        (scene->rtg_config.shepard_direction == SHEPARD_DIR_FALLING) ? "Falling" : "Rising";
      snprintf(s_direction_label[buf], sizeof(s_direction_label[buf]), "Dir: %s", dir_str);
      s_rtg_items[idx++] = (menu_item_t){ s_direction_label[buf], nav_to_direction, NULL, false, MENU_ITEM_KIND_ROLLER };

      // Smooth (uses the existing glide field with a Shepard-friendly label)
      snprintf(s_glide_label[buf], sizeof(s_glide_label[buf]),
        "Smooth: %s", scene->rtg_config.glide ? "On" : "Off");
      s_rtg_items[idx++] = (menu_item_t){ s_glide_label[buf], nav_to_smooth, NULL, false, MENU_ITEM_KIND_ROLLER };

      // Style, Layout, Fade, and (when Wide) Retrigger only matter when smooth is on
      if (scene->rtg_config.glide) {
        const char* style_str = "Stream";
        switch (scene->rtg_config.shepard_style) {
          case SHEPARD_STYLE_STREAM:    style_str = "Stream"; break;
          case SHEPARD_STYLE_WIDE:      style_str = "Wide"; break;
          case SHEPARD_STYLE_CROSSFADE: style_str = "Crossfade"; break;
        }
        snprintf(s_style_label[buf], sizeof(s_style_label[buf]), "Style: %s", style_str);
        s_rtg_items[idx++] = (menu_item_t){ s_style_label[buf], nav_to_style, NULL, false, MENU_ITEM_KIND_ROLLER };

        if (scene->rtg_config.shepard_style == SHEPARD_STYLE_WIDE) {
          uint8_t semis = scene->rtg_config.shepard_wide_semis;
          if (semis < 2) semis = 2;
          if (semis > 4) semis = 4;
          snprintf(s_wide_semis_label[buf], sizeof(s_wide_semis_label[buf]),
            "Retrigger: %d semis", semis);
          s_rtg_items[idx++] = (menu_item_t){ s_wide_semis_label[buf], nav_to_wide_semis, NULL, false, MENU_ITEM_KIND_ROLLER };
        }

        const char* layout_str =
          (scene->rtg_config.shepard_layout == SHEPARD_LAYOUT_MULTI) ? "Multi-Ch" : "Single";
        snprintf(s_layout_label[buf], sizeof(s_layout_label[buf]), "Layout: %s", layout_str);
        s_rtg_items[idx++] = (menu_item_t){ s_layout_label[buf], nav_to_layout, NULL, false, MENU_ITEM_KIND_ROLLER };

        const char* fade_str = "None";
        switch (scene->rtg_config.shepard_fade) {
          case SHEPARD_FADE_NONE:    fade_str = "None"; break;
          case SHEPARD_FADE_CC11:    fade_str = "CC11"; break;
          case SHEPARD_FADE_POLY_AT: fade_str = "Poly AT"; break;
        }
        snprintf(s_fade_label[buf], sizeof(s_fade_label[buf]), "Fade: %s", fade_str);
        s_rtg_items[idx++] = (menu_item_t){ s_fade_label[buf], nav_to_fade, NULL, false, MENU_ITEM_KIND_ROLLER };
      }
    } else {
      // Random generator: classic Glide toggle
      snprintf(s_glide_label[buf], sizeof(s_glide_label[buf]),
        "Glide: %s", scene->rtg_config.glide ? "On" : "Off");
      s_rtg_items[idx++] = (menu_item_t){ s_glide_label[buf], nav_to_glide, NULL, false, MENU_ITEM_KIND_ROLLER };
    }
  }

  return menu_create_page("RTG", s_rtg_items, idx);
}
