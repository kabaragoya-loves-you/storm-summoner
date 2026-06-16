#include "scene.h"
#include "touchwheel.h"
#include "touch.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "midi_messages.h"
#include "midi_out.h"
#include "midi_local_output.h"
#include "device_config.h"
#include "assets_manager.h"
#include "config.h"
#include "app_settings.h"
#include "event_bus.h"
#include "action.h"
#include "action_migration.h"
#include "param_stream.h"
#include "tempo.h"
#include "tempo_nudge.h"
#include "input_manager.h"
#include "ui.h"
#include "screensaver.h"
#include "memory_utils.h"
#include "lfo.h"
#include "rtg.h"
#include "tilt.h"
#include "sample_hold.h"
#include "version.h"
#include "scene_inspect.h"
#include "cJSON.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

static const char* TAG = "scene";

// Scene storage paths.
// Scenes are user data and live on the RW `userdata` LittleFS partition so
// they survive ASSETS-OTA replacement of the shared content. If userdata
// is unavailable (degraded boot), fopen-on-write will silently fail and
// scene_load_manifest's existing fallback synthesizes a single in-memory
// "Scene 1" so the device remains usable.
#define SCENES_BASE_PATH USERDATA_BASE_PATH "/scenes"
#define MANIFEST_PATH    USERDATA_BASE_PATH "/scenes/manifest.json"

// Factory presets baked into the read-only assets image. On first boot the
// firmware copies each into /userdata/scenes/ as an inactive manifest entry.
// On subsequent boots, if the active assets checksum (NVS "assets_csum")
// differs from the last-seeded checksum (NVS NVS_KEY_FACTORY_SEED_CSUM),
// the same seeding pass runs again and any new factory filenames not in
// the user's manifest (and not in the tombstone list) are appended.
// See scenes/factory/ in the source tree and main/CMakeLists.txt for the
// build-side mirror into /assets/scenes/factory/.
#define FACTORY_SCENES_DIR "/assets/scenes/factory"
// Sanity cap so a malformed/oversized factory file can't OOM the boot path.
#define FACTORY_SCENE_MAX_BYTES 32768

// Persistent record of factory presets the user has deleted. We never
// resurrect a tombstoned filename even if a future assets blob still ships
// the same preset. Lives on /userdata so a userdata wipe correctly forgets
// the tombstones (and re-seeds everything).
#define FACTORY_TOMBSTONES_PATH USERDATA_BASE_PATH "/scenes/.factory_tombstones.json"

// Forward declarations
static void get_scene_filename(uint8_t scene_index, char* buffer, size_t buffer_size);
static void scene_name_to_slug(const char* name, char* slug, size_t slug_size);
static void scene_trim_name(char *name);
static int scene_count_json_files_on_disk(void);
static esp_err_t scene_read_json_name(const char *filepath, char *name, size_t name_size);
static void scene_sync_all_manifest_names_from_json(void);
static esp_err_t scene_update_manifest_name(uint8_t scene_index, const char *name);
static esp_err_t json_to_scene(cJSON* root, scene_t* scene);
static void scene_init_defaults(scene_t* scene, uint8_t index);
static void scene_cleanup_touchwheel(void);
static void scene_setup_touchwheel_for_mode(const scene_t* scene);
static void scene_refresh_cv_velocity_sources(void);
static void scene_post_updated_event(uint8_t scene_index);
static void scene_post_list_changed_event(uint8_t scene_index);
static uint8_t scene_get_first_active_index(void);
static void scene_invalidate_cache_index(uint8_t scene_index);
static esp_err_t scene_load_into_cache_slot(int cache_idx, uint8_t scene_index);
static void scene_reapply_runtime(uint8_t scene_index, scene_t *scene);

static char s_deferred_ui_module[MAX_UI_MODULE_NAME + 1];
static lv_timer_t *s_deferred_ui_module_timer = NULL;

static void scene_apply_ui_module_now(void) {
  const char *mod_name =
    s_deferred_ui_module[0] != '\0' ? s_deferred_ui_module : "beat";
  ui_draw_module_t *mod = ui_get_module_by_name(mod_name);
  if (mod) ui_set_draw_module(mod);
}

static void scene_deferred_ui_module_step2_cb(lv_timer_t *timer) {
  lv_timer_delete(timer);
  scene_apply_ui_module_now();
}

static void scene_deferred_ui_module_step1_cb(lv_timer_t *timer) {
  s_deferred_ui_module_timer = NULL;
  lv_timer_delete(timer);

  if (ui_is_in_screensaver_mode()) {
    ESP_LOGI(TAG, "Exiting screensaver (deferred) before applying scene UI module");
    screensaver_disable();
    lv_timer_t *step2 = lv_timer_create(scene_deferred_ui_module_step2_cb, 10, NULL);
    if (step2) lv_timer_set_repeat_count(step2, 1);
    else scene_apply_ui_module_now();
    return;
  }

  scene_apply_ui_module_now();
}

static void scene_apply_ui_module_for_performance(const char *module_name) {
  if (ui_is_in_programming_mode()) return;

  if (module_name && module_name[0] != '\0') {
    strncpy(s_deferred_ui_module, module_name, MAX_UI_MODULE_NAME);
    s_deferred_ui_module[MAX_UI_MODULE_NAME] = '\0';
  } else {
    s_deferred_ui_module[0] = '\0';
  }

  if (!ui_is_in_screensaver_mode()) {
    scene_apply_ui_module_now();
    return;
  }

  if (s_deferred_ui_module_timer) {
    lv_timer_delete(s_deferred_ui_module_timer);
    s_deferred_ui_module_timer = NULL;
  }

  s_deferred_ui_module_timer =
    lv_timer_create(scene_deferred_ui_module_step1_cb, 0, NULL);
  if (s_deferred_ui_module_timer) {
    lv_timer_set_repeat_count(s_deferred_ui_module_timer, 1);
  } else {
    ESP_LOGE(TAG, "Failed to schedule deferred UI module apply");
  }
}

// NVS keys
#define NVS_KEY_SCENE_MODE       "scene_mode"
#define NVS_KEY_CHANGE_MODE      "change_mode"
// 8-hex assets checksum we last reconciled factory presets against. Kept
// separate from `assets_csum` (which version.c manages) so the merge
// semantics can change later without touching the user-facing version.
#define NVS_KEY_FACTORY_SEED_CSUM "fac_seed_csum"

// Global scene manager instance
static scene_manager_t g_scene_manager = {
  .cache = NULL,  // Allocated from PSRAM in scene_init()
  .current_cache_idx = 0,
  .current_scene_index = 0,
  .pending_scene_index = 0,
  .has_pending_change = false,
  .manifest = NULL,
  .num_scenes = 1,
  .mode = SCENE_MODE_SINGLE,
  .change_mode = CHANGE_MODE_IMMEDIATE,
  .initialized = false
};

// Deferred init flag: when true, the MIDI phase of scene initialization
// (PC send, on-load actions, LFO start) was skipped because we were in
// programming mode. It will be replayed on return to performance mode.
static bool s_needs_deferred_init = false;

// Helper: Save current scene immediately if in programming mode
// Programming mode changes are always persisted; performance mode changes are temporary
static void scene_persist_if_programming(void) {
  if (ui_is_in_programming_mode()) {
    uint8_t scene_index = g_scene_manager.current_scene_index;
    esp_err_t ret = scene_save_to_flash(scene_index);
    if (ret == ESP_OK) {
      ESP_LOGD(TAG, "Scene %d saved (programming mode)", scene_index + 1);
    } else {
      ESP_LOGW(TAG, "Failed to save scene %d: %s", scene_index + 1, esp_err_to_name(ret));
    }
  }
}

// Touchwheel instance for scene encoder mode
static touchwheel_instance_t* s_scene_touchwheel = NULL;
static bool s_input_suspended = false;  // True when in programming mode

// Pitch bend return-to-center animation (declared early for cleanup function)
static esp_timer_handle_t s_pitch_bend_timer = NULL;
static esp_timer_handle_t s_tw_nudge_return_timer = NULL;
static esp_timer_handle_t s_tw_at_return_timer = NULL;

// Cached device definition for current scene
static device_def_t* s_cached_device = NULL;
static char s_cached_device_slug[64] = "";

// Helper: Get scene by index (returns current scene if it matches, otherwise error)
// For now, we only allow modifications to the current scene
static scene_t* get_scene_for_modification(uint8_t scene_index) {
  if (scene_index != g_scene_manager.current_scene_index) {
    ESP_LOGW(TAG, "Can only modify current scene (currently on %d, requested %d)", 
             g_scene_manager.current_scene_index, scene_index);
    return NULL;
  }
  
  return scene_get_current();
}

// Set default button assignments based on scene mode.
static void set_default_button_assignments(scene_t* scene) {
  scene_mode_t mode = g_scene_manager.mode;

  if (mode == SCENE_MODE_ADVANCED) {
    // Advanced: buttons navigate scenes (presets are arbitrary per scene)
    scene->button_left = action_create_scene_dec();
    scene->button_right = action_create_scene_inc();
  } else {
    // Simple and Preset Sync: buttons change presets
    scene->button_left = action_create_preset_dec();
    scene->button_right = action_create_preset_inc();
  }
  scene->button_both = action_create_inspect_scene();
}

// Initialize a single scene with defaults
static void scene_init_defaults(scene_t* scene, uint8_t index) {
  memset(scene, 0, sizeof(scene_t));
  
  // Set default name
  snprintf(scene->name, sizeof(scene->name), "Scene %d", index + 1);
  
  // Program change defaults (display Preset 1 for this device)
  scene->program_number = (uint8_t)device_config_get_min_preset();
  scene->send_pc_on_load = true;
  
  // Default touchwheel mode
  scene->touchwheel_mode = TOUCHWHEEL_MODE_PADS;
  scene->touchwheel_mode_prev_valid = false;
  scene->touchwheel_style = TOUCHWHEEL_STYLE_ODOMETER;  // Default: position-based (~15 values)
  scene->touchwheel = continuous_mapping_create(0);     // No CC hint -- user picks on enable
  scene->touchwheel.enabled = false;                    // Disabled by default (PADS mode)
  scene->touchwheel_lfo_target = LFO_TARGET_BOTH;       // Default: affect both LFOs
  scene->touchwheel_initial_value = 0;                   // Default: start at 0

  // Touchpads default to unassigned (ACTION_NONE).
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    scene->touchpads[i].action = (action_t){0};
  }

  // Set default button assignments
  set_default_button_assignments(scene);

  // On-load actions default to empty -- a blank scene fires nothing on load.
  scene->num_on_load_actions = 0;

  // Discrete trigger inputs default to unassigned
  scene->bump = (action_t){0};

  // Continuous input mappings: all disabled by default so a blank scene
  // transmits nothing. Expression keeps its CC4 (Foot Controller) hint
  // because that's the standard MIDI assignment for a sustain/expression
  // jack; the others are zeroed so the user picks a CC on enable.
  scene->expression = continuous_mapping_create(4);    // CC4 = Foot Controller
  scene->expression.enabled = false;
  scene->cv = continuous_mapping_create(0);
  scene->cv.enabled = false;
  scene->proximity = continuous_mapping_create(0);
  scene->proximity.enabled = false;
  scene->proximity.use_idle_value = true;              // Proximity returns to center
  scene->proximity.idle_value = 64;                    // Center for CC (60 for NOTE mode)
  scene->proximity.idle_timeout_ms = 1000;
  scene->proximity.polarity = POLARITY_BIPOLAR;
  scene->als = continuous_mapping_create(0);
  scene->als.enabled = false;
  scene->tilt_x = continuous_mapping_create(20);       // CC20 (defaults: disabled)
  scene->tilt_x.enabled = false;
  scene->tilt_x.use_idle_value = true;
  scene->tilt_x.idle_value = 64;
  scene->tilt_x.idle_timeout_ms = 1000;
  scene->tilt_x.polarity = POLARITY_UNIPOLAR;
  scene->tilt_y = continuous_mapping_create(21);       // CC21 (defaults: disabled)
  scene->tilt_y.enabled = false;
  scene->tilt_y.use_idle_value = true;
  scene->tilt_y.idle_value = 64;
  scene->tilt_y.idle_timeout_ms = 1000;
  scene->tilt_y.polarity = POLARITY_UNIPOLAR;
  scene->note_track = continuous_mapping_create(1);    // CC1 = Mod Wheel
  scene->note_track.enabled = false;                   // Disabled by default

  for (int i = 0; i < NUM_CC_TRIGGERS; i++) {
    scene->cc_triggers[i].cc_number = 0;
    scene->cc_triggers[i].action.type = ACTION_NONE;
    scene->cc_triggers[i].pressing = false;
  }

  // Expression jack configuration
  scene->expression_mode = EXPRESSION_MODE_PEDAL;      // Default to expression pedal mode
  
  // Default sustain mount: Piano Pedal targeting CC 64 (Damper)
  scene->sustain = action_create_piano_pedal(64);

  // Default sostenuto mount: Piano Pedal targeting CC 66 (Sostenuto)
  scene->sostenuto = action_create_piano_pedal(66);
  
  // CV input configuration
  scene->cv_input_mode = INPUT_MODE_NONE;
  
  // CV NOTE mode velocity configuration
  scene->cv_velocity_mode = VELOCITY_MODE_FIXED;       // Default to fixed velocity
  scene->cv_velocity = 100;                            // Default velocity value
  
  // Audio envelope follower defaults (when cv_input_mode = AUDIO)
  scene->audio_config.range = CV_RANGE_BIPOLAR_5V;     // Default to ±5V for audio
  scene->audio_config.sensitivity = 128;              // 1.0x gain (middle)
  scene->audio_config.attack_ms = 10;                 // Fast attack for transients
  scene->audio_config.release_ms = 200;               // Medium release
  scene->audio_config.threshold = 5;                  // Small noise gate
  scene->audio_config.polarity = AUDIO_POLARITY_ATTRACT;  // Louder = higher value

  // CV Trigger mode defaults (when cv_input_mode = TRIGGER)
  scene->cv_trigger_action.type = ACTION_NONE;
  scene->cv_trigger_threshold = 50;
  scene->cv_trigger_debounce_ms = 0;
  scene->cv_trigger_pressing = false;
  
  // Velocity modes for other continuous inputs (all default to fixed)
  scene->expression_velocity_mode = VELOCITY_MODE_FIXED;
  scene->proximity_velocity_mode = VELOCITY_MODE_FIXED;
  scene->als_velocity_mode = VELOCITY_MODE_FIXED;
  scene->tilt_x_velocity_mode = VELOCITY_MODE_FIXED;
  scene->tilt_y_velocity_mode = VELOCITY_MODE_FIXED;
  scene->tilt_x_tempo_nudge_pct = 10;
  scene->tilt_y_tempo_nudge_pct = 10;
  scene->tilt_x_tempo_nudge_direction = TEMPO_NUDGE_DIR_BOTH;
  scene->tilt_y_tempo_nudge_direction = TEMPO_NUDGE_DIR_BOTH;
  scene->expression_tempo_nudge_pct = 10;
  scene->expression_tempo_nudge_direction = TEMPO_NUDGE_DIR_BOTH;
  scene->cv_tempo_nudge_pct = 10;
  scene->cv_tempo_nudge_direction = TEMPO_NUDGE_DIR_BOTH;
  scene->proximity_tempo_nudge_pct = 10;
  scene->proximity_tempo_nudge_direction = TEMPO_NUDGE_DIR_BOTH;
  scene->touchwheel_tempo_nudge_pct = 10;
  scene->touchwheel_tempo_nudge_direction = TEMPO_NUDGE_DIR_BOTH;
  scene->touchwheel_tempo_nudge_return = TOUCHWHEEL_NUDGE_RETURN_INSTANT;
  scene->touchwheel_aftertouch_return = TOUCHWHEEL_NUDGE_RETURN_FAST;
  scene->touchwheel_tempo_floor = 20;
  scene->touchwheel_tempo_ceiling = 300;
  scene->als_tempo_nudge_pct = 10;
  scene->als_tempo_nudge_direction = TEMPO_NUDGE_DIR_BOTH;
  scene->lfo1_tempo_nudge_pct = 10;
  scene->lfo2_tempo_nudge_pct = 10;
  
  // Tempo configuration
  scene->bpm = 120;                                    // Default to 120 BPM
  scene->clock_source = CLOCK_SOURCE_INTERNAL;         // Default to internal clock
  scene->beat_divider = DIVIDER_QUARTER;               // Default to quarter note beats
  scene->time_signature.numerator = 4;                 // Default to 4/4 time
  scene->time_signature.denominator = 4;
  scene->use_transport = false;                        // Default: animation always runs
  scene->send_clock = true;                            // Default: send MIDI clock
  
  // LFO configuration
  scene->lfo1_config = lfo_config_create_default();
  scene->lfo2_config = lfo_config_create_default();
  scene->lfo1 = continuous_mapping_create(80);         // CC80 = General Purpose 5
  scene->lfo1.enabled = false;                         // Disabled by default
  scene->lfo2 = continuous_mapping_create(81);         // CC81 = General Purpose 6
  scene->lfo2.enabled = false;                         // Disabled by default
  scene->lfo1_velocity_mode = VELOCITY_MODE_FIXED;
  scene->lfo2_velocity_mode = VELOCITY_MODE_FIXED;

  // RTG configuration
  scene->rtg_config = rtg_config_create_default();

  // Sample+Hold configuration
  scene->sample_hold_config = sample_hold_config_create_default();
  scene->sample_hold = continuous_mapping_create(1);  // Default CC1
  scene->sample_hold.enabled = scene->sample_hold_config.enabled;
}

// Cleanup existing touchwheel instance
static void scene_cleanup_touchwheel(void) {
  // Stop and delete pitch bend return timer
  if (s_pitch_bend_timer) {
    esp_timer_stop(s_pitch_bend_timer);
    esp_timer_delete(s_pitch_bend_timer);
    s_pitch_bend_timer = NULL;
  }
  if (s_tw_nudge_return_timer) {
    esp_timer_stop(s_tw_nudge_return_timer);
    esp_timer_delete(s_tw_nudge_return_timer);
    s_tw_nudge_return_timer = NULL;
  }
  if (s_tw_at_return_timer) {
    esp_timer_stop(s_tw_at_return_timer);
    esp_timer_delete(s_tw_at_return_timer);
    s_tw_at_return_timer = NULL;
  }
  
  if (s_scene_touchwheel) {
    touch_unregister_touchwheel_instance(s_scene_touchwheel);
    touchwheel_destroy(s_scene_touchwheel);
    s_scene_touchwheel = NULL;
  }
}

// Callback for program change mode touchwheel
static void touchwheel_program_change_callback(int value, void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  // In Preset Sync mode, preset is locked to scene ordinal - ignore touchwheel PC
  if (scene_get_mode() == SCENE_MODE_PRESET_SYNC) {
    ESP_LOGW(TAG, "Touchwheel program change ignored: not allowed in Preset Sync mode");
    return;
  }
  
  (void)user_data;
  
  // value is delta from endless encoder (+1, -1, etc.)
  if (value == 0) return;
  
  // Check if bank mode is enabled for extended preset range
  bank_select_mode_t bank_mode = device_config_get_bank_mode();
  uint16_t min_preset = device_config_get_min_preset();
  uint16_t max_preset = device_config_get_max_preset();
  
  ESP_LOGD(TAG, "PC touchwheel: bank_mode=%d, min=%u, max=%u, wrap=%d, count=%u",
    bank_mode, (unsigned)min_preset, (unsigned)max_preset,
    config_get_preset_wrap(), (unsigned)device_config_get_preset_count());
  
  if (bank_mode != BANK_SELECT_NONE) {
    // Bank mode: use preset-based calculation, respecting device preset count and indexBase
    uint16_t base_preset = device_config_has_pending_program()
                           ? device_config_get_pending_preset()
                           : device_config_get_preset();
    int new_preset = (int)base_preset + value;
    
    // Clamp or wrap at boundaries based on wrap setting
    bool wrap = config_get_preset_wrap();
    int range = (int)max_preset - (int)min_preset + 1;
    if (wrap) {
      // Wrap around within valid range
      while (new_preset < (int)min_preset) new_preset += range;
      while (new_preset > (int)max_preset) new_preset -= range;
    } else {
      // Clamp at boundaries (no wrap)
      if (new_preset < (int)min_preset) new_preset = (int)min_preset;
      if (new_preset > (int)max_preset) new_preset = (int)max_preset;
    }
    
    if ((uint16_t)new_preset == base_preset) return;
    
    // Respect immediate/pending mode
    if (device_config_get_pc_mode() == PC_MODE_IMMEDIATE) {
      device_config_set_preset((uint16_t)new_preset);
      ESP_LOGD(TAG, "Touchwheel preset change: %u -> %d (bank %d, prog %d)", 
               (unsigned)base_preset, new_preset, new_preset / 128, new_preset % 128);
    } else {
      device_config_set_pending_preset((uint16_t)new_preset);
      ESP_LOGI(TAG, "Touchwheel pending preset: %d (bank %d, prog %d)", 
               new_preset, new_preset / 128, new_preset % 128);
    }
    return;
  }
  
  // No bank mode: respect indexBase, cap at 127
  uint8_t min_prog = (uint8_t)min_preset;
  uint8_t max_prog = (max_preset > 127) ? 127 : (uint8_t)max_preset;
  uint8_t base = device_config_has_pending_program() 
                 ? device_config_get_pending_program() 
                 : device_config_get_program();
  int new_program = (int)base + value;
  
  bool should_clamp = !config_get_preset_wrap();
  int range = (int)max_prog - (int)min_prog + 1;
  
  if (should_clamp) {
    // Clamp at boundaries (no wrap)
    if (new_program < (int)min_prog) new_program = (int)min_prog;
    if (new_program > (int)max_prog) new_program = (int)max_prog;
  } else {
    // Wrap around within valid range (only if not locked)
    while (new_program < (int)min_prog) new_program += range;
    while (new_program > (int)max_prog) new_program -= range;
  }
  
  if ((uint8_t)new_program == base) return;
  
  // Respect immediate/pending mode
  if (device_config_get_pc_mode() == PC_MODE_IMMEDIATE) {
    device_config_set_program((uint8_t)new_program);
    ESP_LOGD(TAG, "Touchwheel program change: %d -> %d", base, new_program);
  } else {
    // Set as pending - will be confirmed by confirm_pending action
    device_config_set_pending_program((uint8_t)new_program);
    ESP_LOGI(TAG, "Touchwheel pending program: %d (confirm to send)", new_program);
  }
}

// Tracked value for endless encoder continuous mode
static int s_touchwheel_endless_value = 64;  // Start at center
static int s_touchwheel_last_sent_cc = -1;   // Last sent CC value (-1 = none sent yet)

// Tracked values for new touchwheel modes
static int s_touchwheel_tempo_bpm = 120;           // BPM 20-300
static int s_touchwheel_pitch_bend = 0;            // -8192 to 8191, center at 0
static int s_touchwheel_prev_pitch_bend = 0;       // Previous value for smooth interpolation
static int s_touchwheel_aftertouch = 0;            // 0-127
static int s_touchwheel_14bit_value = 0;           // For NRPN/RPN/DoubleCC (0-16383)
static volatile uint8_t s_touchwheel_velocity = 100; // For TOUCHWHEEL_MODE_VELOCITY / CV takeover
static volatile uint8_t s_proximity_velocity_sample = 64;
static volatile uint8_t s_als_velocity_sample = 64;
static volatile uint8_t s_touchwheel_lfo_rate = 64;   // For TOUCHWHEEL_MODE_LFO_RATE (64 = center/default)
static volatile uint8_t s_touchwheel_lfo_depth = 127; // For TOUCHWHEEL_MODE_LFO_DEPTH (127 = full depth)
static volatile uint8_t s_touchwheel_rtg_rate = 64;   // For TOUCHWHEEL_MODE_RTG_RATE (64 = center/default)

// External LFO rate sources (updated from sensor events)
static volatile uint8_t s_expression_lfo_rate = 64;
static volatile uint8_t s_cv_lfo_rate = 64;
static volatile uint8_t s_als_lfo_rate = 64;
static volatile uint8_t s_proximity_lfo_rate = 64;
static volatile uint8_t s_tilt_x_lfo_rate = 64;
static volatile uint8_t s_tilt_y_lfo_rate = 64;

// Pitch bend return-to-center animation
// Precalculated eased return sequences for each distance bucket
// Uses ease-out curve (fast start, slow finish) for natural feel
#define PB_ANIM_INTERVAL_US  10000  // 10ms between frames (100Hz)

// Return sequences for 7 distance buckets (1=closest to center, 7=furthest)
// Each sequence eases from the bucket's value down to 0
static const int16_t pb_return_1[] = {1024, 0};  // 2 frames
static const int16_t pb_return_2[] = {1600, 800, 0};  // 3 frames
static const int16_t pb_return_3[] = {2800, 1800, 900, 0};  // 4 frames
static const int16_t pb_return_4[] = {4200, 3000, 1800, 800, 0};  // 5 frames
static const int16_t pb_return_5[] = {5400, 4200, 3000, 1900, 900, 0};  // 6 frames
static const int16_t pb_return_6[] = {6400, 5200, 4000, 2800, 1700, 800, 0};  // 7 frames
static const int16_t pb_return_7[] = {7200, 6000, 4800, 3600, 2500, 1500, 700, 0};  // 8 frames

static const int16_t* pb_return_sequences[] = {
  pb_return_1, pb_return_2, pb_return_3, pb_return_4,
  pb_return_5, pb_return_6, pb_return_7
};
static const uint8_t pb_return_lengths[] = {2, 3, 4, 5, 6, 7, 8};

// Animation state
static const int16_t* s_pb_anim_sequence = NULL;  // Current sequence being played
static uint8_t s_pb_anim_length = 0;              // Length of current sequence
static uint8_t s_pb_anim_index = 0;               // Current frame index
static bool s_pb_anim_negative = false;           // True if returning from negative value

// Continuous note mode state - supports polyphony up to 4 voices
typedef struct {
  uint8_t note;
  bool active;
  esp_timer_handle_t release_timer;  // For latch mode delayed release
} poly_voice_t;

static poly_voice_t s_poly_voices[MAX_POLY_VOICES] = {0};
static uint8_t s_current_voice_idx = 0;  // Round-robin for poly mode

// Legacy mono mode state (for compatibility)
static bool s_touchwheel_note_active = false;
static uint8_t s_touchwheel_current_note = 0;

// Forward declaration for timer callback
static void latch_release_timer_cb(void* arg);

// Get the next available voice for poly mode
static int get_next_poly_voice(uint8_t note) {
  // First, check if this note is already playing
  for (int i = 0; i < MAX_POLY_VOICES; i++) {
    if (s_poly_voices[i].active && s_poly_voices[i].note == note) {
      return i;  // Retrigger same voice
    }
  }
  // Find an inactive voice
  for (int i = 0; i < MAX_POLY_VOICES; i++) {
    if (!s_poly_voices[i].active) return i;
  }
  // All voices active, steal oldest (round-robin)
  int victim = s_current_voice_idx;
  s_current_voice_idx = (s_current_voice_idx + 1) % MAX_POLY_VOICES;
  return victim;
}

// Clean up any active touchwheel notes (call before mode changes, suspend, disable, etc.)
static void touchwheel_cleanup_active_notes(void) {
  uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
  
  // Clean up poly voices
  for (int i = 0; i < MAX_POLY_VOICES; i++) {
    if (s_poly_voices[i].active) {
      send_note_off(channel, s_poly_voices[i].note, 0);
      ESP_LOGI(TAG, "Touchwheel Note OFF (cleanup): %d", s_poly_voices[i].note);
      s_poly_voices[i].active = false;
    }
    if (s_poly_voices[i].release_timer) {
      esp_timer_stop(s_poly_voices[i].release_timer);
    }
  }
  
  // Clean up mono state
  if (s_touchwheel_note_active) {
    send_note_off(channel, s_touchwheel_current_note, 0);
    ESP_LOGI(TAG, "Touchwheel Note OFF (cleanup): %d", s_touchwheel_current_note);
    s_touchwheel_note_active = false;
  }
}

// Public wrapper for cleanup (called from console commands, etc.)
void scene_touchwheel_cleanup_notes(void) {
  touchwheel_cleanup_active_notes();
}

// Timer callback for latch mode delayed note release
static void latch_release_timer_cb(void* arg) {
  int voice_idx = (int)(intptr_t)arg;
  if (voice_idx < 0 || voice_idx >= MAX_POLY_VOICES) return;
  
  poly_voice_t* voice = &s_poly_voices[voice_idx];
  if (voice->active) {
    uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
    send_note_off(channel, voice->note, 0);
    ESP_LOGD(TAG, "Latch release: Note OFF %d (voice %d)", voice->note, voice_idx);
    voice->active = false;
  }
}

static void touchwheel_stop_nudge_return(void);
static void touchwheel_start_nudge_return(scene_t* scene);

// Release callback for continuous mode touchwheel (note off)
static void touchwheel_continuous_release_callback(void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = (scene_t*)user_data;
  if (scene && scene->touchwheel.output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
    touchwheel_start_nudge_return(scene);
    return;
  }
  
  // In latch mode, don't release immediately - start timers
  if (scene && scene->touchwheel.note_latch) {
    uint16_t release_ms = scene->touchwheel.note_release_ms;
    if (release_ms < 100) release_ms = 500;  // Default
    
    // Start release timers for all active voices
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
      if (s_poly_voices[i].active) {
        // Create timer if needed
        if (!s_poly_voices[i].release_timer) {
          esp_timer_create_args_t args = {
            .callback = latch_release_timer_cb,
            .arg = (void*)(intptr_t)i,
            .name = "latch_rel"
          };
          esp_timer_create(&args, &s_poly_voices[i].release_timer);
        }
        esp_timer_stop(s_poly_voices[i].release_timer);
        esp_timer_start_once(s_poly_voices[i].release_timer, release_ms * 1000);
      }
    }
    
    // Handle mono mode latch
    if (s_touchwheel_note_active) {
      // For mono, just use voice 0's timer
      if (!s_poly_voices[0].release_timer) {
        esp_timer_create_args_t args = {
          .callback = latch_release_timer_cb,
          .arg = (void*)(intptr_t)0,
          .name = "latch_rel"
        };
        esp_timer_create(&args, &s_poly_voices[0].release_timer);
      }
      s_poly_voices[0].note = s_touchwheel_current_note;
      s_poly_voices[0].active = true;
      s_touchwheel_note_active = false;  // Transfer to poly tracking
      esp_timer_stop(s_poly_voices[0].release_timer);
      esp_timer_start_once(s_poly_voices[0].release_timer, release_ms * 1000);
    }
    return;
  }
  
  // Non-latch mode: immediate release
  touchwheel_cleanup_active_notes();
}

// Callback for set_tempo mode touchwheel
static void touchwheel_tempo_callback(int value, void* user_data) {
  // Don't change tempo in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = (scene_t*)user_data;
  (void)scene;  // May use for style check later
  
  if (value == 0) return;
  
  uint16_t floor = scene->touchwheel_tempo_floor;
  uint16_t ceiling = scene->touchwheel_tempo_ceiling;
  if (floor < 20) floor = 20;
  if (ceiling > 300) ceiling = 300;
  if (floor > ceiling) floor = ceiling;

  s_touchwheel_tempo_bpm += value;
  if (s_touchwheel_tempo_bpm < (int)floor) s_touchwheel_tempo_bpm = (int)floor;
  if (s_touchwheel_tempo_bpm > (int)ceiling) s_touchwheel_tempo_bpm = (int)ceiling;
  
  tempo_set_bpm((uint16_t)s_touchwheel_tempo_bpm);
  ESP_LOGD(TAG, "Touchwheel tempo: %d BPM", s_touchwheel_tempo_bpm);
}

// Callback for pitch bend mode touchwheel (true bipolar - position maps directly to value)
static void touchwheel_pitch_bend_callback(int value, void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  (void)user_data;
  
  // Stop any running return animation when finger touches
  if (s_pitch_bend_timer) {
    esp_timer_stop(s_pitch_bend_timer);
  }
  
  // Bipolar mode: value is position from -100 to +100 (approx)
  // Scale to pitch bend range -8192 to +8191
  int new_pitch_bend = (value * 8192) / 100;
  
  // Clamp to valid range
  if (new_pitch_bend < -8192) new_pitch_bend = -8192;
  if (new_pitch_bend > 8191) new_pitch_bend = 8191;
  
  uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
  
  // Calculate delta from previous position
  int delta = new_pitch_bend - s_touchwheel_prev_pitch_bend;
  if (delta < 0) delta = -delta;
  
  // Smooth interpolation for large jumps
  if (delta > 800) {
    // Determine number of intermediate steps based on delta size
    int steps = (delta > 4000) ? 5 : (delta > 2500) ? 4 : (delta > 1500) ? 3 : 2;
    
    // Send intermediate values
    for (int i = 1; i < steps; i++) {
      int interp = s_touchwheel_prev_pitch_bend + 
        ((new_pitch_bend - s_touchwheel_prev_pitch_bend) * i) / steps;
      send_pitch_bend(channel, (int16_t)interp);
    }
  }
  
  // Send final value and update state
  s_touchwheel_pitch_bend = new_pitch_bend;
  s_touchwheel_prev_pitch_bend = new_pitch_bend;
  send_pitch_bend(channel, (int16_t)s_touchwheel_pitch_bend);
  ESP_LOGD(TAG, "Touchwheel pitch bend: %d", s_touchwheel_pitch_bend);
}

// Timer callback for pitch bend return-to-center animation (table-driven)
static void pitch_bend_return_timer_cb(void* arg) {
  (void)arg;
  
  // Safety check
  if (!s_pb_anim_sequence || s_pb_anim_index >= s_pb_anim_length) {
    s_touchwheel_pitch_bend = 0;
    s_touchwheel_prev_pitch_bend = 0;
    uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
    send_pitch_bend(channel, 0);
    esp_timer_stop(s_pitch_bend_timer);
    return;
  }
  
  // Get the next value from the sequence
  int16_t value = s_pb_anim_sequence[s_pb_anim_index];
  
  // Apply sign if returning from negative
  s_touchwheel_pitch_bend = s_pb_anim_negative ? -value : value;
  
  // Send the pitch bend
  uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
  send_pitch_bend(channel, (int16_t)s_touchwheel_pitch_bend);
  
  // Advance to next frame
  s_pb_anim_index++;
  
  // Stop when we've played all frames (last frame is always 0)
  if (s_pb_anim_index >= s_pb_anim_length) {
    esp_timer_stop(s_pitch_bend_timer);
    s_pb_anim_sequence = NULL;
    s_touchwheel_prev_pitch_bend = 0;  // Reset for next touch
    ESP_LOGD(TAG, "Pitch bend return complete");
  }
}

// Release callback for pitch bend - starts return-to-center animation
static void touchwheel_pitch_bend_release_callback(void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  (void)user_data;
  
  // Already at center, nothing to do
  if (s_touchwheel_pitch_bend == 0) return;
  
  // Create timer if needed
  if (!s_pitch_bend_timer) {
    const esp_timer_create_args_t timer_args = {
      .callback = pitch_bend_return_timer_cb,
      .name = "pb_return"
    };
    if (esp_timer_create(&timer_args, &s_pitch_bend_timer) != ESP_OK) {
      // Fallback: immediate return
      s_touchwheel_pitch_bend = 0;
      uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
      send_pitch_bend(channel, 0);
      return;
    }
  }
  
  // Determine if negative and get absolute value
  int abs_value = s_touchwheel_pitch_bend;
  s_pb_anim_negative = (abs_value < 0);
  if (abs_value < 0) abs_value = -abs_value;
  
  // Map absolute value to bucket (0-6)
  // Buckets: 0=0-1170, 1=1171-2340, 2=2341-3510, 3=3511-4680,
  //          4=4681-5850, 5=5851-7020, 6=7021-8192
  int bucket = (abs_value * 7) / 8192;
  if (bucket > 6) bucket = 6;
  
  // Select the return sequence for this bucket
  s_pb_anim_sequence = pb_return_sequences[bucket];
  s_pb_anim_length = pb_return_lengths[bucket];
  s_pb_anim_index = 0;
  
  ESP_LOGD(TAG, "Pitch bend return: from %d, bucket %d, %d frames",
    s_touchwheel_pitch_bend, bucket, s_pb_anim_length);
  
  // Start the periodic timer
  esp_timer_start_periodic(s_pitch_bend_timer, PB_ANIM_INTERVAL_US);
}

#define TW_NUDGE_RETURN_INTERVAL_US  20000  // 20ms between frames

static uint32_t s_tw_last_tempo_apply_ms = 0;
static uint8_t  s_tw_last_applied_midi = 64;
static int      s_tw_last_nudge_input = -9999;
static int      s_tw_nudge_endless_pos = 0;

static int32_t s_tw_nudge_return_start_bpm = 0;
static int32_t s_tw_nudge_return_target_bpm = 0;
static uint16_t s_tw_nudge_return_frame = 0;
static uint16_t s_tw_nudge_return_total_frames = 0;

static uint16_t touchwheel_nudge_return_duration_ms(uint8_t speed) {
  switch (speed) {
    case TOUCHWHEEL_NUDGE_RETURN_FAST: return 200;
    case TOUCHWHEEL_NUDGE_RETURN_MEDIUM: return 500;
    case TOUCHWHEEL_NUDGE_RETURN_SLOW: return 1000;
    default: return 0;
  }
}

static void touchwheel_nudge_return_complete(void) {
  if (s_tw_nudge_return_timer) esp_timer_stop(s_tw_nudge_return_timer);
  s_tw_nudge_return_frame = 0;
  s_tw_nudge_return_total_frames = 0;
  s_tw_last_applied_midi = 64;
  s_tw_last_nudge_input = -9999;
}

static void nudge_return_timer_cb(void* arg) {
  (void)arg;

  if (s_tw_nudge_return_total_frames == 0) {
    touchwheel_nudge_return_complete();
    return;
  }

  s_tw_nudge_return_frame++;
  if (s_tw_nudge_return_frame >= s_tw_nudge_return_total_frames) {
    tempo_set_bpm((uint16_t)s_tw_nudge_return_target_bpm);
    touchwheel_nudge_return_complete();
    ESP_LOGD(TAG, "Touchwheel tempo nudge return complete -> bpm=%d",
      (int)s_tw_nudge_return_target_bpm);
    return;
  }

  int32_t delta = s_tw_nudge_return_target_bpm - s_tw_nudge_return_start_bpm;
  int32_t new_bpm = s_tw_nudge_return_start_bpm +
    (delta * (int32_t)s_tw_nudge_return_frame) / (int32_t)s_tw_nudge_return_total_frames;
  if (new_bpm < 20) new_bpm = 20;
  if (new_bpm > 300) new_bpm = 300;
  tempo_set_bpm((uint16_t)new_bpm);
}

static void touchwheel_stop_nudge_return(void) {
  if (s_tw_nudge_return_timer) esp_timer_stop(s_tw_nudge_return_timer);
  s_tw_nudge_return_frame = 0;
  s_tw_nudge_return_total_frames = 0;
}

static void touchwheel_start_nudge_return(scene_t* scene) {
  if (!scene) return;

  touchwheel_stop_nudge_return();

  int32_t target = (int32_t)scene->bpm;
  int32_t current = (int32_t)tempo_get_bpm();
  if (current == target) {
    s_tw_last_applied_midi = 64;
    return;
  }

  uint8_t speed = scene->touchwheel_tempo_nudge_return;
  if (speed > TOUCHWHEEL_NUDGE_RETURN_SLOW) speed = TOUCHWHEEL_NUDGE_RETURN_INSTANT;

  uint16_t duration_ms = touchwheel_nudge_return_duration_ms(speed);
  if (duration_ms == 0) {
    tempo_set_bpm((uint16_t)target);
    s_tw_last_applied_midi = 64;
    ESP_LOGD(TAG, "Touchwheel tempo nudge instant return -> bpm=%d", (int)target);
    return;
  }

  s_tw_nudge_return_start_bpm = current;
  s_tw_nudge_return_target_bpm = target;
  s_tw_nudge_return_frame = 0;
  s_tw_nudge_return_total_frames = duration_ms / 20;
  if (s_tw_nudge_return_total_frames < 1) s_tw_nudge_return_total_frames = 1;

  if (!s_tw_nudge_return_timer) {
    const esp_timer_create_args_t timer_args = {
      .callback = nudge_return_timer_cb,
      .name = "tw_nudge_ret"
    };
    if (esp_timer_create(&timer_args, &s_tw_nudge_return_timer) != ESP_OK) {
      tempo_set_bpm((uint16_t)target);
      s_tw_last_applied_midi = 64;
      return;
    }
  }

  esp_timer_start_periodic(s_tw_nudge_return_timer, TW_NUDGE_RETURN_INTERVAL_US);
  ESP_LOGD(TAG, "Touchwheel tempo nudge return: %d -> %d over %u ms",
    (int)current, (int)target, (unsigned)duration_ms);
}

#define TW_AT_RETURN_INTERVAL_US  20000  // 20ms between frames

static int32_t s_tw_at_return_start = 0;
static int32_t s_tw_at_return_target = 0;
static uint16_t s_tw_at_return_frame = 0;
static uint16_t s_tw_at_return_total_frames = 0;

static void touchwheel_stop_aftertouch_return(void) {
  if (s_tw_at_return_timer) esp_timer_stop(s_tw_at_return_timer);
  s_tw_at_return_frame = 0;
  s_tw_at_return_total_frames = 0;
}

static void touchwheel_aftertouch_return_complete(void) {
  if (s_tw_at_return_timer) esp_timer_stop(s_tw_at_return_timer);
  s_tw_at_return_frame = 0;
  s_tw_at_return_total_frames = 0;
}

static void aftertouch_return_timer_cb(void* arg) {
  (void)arg;

  if (s_tw_at_return_total_frames == 0) {
    touchwheel_aftertouch_return_complete();
    return;
  }

  s_tw_at_return_frame++;
  if (s_tw_at_return_frame >= s_tw_at_return_total_frames) {
    s_touchwheel_aftertouch = 0;
    uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
    send_channel_aftertouch(channel, 0);
    touchwheel_aftertouch_return_complete();
    ESP_LOGD(TAG, "Touchwheel aftertouch return complete");
    return;
  }

  int32_t delta = s_tw_at_return_target - s_tw_at_return_start;
  int32_t new_val = s_tw_at_return_start +
    (delta * (int32_t)s_tw_at_return_frame) / (int32_t)s_tw_at_return_total_frames;
  if (new_val < 0) new_val = 0;
  if (new_val > 127) new_val = 127;
  s_touchwheel_aftertouch = (int)new_val;

  uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
  send_channel_aftertouch(channel, (uint8_t)new_val);
}

static void touchwheel_start_aftertouch_return(scene_t* scene) {
  if (!scene) return;

  touchwheel_stop_aftertouch_return();

  if (s_touchwheel_aftertouch == 0) return;

  uint8_t speed = scene->touchwheel_aftertouch_return;
  if (speed > TOUCHWHEEL_NUDGE_RETURN_SLOW) speed = TOUCHWHEEL_NUDGE_RETURN_INSTANT;

  uint16_t duration_ms = touchwheel_nudge_return_duration_ms(speed);
  if (duration_ms == 0) {
    s_touchwheel_aftertouch = 0;
    uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
    send_channel_aftertouch(channel, 0);
    ESP_LOGD(TAG, "Touchwheel aftertouch instant return");
    return;
  }

  s_tw_at_return_start = s_touchwheel_aftertouch;
  s_tw_at_return_target = 0;
  s_tw_at_return_frame = 0;
  s_tw_at_return_total_frames = duration_ms / 20;
  if (s_tw_at_return_total_frames < 1) s_tw_at_return_total_frames = 1;

  if (!s_tw_at_return_timer) {
    const esp_timer_create_args_t timer_args = {
      .callback = aftertouch_return_timer_cb,
      .name = "tw_at_ret"
    };
    if (esp_timer_create(&timer_args, &s_tw_at_return_timer) != ESP_OK) {
      s_touchwheel_aftertouch = 0;
      uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
      send_channel_aftertouch(channel, 0);
      return;
    }
  }

  esp_timer_start_periodic(s_tw_at_return_timer, TW_AT_RETURN_INTERVAL_US);
  ESP_LOGD(TAG, "Touchwheel aftertouch return: %d -> 0 over %u ms",
    s_tw_at_return_start, (unsigned)duration_ms);
}

static void touchwheel_aftertouch_release_callback(void* user_data) {
  if (ui_is_in_programming_mode()) return;

  scene_t* scene = (scene_t*)user_data;
  touchwheel_start_aftertouch_return(scene);
}

// Callback for channel aftertouch mode touchwheel
static void touchwheel_aftertouch_callback(int value, void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = (scene_t*)user_data;

  if (s_tw_at_return_timer) esp_timer_stop(s_tw_at_return_timer);
  
  uint8_t midi_value;
  
  if (scene && scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is speed-multiplied delta (can be up to ±20)
    // Scale so ~2 rotations at normal speed = full range
    // 2 rotations * 8 steps = 16 steps, 127/16 ≈ 8
    int scaled_delta = value * 8;
    s_touchwheel_aftertouch += scaled_delta;
    if (s_touchwheel_aftertouch < 0) s_touchwheel_aftertouch = 0;
    if (s_touchwheel_aftertouch > 127) s_touchwheel_aftertouch = 127;
    midi_value = (uint8_t)s_touchwheel_aftertouch;
  } else {
    // Odometer mode: value is 0-100%, scale to 0-127
    midi_value = (uint8_t)((value * 127) / 100);
    s_touchwheel_aftertouch = midi_value;
  }
  
  uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
  send_channel_aftertouch(channel, midi_value);
  ESP_LOGD(TAG, "Touchwheel aftertouch: %d", midi_value);
}

// Callback for double CC mode touchwheel (14-bit CC, MSB=cc_numbers[0], LSB=cc_numbers[0]+32)
static void touchwheel_double_cc_callback(int value, void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = (scene_t*)user_data;
  if (!scene) return;

  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is speed-multiplied delta
    // Scale so ~4 rotations at normal speed = full 14-bit range
    // Use 500 (not a multiple of 128) so both MSB and LSB visibly change
    s_touchwheel_14bit_value += value * 500;
  } else {
    // Odometer mode: value is 0-100%, scale to 0-16383
    s_touchwheel_14bit_value = (value * 16383) / 100;
  }

  if (s_touchwheel_14bit_value < 0) s_touchwheel_14bit_value = 0;
  if (s_touchwheel_14bit_value > 16383) s_touchwheel_14bit_value = 16383;

  uint8_t channel = scene_get_effective_channel(g_scene_manager.current_scene_index) - 1;
  uint16_t cc_value = (uint16_t)s_touchwheel_14bit_value;

  for (int i = 0; i < MAX_MULTI_CC; i++) {
    uint8_t msb_cc = scene->touchwheel.cc_numbers[i];
    if (msb_cc == 0) continue;
    uint8_t lsb_cc = msb_cc + 32;  // Standard 14-bit CC: LSB = MSB + 32
    send_double_control_change(channel, msb_cc, lsb_cc, cc_value);
    ESP_LOGD(TAG, "Touchwheel DoubleCC slot %d [%u/%u]: %u (MSB=%u, LSB=%u)",
      i, (unsigned)msb_cc, (unsigned)lsb_cc, (unsigned)cc_value,
      (unsigned)((cc_value >> 7) & 0x7F), (unsigned)(cc_value & 0x7F));
  }
}

// Callback for velocity mode touchwheel (internal only - no MIDI output)
static void touchwheel_velocity_callback(int value, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  uint8_t new_velocity;
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is delta, accumulate
    int current = (int)s_touchwheel_velocity;
    current += value * 4;  // Scale delta for reasonable speed
    if (current < 1) current = 1;
    if (current > 127) current = 127;
    new_velocity = (uint8_t)current;
  } else {
    // Odometer mode: value is 0-100%, scale to 1-127
    new_velocity = 1 + (value * 126) / 100;
    if (new_velocity > 127) new_velocity = 127;
  }
  
  s_touchwheel_velocity = new_velocity;
  ESP_LOGD(TAG, "Touchwheel velocity: %d", new_velocity);
}

// Callback for LFO rate mode touchwheel (internal only - modulates LFO speed)
static void touchwheel_lfo_rate_callback(int value, void* user_data) {
  (void)user_data;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  uint8_t new_rate;
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is delta, accumulate
    int current = (int)s_touchwheel_lfo_rate;
    current += value * 2;  // Scale delta
    if (current < 0) current = 0;
    if (current > 127) current = 127;
    new_rate = (uint8_t)current;
  } else {
    // Odometer mode: value is 0-100%, scale to 0-127
    new_rate = (value * 127) / 100;
    if (new_rate > 127) new_rate = 127;
  }

  s_touchwheel_lfo_rate = new_rate;

  // Apply rate to target LFO(s) using dynamic modulation
  lfo_target_t target = scene->touchwheel_lfo_target;
  if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
    lfo_set_dynamic_rate(0, new_rate);
  }
  if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
    lfo_set_dynamic_rate(1, new_rate);
  }

  ESP_LOGD(TAG, "Touchwheel LFO rate: %d (target: %d)", new_rate, target);
}

// Callback for LFO depth mode touchwheel (internal only - modulates LFO depth)
static void touchwheel_lfo_depth_callback(int value, void* user_data) {
  (void)user_data;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  uint8_t new_depth;
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is delta, accumulate
    int current = (int)s_touchwheel_lfo_depth;
    current += value * 2;  // Scale delta
    if (current < 0) current = 0;
    if (current > 127) current = 127;
    new_depth = (uint8_t)current;
  } else {
    // Odometer mode: value is 0-100%, scale to 0-127
    new_depth = (value * 127) / 100;
    if (new_depth > 127) new_depth = 127;
  }

  s_touchwheel_lfo_depth = new_depth;

  // Apply depth to target LFO(s) using dynamic modulation
  lfo_target_t target = scene->touchwheel_lfo_target;
  if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
    lfo_set_dynamic_depth(0, new_depth);
  }
  if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
    lfo_set_dynamic_depth(1, new_depth);
  }

  ESP_LOGD(TAG, "Touchwheel LFO depth: %d (target: %d)", new_depth, target);
}

// Callback for RTG rate mode touchwheel (internal only - modulates RTG speed)
static void touchwheel_rtg_rate_callback(int value, void* user_data) {
  (void)user_data;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  uint8_t new_rate;
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is delta, accumulate
    int current = (int)s_touchwheel_rtg_rate;
    current += value * 2;  // Scale delta
    if (current < 0) current = 0;
    if (current > 127) current = 127;
    new_rate = (uint8_t)current;
  } else {
    // Odometer mode: value is 0-100%, scale to 0-127
    new_rate = (value * 127) / 100;
    if (new_rate > 127) new_rate = 127;
  }

  s_touchwheel_rtg_rate = new_rate;
  ESP_LOGD(TAG, "Touchwheel RTG rate: %d", new_rate);

  // Notify RTG to update its timer rate
  rtg_touchwheel_rate_changed();
}

// Apply tempo nudge from a signed scale (-1..+1) around scene->bpm.
static void touchwheel_apply_tempo_nudge_scale(float scale, scene_t* scene, int input_key) {
  touchwheel_stop_nudge_return();

  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (now_ms - s_tw_last_tempo_apply_ms < 50) return;
  s_tw_last_tempo_apply_ms = now_ms;
  if (s_tw_last_nudge_input == input_key) return;
  s_tw_last_nudge_input = input_key;

  uint8_t pct = scene->touchwheel_tempo_nudge_pct;
  uint16_t new_bpm = tempo_nudge_compute_bpm(scene->bpm, pct, scale);
  tempo_set_bpm(new_bpm);
  ESP_LOGD(TAG, "Touchwheel tempo nudge: scale=%.2f pct=%u -> bpm=%u (base=%d)",
    (double)scale, (unsigned)pct, (unsigned)new_bpm, (int)scene->bpm);
}

static void touchwheel_tempo_nudge_bipolar_callback(int value, void* user_data) {
  if (!midi_local_output_is_enabled()) return;

  scene_t* scene = (scene_t*)user_data;
  if (!scene || !scene->touchwheel.enabled) return;
  if (scene->touchwheel.output_type != OUTPUT_TYPE_TEMPO_NUDGE) return;

  float scale = (float)value / 100.0f;
  if (scale > 1.0f) scale = 1.0f;
  if (scale < -1.0f) scale = -1.0f;
  touchwheel_apply_tempo_nudge_scale(scale, scene, value);
}

// Callback for continuous mode touchwheel (CC/Note output)
static void touchwheel_continuous_callback(int value, void* user_data) {
  // Don't send MIDI when on-device output is silenced (programming mode)
  if (!midi_local_output_is_enabled()) return;
  
  scene_t* scene = (scene_t*)user_data;
  if (!scene || !scene->touchwheel.enabled) return;

  if (scene->touchwheel.output_type == OUTPUT_TYPE_TEMPO_NUDGE &&
      scene->touchwheel_tempo_nudge_direction != TEMPO_NUDGE_DIR_BOTH) {
    uint8_t dir = scene->touchwheel_tempo_nudge_direction;
    int pos = value;

    if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
      s_tw_nudge_endless_pos += value;
      if (s_tw_nudge_endless_pos < 0) s_tw_nudge_endless_pos = 0;
      if (s_tw_nudge_endless_pos > 100) s_tw_nudge_endless_pos = 100;
      pos = s_tw_nudge_endless_pos;
    }

    float scale = (dir == TEMPO_NUDGE_DIR_FASTER)
      ? ((float)pos / 100.0f)
      : -((100.0f - (float)pos) / 100.0f);
    touchwheel_apply_tempo_nudge_scale(scale, scene, pos);
    return;
  }

  // Get device for discrete value handling
  const device_def_t* device = s_cached_device;
  uint8_t cc_num = scene->touchwheel.cc_number;
  bool has_discrete = (device && scene->touchwheel.output_type == OUTPUT_TYPE_CC &&
    assets_cc_has_discrete_values(device, cc_num));
  
  uint8_t midi_value;
  
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is a delta (+1, -1, etc.)
    if (has_discrete) {
      // For discrete CCs, cycle through discrete values
      if (value > 0) {
        s_touchwheel_endless_value = assets_get_next_discrete(device, cc_num, s_touchwheel_endless_value);
      } else if (value < 0) {
        s_touchwheel_endless_value = assets_get_prev_discrete(device, cc_num, s_touchwheel_endless_value);
      }
      midi_value = (uint8_t)s_touchwheel_endless_value;
    } else {
      // Standard continuous: increment by delta
      s_touchwheel_endless_value += value;
      // Clamp to device min/max or 0-127
      int min_val = 0, max_val = 127;
      if (device) {
        const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
        if (ctrl) {
          min_val = ctrl->min;
          max_val = ctrl->max;
        }
      }
      if (s_touchwheel_endless_value < min_val) s_touchwheel_endless_value = min_val;
      if (s_touchwheel_endless_value > max_val) s_touchwheel_endless_value = max_val;
      midi_value = (uint8_t)s_touchwheel_endless_value;
    }
  } else {
    // Odometer mode: value is 0-100%, scale to appropriate range
    if (has_discrete) {
      // Map odometer position to discrete value index
      const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
      if (ctrl && ctrl->discrete_count > 0) {
        int idx = (value * (ctrl->discrete_count - 1)) / 100;
        if (idx >= ctrl->discrete_count) idx = ctrl->discrete_count - 1;
        if (idx < 0) idx = 0;
        midi_value = ctrl->discrete_values[idx].value;
        s_touchwheel_endless_value = midi_value;  // Update for display
      } else {
        midi_value = (uint8_t)((value * 127) / 100);
      }
    } else {
      // Scale to device min/max range or 0-127
      int min_val = 0, max_val = 127;
      if (device) {
        const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
        if (ctrl) {
          min_val = ctrl->min;
          max_val = ctrl->max;
        }
      }
      midi_value = (uint8_t)(min_val + ((value * (max_val - min_val)) / 100));
    }
  }
  
  // Process through continuous mapping (applies curve, polarity, scaling)
  uint8_t output = continuous_mapping_process(midi_value, &scene->touchwheel);

  // Tempo nudge short-circuits the CC/Note path and modulates scene BPM.
  if (scene->touchwheel.output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
    float scale = tempo_nudge_scale_bipolar(output);
    touchwheel_apply_tempo_nudge_scale(scale, scene, (int)output);
    return;
  }

  // Send MIDI based on output type
  if (scene->touchwheel.output_type == OUTPUT_TYPE_CC) {
    uint8_t channel = scene_get_effective_channel(g_scene_manager.current_scene_index) - 1;
    
    // Don't send if no CCs are configured (neither multi-CC slots nor single CC)
    if (scene->touchwheel.num_cc_numbers == 0 && scene->touchwheel.cc_number == 0) {
      return;
    }
    
    // Skip sending if value hasn't changed (prevents spam at min/max)
    if ((int)output == s_touchwheel_last_sent_cc) {
      return;
    }
    s_touchwheel_last_sent_cc = (int)output;
    
    continuous_mapping_send_cc(&scene->touchwheel, channel, output);
    if (has_discrete) {
      const char* name = assets_get_discrete_name(device, cc_num, output);
      ESP_LOGD(TAG, "Touchwheel CC%d = %d (%s)", cc_num, output, name ? name : "");
    } else {
      ESP_LOGD(TAG, "Touchwheel CC = %d", output);
    }
  } else {
    // Note mode: convert value to note number and play
    uint8_t channel = scene_get_note_channel(g_scene_manager.current_scene_index) - 1;
    uint8_t note = continuous_mapping_value_to_note(output, &scene->touchwheel);
    uint8_t vel = scene->touchwheel.velocity;
    if (vel == 0) vel = 100;
    
    if (scene->touchwheel.polyphony == POLYPHONY_POLY) {
      // Polyphonic mode: each position can trigger a new voice
      int voice_idx = get_next_poly_voice(note);
      poly_voice_t* voice = &s_poly_voices[voice_idx];
      
      // Stop any pending release timer for this voice
      if (voice->release_timer) {
        esp_timer_stop(voice->release_timer);
      }
      
      // If voice was playing a different note, turn it off
      if (voice->active && voice->note != note) {
        send_note_off(channel, voice->note, 0);
      }
      
      // If this note isn't already playing on this voice, send note on
      if (!voice->active || voice->note != note) {
        send_note_on(channel, note, vel);
        ESP_LOGD(TAG, "Poly Note ON: %d vel=%d (voice %d)", note, vel, voice_idx);
      }
      
      voice->note = note;
      voice->active = true;
    } else {
      // Mono mode: single voice
      if (!s_touchwheel_note_active) {
        // First touch - send note on
        send_note_on(channel, note, vel);
        s_touchwheel_note_active = true;
        s_touchwheel_current_note = note;
        ESP_LOGD(TAG, "Touchwheel Note ON: %d vel=%d", note, vel);
      } else if (note != s_touchwheel_current_note) {
        // Note changed while holding - retrigger
        send_note_off(channel, s_touchwheel_current_note, 0);
        send_note_on(channel, note, vel);
        s_touchwheel_current_note = note;
        ESP_LOGD(TAG, "Touchwheel Note change: %d -> %d", s_touchwheel_current_note, note);
      }
    }
  }
}

// Setup touchwheel instance based on scene mode
static void scene_setup_touchwheel_for_mode(const scene_t* scene) {
  if (!scene) return;

  touchwheel_mode_t effective_mode = scene->touchwheel_mode;
  if (scene_cv_claims_source_for_scene(scene, VELOCITY_MODE_TOUCHWHEEL)) {
    effective_mode = TOUCHWHEEL_MODE_VELOCITY;
  }
  
  touchwheel_mode_processor_t* mode_proc = NULL;
  touchwheel_output_t* output = NULL;
  const char* mode_desc = NULL;
  
  switch (effective_mode) {
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE:
      mode_proc = touchwheel_mode_create_endless();
      output = touchwheel_output_callback_create(touchwheel_program_change_callback, NULL);
      mode_desc = "program_change";
      break;
      
    case TOUCHWHEEL_MODE_CONTINUOUS:
      if (scene->touchwheel.output_type == OUTPUT_TYPE_TEMPO_NUDGE &&
          scene->touchwheel_tempo_nudge_direction == TEMPO_NUDGE_DIR_BOTH) {
        mode_proc = touchwheel_mode_create_bipolar();
        mode_desc = "tempo_nudge (bipolar)";
        output = touchwheel_output_callback_create(
          touchwheel_tempo_nudge_bipolar_callback, (void*)scene);
        if (output) {
          touchwheel_output_set_release_callback(
            output, touchwheel_continuous_release_callback);
        }
        s_tw_last_nudge_input = -9999;
      } else if (scene->touchwheel.output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
        uint8_t dir = scene->touchwheel_tempo_nudge_direction;
        int start_pos = (dir == TEMPO_NUDGE_DIR_SLOWER) ? 100 : 0;
        s_tw_nudge_endless_pos = start_pos;
        if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
          mode_proc = touchwheel_mode_create_endless();
          mode_desc = "tempo_nudge (endless)";
        } else {
          mode_proc = touchwheel_mode_create_odometer();
          touchwheel_mode_set_value(mode_proc, start_pos);
          mode_desc = "tempo_nudge (odometer)";
        }
        output = touchwheel_output_callback_create(touchwheel_continuous_callback, (void*)scene);
        if (output) {
          touchwheel_output_set_release_callback(
            output, touchwheel_continuous_release_callback);
        }
        s_tw_last_nudge_input = -9999;
      } else if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "continuous (endless)";
        // Apply initial value for CC endless mode
        if (scene->touchwheel.output_type == OUTPUT_TYPE_CC) {
          s_touchwheel_endless_value = scene->touchwheel_initial_value;
        }
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "continuous (odometer)";
      }
      if (!output) {
        output = touchwheel_output_callback_create(touchwheel_continuous_callback, (void*)scene);
        if (output) {
          touchwheel_output_set_release_callback(output, touchwheel_continuous_release_callback);
        }
      }
      // Reset state when mode is initialized
      s_touchwheel_note_active = false;
      s_touchwheel_last_sent_cc = -1;  // Reset so first value is always sent
      break;
      
    case TOUCHWHEEL_MODE_SET_TEMPO:
      // Tempo mode: default to endless (only use odometer if explicitly set)
      s_touchwheel_tempo_bpm = tempo_get_bpm();  // Initialize to current BPM
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ODOMETER) {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "set_tempo (odometer)";
      } else {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "set_tempo (endless)";
      }
      output = touchwheel_output_callback_create(touchwheel_tempo_callback, (void*)scene);
      break;
      
    case TOUCHWHEEL_MODE_PITCH_BEND:
      // Pitch bend: true bipolar - position directly maps to value
      s_touchwheel_pitch_bend = 0;  // Start centered
      mode_proc = touchwheel_mode_create_bipolar();  // Position-based bipolar
      output = touchwheel_output_callback_create(touchwheel_pitch_bend_callback, NULL);
      if (output) {
        touchwheel_output_set_release_callback(output, touchwheel_pitch_bend_release_callback);
      }
      mode_desc = "pitch_bend (bipolar)";
      break;
      
    case TOUCHWHEEL_MODE_AFTERTOUCH:
      // Channel aftertouch: default to odometer, respects style
      s_touchwheel_aftertouch = 0;
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "aftertouch (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "aftertouch (odometer)";
      }
      output = touchwheel_output_callback_create(touchwheel_aftertouch_callback, (void*)scene);
      if (output) {
        touchwheel_output_set_release_callback(output, touchwheel_aftertouch_release_callback);
      }
      break;
      
    case TOUCHWHEEL_MODE_DOUBLE_CC:
      // Double CC (14-bit): default to endless for fine control
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ODOMETER) {
        s_touchwheel_14bit_value = 0;
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "double_cc (odometer)";
      } else {
        s_touchwheel_14bit_value = (scene->touchwheel_initial_value * 16383) / 127;
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "double_cc (endless)";
      }
      output = touchwheel_output_callback_create(touchwheel_double_cc_callback, (void*)scene);
      break;
      
    case TOUCHWHEEL_MODE_VELOCITY:
      // Velocity mode: internal only - no MIDI output, just provides velocity value
      // Default to odometer for discrete velocity steps
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "velocity (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "velocity (odometer)";
      }
      // Velocity callback just updates the internal velocity value
      output = touchwheel_output_callback_create(touchwheel_velocity_callback, NULL);
      break;

    case TOUCHWHEEL_MODE_LFO_RATE:
      // LFO rate mode: internal only - modulates LFO speed
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "lfo_rate (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "lfo_rate (odometer)";
      }
      // LFO rate callback just updates the internal LFO rate value
      output = touchwheel_output_callback_create(touchwheel_lfo_rate_callback, NULL);
      break;

    case TOUCHWHEEL_MODE_LFO_DEPTH:
      // LFO depth mode: internal only - modulates LFO depth/amplitude
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "lfo_depth (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "lfo_depth (odometer)";
      }
      // LFO depth callback updates the internal LFO depth value
      output = touchwheel_output_callback_create(touchwheel_lfo_depth_callback, NULL);
      break;

    case TOUCHWHEEL_MODE_RTG_RATE:
      // RTG rate mode: internal only - modulates RTG speed
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "rtg_rate (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "rtg_rate (odometer)";
      }
      // RTG rate callback just updates the internal RTG rate value
      output = touchwheel_output_callback_create(touchwheel_rtg_rate_callback, NULL);
      break;

    case TOUCHWHEEL_MODE_PADS:
    default:
      // No touchwheel instance needed for pads mode
      return;
  }
  
  if (mode_proc && output) {
    s_scene_touchwheel = touchwheel_create(mode_proc, output, 500);  // 500ms timeout
    if (s_scene_touchwheel) {
      touch_register_touchwheel_instance(s_scene_touchwheel);
      ESP_LOGD(TAG, "Created touchwheel instance for %s mode", mode_desc);
    } else {
      touchwheel_mode_destroy(mode_proc);
      touchwheel_output_destroy(output);
      ESP_LOGE(TAG, "Failed to create touchwheel instance");
    }
  } else {
    if (mode_proc) touchwheel_mode_destroy(mode_proc);
    if (output) touchwheel_output_destroy(output);
    ESP_LOGE(TAG, "Failed to create touchwheel mode or output");
  }
}

// Event handler for LFO rate source inputs
static void lfo_rate_source_event_handler(const event_t* event, void* context) {
  (void)context;
  
  switch (event->type) {
    case EVENT_EXPRESSION_VALUE:
      s_expression_lfo_rate = event->data.sensor.value;
      break;
    case EVENT_CV_VALUE:
      s_cv_lfo_rate = event->data.sensor.value;
      break;
    case EVENT_SENSOR_ALS:
      s_als_lfo_rate = event->data.sensor.value;
      break;
    case EVENT_SENSOR_PROXIMITY:
      s_proximity_lfo_rate = event->data.sensor.value;
      break;
    case EVENT_SENSOR_TILT_X:
      s_tilt_x_lfo_rate = event->data.sensor.value;
      break;
    case EVENT_SENSOR_TILT_Y:
      s_tilt_y_lfo_rate = event->data.sensor.value;
      break;
    default:
      break;
  }
}

// Walk g_scene_manager.manifest and return the smallest scene index in
// [0, MAX_SCENE_INDEX] that is not currently used. Returns -1 if every
// index is taken (manifest full).
static int next_free_scene_index(void) {
  for (int candidate = 0; candidate <= MAX_SCENE_INDEX; candidate++) {
    bool taken = false;
    for (int i = 0; i < g_scene_manager.num_scenes; i++) {
      if (g_scene_manager.manifest[i].index == candidate) {
        taken = true;
        break;
      }
    }
    if (!taken) return candidate;
  }
  return -1;
}

// Load the factory-preset tombstone file (JSON array of filenames) into a
// freshly-allocated cJSON tree. Returns NULL if the file doesn't exist or
// can't be parsed. On parse error the tombstone file is left alone — the
// caller fails closed (treats every filename as tombstoned) to avoid
// resurrecting presets the user deleted.
static cJSON *factory_tombstones_load(void) {
  FILE *f = fopen(FACTORY_TOMBSTONES_PATH, "r");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (fsize <= 0 || fsize > 16384) { fclose(f); return NULL; }
  char *buf = malloc((size_t)fsize + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t nread = fread(buf, 1, (size_t)fsize, f);
  fclose(f);
  if (nread != (size_t)fsize) { free(buf); return NULL; }
  buf[fsize] = '\0';
  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if (!root) return NULL;
  if (!cJSON_IsArray(root)) { cJSON_Delete(root); return NULL; }
  return root;
}

// True if `fname` appears in the tombstone list. Fails closed on parse error
// (i.e. returns true when the file exists but can't be parsed) so we never
// re-seed a preset the user has explicitly deleted.
static bool factory_tombstones_contains(const char *fname) {
  struct stat st;
  if (stat(FACTORY_TOMBSTONES_PATH, &st) != 0) return false;
  cJSON *arr = factory_tombstones_load();
  if (!arr) {
    ESP_LOGW(TAG, "Tombstone file unreadable - failing closed");
    return true;
  }
  bool found = false;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, arr) {
    if (cJSON_IsString(item) && item->valuestring &&
        strcmp(item->valuestring, fname) == 0) {
      found = true;
      break;
    }
  }
  cJSON_Delete(arr);
  return found;
}

// Append a filename to the tombstone list (idempotent). Creates the file
// if it doesn't exist yet. Non-fatal: returns the error but never aborts
// the caller's flow.
static esp_err_t factory_tombstones_add(const char *fname) {
  cJSON *arr = factory_tombstones_load();
  bool created = false;
  if (!arr) {
    arr = cJSON_CreateArray();
    if (!arr) return ESP_ERR_NO_MEM;
    created = true;
  }
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, arr) {
    if (cJSON_IsString(item) && item->valuestring &&
        strcmp(item->valuestring, fname) == 0) {
      cJSON_Delete(arr);
      return ESP_OK;
    }
  }
  cJSON_AddItemToArray(arr, cJSON_CreateString(fname));
  char *json_str = cJSON_PrintUnformatted(arr);
  cJSON_Delete(arr);
  if (!json_str) return ESP_ERR_NO_MEM;
  FILE *f = fopen(FACTORY_TOMBSTONES_PATH, "w");
  if (!f) { free(json_str); return ESP_ERR_NOT_FOUND; }
  size_t len = strlen(json_str);
  size_t nwrote = fwrite(json_str, 1, len, f);
  fclose(f);
  free(json_str);
  if (nwrote != len) return ESP_FAIL;
  ESP_LOGI(TAG, "Tombstoned factory preset '%s'%s",
    fname, created ? " (created list)" : "");
  return ESP_OK;
}

// True if `fname` corresponds to a file under /assets/scenes/factory/.
// Used by scene_delete() to decide whether to tombstone. The buffer is sized
// well past NAME_MAX (255) + prefix so format-truncation analysis is happy.
static bool factory_source_exists(const char *fname) {
  if (!fname || fname[0] == '\0') return false;
  char src_path[320];
  snprintf(src_path, sizeof(src_path), "%s/%s", FACTORY_SCENES_DIR, fname);
  struct stat st;
  return stat(src_path, &st) == 0;
}

// Seed factory presets from /assets/scenes/factory/ into /userdata/scenes/
// as inactive manifest entries. Called both on first boot (when no manifest
// existed on /userdata yet) and on subsequent boots when the active assets
// checksum has changed since the last seed. Intentionally non-fatal: if the
// factory dir is missing, malformed, or any individual file fails, we log
// and move on so a busted preset can't block the device from booting.
//
// Skip rules: an entry is not seeded if (a) a file with the same basename
// already exists under /userdata/scenes/ (so the user's edits, activation,
// or prior seed are preserved), (b) the filename is in the tombstone list
// (so user-deleted presets stay deleted), or (c) the display name collides
// with an existing scene name.
//
// Returns the number of presets actually added to the manifest.
static int seed_factory_presets(void) {
  DIR *dir = opendir(FACTORY_SCENES_DIR);
  if (!dir) {
    ESP_LOGI(TAG, "No factory presets at %s (skipping seed)", FACTORY_SCENES_DIR);
    return 0;
  }

  int seeded = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    const char *fname = entry->d_name;
    size_t flen = strlen(fname);
    if (flen < 6) continue;
    if (strcmp(fname + flen - 5, ".json") != 0) continue;
    if (fname[0] == '.') continue;
    // Must fit in scene_manifest_entry_t.filename[64] including the NUL.
    if (flen >= sizeof(((scene_manifest_entry_t*)0)->filename)) {
      ESP_LOGW(TAG, "Factory '%s' filename too long (%u), skipping",
        fname, (unsigned)flen);
      continue;
    }

    // 320 is comfortably more than NAME_MAX (255) plus the longest prefix
    // we use, so gcc's format-truncation analysis is satisfied even when
    // it can't see the flen guard above.
    char src_path[320];
    char dst_path[320];
    snprintf(src_path, sizeof(src_path), "%s/%s", FACTORY_SCENES_DIR, fname);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", SCENES_BASE_PATH, fname);

    // User explicitly deleted this preset on a previous boot; never resurrect.
    if (factory_tombstones_contains(fname)) {
      ESP_LOGI(TAG, "Factory '%s' is tombstoned, skipping", fname);
      continue;
    }

    // Don't clobber existing user content. On merge boots this is the
    // normal "already seeded earlier" path; the file might also be one
    // the user has edited or activated, both of which we want to leave alone.
    struct stat st;
    if (stat(dst_path, &st) == 0) {
      ESP_LOGD(TAG, "Factory '%s' already present in /userdata, skipping", fname);
      continue;
    }

    FILE *src = fopen(src_path, "r");
    if (!src) {
      ESP_LOGW(TAG, "Factory '%s' open failed", fname);
      continue;
    }
    fseek(src, 0, SEEK_END);
    long fsize = ftell(src);
    fseek(src, 0, SEEK_SET);
    if (fsize <= 0 || fsize > FACTORY_SCENE_MAX_BYTES) {
      ESP_LOGW(TAG, "Factory '%s' size %ld out of range, skipping", fname, fsize);
      fclose(src);
      continue;
    }
    char *buf = malloc((size_t)fsize + 1);
    if (!buf) {
      ESP_LOGW(TAG, "Factory '%s' alloc failed", fname);
      fclose(src);
      continue;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, src);
    fclose(src);
    if (nread != (size_t)fsize) {
      ESP_LOGW(TAG, "Factory '%s' short read %u/%ld", fname, (unsigned)nread, fsize);
      free(buf);
      continue;
    }
    buf[fsize] = '\0';

    // Pull the display name out of the JSON for the manifest entry.
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
      ESP_LOGW(TAG, "Factory '%s' invalid JSON, skipping", fname);
      free(buf);
      continue;
    }
    cJSON *name_node = cJSON_GetObjectItem(root, "name");
    if (!name_node || !cJSON_IsString(name_node) || !name_node->valuestring) {
      ESP_LOGW(TAG, "Factory '%s' missing top-level 'name', skipping", fname);
      cJSON_Delete(root);
      free(buf);
      continue;
    }
    char name_copy[sizeof(((scene_manifest_entry_t*)0)->name)];
    strncpy(name_copy, name_node->valuestring, sizeof(name_copy) - 1);
    name_copy[sizeof(name_copy) - 1] = '\0';
    cJSON_Delete(root);

    if (scene_name_exists(name_copy, -1)) {
      ESP_LOGW(TAG, "Factory '%s' name '%s' collides with existing scene, skipping",
        fname, name_copy);
      free(buf);
      continue;
    }

    int new_index = next_free_scene_index();
    if (new_index < 0) {
      ESP_LOGW(TAG, "Manifest full, cannot seed remaining factory presets");
      free(buf);
      break;
    }

    // Copy the JSON byte-for-byte so we keep the original formatting and
    // any author-only fields the runtime doesn't currently parse.
    FILE *dst = fopen(dst_path, "w");
    if (!dst) {
      ESP_LOGW(TAG, "Factory '%s' dst open failed", fname);
      free(buf);
      continue;
    }
    size_t nwrote = fwrite(buf, 1, (size_t)fsize, dst);
    fclose(dst);
    free(buf);
    if (nwrote != (size_t)fsize) {
      ESP_LOGW(TAG, "Factory '%s' short write %u/%ld", fname, (unsigned)nwrote, fsize);
      remove(dst_path);
      continue;
    }

    scene_manifest_entry_t *grown = realloc_prefer_psram(g_scene_manager.manifest,
      (g_scene_manager.num_scenes + 1) * sizeof(scene_manifest_entry_t));
    if (!grown) {
      ESP_LOGW(TAG, "Manifest realloc failed seeding '%s'", fname);
      remove(dst_path);
      break;
    }
    g_scene_manager.manifest = grown;
    scene_manifest_entry_t *slot = &g_scene_manager.manifest[g_scene_manager.num_scenes];
    slot->index = (uint8_t)new_index;
    strncpy(slot->name, name_copy, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
    strncpy(slot->filename, fname, sizeof(slot->filename) - 1);
    slot->filename[sizeof(slot->filename) - 1] = '\0';
    slot->active = false;
    g_scene_manager.num_scenes++;
    seeded++;
    ESP_LOGI(TAG, "Seeded factory preset '%s' (index %d, inactive)",
      name_copy, new_index);
  }
  closedir(dir);
  return seeded;
}

// First active scene in manifest list order (position 0 when active).
// Used at boot so reordering the manifest does not leave us on a stale
// hardcoded scene index 0 when that slot is no longer first in the list.
static uint8_t scene_get_first_active_index(void) {
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].active)
      return g_scene_manager.manifest[i].index;
  }
  return 0;
}

esp_err_t scene_init(void) {
  if (g_scene_manager.initialized) {
    ESP_LOGW(TAG, "Scene manager already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing scene manager with flash-based storage");
  
  // Load scene mode from NVS
  uint8_t mode_val;
  if (app_settings_load_u8(NVS_KEY_SCENE_MODE, &mode_val) == ESP_OK) {
    g_scene_manager.mode = (scene_mode_t)mode_val;
  }
  
  // Load change mode from NVS
  uint8_t change_val;
  if (app_settings_load_u8(NVS_KEY_CHANGE_MODE, &change_val) == ESP_OK) {
    g_scene_manager.change_mode = (scene_change_mode_t)change_val;
  }
  
  // Allocate scene cache from PSRAM to preserve internal RAM for SPI DMA
  if (g_scene_manager.cache == NULL) {
    g_scene_manager.cache = heap_caps_malloc(SCENE_CACHE_SIZE * sizeof(scene_cache_entry_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (g_scene_manager.cache == NULL) {
      ESP_LOGE(TAG, "Failed to allocate scene cache from PSRAM");
      return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Allocated scene cache (%d bytes) from PSRAM", 
      SCENE_CACHE_SIZE * (int)sizeof(scene_cache_entry_t));
  }
  
  // Initialize cache
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    g_scene_manager.cache[i].valid = false;
    g_scene_manager.cache[i].index = 0;
  }
  
  // Load or create scene manifest. Track whether we had to synthesize a
  // default so we can persist it to /userdata after scene_0 defaults are
  // populated below (first-boot seeding).
  bool manifest_was_synthesized = false;
  esp_err_t ret = scene_load_manifest();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to load manifest (%s), rebuilding from scene files",
      esp_err_to_name(ret));
    if (scene_rebuild_manifest_from_disk(false) == ESP_OK)
      ret = scene_load_manifest();
  }
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to load manifest, creating default");
    g_scene_manager.manifest = malloc_prefer_psram(sizeof(scene_manifest_entry_t));
    if (!g_scene_manager.manifest) {
      ESP_LOGE(TAG, "Failed to allocate manifest");
      return ESP_ERR_NO_MEM;
    }
    g_scene_manager.num_scenes = 1;
    g_scene_manager.manifest[0].index = 0;
    strncpy(g_scene_manager.manifest[0].name, "Scene 1", sizeof(g_scene_manager.manifest[0].name));
    strncpy(g_scene_manager.manifest[0].filename, "scene_1.json", sizeof(g_scene_manager.manifest[0].filename));
    g_scene_manager.manifest[0].active = true;
    manifest_was_synthesized = true;
  } else {
    int on_disk = scene_count_json_files_on_disk();
    if (on_disk > g_scene_manager.num_scenes) {
      ESP_LOGW(TAG, "Manifest lists %d scene(s) but %d JSON file(s) on disk, reconciling",
        g_scene_manager.num_scenes, on_disk);
      if (scene_rebuild_manifest_from_disk(false) == ESP_OK)
        ret = scene_load_manifest();
    }
  }

  if (g_scene_manager.manifest && g_scene_manager.num_scenes > 0) {
    scene_sync_all_manifest_names_from_json();
  }
  
  // Load first scene in manifest order into cache slot 0
  uint8_t boot_index = scene_get_first_active_index();
  g_scene_manager.current_cache_idx = 0;
  g_scene_manager.current_scene_index = boot_index;

  // Load boot scene directly into cache[0] (NOT using scene_load_from_flash
  // which uses wrong slot)
  bool boot_scene_was_synthesized = false;
  char filepath[128];
  get_scene_filename(boot_index, filepath, sizeof(filepath));

  FILE* f = fopen(filepath, "r");
  if (f) {
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* json_str = malloc(fsize + 1);
    if (json_str) {
      fread(json_str, 1, fsize, f);
      fclose(f);
      json_str[fsize] = '\0';
      
      cJSON* root = cJSON_Parse(json_str);
      free(json_str);
      
      if (root) {
        // Initialize with defaults first to ensure all fields have valid values,
        // even if JSON file is missing newer fields (e.g., velocity modes)
        scene_init_defaults(&g_scene_manager.cache[0].scene, boot_index);
        ret = json_to_scene(root, &g_scene_manager.cache[0].scene);
        cJSON_Delete(root);
        
        if (ret == ESP_OK) {
          ESP_LOGI(TAG, "Loaded scene %u from flash (manifest position 0)",
            (unsigned)boot_index);
        } else {
          ESP_LOGW(TAG, "Failed to parse scene %u, using defaults", (unsigned)boot_index);
          scene_init_defaults(&g_scene_manager.cache[0].scene, boot_index);
        }
      } else {
        ESP_LOGW(TAG, "Failed to parse scene %u JSON, using defaults", (unsigned)boot_index);
        scene_init_defaults(&g_scene_manager.cache[0].scene, boot_index);
      }
    } else {
      fclose(f);
      ESP_LOGW(TAG, "Failed to allocate memory for scene %u, using defaults",
        (unsigned)boot_index);
      scene_init_defaults(&g_scene_manager.cache[0].scene, boot_index);
    }
  } else {
    ESP_LOGW(TAG, "Scene %u file not found, using defaults", (unsigned)boot_index);
    scene_init_defaults(&g_scene_manager.cache[0].scene, boot_index);
    boot_scene_was_synthesized = true;
  }

  g_scene_manager.cache[0].index = boot_index;
  g_scene_manager.cache[0].valid = true;

  // First-boot seeding: persist the synthesized manifest and/or default
  // boot scene to /userdata so the device has real files to read on next boot
  // (and so the user can see them in the web app's file browser). We only
  // write what was actually synthesized; if the file existed but failed to
  // parse, we leave it alone rather than clobbering possibly-recoverable
  // user data with defaults.
  if (assets_userdata_available()) {
    if (boot_scene_was_synthesized) {
      esp_err_t save_ret = scene_save_to_flash(boot_index);
      if (save_ret == ESP_OK)
        ESP_LOGI(TAG, "Seeded default scene %u to /userdata", (unsigned)boot_index);
      else ESP_LOGW(TAG, "Failed to seed scene %u: %s", (unsigned)boot_index,
        esp_err_to_name(save_ret));
    }
    const char *cur_csum = version_get_assets_checksum();
    bool csum_known = cur_csum && cur_csum[0] != '\0' &&
                      strcmp(cur_csum, "unknown") != 0;

    if (manifest_was_synthesized) {
      // First-boot seed: every factory preset is fair game (no tombstones
      // can exist yet because the userdata partition is fresh). Each is
      // added to g_scene_manager.manifest as an inactive entry so they
      // don't disrupt navigation but are visible in the Scenes menu / web
      // file browser for the user to activate later.
      int n_factory = seed_factory_presets();
      if (n_factory > 0) {
        ESP_LOGI(TAG, "Seeded %d factory preset(s) to /userdata", n_factory);
      }
      // Save manifest AFTER factory seeding so the on-disk file reflects
      // the synthesized "Scene 1" + every factory entry in one shot.
      esp_err_t save_ret = scene_save_manifest();
      if (save_ret == ESP_OK) ESP_LOGI(TAG, "Seeded default manifest to /userdata");
      else ESP_LOGW(TAG, "Failed to seed manifest: %s", esp_err_to_name(save_ret));
      if (csum_known) {
        app_settings_save_str(NVS_KEY_FACTORY_SEED_CSUM, cur_csum);
      }
    } else if (csum_known) {
      // Merge path: manifest already existed, but the assets blob may have
      // shipped new factory presets since we last reconciled. Compare the
      // current assets checksum against the last-seeded checksum from NVS;
      // if they differ, run the same seeding pass. Skip silently when the
      // checksum is unknown (device never had an assets OTA persist one
      // yet) so we don't stamp a meaningless value.
      char seed_csum[16] = {0};
      esp_err_t load_ret = app_settings_load_str(NVS_KEY_FACTORY_SEED_CSUM,
        seed_csum, sizeof(seed_csum));
      bool need_merge = (load_ret != ESP_OK) ||
                        strncmp(seed_csum, cur_csum, 8) != 0;
      if (need_merge) {
        ESP_LOGI(TAG, "Assets checksum changed (%s -> %s), merging factory presets",
          (load_ret == ESP_OK) ? seed_csum : "(none)", cur_csum);
        int n_new = seed_factory_presets();
        if (n_new > 0) {
          ESP_LOGI(TAG, "Merged %d new factory preset(s) from updated assets", n_new);
          esp_err_t save_ret = scene_save_manifest();
          if (save_ret != ESP_OK)
            ESP_LOGW(TAG, "Failed to save manifest after merge: %s",
              esp_err_to_name(save_ret));
        } else {
          ESP_LOGI(TAG, "No new factory presets to merge");
        }
        app_settings_save_str(NVS_KEY_FACTORY_SEED_CSUM, cur_csum);
      }
    }
  } else if (manifest_was_synthesized || boot_scene_was_synthesized) {
    ESP_LOGW(TAG, "userdata unavailable - default scene held in memory only");
  }

  // Subscribe to sensor events for LFO rate sources
  event_bus_subscribe(EVENT_EXPRESSION_VALUE, lfo_rate_source_event_handler, NULL);
  event_bus_subscribe(EVENT_CV_VALUE, lfo_rate_source_event_handler, NULL);
  event_bus_subscribe(EVENT_SENSOR_ALS, lfo_rate_source_event_handler, NULL);
  event_bus_subscribe(EVENT_SENSOR_PROXIMITY, lfo_rate_source_event_handler, NULL);
  event_bus_subscribe(EVENT_SENSOR_TILT_X, lfo_rate_source_event_handler, NULL);
  event_bus_subscribe(EVENT_SENSOR_TILT_Y, lfo_rate_source_event_handler, NULL);

  g_scene_manager.initialized = true;

  const char* mode_str = (g_scene_manager.mode == SCENE_MODE_SINGLE) ? "Single" :
                         (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  ESP_LOGI(TAG, "Scene manager initialized: mode=%s, total_scenes=%d", mode_str, g_scene_manager.num_scenes);
  
  // Initialize device current_program from scene's program_number
  // For PRESET_SYNC mode, initial scene is at position 0, so PC = 0 + min_preset
  scene_t* initial_scene = &g_scene_manager.cache[0].scene;
  uint8_t initial_program = (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC)
                            ? (uint8_t)device_config_get_min_preset()
                            : initial_scene->program_number;
  
  // In PRESET_SYNC mode, always send PC; otherwise respect per-scene flag
  if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC || initial_scene->send_pc_on_load) {
    device_config_set_program(initial_program);
    ESP_LOGI(TAG, "Sent initial PC %d on channel %d", initial_program, device_config_get_channel());
  } else {
    ESP_LOGI(TAG, "Scene loaded but send_pc_on_load=false, PC not sent");
  }
  
  // Configure tempo settings for initial scene
  tempo_set_bpm(initial_scene->bpm);
  tempo_set_source(initial_scene->clock_source);
  tempo_set_note_divider(initial_scene->beat_divider);
  tempo_set_time_signature(initial_scene->time_signature.numerator, initial_scene->time_signature.denominator);
  ESP_LOGD(TAG, "Set initial tempo: bpm=%d, source=%d, beat_divider=%d, time_sig=%d/%d", 
           initial_scene->bpm, initial_scene->clock_source, initial_scene->beat_divider,
           initial_scene->time_signature.numerator, initial_scene->time_signature.denominator);
  
  // Validate action timings against time signature (remap invalid beats to beat 1)
  action_validate_scene_timings(initial_scene);
  
  // Execute on_load actions
  if (initial_scene->num_on_load_actions > 0) {
    ESP_LOGI(TAG, "Executing %d on_load action(s)", initial_scene->num_on_load_actions);
    for (int i = 0; i < initial_scene->num_on_load_actions; i++) {
      action_execute(&initial_scene->on_load[i], 127, true);
    }
  }
  
  // Apply LFO configurations from scene to LFO engine, then apply start modes
  lfo_apply_config(0, &initial_scene->lfo1_config);
  lfo_apply_config(1, &initial_scene->lfo2_config);
  lfo_apply_start_modes();

  // Apply RTG configuration and start mode
  rtg_apply_config(&initial_scene->rtg_config);
  rtg_apply_start_mode();

  // Apply Sample+Hold configuration and start mode
  sample_hold_apply_config(&initial_scene->sample_hold_config);
  initial_scene->sample_hold.enabled = initial_scene->sample_hold_config.enabled;
  sample_hold_apply_start_mode();

  // Sync tilt per-axis enable so the unified LIS3DHTR sampling task starts
  // polling if this scene has tilt enabled. Without this, the persisted
  // scene->tilt_x.enabled flag is loaded into RAM but never reaches the
  // hardware sampling layer until the user manually toggles the axis.
  tilt_axis_set_enabled(TILT_AXIS_X, initial_scene->tilt_x.enabled);
  tilt_axis_set_enabled(TILT_AXIS_Y, initial_scene->tilt_y.enabled);

  // Setup touchwheel instance for non-buttons modes
  scene_setup_touchwheel_for_mode(initial_scene);
  
  // Post event for scene change
  event_t event = {
    .type = EVENT_SCENE_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = boot_index}
  };
  event_bus_post(&event);
  
  // Restore persisted scene if enabled
  if (config_get_persist_scene()) {
    uint8_t last_scene = config_get_last_scene();
    // Verify the scene index exists in the manifest
    bool valid = false;
    for (int i = 0; i < g_scene_manager.num_scenes; i++) {
      if (g_scene_manager.manifest[i].index == last_scene) {
        valid = true;
        break;
      }
    }
    if (valid) {
      ESP_LOGI(TAG, "Restoring persisted scene %u", (unsigned)last_scene);
      scene_set_current(last_scene);
    } else {
      ESP_LOGW(TAG, "Persisted scene %u no longer exists, staying on scene %u",
        (unsigned)last_scene, (unsigned)boot_index);
    }
  }
  
  return ESP_OK;
}

esp_err_t scene_set_current(uint8_t scene_index) {
  if (!g_scene_manager.initialized) {
    ESP_LOGE(TAG, "Scene manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  if (scene_index > MAX_SCENE_INDEX) {
    ESP_LOGE(TAG, "Invalid scene index %d", scene_index);
    return ESP_ERR_INVALID_ARG;
  }
  
  // Check if scene exists in manifest and is active
  bool scene_exists = false;
  bool scene_active = false;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) {
      scene_exists = true;
      scene_active = g_scene_manager.manifest[i].active;
      break;
    }
  }
  
  if (!scene_exists) {
    ESP_LOGE(TAG, "Scene %d does not exist in manifest", scene_index);
    return ESP_ERR_NOT_FOUND;
  }
  
  if (!scene_active) {
    ESP_LOGW(TAG, "Scene %d is inactive", scene_index + 1);
    return ESP_ERR_INVALID_STATE;
  }
  
  // In pending mode, set pending instead of changing immediately
  if (g_scene_manager.change_mode == CHANGE_MODE_PENDING) {
    g_scene_manager.pending_scene_index = scene_index;
    g_scene_manager.has_pending_change = true;
    ESP_LOGI(TAG, "Pending scene change to %d (confirm or cancel)", scene_index + 1);
    return ESP_OK;
  }
  
  if (g_scene_manager.current_scene_index == scene_index) {
    return ESP_OK;  // Already on this scene
  }

  // Open the scene-transition window. Pauses the canvas refresh timer,
  // stages any ui_set_draw_module() call inside the body (so the LVGL
  // teardown/build does not race with the heavy scene configuration), and
  // tells touch.c to drop inbound PRESS/RELEASE for the duration. Closed
  // by the matching ui_scene_transition_end() just before the final return.
  ui_scene_transition_begin();

  // Release any notes still sounding from the outgoing scene's producers
  // (touchwheel, RTG voices, LFO note mappings, sensor note mappings,
  // ACTION_NOTE pads, CV/Gate). The new scene's apply_config block below
  // will repopulate fresh state as needed.
  midi_local_output_release_all();

  // Clear any pending timed actions from the previous scene
  action_clear_pending();
  
  // Clear any active morphs from the previous scene
  action_clear_morphs();
  
  // Check if scene is already in cache
  int cache_idx = -1;
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid && g_scene_manager.cache[i].index == scene_index) {
      cache_idx = i;
      break;
    }
  }
  
  // If not in cache, need to load it
  if (cache_idx == -1) {
    // Find least recently used cache slot (for now, just use round-robin)
    cache_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;
    
    // Load new scene into cache
    esp_err_t ret = scene_load_from_flash(scene_index);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to load scene %d, using defaults", scene_index);
      scene_init_defaults(&g_scene_manager.cache[cache_idx].scene, scene_index);
    }
    
    g_scene_manager.cache[cache_idx].index = scene_index;
    g_scene_manager.cache[cache_idx].valid = true;
  }
  
  g_scene_manager.current_cache_idx = cache_idx;
  g_scene_manager.current_scene_index = scene_index;
  
  // Invalidate cached device so it reloads for the new scene
  if (s_cached_device) {
    assets_free_device(s_cached_device);
    s_cached_device = NULL;
    s_cached_device_slug[0] = '\0';
  }
  
  scene_t* new_scene = &g_scene_manager.cache[cache_idx].scene;
  
  // Clear pending preset state - scene change supersedes preset changes
  if (device_config_has_pending_program()) {
    device_config_cancel_pending_program();
  }
  
  // Update device current_program and send PC based on mode
  // For PRESET_SYNC mode, use active ordinal (0-based) + indexBase
  uint8_t program;
  if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) {
    int ordinal = 0;
    for (int i = 0; i < g_scene_manager.num_scenes; i++) {
      if (g_scene_manager.manifest[i].index == scene_index) break;
      if (g_scene_manager.manifest[i].active) ordinal++;
    }
    program = (uint8_t)(ordinal + device_config_get_min_preset());
  } else {
    program = new_scene->program_number;
  }
  
  ESP_LOGI(TAG, "Switched to scene %d: %s", scene_index + 1, new_scene->name);
  
  // Configure CV input mode before expression — NOTE mode locks expression to GATE.
  if (new_scene->cv_input_mode != INPUT_MODE_NOTE &&
      new_scene->expression_mode == EXPRESSION_MODE_GATE) {
    new_scene->expression_mode = EXPRESSION_MODE_NONE;
    new_scene->expression.enabled = false;
  }
  input_manager_cv_trigger_scene_changed();
  input_set_mode(new_scene->cv_input_mode);
  expression_set_mode(new_scene->expression_mode);
  
  // Configure tempo settings for this scene
  tempo_set_bpm(new_scene->bpm);
  tempo_set_source(new_scene->clock_source);
  tempo_set_note_divider(new_scene->beat_divider);
  tempo_set_time_signature(new_scene->time_signature.numerator, new_scene->time_signature.denominator);
  ESP_LOGD(TAG, "Set tempo: bpm=%d, source=%d, beat_divider=%d, time_sig=%d/%d", 
           new_scene->bpm, new_scene->clock_source, new_scene->beat_divider,
           new_scene->time_signature.numerator, new_scene->time_signature.denominator);
  
  // Validate action timings against time signature (remap invalid beats to beat 1)
  action_validate_scene_timings(new_scene);
  
  // Reset cut states on scene change (cut is a temporary runtime state)
  midi_out_reset_cut();

  // Apply effective TRS type for this scene
  midi_trs_type_t trs = scene_get_effective_trs_type(scene_index);
  midi_transmit_mode_t trs_mode = (midi_transmit_mode_t)assets_trs_type_to_transmit_mode(trs);
  midi_set_uart_transmit_mode(trs_mode);
  
  // Apply LFO configurations (configures but does not start - no MIDI sent)
  lfo_apply_config(0, &new_scene->lfo1_config);
  lfo_apply_config(1, &new_scene->lfo2_config);

  // Apply RTG configuration
  rtg_apply_config(&new_scene->rtg_config);

  // Apply Sample+Hold configuration
  sample_hold_apply_config(&new_scene->sample_hold_config);
  new_scene->sample_hold.enabled = new_scene->sample_hold_config.enabled;
  
  // MIDI phase: PC send, on-load actions, LFO start
  // In programming mode, defer these until returning to performance mode
  if (!ui_is_in_programming_mode()) {
    if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC || new_scene->send_pc_on_load) {
      device_config_set_program(program);
      ESP_LOGD(TAG, "Sent PC %d on channel %d", program, device_config_get_channel());
    }
    
    if (new_scene->num_on_load_actions > 0) {
      ESP_LOGD(TAG, "Executing %d on_load action(s)", new_scene->num_on_load_actions);
      for (int i = 0; i < new_scene->num_on_load_actions; i++) {
        action_execute(&new_scene->on_load[i], 127, true);
      }
    }
    
    lfo_apply_start_modes();
    rtg_apply_start_mode();
    sample_hold_apply_start_mode();
    s_needs_deferred_init = false;
  } else {
    ESP_LOGI(TAG, "Programming mode: deferring MIDI phase for scene %d", scene_index + 1);
    s_needs_deferred_init = true;
  }

  // Sync tilt per-axis enable unconditionally. This must run on every scene
  // apply (including at boot and on scene switches, regardless of programming
  // vs performance mode) so the unified LIS3DHTR sampling task picks up the
  // scene's tilt configuration. No MIDI is emitted while local output is
  // silenced (midi_tilt_scene_handler bails on !midi_local_output_is_enabled).
  tilt_axis_set_enabled(TILT_AXIS_X, new_scene->tilt_x.enabled);
  tilt_axis_set_enabled(TILT_AXIS_Y, new_scene->tilt_y.enabled);

  // Switch UI module for this scene (only in performance mode)
  scene_apply_ui_module_for_performance(new_scene->ui_module);
  
  // Setup touchwheel instance for non-buttons modes
  scene_cleanup_touchwheel();
  scene_setup_touchwheel_for_mode(new_scene);
  
  // Persist current scene index if enabled
  if (config_get_persist_scene()) {
    config_set_last_scene(scene_index);
  }
  
  // Post event for scene change
  event_t event = {
    .type = EVENT_SCENE_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = scene_index}
  };
  event_bus_post(&event);

  // Close the transition window. Flushes the staged UI module switch,
  // force-releases any pads physically held across the window, resumes
  // canvas refresh, and clears the flag so touch.c stops dropping events.
  ui_scene_transition_end();

  return ESP_OK;
}

void scene_apply_deferred_init(void) {
  if (!s_needs_deferred_init) return;
  s_needs_deferred_init = false;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  uint8_t scene_index = g_scene_manager.current_scene_index;
  ESP_LOGI(TAG, "Applying deferred MIDI init for scene %d: %s", scene_index + 1, scene->name);
  
  // Reset CC value cache to device defaults for this scene
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  action_reset_cc_values(device);
  
  // Compute program number (same logic as scene_set_current)
  uint8_t program;
  if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) {
    int ordinal = 0;
    for (int i = 0; i < g_scene_manager.num_scenes; i++) {
      if (g_scene_manager.manifest[i].index == scene_index) break;
      if (g_scene_manager.manifest[i].active) ordinal++;
    }
    program = (uint8_t)(ordinal + device_config_get_min_preset());
  } else {
    program = scene->program_number;
  }
  
  // Send PC
  if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC || scene->send_pc_on_load) {
    device_config_set_program(program);
    ESP_LOGD(TAG, "Deferred PC %d on channel %d", program, device_config_get_channel());
  }
  
  // Execute on-load actions
  if (scene->num_on_load_actions > 0) {
    ESP_LOGD(TAG, "Executing %d deferred on_load action(s)", scene->num_on_load_actions);
    for (int i = 0; i < scene->num_on_load_actions; i++) {
      action_execute(&scene->on_load[i], 127, true);
    }
  }
  
  // Start LFOs, RTG, and Sample+Hold
  lfo_apply_start_modes();
  rtg_apply_start_mode();
  sample_hold_apply_start_mode();

  // Restore LED state based on current scene's proximity setting
  // (proximity may have been enabled/disabled in programming mode)
  led_restore_baseline();

  // Switch UI module for this scene
  scene_apply_ui_module_for_performance(scene->ui_module);
}

uint8_t scene_get_current_index(void) {
  return g_scene_manager.current_scene_index;
}

scene_t* scene_get_current(void) {
  if (!g_scene_manager.initialized) return NULL;
  
  // Return current scene from cache
  int idx = g_scene_manager.current_cache_idx;
  if (idx >= 0 && idx < SCENE_CACHE_SIZE && g_scene_manager.cache[idx].valid) {
    return &g_scene_manager.cache[idx].scene;
  }
  
  return NULL;
}

esp_err_t scene_next(void) {
  // Scene navigation disabled in single mode
  if (g_scene_manager.mode == SCENE_MODE_SINGLE) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  // Find current position in manifest
  int current_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == g_scene_manager.current_scene_index) {
      current_pos = i;
      break;
    }
  }
  
  if (current_pos == -1) return ESP_ERR_INVALID_STATE;
  
  // Move to next active scene in manifest (wrap around)
  int n = g_scene_manager.num_scenes;
  for (int step = 1; step < n; step++) {
    int pos = (current_pos + step) % n;
    if (g_scene_manager.manifest[pos].active)
      return scene_set_current(g_scene_manager.manifest[pos].index);
  }
  return ESP_ERR_NOT_FOUND;  // No other active scene
}

esp_err_t scene_previous(void) {
  // Scene navigation disabled in single mode
  if (g_scene_manager.mode == SCENE_MODE_SINGLE) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  // Find current position in manifest
  int current_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == g_scene_manager.current_scene_index) {
      current_pos = i;
      break;
    }
  }
  
  if (current_pos == -1) return ESP_ERR_INVALID_STATE;
  
  // Move to previous active scene in manifest (wrap around)
  int n = g_scene_manager.num_scenes;
  for (int step = 1; step < n; step++) {
    int pos = (current_pos - step + n) % n;
    if (g_scene_manager.manifest[pos].active)
      return scene_set_current(g_scene_manager.manifest[pos].index);
  }
  return ESP_ERR_NOT_FOUND;  // No other active scene
}

esp_err_t scene_set_name(uint8_t scene_index, const char* name) {
  if (scene_index > MAX_SCENE_INDEX || !name) {
    return ESP_ERR_INVALID_ARG;
  }

  char trimmed[17];
  strncpy(trimmed, name, sizeof(trimmed) - 1);
  trimmed[sizeof(trimmed) - 1] = '\0';
  scene_trim_name(trimmed);
  if (trimmed[0] == '\0') {
    ESP_LOGW(TAG, "Scene name is required");
    return ESP_ERR_INVALID_ARG;
  }
  name = trimmed;

  // Check for name uniqueness (excluding this scene's current name)
  if (scene_name_exists(name, scene_index)) {
    ESP_LOGW(TAG, "Scene name '%s' already exists", name);
    return ESP_ERR_INVALID_ARG;
  }
  if (scene_name_is_reserved(name)) {
    ESP_LOGW(TAG, "Scene name '%s' is reserved", name);
    return ESP_ERR_INVALID_ARG;
  }

  // Find manifest entry for this scene
  int pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) {
      pos = i;
      break;
    }
  }
  if (pos == -1) return ESP_ERR_NOT_FOUND;

  // Compute old and new file paths
  char old_filepath[128], new_filepath[128];
  snprintf(old_filepath, sizeof(old_filepath), "%s/%s", SCENES_BASE_PATH,
    g_scene_manager.manifest[pos].filename);

  char new_slug[64];
  scene_name_to_slug(name, new_slug, sizeof(new_slug));
  snprintf(new_filepath, sizeof(new_filepath), "%s/%s",
    SCENES_BASE_PATH, new_slug);

  bool file_changed =
    strcmp(g_scene_manager.manifest[pos].filename, new_slug) != 0;
  bool is_current =
    (scene_index == g_scene_manager.current_scene_index);

  if (is_current) {
    // Current scene: update in-memory cache, then save fresh JSON
    scene_t* scene = scene_get_current();
    if (!scene) return ESP_ERR_INVALID_STATE;

    strncpy(scene->name, name, sizeof(scene->name) - 1);
    scene->name[sizeof(scene->name) - 1] = '\0';

    // Update manifest BEFORE save so get_scene_filename resolves to new path
    strncpy(g_scene_manager.manifest[pos].name, name,
      sizeof(g_scene_manager.manifest[pos].name) - 1);
    g_scene_manager.manifest[pos].name[
      sizeof(g_scene_manager.manifest[pos].name) - 1] = '\0';
    strncpy(g_scene_manager.manifest[pos].filename, new_slug,
      sizeof(g_scene_manager.manifest[pos].filename) - 1);
    g_scene_manager.manifest[pos].filename[
      sizeof(g_scene_manager.manifest[pos].filename) - 1] = '\0';

    // Write fresh scene JSON to (potentially new) path
    esp_err_t ret = scene_save_to_flash(scene_index);
    if (ret != ESP_OK)
      ESP_LOGW(TAG, "Failed to save renamed scene: %s",
        esp_err_to_name(ret));

    // Remove old file if the filename changed
    if (file_changed) remove(old_filepath);
  } else {
    // Non-current scene: read JSON, patch name, write to new path
    FILE* f = fopen(old_filepath, "r");
    if (!f) {
      ESP_LOGW(TAG, "Cannot open scene file: %s", old_filepath);
      return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* json_str = malloc(fsize + 1);
    if (!json_str) { fclose(f); return ESP_ERR_NO_MEM; }

    fread(json_str, 1, fsize, f);
    fclose(f);
    json_str[fsize] = '\0';

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
      ESP_LOGW(TAG, "Failed to parse scene JSON: %s", old_filepath);
      return ESP_ERR_INVALID_STATE;
    }

    // Patch the name field
    cJSON_DeleteItemFromObject(root, "name");
    cJSON_AddStringToObject(root, "name", name);

    char* updated = cJSON_Print(root);
    cJSON_Delete(root);
    if (!updated) return ESP_ERR_NO_MEM;

    FILE* out = fopen(new_filepath, "w");
    if (!out) {
      free(updated);
      return ESP_ERR_NOT_FOUND;
    }
    fwrite(updated, 1, strlen(updated), out);
    fclose(out);
    free(updated);

    // Remove old file if the filename changed
    if (file_changed) remove(old_filepath);

    // Update cache if this scene happens to be cached (prev/next)
    for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
      if (g_scene_manager.cache[i].valid &&
          g_scene_manager.cache[i].index == scene_index) {
        strncpy(g_scene_manager.cache[i].scene.name, name,
          sizeof(g_scene_manager.cache[i].scene.name) - 1);
        g_scene_manager.cache[i].scene.name[
          sizeof(g_scene_manager.cache[i].scene.name) - 1] = '\0';
        break;
      }
    }

    // Update manifest
    strncpy(g_scene_manager.manifest[pos].name, name,
      sizeof(g_scene_manager.manifest[pos].name) - 1);
    g_scene_manager.manifest[pos].name[
      sizeof(g_scene_manager.manifest[pos].name) - 1] = '\0';
    strncpy(g_scene_manager.manifest[pos].filename, new_slug,
      sizeof(g_scene_manager.manifest[pos].filename) - 1);
    g_scene_manager.manifest[pos].filename[
      sizeof(g_scene_manager.manifest[pos].filename) - 1] = '\0';
  }

  scene_save_manifest();

  scene_post_list_changed_event(scene_index);

  ESP_LOGI(TAG, "Scene %d renamed to: %s (file: %s)",
    scene_index + 1, name, new_slug);
  return ESP_OK;
}

esp_err_t scene_set_ui_module(uint8_t scene_index, const char* module_name) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  if (module_name && module_name[0] != '\0') {
    strncpy(scene->ui_module, module_name,
      sizeof(scene->ui_module) - 1);
    scene->ui_module[sizeof(scene->ui_module) - 1] = '\0';
  } else {
    scene->ui_module[0] = '\0';
  }

  scene_persist_if_programming();
  ESP_LOGI(TAG, "Scene %d ui_module set to: %s",
    scene_index + 1,
    scene->ui_module[0] ? scene->ui_module : "beat (default)");

  // Switch immediately if this is the current scene and we're in performance mode
  if (scene_index == g_scene_manager.current_scene_index) {
    scene_apply_ui_module_for_performance(scene->ui_module);
  }

  return ESP_OK;
}

const char* scene_get_ui_module(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return "beat";
  // Only current scene is kept in memory; return default for non-current
  if (scene_index != g_scene_manager.current_scene_index) return "beat";
  scene_t* scene = scene_get_current();
  if (!scene || scene->ui_module[0] == '\0') return "beat";
  return scene->ui_module;
}

esp_err_t scene_set_device_id(uint8_t scene_index, const char* device_id) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  if (device_id && device_id[0] != '\0') {
    strncpy(scene->device_id, device_id, sizeof(scene->device_id) - 1);
    scene->device_id[sizeof(scene->device_id) - 1] = '\0';
    ESP_LOGI(TAG, "Scene %d device set to: %s", scene_index + 1, device_id);
  } else {
    scene->device_id[0] = '\0';
    ESP_LOGI(TAG, "Scene %d device cleared (using global)", scene_index + 1);
  }

  scene_persist_if_programming();

  // Invalidate cached device so it reloads on next access
  if (s_cached_device) {
    assets_free_device(s_cached_device);
    s_cached_device = NULL;
    s_cached_device_slug[0] = '\0';
  }

  return ESP_OK;
}

const char* scene_get_device_id(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return NULL;

  if (scene_index != g_scene_manager.current_scene_index) {
    // Can only get device_id for current scene
    return NULL;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  return scene->device_id;
}

esp_err_t scene_clear_device_id(uint8_t scene_index) {
  return scene_set_device_id(scene_index, NULL);
}

esp_err_t scene_set_midi_channel(uint8_t scene_index, uint8_t channel) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (channel > 16) return ESP_ERR_INVALID_ARG;

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  scene->midi_channel = channel;
  scene_persist_if_programming();

  if (channel == 0) {
    ESP_LOGI(TAG, "Scene %d MIDI channel cleared (using global)", scene_index + 1);
  } else {
    ESP_LOGI(TAG, "Scene %d MIDI channel set to: %u", scene_index + 1, (unsigned)channel);
  }

  return ESP_OK;
}

uint8_t scene_get_midi_channel(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return 0;

  if (scene_index != g_scene_manager.current_scene_index) {
    return 0;  // Can only get for current scene
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 0;

  return scene->midi_channel;
}

const char* scene_get_effective_device_slug(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return NULL;

  if (scene_index != g_scene_manager.current_scene_index) {
    return NULL;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  // In single device mode, always use global device
  if (config_get_device_mode() == DEVICE_MODE_SINGLE) {
    return device_config_get_pedal_slug();
  }

  // In per-scene mode, use scene's device_id if set
  if (scene->device_id[0] != '\0') {
    return scene->device_id;
  }

  // Fall back to global device_config
  return device_config_get_pedal_slug();
}

uint8_t scene_get_effective_channel(uint8_t scene_index) {
  // In single device mode, always use global channel
  if (config_get_device_mode() == DEVICE_MODE_SINGLE) {
    return device_config_get_channel();
  }

  if (scene_index > MAX_SCENE_INDEX) {
    return device_config_get_channel();
  }

  if (scene_index != g_scene_manager.current_scene_index) {
    return device_config_get_channel();
  }

  scene_t* scene = scene_get_current();
  if (!scene) return device_config_get_channel();

  // In per-scene mode, use scene's midi_channel if set (1-16)
  if (scene->midi_channel > 0 && scene->midi_channel <= 16) {
    return scene->midi_channel;
  }

  // Fall back to global channel
  return device_config_get_channel();
}

uint8_t scene_get_note_channel(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) {
    return scene_get_effective_channel(scene_index);
  }

  if (scene_index != g_scene_manager.current_scene_index) {
    return scene_get_effective_channel(scene_index);
  }

  scene_t* scene = scene_get_current();
  if (!scene) return scene_get_effective_channel(scene_index);

  // If note_channel is explicitly set (1-16), use it
  if (scene->note_channel > 0 && scene->note_channel <= 16) {
    return scene->note_channel;
  }

  // Fall back to scene's effective channel
  return scene_get_effective_channel(scene_index);
}

esp_err_t scene_set_note_channel(uint8_t scene_index, uint8_t channel) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (channel > 16) return ESP_ERR_INVALID_ARG;

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  scene->note_channel = channel;
  scene_persist_if_programming();

  if (channel == 0) {
    ESP_LOGI(TAG, "Scene %d note channel cleared (using scene channel)", scene_index + 1);
  } else {
    ESP_LOGI(TAG, "Scene %d note channel set to: %u", scene_index + 1, (unsigned)channel);
  }

  return ESP_OK;
}

uint8_t scene_get_note_channel_setting(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return 0;
  if (scene_index != g_scene_manager.current_scene_index) return 0;

  scene_t* scene = scene_get_current();
  if (!scene) return 0;

  return scene->note_channel;
}

esp_err_t scene_set_trs_type(uint8_t scene_index, uint8_t trs_type) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (trs_type > 4) return ESP_ERR_INVALID_ARG;

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  scene->trs_type = trs_type;
  scene_persist_if_programming();

  if (trs_type == 0) {
    ESP_LOGI(TAG, "Scene %d TRS type cleared (using global)", scene_index + 1);
  } else {
    const char* names[] = {"", "Type A", "Type B", "TS", "Both"};
    ESP_LOGI(TAG, "Scene %d TRS type set to: %s", scene_index + 1, names[trs_type]);
  }

  return ESP_OK;
}

uint8_t scene_get_trs_type(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return 0;
  if (scene_index != g_scene_manager.current_scene_index) return 0;

  scene_t* scene = scene_get_current();
  if (!scene) return 0;

  return scene->trs_type;
}

midi_trs_type_t scene_get_effective_trs_type(uint8_t scene_index) {
  // In single device mode, always use global TRS type
  if (config_get_device_mode() == DEVICE_MODE_SINGLE) {
    return device_config_get_trs_type();
  }

  if (scene_index > MAX_SCENE_INDEX) {
    return device_config_get_trs_type();
  }

  if (scene_index != g_scene_manager.current_scene_index) {
    return device_config_get_trs_type();
  }

  scene_t* scene = scene_get_current();
  if (!scene) return device_config_get_trs_type();

  // In per-scene mode, use scene's trs_type if set (1-4)
  // Map: 1=A, 2=B, 3=TS, 4=Both to enum values 0-3
  if (scene->trs_type >= 1 && scene->trs_type <= 4) {
    midi_trs_type_t types[] = {
      MIDI_TRS_TYPE_A, MIDI_TRS_TYPE_B, MIDI_TRS_TYPE_TS, MIDI_TRS_TYPE_BOTH
    };
    return types[scene->trs_type - 1];
  }

  // Fall back to global TRS type
  return device_config_get_trs_type();
}

const struct device_def_t* scene_get_device(uint8_t scene_index) {
  const char* slug = scene_get_effective_device_slug(scene_index);
  if (!slug || slug[0] == '\0') {
    return NULL;  // No device configured
  }

  // Check if already cached
  if (s_cached_device && strcmp(s_cached_device_slug, slug) == 0) {
    return (const struct device_def_t*)s_cached_device;
  }

  // Free old cached device if any
  if (s_cached_device) {
    assets_free_device(s_cached_device);
    s_cached_device = NULL;
    s_cached_device_slug[0] = '\0';
  }

  // Load new device
  s_cached_device = assets_load_device(slug);
  if (s_cached_device) {
    strncpy(s_cached_device_slug, slug, sizeof(s_cached_device_slug) - 1);
    s_cached_device_slug[sizeof(s_cached_device_slug) - 1] = '\0';
    ESP_LOGI(TAG, "Loaded device: %s", slug);
  } else {
    ESP_LOGW(TAG, "Failed to load device: %s", slug);
  }

  return (const struct device_def_t*)s_cached_device;
}

esp_err_t scene_set_mode(scene_mode_t mode) {
  g_scene_manager.mode = mode;
  
  const char* mode_str = (mode == SCENE_MODE_SINGLE) ? "Single" :
                         (mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  ESP_LOGI(TAG, "Scene mode set to %s", mode_str);
  
  uint8_t mode_val = (uint8_t)mode;
  return app_settings_save_u8(NVS_KEY_SCENE_MODE, mode_val);
}

scene_mode_t scene_get_mode(void) {
  return g_scene_manager.mode;
}

esp_err_t scene_set_change_mode(scene_change_mode_t mode) {
  g_scene_manager.change_mode = mode;
  
  ESP_LOGI(TAG, "Change mode set to %s", mode == CHANGE_MODE_IMMEDIATE ? "immediate" : "pending");
  
  uint8_t mode_val = (uint8_t)mode;
  return app_settings_save_u8(NVS_KEY_CHANGE_MODE, mode_val);
}

scene_change_mode_t scene_get_change_mode(void) {
  return g_scene_manager.change_mode;
}

// Autosave mode removed - programming mode changes are always persisted immediately

esp_err_t scene_set_program_number(uint8_t scene_index, uint8_t program) {
  if (scene_index > MAX_SCENE_INDEX || program > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->program_number = program;
  scene_persist_if_programming();
  
  // Also update device's current_program and send PC
  device_config_set_program(program);
  
  ESP_LOGI(TAG, "Scene %d program number set to %d (PC sent)", scene_index + 1, program);
  return ESP_OK;
}

esp_err_t scene_set_send_pc_on_load(uint8_t scene_index, bool send_pc) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  // Get current scene if it matches
  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    scene->send_pc_on_load = send_pc;
    scene_persist_if_programming();
  } else {
    ESP_LOGW(TAG, "Can only modify current scene (load scene %d first)", scene_index);
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGI(TAG, "Scene %d send PC on load: %s", scene_index + 1, send_pc ? "enabled" : "disabled");
  return ESP_OK;
}

esp_err_t scene_set_touchwheel_mode(uint8_t scene_index, touchwheel_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;

  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    // Clean up any active notes before changing mode
    touchwheel_cleanup_active_notes();
    
    scene->touchwheel_mode = mode;
    scene_persist_if_programming();

    // Re-setup touchwheel instance for new mode
    scene_cleanup_touchwheel();
    scene_setup_touchwheel_for_mode(scene);
  } else {
    ESP_LOGW(TAG, "Can only modify current scene");
    return ESP_ERR_INVALID_STATE;
  }
  
  const char* mode_str;
  switch (mode) {
    case TOUCHWHEEL_MODE_PADS: mode_str = "pads"; break;
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE: mode_str = "program_change"; break;
    case TOUCHWHEEL_MODE_SET_TEMPO: mode_str = "set_tempo"; break;
    case TOUCHWHEEL_MODE_PITCH_BEND: mode_str = "pitch_bend"; break;
    case TOUCHWHEEL_MODE_AFTERTOUCH: mode_str = "aftertouch"; break;
    case TOUCHWHEEL_MODE_DOUBLE_CC: mode_str = "double_cc"; break;
    case TOUCHWHEEL_MODE_CONTINUOUS: mode_str = "continuous"; break;
    case TOUCHWHEEL_MODE_VELOCITY: mode_str = "velocity"; break;
    case TOUCHWHEEL_MODE_LFO_RATE: mode_str = "lfo_rate"; break;
    case TOUCHWHEEL_MODE_LFO_DEPTH: mode_str = "lfo_depth"; break;
    case TOUCHWHEEL_MODE_RTG_RATE: mode_str = "rtg_rate"; break;
    default: mode_str = "unknown"; break;
  }
  ESP_LOGI(TAG, "Scene %d touchwheel mode set to %s", scene_index + 1, mode_str);
  return ESP_OK;
}

// Runtime-only touchwheel mode change (no persistence)
// Used by touchwheel actions during performance - changes are temporary
esp_err_t scene_set_touchwheel_mode_runtime(uint8_t scene_index, touchwheel_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;

  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    // Clean up any active notes before changing mode
    touchwheel_cleanup_active_notes();
    
    scene->touchwheel_mode = mode;
    // NOTE: No persistence - this is a runtime-only change

    // Re-setup touchwheel instance for new mode
    scene_cleanup_touchwheel();
    scene_setup_touchwheel_for_mode(scene);
  } else {
    ESP_LOGW(TAG, "Can only modify current scene");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGD(TAG, "Touchwheel mode set to %d (runtime, no persist)", mode);
  return ESP_OK;
}

// Set the touchwheel's internal value (used when switching CC parameters or setting initial value)
void scene_set_touchwheel_value(uint8_t value) {
  scene_t* scene = scene_get_current();
  if (scene && scene->touchwheel_mode == TOUCHWHEEL_MODE_DOUBLE_CC) {
    s_touchwheel_14bit_value = (value * 16383) / 127;
  } else {
    s_touchwheel_endless_value = value;
    s_touchwheel_last_sent_cc = -1;  // Force next send even if same value
  }
  ESP_LOGD(TAG, "Touchwheel value set to %u", (unsigned)value);
}

uint8_t scene_get_touchwheel_value(void) {
  if (s_touchwheel_endless_value < 0) return 0;
  if (s_touchwheel_endless_value > 255) return 255;
  return (uint8_t)s_touchwheel_endless_value;
}

esp_err_t scene_set_touchpad_cc(uint8_t scene_index, uint8_t pad_index, uint8_t cc_number, uint8_t value) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS || cc_number > 127 || value > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  
  // Create simple CC action
  mapping->action = action_create_control(cc_number, value);
  
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d pad %d: CC%d value %d", 
    scene_index + 1, pad_index, cc_number, value);
  return ESP_OK;
}

touchpad_mapping_t* scene_get_touchpad_mapping(uint8_t scene_index, uint8_t pad_index) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS) return NULL;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return NULL;
  
  return &scene->touchpads[pad_index];
}

uint8_t scene_get_pending_index(void) {
  return g_scene_manager.pending_scene_index;
}

bool scene_has_pending_change(void) {
  return g_scene_manager.has_pending_change;
}

esp_err_t scene_confirm_change(void) {
  if (!g_scene_manager.has_pending_change) {
    ESP_LOGW(TAG, "No pending scene change to confirm");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Temporarily switch to immediate mode to perform the change
  scene_change_mode_t old_mode = g_scene_manager.change_mode;
  g_scene_manager.change_mode = CHANGE_MODE_IMMEDIATE;
  g_scene_manager.has_pending_change = false;
  
  esp_err_t ret = scene_set_current(g_scene_manager.pending_scene_index);
  
  // Restore pending mode
  g_scene_manager.change_mode = old_mode;
  
  return ret;
}

esp_err_t scene_cancel_pending(void) {
  if (!g_scene_manager.has_pending_change) {
    ESP_LOGW(TAG, "No pending scene change to cancel");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGI(TAG, "Cancelled pending scene change to %d", g_scene_manager.pending_scene_index + 1);
  g_scene_manager.has_pending_change = false;
  g_scene_manager.pending_scene_index = g_scene_manager.current_scene_index;
  
  return ESP_OK;
}


esp_err_t scene_save_config(void) {
  ESP_LOGI(TAG, "Saving scene configuration to NVS");
  
  uint8_t mode_val = (uint8_t)g_scene_manager.mode;
  esp_err_t ret = app_settings_save_u8(NVS_KEY_SCENE_MODE, mode_val);
  if (ret != ESP_OK) return ret;
  
  uint8_t change_val = (uint8_t)g_scene_manager.change_mode;
  return app_settings_save_u8(NVS_KEY_CHANGE_MODE, change_val);
}

esp_err_t scene_load_config(void) {
  uint8_t mode_val;
  if (app_settings_load_u8(NVS_KEY_SCENE_MODE, &mode_val) == ESP_OK) {
    g_scene_manager.mode = (scene_mode_t)mode_val;
  }
  
  uint8_t change_val;
  if (app_settings_load_u8(NVS_KEY_CHANGE_MODE, &change_val) == ESP_OK) {
    g_scene_manager.change_mode = (scene_change_mode_t)change_val;
  }
  
  return ESP_OK;
}

esp_err_t scene_process_touchpad(uint8_t pad_index, bool pressed) {
  if (!g_scene_manager.initialized || pad_index >= NUM_TOUCHPADS) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = scene_get_current();
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  if (mapping->action.type == ACTION_NONE) return ESP_OK;

  // Handle touchwheel modes - pads 0-7 are routed to touchwheel instance
  // Touchwheel instance handles program change or continuous output
  if (scene->touchwheel_mode != TOUCHWHEEL_MODE_PADS && pad_index <= TOUCHWHEEL_END) {
    // Pad events are already routed to touchwheel instance via touch.c
    // Don't process as individual pad presses
    return ESP_OK;
  }
  
  // Execute action
  ESP_LOGD(TAG, "Pad %d %s: executing %s", pad_index, 
           pressed ? "pressed" : "released", action_type_to_string(mapping->action.type));
  
  // Notify touch component when hold actions start/end to suppress health check interventions
  if (action_requires_hold_for(&mapping->action)) {
    touch_set_hold_active(pad_index, pressed);
  }
  
  return action_execute(&mapping->action, pressed ? 127 : 0, pressed);
}

esp_err_t scene_assign_touchpad_action(uint8_t scene_index, uint8_t pad_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS || !action) {
    return ESP_ERR_INVALID_ARG;
  }
  
  // Validate action against trigger type
  action_trigger_type_t trigger = (pad_index <= 7) ?
    ACTION_TRIGGER_TOUCHPAD_0_7 : ACTION_TRIGGER_TOUCHPAD_8_11;
  
  if (!action_is_valid_for_trigger_for(action, trigger)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to pad %d (invalid for this trigger)",
      action_type_to_string(action->type), pad_index);
    return ESP_ERR_NOT_SUPPORTED;
  }

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  mapping->action = *action;
  scene_persist_if_programming();

  ESP_LOGI(TAG, "Assigned action '%s' to pad %d", action_type_to_string(action->type), pad_index);
  return ESP_OK;
}

esp_err_t scene_assign_button_left(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against button trigger
  if (!action_is_valid_for_trigger_for(action, ACTION_TRIGGER_BUTTON)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to left button (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->button_left = *action;
  scene_persist_if_programming();
  ESP_LOGI(TAG, "Assigned action to left button: %s", action_type_to_string(action->type));
  return ESP_OK;
}

esp_err_t scene_assign_button_right(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against button trigger
  if (!action_is_valid_for_trigger_for(action, ACTION_TRIGGER_BUTTON)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to right button (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->button_right = *action;
  scene_persist_if_programming();
  ESP_LOGI(TAG, "Assigned action to right button: %s", action_type_to_string(action->type));
  return ESP_OK;
}

esp_err_t scene_assign_button_both(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against button trigger
  if (!action_is_valid_for_trigger_for(action, ACTION_TRIGGER_BUTTON)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to both buttons (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->button_both = *action;
  scene_persist_if_programming();
  ESP_LOGI(TAG, "Assigned action to both buttons: %s", action_type_to_string(action->type));
  return ESP_OK;
}

action_t* scene_get_button_left(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->button_left : NULL;
}

action_t* scene_get_button_right(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->button_right : NULL;
}

action_t* scene_get_button_both(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->button_both : NULL;
}

esp_err_t scene_assign_bump(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against bump trigger
  if (!action_is_valid_for_trigger_for(action, ACTION_TRIGGER_BUMP)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to bump (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->bump = *action;
  scene_persist_if_programming();
  ESP_LOGI(TAG, "Assigned action to bump: %s", action_type_to_string(action->type));
  return ESP_OK;
}

action_t* scene_get_bump(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->bump : NULL;
}

esp_err_t scene_add_on_load_action(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against on_load trigger
  if (!action_is_valid_for_trigger_for(action, ACTION_TRIGGER_ON_LOAD)) {
    ESP_LOGW(TAG, "Cannot add '%s' to on_load (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  if (scene->num_on_load_actions >= MAX_ON_LOAD_ACTIONS) {
    ESP_LOGW(TAG, "on_load already has %d actions (max)", MAX_ON_LOAD_ACTIONS);
    return ESP_ERR_NO_MEM;
  }
  
  scene->on_load[scene->num_on_load_actions++] = *action;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Added on_load action: %s (now %d total)",
    action_type_to_string(action->type), scene->num_on_load_actions);
  return ESP_OK;
}

esp_err_t scene_clear_on_load_actions(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->num_on_load_actions = 0;
  memset(scene->on_load, 0, sizeof(scene->on_load));
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Cleared on_load actions");
  return ESP_OK;
}

uint8_t scene_get_num_on_load_actions(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->num_on_load_actions : 0;
}

action_t* scene_get_on_load_action(uint8_t scene_index, uint8_t action_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene || action_index >= scene->num_on_load_actions) return NULL;
  return &scene->on_load[action_index];
}

esp_err_t scene_add_on_play_action(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against on_play trigger
  if (!action_is_valid_for_trigger_for(action, ACTION_TRIGGER_ON_PLAY)) {
    ESP_LOGW(TAG, "Cannot add '%s' to on_play (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  if (scene->num_on_play_actions >= MAX_ON_PLAY_ACTIONS) {
    ESP_LOGW(TAG, "on_play already has %d actions (max)", MAX_ON_PLAY_ACTIONS);
    return ESP_ERR_NO_MEM;
  }
  
  scene->on_play[scene->num_on_play_actions++] = *action;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Added on_play action: %s (now %d total)",
    action_type_to_string(action->type), scene->num_on_play_actions);
  return ESP_OK;
}

esp_err_t scene_clear_on_play_actions(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->num_on_play_actions = 0;
  memset(scene->on_play, 0, sizeof(scene->on_play));
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Cleared on_play actions");
  return ESP_OK;
}

uint8_t scene_get_num_on_play_actions(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->num_on_play_actions : 0;
}

action_t* scene_get_on_play_action(uint8_t scene_index, uint8_t action_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene || action_index >= scene->num_on_play_actions) return NULL;
  return &scene->on_play[action_index];
}

esp_err_t scene_set_expression_mode(uint8_t scene_index, expression_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  // State machine: Cannot change expression mode when in NOTE input mode (except to GATE)
  if (scene->cv_input_mode == INPUT_MODE_NOTE && mode != EXPRESSION_MODE_GATE) {
    ESP_LOGE(TAG, "Cannot change expression mode - locked by NOTE input mode");
    return ESP_ERR_INVALID_STATE;
  }
  
  scene->expression_mode = mode;
  
  // Auto-manage expression.enabled based on mode
  // Only EXPRESSION_MODE_PEDAL needs continuous value routing
  scene->expression.enabled = (mode == EXPRESSION_MODE_PEDAL);
  
  scene_persist_if_programming();
  
  // Update hardware configuration immediately if this is the current scene
  expression_set_mode(mode);
  
  const char* mode_str = (mode == EXPRESSION_MODE_NONE) ? "none" :
                         (mode == EXPRESSION_MODE_PEDAL) ? "expression" :
                         (mode == EXPRESSION_MODE_SUSTAIN) ? "sustain" :
                         (mode == EXPRESSION_MODE_SOSTENUTO) ? "sostenuto" :
                         (mode == EXPRESSION_MODE_SWITCH) ? "switch" : "gate";
  ESP_LOGI(TAG, "Scene %d expression mode set to %s", scene_index + 1, mode_str);
  return ESP_OK;
}

expression_mode_t scene_get_expression_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->expression_mode : EXPRESSION_MODE_PEDAL;
}

esp_err_t scene_assign_sustain(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->sustain = *action;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Assigned sustain action: %s", action_type_to_string(action->type));
  return ESP_OK;
}

esp_err_t scene_assign_sostenuto(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->sostenuto = *action;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Assigned sostenuto action: %s", action_type_to_string(action->type));
  return ESP_OK;
}

action_t* scene_get_sustain(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->sustain : NULL;
}

action_t* scene_get_sostenuto(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->sostenuto : NULL;
}

esp_err_t scene_assign_expr_switch(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against expression switch trigger
  if (!action_is_valid_for_trigger_for(action, ACTION_TRIGGER_EXPR_SWITCH)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to expr_switch (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->expr_switch = *action;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Assigned expr_switch action: %s", action_type_to_string(action->type));
  return ESP_OK;
}

action_t* scene_get_expr_switch(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->expr_switch : NULL;
}

esp_err_t scene_set_cv_input_mode(uint8_t scene_index, input_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  input_mode_t old_mode = scene->cv_input_mode;
  scene->cv_input_mode = mode;
  
  // Auto-manage cv.enabled based on mode
  if (mode == INPUT_MODE_NONE) {
    scene->cv.enabled = false;
  } else if (mode == INPUT_MODE_CV) {
    scene->cv.enabled = true;
  } else if (mode == INPUT_MODE_TRIGGER) {
    scene->cv.enabled = false;
    scene->cv_trigger_pressing = false;
  }
  // INPUT_MODE_NOTE: enabled is managed by input_manager
  // INPUT_MODE_CLOCK_SYNC: CV is used for tempo, not continuous routing
  
  scene_persist_if_programming();

  bool is_current = (scene_index == g_scene_manager.current_scene_index);

  // State machine: NOTE mode requires GATE expression mode
  if (mode == INPUT_MODE_NOTE) {
    scene->expression_mode = EXPRESSION_MODE_GATE;
    ESP_LOGI(TAG, "Expression mode automatically set to GATE for NOTE input mode");
    if (is_current) input_set_mode(INPUT_MODE_NOTE);
  } else if (old_mode == INPUT_MODE_NOTE) {
    scene->expression_mode = EXPRESSION_MODE_NONE;
    ESP_LOGI(TAG, "Expression mode set to Disabled (leaving NOTE mode)");
    if (is_current) {
      input_set_mode(mode);
      expression_set_mode(EXPRESSION_MODE_NONE);
    }
  } else if (is_current && mode != old_mode) {
    input_set_mode(mode);
  }

  if (is_current && (mode == INPUT_MODE_NOTE || old_mode == INPUT_MODE_NOTE)) {
    scene_refresh_cv_velocity_sources();
  }
  
  const char* mode_str = (mode == INPUT_MODE_NONE) ? "none" :
                         (mode == INPUT_MODE_CV) ? "cv" :
                         (mode == INPUT_MODE_CLOCK_SYNC) ? "clock_sync" :
                         (mode == INPUT_MODE_AUDIO) ? "audio" :
                         (mode == INPUT_MODE_TRIGGER) ? "trigger" : "note";
  ESP_LOGI(TAG, "Scene %d CV input mode set to %s", scene_index + 1, mode_str);
  return ESP_OK;
}

input_mode_t scene_get_cv_input_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_input_mode : INPUT_MODE_NONE;
}

action_t* scene_get_cv_trigger_action(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->cv_trigger_action : NULL;
}

esp_err_t scene_set_cv_trigger_threshold(uint8_t scene_index, uint8_t threshold) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (threshold > 100) return ESP_ERR_INVALID_ARG;

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  scene->cv_trigger_threshold = threshold;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_cv_trigger_threshold(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_trigger_threshold : 50;
}

esp_err_t scene_set_cv_trigger_debounce_ms(uint8_t scene_index, uint16_t debounce_ms) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (debounce_ms > 2000) return ESP_ERR_INVALID_ARG;

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  scene->cv_trigger_debounce_ms = debounce_ms;
  scene_persist_if_programming();
  return ESP_OK;
}

uint16_t scene_get_cv_trigger_debounce_ms(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_trigger_debounce_ms : 0;
}

esp_err_t scene_set_bpm(uint8_t scene_index, uint16_t bpm) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (bpm < 20 || bpm > 300) return ESP_ERR_INVALID_ARG;
  
  // Scene BPM can only be modified in programming mode
  // Performance tempo changes should use tempo_set_bpm() directly
  if (!ui_is_in_programming_mode()) {
    ESP_LOGW(TAG, "Cannot modify scene BPM outside programming mode (use tempo bpm for live changes)");
    return ESP_ERR_INVALID_STATE;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->bpm = bpm;
  scene_persist_if_programming();
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    tempo_set_bpm(bpm);
  }
  
  ESP_LOGI(TAG, "Scene %d BPM set to %d", scene_index + 1, bpm);
  return ESP_OK;
}

uint16_t scene_get_bpm(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->bpm : 120;
}

esp_err_t scene_set_clock_source(uint8_t scene_index, tempo_clock_source_t source) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->clock_source = source;
  scene_persist_if_programming();
  
  // If setting to SYNC, automatically set cv_input_mode to CLOCK_SYNC for coherence
  if (source == CLOCK_SOURCE_SYNC) {
    input_mode_t old_input_mode = scene->cv_input_mode;
    scene->cv_input_mode = INPUT_MODE_CLOCK_SYNC;
    
    // If we were in NOTE mode, also reset expression mode to Disabled
    if (old_input_mode == INPUT_MODE_NOTE) {
      scene->expression_mode = EXPRESSION_MODE_NONE;
      ESP_LOGI(TAG, "Expression mode set to Disabled (leaving NOTE mode for clock sync)");
      
      if (scene_index == g_scene_manager.current_scene_index) {
        expression_set_mode(EXPRESSION_MODE_NONE);
      }
    }
    
    // Switch to clock sync mode if this is the current scene
    if (scene_index == g_scene_manager.current_scene_index) {
      input_set_mode(INPUT_MODE_CLOCK_SYNC);
      ESP_LOGI(TAG, "Switched to clock sync input mode");
    }
  }
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    ESP_LOGI(TAG, "Updating tempo to clock source %d (current scene)", source);
    tempo_set_source(source);
  } else {
    ESP_LOGI(TAG, "Not updating tempo (scene %u is not current scene %u)", 
             (unsigned)scene_index, (unsigned)g_scene_manager.current_scene_index);
  }
  
  const char* source_str = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                           (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  ESP_LOGI(TAG, "Scene %d clock source set to %s", scene_index + 1, source_str);
  
  return ESP_OK;
}

tempo_clock_source_t scene_get_clock_source(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->clock_source : CLOCK_SOURCE_INTERNAL;
}

esp_err_t scene_set_beat_divider(uint8_t scene_index, tempo_note_divider_t divider) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->beat_divider = divider;
  scene_persist_if_programming();
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    tempo_set_note_divider(divider);
  }
  
  const char* div_str = (divider == DIVIDER_QUARTER) ? "Quarter" :
                        (divider == DIVIDER_EIGHTH) ? "Eighth" : "Sixteenth";
  ESP_LOGI(TAG, "Scene %d beat divider set to %s", scene_index + 1, div_str);
  
  return ESP_OK;
}

tempo_note_divider_t scene_get_beat_divider(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->beat_divider : DIVIDER_QUARTER;
}

esp_err_t scene_set_time_signature(uint8_t scene_index, uint8_t numerator, uint8_t denominator) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (numerator == 0 || numerator > 16 || denominator == 0 || denominator > 16) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->time_signature.numerator = numerator;
  scene->time_signature.denominator = denominator;
  scene_persist_if_programming();
  
  // Validate action timings against new time signature (remap invalid beats to beat 1)
  action_validate_scene_timings(scene);
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    tempo_set_time_signature(numerator, denominator);
  }
  
  ESP_LOGI(TAG, "Scene %d time signature set to %d/%d", scene_index + 1, numerator, denominator);
  
  return ESP_OK;
}

time_signature_t scene_get_time_signature(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  time_signature_t default_sig = {4, 4};
  return scene ? scene->time_signature : default_sig;
}

esp_err_t scene_set_use_transport(uint8_t scene_index, bool use_transport) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->use_transport = use_transport;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d use_transport set to %s", scene_index + 1, 
           use_transport ? "true" : "false");
  
  return ESP_OK;
}

bool scene_get_use_transport(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->use_transport : false;
}

esp_err_t scene_set_send_clock(uint8_t scene_index, bool send_clock) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->send_clock = send_clock;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d send_clock set to %s", scene_index + 1,
    send_clock ? "true" : "false");
  
  return ESP_OK;
}

bool scene_get_send_clock(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->send_clock : true;  // Default to true if no scene
}

// Helper to get velocity mode name (note-output modules: fixed/gate/touchwheel only)
static const char* velocity_mode_to_string(velocity_mode_t mode) {
  switch (mode) {
    case VELOCITY_MODE_GATE_VOLTAGE: return "Gate Voltage";
    case VELOCITY_MODE_TOUCHWHEEL: return "Touchwheel";
    case VELOCITY_MODE_FIXED:
    default: return "Fixed";
  }
}

static const char* touchwheel_mode_json_str(touchwheel_mode_t mode) {
  switch (mode) {
    case TOUCHWHEEL_MODE_PADS: return "pads";
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE: return "program_change";
    case TOUCHWHEEL_MODE_SET_TEMPO: return "set_tempo";
    case TOUCHWHEEL_MODE_PITCH_BEND: return "pitch_bend";
    case TOUCHWHEEL_MODE_AFTERTOUCH: return "aftertouch";
    case TOUCHWHEEL_MODE_DOUBLE_CC: return "double_cc";
    case TOUCHWHEEL_MODE_VELOCITY: return "velocity";
    case TOUCHWHEEL_MODE_LFO_RATE: return "lfo_rate";
    case TOUCHWHEEL_MODE_LFO_DEPTH: return "lfo_depth";
    case TOUCHWHEEL_MODE_RTG_RATE: return "rtg_rate";
    default: return "continuous";
  }
}

static touchwheel_mode_t touchwheel_mode_from_json_str(const char* mode_str) {
  if (!mode_str) return TOUCHWHEEL_MODE_PADS;
  if (strcmp(mode_str, "pads") == 0 || strcmp(mode_str, "buttons") == 0) return TOUCHWHEEL_MODE_PADS;
  if (strcmp(mode_str, "program_change") == 0) return TOUCHWHEEL_MODE_PROGRAM_CHANGE;
  if (strcmp(mode_str, "continuous") == 0) return TOUCHWHEEL_MODE_CONTINUOUS;
  if (strcmp(mode_str, "set_tempo") == 0) return TOUCHWHEEL_MODE_SET_TEMPO;
  if (strcmp(mode_str, "pitch_bend") == 0) return TOUCHWHEEL_MODE_PITCH_BEND;
  if (strcmp(mode_str, "aftertouch") == 0) return TOUCHWHEEL_MODE_AFTERTOUCH;
  if (strcmp(mode_str, "double_cc") == 0) return TOUCHWHEEL_MODE_DOUBLE_CC;
  if (strcmp(mode_str, "velocity") == 0) return TOUCHWHEEL_MODE_VELOCITY;
  if (strcmp(mode_str, "lfo_rate") == 0) return TOUCHWHEEL_MODE_LFO_RATE;
  if (strcmp(mode_str, "lfo_depth") == 0) return TOUCHWHEEL_MODE_LFO_DEPTH;
  if (strcmp(mode_str, "rtg_rate") == 0) return TOUCHWHEEL_MODE_RTG_RATE;
  return TOUCHWHEEL_MODE_PADS;
}

static void scene_fixup_touchwheel_orphan(scene_t* scene) {
  if (!scene) return;
  if (scene->touchwheel_mode != TOUCHWHEEL_MODE_VELOCITY) return;
  if (scene_cv_claims_source_for_scene(scene, VELOCITY_MODE_TOUCHWHEEL)) return;
  if (scene->touchwheel_mode_prev_valid) {
    scene->touchwheel_mode = scene->touchwheel_mode_prev;
    scene->touchwheel_mode_prev_valid = false;
  } else {
    scene->touchwheel_mode = TOUCHWHEEL_MODE_PADS;
  }
}

static void scene_reconcile_touchwheel_for_cv_velocity(scene_t* scene,
    velocity_mode_t old_mode, velocity_mode_t new_mode) {
  if (!scene) return;

  if (new_mode == VELOCITY_MODE_TOUCHWHEEL && old_mode != VELOCITY_MODE_TOUCHWHEEL) {
    if (scene->touchwheel_mode != TOUCHWHEEL_MODE_VELOCITY && !scene->touchwheel_mode_prev_valid) {
      scene->touchwheel_mode_prev = scene->touchwheel_mode;
      scene->touchwheel_mode_prev_valid = true;
    }
  }

  if (old_mode == VELOCITY_MODE_TOUCHWHEEL && new_mode != VELOCITY_MODE_TOUCHWHEEL) {
    scene_fixup_touchwheel_orphan(scene);
  } else if (new_mode != VELOCITY_MODE_TOUCHWHEEL) {
    scene_fixup_touchwheel_orphan(scene);
  }
}

touchwheel_mode_t scene_get_effective_touchwheel_mode(const scene_t* scene) {
  if (!scene) return TOUCHWHEEL_MODE_PADS;
  if (scene->touchwheel_mode != TOUCHWHEEL_MODE_VELOCITY) return scene->touchwheel_mode;
  if (scene_cv_claims_source_for_scene(scene, VELOCITY_MODE_TOUCHWHEEL)) return TOUCHWHEEL_MODE_VELOCITY;
  if (scene->touchwheel_mode_prev_valid) return scene->touchwheel_mode_prev;
  return TOUCHWHEEL_MODE_PADS;
}

static void scene_refresh_cv_velocity_sources(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  scene_cleanup_touchwheel();
  scene_setup_touchwheel_for_mode(scene);
}

static uint8_t scene_clamp_midi_velocity(uint8_t vel) {
  if (vel < 1) return 1;
  if (vel > 127) return 127;
  return vel;
}

bool scene_cv_claims_source_for_scene(const scene_t* scene, velocity_mode_t source) {
  if (!scene) return false;
  return scene->cv_input_mode == INPUT_MODE_NOTE && scene->cv_velocity_mode == source;
}

bool scene_cv_claims_source(velocity_mode_t source) {
  return scene_cv_claims_source_for_scene(scene_get_current(), source);
}

void scene_set_proximity_velocity_sample(uint8_t midi_value) {
  s_proximity_velocity_sample = midi_value;
}

uint8_t scene_get_proximity_velocity_sample(void) {
  return s_proximity_velocity_sample;
}

void scene_set_als_velocity_sample(uint8_t midi_value) {
  s_als_velocity_sample = midi_value;
}

uint8_t scene_get_als_velocity_sample(void) {
  return s_als_velocity_sample;
}

const char* scene_cv_velocity_mode_display_name(velocity_mode_t mode) {
  switch (mode) {
    case VELOCITY_MODE_GATE_VOLTAGE: return "Gate Voltage";
    case VELOCITY_MODE_TOUCHWHEEL: return "Touchwheel";
    case VELOCITY_MODE_PROXIMITY: return "Proximity";
    case VELOCITY_MODE_ALS: return "ALS";
    case VELOCITY_MODE_TILT_X: return "Tilt X";
    case VELOCITY_MODE_TILT_Y: return "Tilt Y";
    case VELOCITY_MODE_LFO1: return "LFO 1";
    case VELOCITY_MODE_LFO2: return "LFO 2";
    case VELOCITY_MODE_SAMPLE_HOLD: return "S+H";
    case VELOCITY_MODE_FIXED:
    default: return "Fixed";
  }
}

static const char* velocity_mode_json_str(velocity_mode_t mode) {
  switch (mode) {
    case VELOCITY_MODE_GATE_VOLTAGE: return "gate_voltage";
    case VELOCITY_MODE_TOUCHWHEEL: return "touchwheel";
    case VELOCITY_MODE_PROXIMITY: return "proximity";
    case VELOCITY_MODE_ALS: return "als";
    case VELOCITY_MODE_TILT_X: return "tilt_x";
    case VELOCITY_MODE_TILT_Y: return "tilt_y";
    case VELOCITY_MODE_LFO1: return "lfo1";
    case VELOCITY_MODE_LFO2: return "lfo2";
    case VELOCITY_MODE_SAMPLE_HOLD: return "sample_hold";
    case VELOCITY_MODE_FIXED:
    default: return "fixed";
  }
}

static velocity_mode_t velocity_mode_from_json_str(const char* mode_str) {
  if (!mode_str) return VELOCITY_MODE_FIXED;
  if (strcmp(mode_str, "gate_voltage") == 0) return VELOCITY_MODE_GATE_VOLTAGE;
  if (strcmp(mode_str, "touchwheel") == 0) return VELOCITY_MODE_TOUCHWHEEL;
  if (strcmp(mode_str, "proximity") == 0) return VELOCITY_MODE_PROXIMITY;
  if (strcmp(mode_str, "als") == 0) return VELOCITY_MODE_ALS;
  if (strcmp(mode_str, "tilt_x") == 0) return VELOCITY_MODE_TILT_X;
  if (strcmp(mode_str, "tilt_y") == 0) return VELOCITY_MODE_TILT_Y;
  if (strcmp(mode_str, "lfo1") == 0) return VELOCITY_MODE_LFO1;
  if (strcmp(mode_str, "lfo2") == 0) return VELOCITY_MODE_LFO2;
  if (strcmp(mode_str, "sample_hold") == 0) return VELOCITY_MODE_SAMPLE_HOLD;
  return VELOCITY_MODE_FIXED;
}

static velocity_mode_t velocity_mode_from_json_str_notes_only(const char* mode_str) {
  velocity_mode_t mode = velocity_mode_from_json_str(mode_str);
  if (mode <= VELOCITY_MODE_TOUCHWHEEL) return mode;
  return VELOCITY_MODE_FIXED;
}

static const char* velocity_mode_json_str_notes_only(velocity_mode_t mode) {
  if (mode > VELOCITY_MODE_TOUCHWHEEL) return "fixed";
  return velocity_mode_json_str(mode);
}

uint8_t scene_get_cv_gate_velocity(int16_t gate_raw_adc) {
  scene_t* scene = scene_get_current();
  if (!scene) return 100;

  uint8_t vel = 100;
  switch (scene->cv_velocity_mode) {
    case VELOCITY_MODE_FIXED:
      vel = scene->cv_velocity;
      if (vel == 0) vel = 100;
      break;
    case VELOCITY_MODE_GATE_VOLTAGE:
      vel = (uint8_t)((gate_raw_adc * 127) / 4095);
      break;
    case VELOCITY_MODE_TOUCHWHEEL:
      vel = s_touchwheel_velocity;
      break;
    case VELOCITY_MODE_PROXIMITY:
      vel = s_proximity_velocity_sample;
      break;
    case VELOCITY_MODE_ALS:
      vel = s_als_velocity_sample;
      break;
    case VELOCITY_MODE_TILT_X:
      vel = tilt_get_midi(TILT_AXIS_X);
      break;
    case VELOCITY_MODE_TILT_Y:
      vel = tilt_get_midi(TILT_AXIS_Y);
      break;
    case VELOCITY_MODE_LFO1:
      vel = lfo_get_value(0);
      break;
    case VELOCITY_MODE_LFO2:
      vel = lfo_get_value(1);
      break;
    case VELOCITY_MODE_SAMPLE_HOLD:
      vel = sample_hold_get_value();
      break;
    default:
      vel = scene->cv_velocity;
      if (vel == 0) vel = 100;
      break;
  }
  return scene_clamp_midi_velocity(vel);
}

esp_err_t scene_set_cv_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  velocity_mode_t old_mode = scene->cv_velocity_mode;
  scene->cv_velocity_mode = mode;
  scene_reconcile_touchwheel_for_cv_velocity(scene, old_mode, mode);
  scene_persist_if_programming();

  if (scene_index == g_scene_manager.current_scene_index) {
    scene_refresh_cv_velocity_sources();
  }
  
  ESP_LOGI(TAG, "Scene %d CV velocity mode set to %s", scene_index + 1,
    scene_cv_velocity_mode_display_name(mode));
  return ESP_OK;
}

velocity_mode_t scene_get_cv_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_cv_velocity(uint8_t scene_index, uint8_t velocity) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (velocity < 1 || velocity > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->cv_velocity = velocity;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d CV velocity set to %d", scene_index + 1, velocity);
  return ESP_OK;
}

uint8_t scene_get_cv_velocity(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_velocity : 100;
}

// Audio envelope follower configuration
esp_err_t scene_set_audio_range(uint8_t scene_index, cv_range_t range) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  // Only bipolar ranges are valid for audio
  if (range != CV_RANGE_BIPOLAR_5V && range != CV_RANGE_BIPOLAR_10V) {
    ESP_LOGW(TAG, "Audio mode requires bipolar range, defaulting to ±5V");
    range = CV_RANGE_BIPOLAR_5V;
  }
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.range = range;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio range set to %s", scene_index + 1,
    range == CV_RANGE_BIPOLAR_10V ? "±10V" : "±5V");
  return ESP_OK;
}

cv_range_t scene_get_audio_range(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.range : CV_RANGE_BIPOLAR_5V;
}

esp_err_t scene_set_audio_sensitivity(uint8_t scene_index, uint8_t sensitivity) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.sensitivity = sensitivity;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio sensitivity set to %d", scene_index + 1, sensitivity);
  return ESP_OK;
}

uint8_t scene_get_audio_sensitivity(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.sensitivity : 128;
}

esp_err_t scene_set_audio_attack(uint8_t scene_index, uint16_t attack_ms) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (attack_ms < 5) attack_ms = 5;
  if (attack_ms > 100) attack_ms = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.attack_ms = attack_ms;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio attack set to %ums", scene_index + 1, attack_ms);
  return ESP_OK;
}

uint16_t scene_get_audio_attack(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.attack_ms : 10;
}

esp_err_t scene_set_audio_release(uint8_t scene_index, uint16_t release_ms) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (release_ms < 50) release_ms = 50;
  if (release_ms > 2000) release_ms = 2000;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.release_ms = release_ms;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio release set to %ums", scene_index + 1, release_ms);
  return ESP_OK;
}

uint16_t scene_get_audio_release(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.release_ms : 200;
}

esp_err_t scene_set_audio_threshold(uint8_t scene_index, uint8_t threshold) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (threshold > 127) threshold = 127;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.threshold = threshold;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio threshold set to %d", scene_index + 1, threshold);
  return ESP_OK;
}

uint8_t scene_get_audio_threshold(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.threshold : 5;
}

esp_err_t scene_set_audio_polarity(uint8_t scene_index, audio_polarity_t polarity) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.polarity = polarity;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio polarity set to %s", scene_index + 1,
    polarity == AUDIO_POLARITY_ATTRACT ? "Attract" : "Repel");
  return ESP_OK;
}

audio_polarity_t scene_get_audio_polarity(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.polarity : AUDIO_POLARITY_ATTRACT;
}

audio_config_t* scene_get_audio_config(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->audio_config : NULL;
}

esp_err_t scene_set_expression_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->expression_velocity_mode = mode;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d expression velocity mode set to %s", scene_index + 1, velocity_mode_to_string(mode));
  return ESP_OK;
}

velocity_mode_t scene_get_expression_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->expression_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_proximity_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->proximity_velocity_mode = mode;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d proximity velocity mode set to %s", scene_index + 1, velocity_mode_to_string(mode));
  return ESP_OK;
}

velocity_mode_t scene_get_proximity_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->proximity_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_als_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->als_velocity_mode = mode;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d ALS velocity mode set to %s", scene_index + 1, velocity_mode_to_string(mode));
  return ESP_OK;
}

velocity_mode_t scene_get_als_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->als_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_tilt_x_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->tilt_x_velocity_mode = mode;
  scene_persist_if_programming();
  return ESP_OK;
}

velocity_mode_t scene_get_tilt_x_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->tilt_x_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_tilt_y_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->tilt_y_velocity_mode = mode;
  scene_persist_if_programming();
  return ESP_OK;
}

velocity_mode_t scene_get_tilt_y_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->tilt_y_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_tilt_x_tempo_nudge_pct(uint8_t scene_index, uint8_t pct) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (pct > 100) pct = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->tilt_x_tempo_nudge_pct = pct;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_tilt_x_tempo_nudge_pct(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->tilt_x_tempo_nudge_pct : 10;
}

esp_err_t scene_set_tilt_y_tempo_nudge_pct(uint8_t scene_index, uint8_t pct) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (pct > 100) pct = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->tilt_y_tempo_nudge_pct = pct;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_tilt_y_tempo_nudge_pct(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->tilt_y_tempo_nudge_pct : 10;
}

esp_err_t scene_set_expression_tempo_nudge_pct(uint8_t scene_index, uint8_t pct) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (pct > 100) pct = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->expression_tempo_nudge_pct = pct;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_expression_tempo_nudge_pct(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->expression_tempo_nudge_pct : 10;
}

esp_err_t scene_set_cv_tempo_nudge_pct(uint8_t scene_index, uint8_t pct) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (pct > 100) pct = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->cv_tempo_nudge_pct = pct;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_cv_tempo_nudge_pct(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_tempo_nudge_pct : 10;
}

esp_err_t scene_set_proximity_tempo_nudge_pct(uint8_t scene_index, uint8_t pct) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (pct > 100) pct = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->proximity_tempo_nudge_pct = pct;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_proximity_tempo_nudge_pct(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->proximity_tempo_nudge_pct : 10;
}

esp_err_t scene_set_touchwheel_tempo_nudge_pct(uint8_t scene_index, uint8_t pct) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (pct > 100) pct = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->touchwheel_tempo_nudge_pct = pct;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_touchwheel_tempo_nudge_pct(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->touchwheel_tempo_nudge_pct : 10;
}

esp_err_t scene_set_touchwheel_tempo_nudge_return(uint8_t scene_index, uint8_t speed) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (speed > TOUCHWHEEL_NUDGE_RETURN_SLOW) speed = TOUCHWHEEL_NUDGE_RETURN_SLOW;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->touchwheel_tempo_nudge_return = speed;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_touchwheel_tempo_nudge_return(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->touchwheel_tempo_nudge_return : TOUCHWHEEL_NUDGE_RETURN_INSTANT;
}

esp_err_t scene_set_touchwheel_aftertouch_return(uint8_t scene_index, uint8_t speed) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (speed > TOUCHWHEEL_NUDGE_RETURN_SLOW) speed = TOUCHWHEEL_NUDGE_RETURN_SLOW;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->touchwheel_aftertouch_return = speed;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_touchwheel_aftertouch_return(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->touchwheel_aftertouch_return : TOUCHWHEEL_NUDGE_RETURN_FAST;
}

esp_err_t scene_set_als_tempo_nudge_pct(uint8_t scene_index, uint8_t pct) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (pct > 100) pct = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->als_tempo_nudge_pct = pct;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_als_tempo_nudge_pct(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->als_tempo_nudge_pct : 10;
}

esp_err_t scene_set_lfo1_tempo_nudge_pct(uint8_t scene_index, uint8_t pct) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (pct > 100) pct = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->lfo1_tempo_nudge_pct = pct;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_lfo1_tempo_nudge_pct(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->lfo1_tempo_nudge_pct : 10;
}

esp_err_t scene_set_lfo2_tempo_nudge_pct(uint8_t scene_index, uint8_t pct) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (pct > 100) pct = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->lfo2_tempo_nudge_pct = pct;
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_lfo2_tempo_nudge_pct(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->lfo2_tempo_nudge_pct : 10;
}

static uint8_t clamp_tempo_nudge_direction(uint8_t direction) {
  return direction > TEMPO_NUDGE_DIR_SLOWER ? TEMPO_NUDGE_DIR_BOTH : direction;
}

esp_err_t scene_set_tilt_x_tempo_nudge_direction(uint8_t scene_index, uint8_t direction) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->tilt_x_tempo_nudge_direction = clamp_tempo_nudge_direction(direction);
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_tilt_x_tempo_nudge_direction(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->tilt_x_tempo_nudge_direction : TEMPO_NUDGE_DIR_BOTH;
}

esp_err_t scene_set_tilt_y_tempo_nudge_direction(uint8_t scene_index, uint8_t direction) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->tilt_y_tempo_nudge_direction = clamp_tempo_nudge_direction(direction);
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_tilt_y_tempo_nudge_direction(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->tilt_y_tempo_nudge_direction : TEMPO_NUDGE_DIR_BOTH;
}

esp_err_t scene_set_expression_tempo_nudge_direction(uint8_t scene_index, uint8_t direction) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->expression_tempo_nudge_direction = clamp_tempo_nudge_direction(direction);
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_expression_tempo_nudge_direction(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->expression_tempo_nudge_direction : TEMPO_NUDGE_DIR_BOTH;
}

esp_err_t scene_set_cv_tempo_nudge_direction(uint8_t scene_index, uint8_t direction) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->cv_tempo_nudge_direction = clamp_tempo_nudge_direction(direction);
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_cv_tempo_nudge_direction(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_tempo_nudge_direction : TEMPO_NUDGE_DIR_BOTH;
}

esp_err_t scene_set_proximity_tempo_nudge_direction(uint8_t scene_index, uint8_t direction) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->proximity_tempo_nudge_direction = clamp_tempo_nudge_direction(direction);
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_proximity_tempo_nudge_direction(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->proximity_tempo_nudge_direction : TEMPO_NUDGE_DIR_BOTH;
}

esp_err_t scene_set_touchwheel_tempo_nudge_direction(uint8_t scene_index, uint8_t direction) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->touchwheel_tempo_nudge_direction = clamp_tempo_nudge_direction(direction);
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_touchwheel_tempo_nudge_direction(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->touchwheel_tempo_nudge_direction : TEMPO_NUDGE_DIR_BOTH;
}

esp_err_t scene_set_als_tempo_nudge_direction(uint8_t scene_index, uint8_t direction) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->als_tempo_nudge_direction = clamp_tempo_nudge_direction(direction);
  scene_persist_if_programming();
  return ESP_OK;
}

uint8_t scene_get_als_tempo_nudge_direction(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->als_tempo_nudge_direction : TEMPO_NUDGE_DIR_BOTH;
}

uint8_t scene_get_touchwheel_velocity(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return 100;
  if (scene_cv_claims_source_for_scene(scene, VELOCITY_MODE_TOUCHWHEEL)) {
    return s_touchwheel_velocity ? s_touchwheel_velocity : 100;
  }
  if (scene->touchwheel_mode != TOUCHWHEEL_MODE_VELOCITY) {
    return 100;  // Default velocity when not in velocity mode
  }
  return s_touchwheel_velocity;
}

uint8_t scene_get_touchwheel_lfo_rate(void) {
  scene_t* scene = scene_get_current();
  if (!scene || scene->touchwheel_mode != TOUCHWHEEL_MODE_LFO_RATE) {
    return 64;  // Default center value when not in LFO rate mode
  }
  return s_touchwheel_lfo_rate;
}

uint8_t scene_get_touchwheel_lfo_depth(void) {
  scene_t* scene = scene_get_current();
  if (!scene || scene->touchwheel_mode != TOUCHWHEEL_MODE_LFO_DEPTH) {
    return 127;  // Default full depth when not in LFO depth mode
  }
  return s_touchwheel_lfo_depth;
}

uint8_t scene_get_touchwheel_rtg_rate(void) {
  scene_t* scene = scene_get_current();
  if (!scene || scene->touchwheel_mode != TOUCHWHEEL_MODE_RTG_RATE) {
    return 64;  // Default center value when not in RTG rate mode
  }
  return s_touchwheel_rtg_rate;
}

uint8_t scene_get_expression_lfo_rate(void) {
  return s_expression_lfo_rate;
}

uint8_t scene_get_cv_lfo_rate(void) {
  return s_cv_lfo_rate;
}

uint8_t scene_get_als_lfo_rate(void) {
  return s_als_lfo_rate;
}

uint8_t scene_get_proximity_lfo_rate(void) {
  return s_proximity_lfo_rate;
}

uint8_t scene_get_tilt_x_lfo_rate(void) {
  return s_tilt_x_lfo_rate;
}

uint8_t scene_get_tilt_y_lfo_rate(void) {
  return s_tilt_y_lfo_rate;
}

// Convert scene name to filesystem-safe slug filename
// Example: "STORM BOLT" -> "storm_bolt.json"
static bool scene_name_char_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void scene_trim_name(char *name) {
  if (!name || name[0] == '\0') return;

  char *start = name;
  while (*start && scene_name_char_is_space(*start)) start++;
  if (start != name) {
    memmove(name, start, strlen(start) + 1);
  }

  size_t len = strlen(name);
  while (len > 0 && scene_name_char_is_space(name[len - 1])) {
    name[--len] = '\0';
  }
}

static void scene_name_to_slug(const char* name, char* slug, size_t slug_size) {
  if (!name || !slug || slug_size < 6) {  // Minimum: "x.json"
    if (slug && slug_size > 0) slug[0] = '\0';
    return;
  }
  
  size_t out = 0;
  size_t max_name_len = slug_size - 6;  // Reserve space for ".json\0"
  
  for (size_t i = 0; name[i] && out < max_name_len; i++) {
    char c = name[i];
    if (c >= 'A' && c <= 'Z') {
      slug[out++] = c + ('a' - 'A');  // Lowercase
    } else if (c >= 'a' && c <= 'z') {
      slug[out++] = c;
    } else if (c >= '0' && c <= '9') {
      slug[out++] = c;
    } else if (c == ' ' || c == '-') {
      // Replace spaces and hyphens with underscore (avoid consecutive underscores)
      if (out > 0 && slug[out - 1] != '_') slug[out++] = '_';
    }
    // Skip all other characters
  }
  
  // Remove trailing underscore if present
  if (out > 0 && slug[out - 1] == '_') out--;
  
  // Ensure we have at least one character
  if (out == 0) {
    slug[out++] = 's';  // Fallback to "s.json"
  }
  
  // Append .json extension
  snprintf(slug + out, slug_size - out, ".json");
}

bool scene_name_is_reserved(const char* name) {
  if (!name || name[0] == '\0') return false;
  char slug[64];
  scene_name_to_slug(name, slug, sizeof(slug));
  return strcasecmp(slug, "manifest.json") == 0;
}

// Check if a scene name already exists in the manifest (case-insensitive)
// Optionally exclude a specific scene index from the check (for rename validation)
bool scene_name_exists(const char* name, int8_t exclude_index) {
  if (!name || !g_scene_manager.manifest) return false;
  
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    // Skip the excluded scene (used when renaming to allow same name)
    if (exclude_index >= 0 && g_scene_manager.manifest[i].index == (uint8_t)exclude_index) {
      continue;
    }
    if (strcasecmp(g_scene_manager.manifest[i].name, name) == 0) {
      return true;
    }
  }
  return false;
}

// Helper to get scene filename from manifest
static void get_scene_filename(uint8_t scene_index, char* buffer, size_t buffer_size) {
  // Look up filename in manifest
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) {
      snprintf(buffer, buffer_size, "%s/%s", SCENES_BASE_PATH,
        g_scene_manager.manifest[i].filename);
      return;
    }
  }
  // Fallback for scenes not yet in manifest (should not happen in normal operation)
  snprintf(buffer, buffer_size, "%s/scene_%03d.json", SCENES_BASE_PATH, scene_index + 1);
}

// Action type name lookup table (for JSON serialization)
// Note: ACTION_NONE is NULL - we skip serializing empty actions
static const char* action_type_json_names[] = {
  [ACTION_NONE] = NULL,  // Don't serialize empty actions
  [ACTION_PRESET] = "preset",
  [ACTION_SCENE] = "scene",
  [ACTION_TRANSPORT] = "transport",
  [ACTION_TEMPO] = "tempo",
  [ACTION_CONTROL] = "control",
  [ACTION_NOTE] = "note",
  [ACTION_RANDOMIZE] = "randomize",
  [ACTION_CONFIRM_PENDING] = "confirm_pending",
  [ACTION_RESET] = "reset",
  [ACTION_PIANO_PEDAL] = "piano_pedal",
  [ACTION_TOUCHWHEEL] = "touchwheel",
  [ACTION_LFO] = "lfo",
  [ACTION_CLOCK] = "clock",
  [ACTION_CUT] = "cut",
  [ACTION_UI] = "ui",
  [ACTION_PARAM] = "param",
  [ACTION_RTG] = "rtg",
  [ACTION_SAMPLE_HOLD] = "sample_hold",
  [ACTION_PUNCH_IN] = "punch_in",
  [ACTION_FLAG_CEREMONY] = "flag_ceremony",
  [ACTION_BOOMERANG] = "boomerang",
  [ACTION_INSPECT_SCENE] = "inspect_scene"
};

// Variant string table for consolidated action families. Indexed by
// action_variant_t. Append-only -- order matters for backwards-compatible
// reads via positional indexing if anyone ever does that.
static const char* action_variant_json_names[] = {
  [VARIANT_NONE]      = NULL,        // Don't serialize when variant is default
  [VARIANT_INCREMENT] = "increment",
  [VARIANT_DECREMENT] = "decrement",
  [VARIANT_SET]       = "set",
  [VARIANT_HOLD]      = "hold",
  [VARIANT_CYCLE]     = "cycle",
  [VARIANT_TOGGLE]    = "toggle",
  [VARIANT_START]     = "start",
  [VARIANT_STOP]      = "stop",
  [VARIANT_TAP]       = "tap",
  [VARIANT_BURST]     = "burst",
  [VARIANT_PLAY]      = "play",
  [VARIANT_PAUSE]     = "pause",
  [VARIANT_RECORD]    = "record",
  [VARIANT_MODIFY]    = "modify",
  [VARIANT_STEP]      = "step",
  [VARIANT_DOWNBEAT]  = "downbeat",
};

static action_variant_t action_variant_from_string(const char* name) {
  if (!name) return VARIANT_NONE;
  for (int i = 0; i < VARIANT_MAX; i++) {
    if (action_variant_json_names[i] && strcmp(name, action_variant_json_names[i]) == 0) {
      return (action_variant_t)i;
    }
  }
  return VARIANT_NONE;
}

// Helper to convert action type string to enum.
// Legacy aliases (including pre-consolidation type names that mapped to
// removed enum values) live in components/action_migration. Callers that
// need variant info from a legacy name should go through json_to_action,
// which consults the migration component.
static action_type_t action_type_from_string(const char* name) {
  if (!name) return ACTION_NONE;

  for (int i = 0; i < ACTION_MAX; i++) {
    if (action_type_json_names[i] && strcmp(name, action_type_json_names[i]) == 0) {
      return (action_type_t)i;
    }
  }

  // Try the migration component (handles every legacy alias previously
  // listed inline here, plus consolidated families that need a variant).
  action_type_t mig_type = ACTION_NONE;
  action_variant_t mig_variant = VARIANT_NONE;
  if (action_migration_translate_type(name, &mig_type, &mig_variant)) {
    return mig_type;
  }

  return ACTION_NONE;
}

// Serialize/deserialize actions
// Returns NULL for ACTION_NONE (empty actions should be skipped)
static cJSON* action_to_json(const action_t* action) {
  // Don't serialize empty actions
  if (!action || action->type == ACTION_NONE) {
    return NULL;
  }
  
  cJSON* obj = cJSON_CreateObject();
  
  // Use string name instead of integer
  if (action->type < ACTION_MAX && action_type_json_names[action->type]) {
    cJSON_AddStringToObject(obj, "type", action_type_json_names[action->type]);
  } else {
    // Unknown action type - skip it
    cJSON_Delete(obj);
    return NULL;
  }

  // Consolidated families (ACTION_TEMPO, ACTION_CONTROL) always emit a
  // variant string so the read path can dispatch without legacy aliases.
  // Singleton types omit it; the default VARIANT_NONE is implicit.
  if (action->variant != VARIANT_NONE
        && action->variant < VARIANT_MAX
        && action_variant_json_names[action->variant]) {
    cJSON_AddStringToObject(obj, "variant", action_variant_json_names[action->variant]);
  }

  if (action->type == ACTION_CONTROL &&
      (action->variant == VARIANT_SET || action->variant == VARIANT_HOLD)) {
    uint8_t num_ccs = action->params.control.num_ccs;
    if (num_ccs == 0) num_ccs = 1;  // Backward compat
    bool is_hold = (action->variant == VARIANT_HOLD);

    if (num_ccs == 1) {
      // Single CC: use simple format for backward compatibility
      cJSON_AddNumberToObject(obj, "cc", action->params.control.cc_numbers[0]);
      cJSON_AddNumberToObject(obj, "value", action->params.control.values[0]);
      if (is_hold) {
        cJSON_AddNumberToObject(obj, "value2", action->params.control.values2[0]);
      }
    } else {
      // Multi-CC: use array format
      cJSON* cc_arr = cJSON_CreateArray();
      cJSON* val_arr = cJSON_CreateArray();
      cJSON* val2_arr = is_hold ? cJSON_CreateArray() : NULL;
      for (int i = 0; i < num_ccs && i < 4; i++) {
        cJSON_AddItemToArray(cc_arr, cJSON_CreateNumber(action->params.control.cc_numbers[i]));
        cJSON_AddItemToArray(val_arr, cJSON_CreateNumber(action->params.control.values[i]));
        if (val2_arr) {
          cJSON_AddItemToArray(val2_arr, cJSON_CreateNumber(action->params.control.values2[i]));
        }
      }
      cJSON_AddItemToObject(obj, "cc", cc_arr);
      cJSON_AddItemToObject(obj, "value", val_arr);
      if (val2_arr) cJSON_AddItemToObject(obj, "value2", val2_arr);
    }
  } else if (action->type == ACTION_CONTROL && action->variant == VARIANT_CYCLE) {
    uint8_t num_ccs = action->params.control.num_ccs;
    if (num_ccs == 0) num_ccs = 1;
    uint8_t num_steps = action->params.control.num_cycle_steps;

    if (num_ccs == 1) {
      // Single CC cycle: simple format
      cJSON_AddNumberToObject(obj, "cc", action->params.control.cc_numbers[0]);
      cJSON* vals = cJSON_CreateArray();
      for (int i = 0; i < num_steps && i < 8; i++) {
        cJSON_AddItemToArray(vals, cJSON_CreateNumber(action->params.control.cycle_values[0][i]));
      }
      cJSON_AddItemToObject(obj, "values", vals);
    } else {
      // Multi-CC cycle: array of arrays
      cJSON* cc_arr = cJSON_CreateArray();
      cJSON* vals_arr = cJSON_CreateArray();
      for (int i = 0; i < num_ccs && i < 4; i++) {
        cJSON_AddItemToArray(cc_arr, cJSON_CreateNumber(action->params.control.cc_numbers[i]));
        cJSON* steps = cJSON_CreateArray();
        for (int j = 0; j < num_steps && j < 8; j++) {
          cJSON_AddItemToArray(steps, cJSON_CreateNumber(action->params.control.cycle_values[i][j]));
        }
        cJSON_AddItemToArray(vals_arr, steps);
      }
      cJSON_AddItemToObject(obj, "cc", cc_arr);
      cJSON_AddItemToObject(obj, "values", vals_arr);
    }
  } else if (action->type == ACTION_RANDOMIZE) {
    // Randomize uses a different struct
    cJSON* cc_arr = cJSON_CreateArray();
    for (int i = 0; i < action->params.randomize.num_ccs && i < 8; i++) {
      cJSON_AddItemToArray(cc_arr, cJSON_CreateNumber(action->params.randomize.cc_numbers[i]));
    }
    cJSON_AddItemToObject(obj, "cc", cc_arr);
  } else if (action->type == ACTION_NOTE) {
    cJSON_AddNumberToObject(obj, "note", action->params.note.note);
    cJSON_AddNumberToObject(obj, "velocity", action->params.note.velocity);
    if (action->params.note.note == ACTION_NOTE_RANDOM) {
      if (action->params.note.random_floor != 36)
        cJSON_AddNumberToObject(obj, "random_floor", action->params.note.random_floor);
      if (action->params.note.random_ceiling != 96)
        cJSON_AddNumberToObject(obj, "random_ceiling", action->params.note.random_ceiling);
    }
    if (action->params.note.voices != 1)
      cJSON_AddNumberToObject(obj, "voices", action->params.note.voices);
    if (action->params.note.bass)
      cJSON_AddBoolToObject(obj, "bass", true);
    if (!action->params.note.aftertouch)
      cJSON_AddBoolToObject(obj, "aftertouch", false);
  } else if (action->type == ACTION_PIANO_PEDAL) {
    // Single field: which switch-style MIDI CC to fire on press/release.
    // Whitelist enforced in action_create_piano_pedal() and json_to_action.
    cJSON_AddNumberToObject(obj, "cc", action->params.piano_pedal.cc_number);
  } else if (action->type == ACTION_PRESET) {
    // Variant decides which fields go on the wire. The variant string itself
    // is emitted unconditionally below; consumers route on that. JSON keys
    // are unchanged from the pre-consolidation shape so legacy files
    // (and external editors) round-trip without surprises.
    switch (action->variant) {
      case VARIANT_SET:
        // preset.program is uint16_t -- preserves bank-aware values > 255.
        cJSON_AddNumberToObject(obj, "number", action->params.preset.program);
        break;
      case VARIANT_HOLD:
        cJSON_AddNumberToObject(obj, "press_preset",   action->params.preset.press_preset);
        cJSON_AddNumberToObject(obj, "release_preset", action->params.preset.release_preset);
        // Only emit release_to_original when set; absence == false on read,
        // keeping diffs minimal for the common case.
        if (action->params.preset.release_to_original) {
          cJSON_AddBoolToObject(obj, "release_to_original", true);
        }
        break;
      case VARIANT_CYCLE: {
        uint8_t num_presets = action->params.preset.num_presets;
        cJSON_AddNumberToObject(obj, "num_presets", num_presets);
        cJSON* presets = cJSON_CreateArray();
        for (int i = 0; i < num_presets && i < 8; i++) {
          cJSON_AddItemToArray(presets, cJSON_CreateNumber(action->params.preset.cycle_presets[i]));
        }
        cJSON_AddItemToObject(obj, "presets", presets);
        break;
      }
      case VARIANT_INCREMENT:
      case VARIANT_DECREMENT:
        // No payload; the variant string alone is enough.
        break;
      default:
        break;
    }
  } else if (action->type == ACTION_SCENE) {
    // Only VARIANT_SET targets a specific scene number. INCREMENT and
    // DECREMENT are parameter-less (variant string alone is enough).
    if (action->variant == VARIANT_SET) {
      cJSON_AddNumberToObject(obj, "number", action->params.target.number);
    }
  } else if (action->type == ACTION_TEMPO) {
    // Variant determines which fields are emitted. The "variant" key itself
    // is added unconditionally below for ACTION_TEMPO so the read path can
    // tell SET from HOLD from CYCLE without legacy aliases.
    switch (action->variant) {
      case VARIANT_SET:
        cJSON_AddNumberToObject(obj, "bpm", action->params.tempo.bpm);
        if (action->params.tempo.bpm == ACTION_TEMPO_BPM_RANDOM) {
          cJSON_AddNumberToObject(obj, "random_floor",
            action->params.tempo.random_floor);
          cJSON_AddNumberToObject(obj, "random_ceiling",
            action->params.tempo.random_ceiling);
        }
        break;
      case VARIANT_HOLD:
        cJSON_AddNumberToObject(obj, "press_bpm", action->params.tempo.press_bpm);
        cJSON_AddNumberToObject(obj, "release_bpm", action->params.tempo.release_bpm);
        break;
      case VARIANT_CYCLE: {
        uint8_t num_tempos = action->params.tempo.num_tempos;
        cJSON_AddNumberToObject(obj, "num_tempos", num_tempos);
        cJSON* tempos = cJSON_CreateArray();
        for (int i = 0; i < num_tempos && i < 8; i++) {
          cJSON_AddItemToArray(tempos, cJSON_CreateNumber(action->params.tempo.cycle_tempos[i]));
        }
        cJSON_AddItemToObject(obj, "tempos", tempos);
        break;
      }
      case VARIANT_INCREMENT:
      case VARIANT_DECREMENT: {
        uint8_t amount = action->params.tempo.inc_amount;
        if (amount == 0) amount = 1;
        cJSON_AddNumberToObject(obj, "inc_amount", amount);
        break;
      }
      default:
        // TAP has no extra fields
        break;
    }
  } else if (action->type == ACTION_TOUCHWHEEL) {
    // Variant decides which fields go on the wire. The variant string is
    // emitted unconditionally above; consumers route on that. JSON keys
    // are unchanged from the pre-consolidation shape so legacy files
    // round-trip without surprises. captured_mode is transient and is
    // intentionally not persisted (matches Preset Hold's captured_preset).
    switch (action->variant) {
      case VARIANT_HOLD:
        cJSON_AddNumberToObject(obj, "mode",  action->params.tw_mode.mode);
        cJSON_AddNumberToObject(obj, "mode2", action->params.tw_mode.mode2);
        // Only emit release_to_original when set; absence == false on read.
        if (action->params.tw_mode.release_to_original) {
          cJSON_AddBoolToObject(obj, "release_to_original", true);
        }
        break;
      case VARIANT_CYCLE: {
        cJSON_AddNumberToObject(obj, "num_modes", action->params.tw_mode.num_modes);
        cJSON* modes = cJSON_CreateArray();
        for (int i = 0; i < action->params.tw_mode.num_modes && i < 8; i++) {
          cJSON_AddItemToArray(modes, cJSON_CreateNumber(action->params.tw_mode.modes[i]));
        }
        cJSON_AddItemToObject(obj, "modes", modes);
        break;
      }
      default:
        break;
    }
  } else if (action->type == ACTION_LFO) {
    // Always emit slot. For VARIANT_MODIFY, additionally emit each override
    // only when it carries a non-sentinel value -- absent fields mean
    // "Original" (leave the scene config in place) on the load side too.
    cJSON_AddNumberToObject(obj, "slot", action->params.lfo.slot);
    if (action->variant == VARIANT_MODIFY) {
      if (action->params.lfo.waveform != ACTION_LFO_ORIG_U8)
        cJSON_AddNumberToObject(obj, "waveform", action->params.lfo.waveform);
      if (action->params.lfo.rate_mode != ACTION_LFO_ORIG_U8)
        cJSON_AddNumberToObject(obj, "rate_mode", action->params.lfo.rate_mode);
      if (action->params.lfo.rate_hz_x100 != ACTION_LFO_ORIG_U16)
        cJSON_AddNumberToObject(obj, "rate_hz_x100", action->params.lfo.rate_hz_x100);
      if (action->params.lfo.division != ACTION_LFO_ORIG_U8)
        cJSON_AddNumberToObject(obj, "division", action->params.lfo.division);
      if (action->params.lfo.floor != ACTION_LFO_ORIG_U8)
        cJSON_AddNumberToObject(obj, "floor", action->params.lfo.floor);
      if (action->params.lfo.ceiling != ACTION_LFO_ORIG_U8)
        cJSON_AddNumberToObject(obj, "ceiling", action->params.lfo.ceiling);
      if (action->params.lfo.resolution_mode != ACTION_LFO_ORIG_U8)
        cJSON_AddNumberToObject(obj, "resolution_mode", action->params.lfo.resolution_mode);
      if (action->params.lfo.manual_steps != ACTION_LFO_ORIG_STEPS)
        cJSON_AddNumberToObject(obj, "manual_steps", action->params.lfo.manual_steps);
    }
  } else if (action->type == ACTION_RTG && action->variant == VARIANT_MODIFY) {
    const action_engine_modify_t* m = &action->params.rtg_modify;
    if (m->rate_mode != ACTION_LFO_ORIG_U8)
      cJSON_AddNumberToObject(obj, "rate_mode", m->rate_mode);
    if (m->rate_hz_x100 != ACTION_LFO_ORIG_U16)
      cJSON_AddNumberToObject(obj, "rate_hz_x100", m->rate_hz_x100);
    if (m->division != ACTION_LFO_ORIG_U8)
      cJSON_AddNumberToObject(obj, "division", m->division);
    if (m->glide != ACTION_LFO_ORIG_U8)
      cJSON_AddNumberToObject(obj, "glide", m->glide);
    if (m->probability != ACTION_LFO_ORIG_U8)
      cJSON_AddNumberToObject(obj, "probability", m->probability);
  } else if (action->type == ACTION_SAMPLE_HOLD && action->variant == VARIANT_MODIFY) {
    const action_engine_modify_t* m = &action->params.sh_modify;
    if (m->rate_mode != ACTION_LFO_ORIG_U8)
      cJSON_AddNumberToObject(obj, "rate_mode", m->rate_mode);
    if (m->rate_hz_x100 != ACTION_LFO_ORIG_U16)
      cJSON_AddNumberToObject(obj, "rate_hz_x100", m->rate_hz_x100);
    if (m->division != ACTION_LFO_ORIG_U8)
      cJSON_AddNumberToObject(obj, "division", m->division);
    if (m->glide != ACTION_LFO_ORIG_U8)
      cJSON_AddNumberToObject(obj, "glide", m->glide);
    if (m->probability != ACTION_LFO_ORIG_U8)
      cJSON_AddNumberToObject(obj, "probability", m->probability);
  } else if (action->type == ACTION_CLOCK) {
    switch (action->variant) {
      case VARIANT_TOGGLE:
      case VARIANT_HOLD:
        cJSON_AddBoolToObject(obj, "start_enabled", action->params.clock.start_enabled);
        break;
      case VARIANT_BURST:
        cJSON_AddNumberToObject(obj, "speed_percent", action->params.clock.speed_percent);
        break;
      default:
        break;
    }
  } else if (action->type == ACTION_CUT) {
    const char* mode_str = (action->params.cut.cut_mode == 0) ? "local" :
                           (action->params.cut.cut_mode == 1) ? "passthrough" : "both";
    cJSON_AddStringToObject(obj, "cut_mode", mode_str);
  } else if (action->type == ACTION_CONFIRM_PENDING) {
    // Only serialize if not default (preset = 0)
    if (action->params.confirm.target == CONFIRM_TARGET_SCENE) {
      cJSON_AddStringToObject(obj, "confirm_target", "scene");
    }
  } else if (action->type == ACTION_UI) {
    switch (action->variant) {
      case VARIANT_SET:
        cJSON_AddNumberToObject(obj, "module", action->params.ui.module);
        break;
      case VARIANT_HOLD:
        cJSON_AddNumberToObject(obj, "module", action->params.ui.module);
        cJSON_AddNumberToObject(obj, "module2", action->params.ui.module2);
        break;
      case VARIANT_CYCLE:
        cJSON_AddNumberToObject(obj, "num_modules", action->params.ui.num_modules);
        {
          cJSON* modules = cJSON_CreateArray();
          for (int i = 0; i < action->params.ui.num_modules && i < 8; i++) {
            cJSON_AddItemToArray(modules,
              cJSON_CreateNumber(action->params.ui.modules[i]));
          }
          cJSON_AddItemToObject(obj, "modules", modules);
        }
        break;
      default:
        break;
    }
  } else if (action->type == ACTION_PARAM) {
    if (action->params.tw_param.target != PARAM_TARGET_TOUCHWHEEL) {
      cJSON_AddStringToObject(obj, "target",
        param_target_to_string((param_target_t)action->params.tw_param.target));
    }
    switch (action->variant) {
      case VARIANT_HOLD:
        cJSON_AddNumberToObject(obj, "param", action->params.tw_param.param);
        if (!action->params.tw_param.release_to_original) {
          cJSON_AddNumberToObject(obj, "param2", action->params.tw_param.param2);
        }
        if (action->params.tw_param.release_to_original) {
          cJSON_AddBoolToObject(obj, "release_to_original", true);
        }
        break;
      case VARIANT_CYCLE:
        cJSON_AddNumberToObject(obj, "num_params", action->params.tw_param.num_params);
        {
          cJSON* params = cJSON_CreateArray();
          for (int i = 0; i < action->params.tw_param.num_params && i < 8; i++) {
            cJSON_AddItemToArray(params,
              cJSON_CreateNumber(action->params.tw_param.params[i]));
          }
          cJSON_AddItemToObject(obj, "params", params);
        }
        break;
      default:
        break;
    }
  } else if (action->type == ACTION_PUNCH_IN) {
    cJSON_AddNumberToObject(obj, "start_cc", action->params.punch_in.start_cc);
    cJSON_AddNumberToObject(obj, "start_value", action->params.punch_in.start_value);
    cJSON_AddNumberToObject(obj, "finish_cc", action->params.punch_in.finish_cc);
    cJSON_AddNumberToObject(obj, "finish_value", action->params.punch_in.finish_value);
    cJSON_AddStringToObject(obj, "duration",
      punch_in_duration_to_string(action->params.punch_in.duration));
  } else if (action->type == ACTION_FLAG_CEREMONY) {
    cJSON_AddNumberToObject(obj, "flag_up_cc", action->params.flag_ceremony.flag_up_cc);
    cJSON_AddNumberToObject(obj, "flag_up_value", action->params.flag_ceremony.flag_up_value);
    cJSON_AddNumberToObject(obj, "flag_down_cc", action->params.flag_ceremony.flag_down_cc);
    cJSON_AddNumberToObject(obj, "flag_down_value", action->params.flag_ceremony.flag_down_value);
  } else if (action->type == ACTION_BOOMERANG) {
    const char* ot;
    switch (action->params.boomerang.output_type) {
      case OUTPUT_TYPE_CC:          ot = "cc"; break;
      case OUTPUT_TYPE_LFO_RATE:    ot = "lfo_rate"; break;
      case OUTPUT_TYPE_LFO_DEPTH:   ot = "lfo_depth"; break;
      case OUTPUT_TYPE_LFO2_RATE:   ot = "lfo2_rate"; break;
      case OUTPUT_TYPE_LFO2_DEPTH:  ot = "lfo2_depth"; break;
      case OUTPUT_TYPE_LFO1_RATE:   ot = "lfo1_rate"; break;
      case OUTPUT_TYPE_LFO1_DEPTH:  ot = "lfo1_depth"; break;
      case OUTPUT_TYPE_RTG_RATE:    ot = "rtg_rate"; break;
      case OUTPUT_TYPE_SH_RATE:     ot = "sh_rate"; break;
      case OUTPUT_TYPE_PITCH_BEND:  ot = "pitch_bend"; break;
      case OUTPUT_TYPE_TEMPO_NUDGE: ot = "tempo_nudge"; break;
      default: ot = "cc"; break;
    }
    cJSON_AddStringToObject(obj, "output_type", ot);

    const char* lt;
    switch (action->params.boomerang.lfo_target) {
      case LFO_TARGET_LFO1: lt = "lfo1"; break;
      case LFO_TARGET_LFO2: lt = "lfo2"; break;
      case LFO_TARGET_BOTH: lt = "both"; break;
      default: lt = "both"; break;
    }
    cJSON_AddStringToObject(obj, "lfo_target", lt);

    cJSON_AddNumberToObject(obj, "cc_number", action->params.boomerang.cc_number);
    cJSON_AddStringToObject(obj, "target_mode",
      action->params.boomerang.target_mode == BOOMERANG_TARGET_RANDOM ? "random" : "explicit");
    cJSON_AddNumberToObject(obj, "target_value", action->params.boomerang.target_value);
    cJSON_AddStringToObject(obj, "start_mode",
      action->params.boomerang.start_mode == BOOMERANG_START_EXPLICIT ? "explicit" : "current");
    cJSON_AddNumberToObject(obj, "start_value", action->params.boomerang.start_value);

    // Attack
    const char* amode;
    switch (action->params.boomerang.attack_mode) {
      case BOOMERANG_DUR_INSTANT:  amode = "instant"; break;
      case BOOMERANG_DUR_TIME_MS:  amode = "time_ms"; break;
      case BOOMERANG_DUR_DIVISION: amode = "division"; break;
      default: amode = "instant"; break;
    }
    cJSON_AddStringToObject(obj, "attack_mode", amode);
    cJSON_AddNumberToObject(obj, "attack_time_ms", action->params.boomerang.attack_time_ms);
    cJSON_AddStringToObject(obj, "attack_division",
      morph_division_to_string((morph_division_t)action->params.boomerang.attack_division));
    cJSON_AddNumberToObject(obj, "attack_curve", action->params.boomerang.attack_curve);
    cJSON_AddNumberToObject(obj, "attack_curve_slope", action->params.boomerang.attack_curve_slope);

    // Sustain
    const char* smode;
    switch (action->params.boomerang.sustain_mode) {
      case BOOMERANG_DUR_INSTANT:  smode = "instant"; break;
      case BOOMERANG_DUR_TIME_MS:  smode = "time_ms"; break;
      case BOOMERANG_DUR_DIVISION: smode = "division"; break;
      default: smode = "instant"; break;
    }
    cJSON_AddStringToObject(obj, "sustain_mode", smode);
    cJSON_AddNumberToObject(obj, "sustain_time_ms", action->params.boomerang.sustain_time_ms);
    cJSON_AddStringToObject(obj, "sustain_division",
      morph_division_to_string((morph_division_t)action->params.boomerang.sustain_division));

    // Release
    const char* rmode;
    switch (action->params.boomerang.release_mode) {
      case BOOMERANG_DUR_INSTANT:  rmode = "instant"; break;
      case BOOMERANG_DUR_TIME_MS:  rmode = "time_ms"; break;
      case BOOMERANG_DUR_DIVISION: rmode = "division"; break;
      default: rmode = "instant"; break;
    }
    cJSON_AddStringToObject(obj, "release_mode", rmode);
    cJSON_AddNumberToObject(obj, "release_time_ms", action->params.boomerang.release_time_ms);
    cJSON_AddStringToObject(obj, "release_division",
      morph_division_to_string((morph_division_t)action->params.boomerang.release_division));
    cJSON_AddNumberToObject(obj, "release_curve", action->params.boomerang.release_curve);
    cJSON_AddNumberToObject(obj, "release_curve_slope", action->params.boomerang.release_curve_slope);
  }

  // Serialize timing (only if not immediate default)
  if (action->timing != ACTION_TIMING_IMMEDIATE) {
    const char* timing_str = action_timing_to_string(action->timing, action->timing_beat);
    cJSON_AddStringToObject(obj, "timing", timing_str);
  }
  
  // Serialize repeat settings (only if enabled)
  if (action->repeat_enabled) {
    cJSON_AddBoolToObject(obj, "repeat", true);
    cJSON_AddStringToObject(obj, "repeat_division",
      action_repeat_division_to_string(action->repeat_division));
    // Only serialize probability if not default (100%)
    if (action->probability > 0 && action->probability < 100) {
      cJSON_AddNumberToObject(obj, "probability", action->probability);
    }
    // Only serialize pattern if enabled (length >= 2)
    if (action->pattern_length >= 2) {
      cJSON_AddNumberToObject(obj, "pattern_length", action->pattern_length);
      cJSON_AddNumberToObject(obj, "pattern_mask", action->pattern_mask);
    }
  }

  // Serialize raise_flag (only if enabled)
  if (action->raise_flag) {
    cJSON_AddBoolToObject(obj, "raise_flag", true);
  }

  // Serialize morph settings (only if enabled; ACTION_CONTROL + HOLD/CYCLE variants, RANDOMIZE)
  if (action->morph_enabled && action_supports_morph(action->type)) {
    cJSON_AddBoolToObject(obj, "morph", true);
    // Only serialize non-default values
    if (action->morph_steps_mode != MORPH_STEPS_AUTO) {
      cJSON_AddStringToObject(obj, "morph_steps", 
        morph_steps_mode_to_string(action->morph_steps_mode));
    }
    if (action->morph_steps_mode == MORPH_STEPS_MANUAL && action->morph_manual_steps != 32) {
      cJSON_AddNumberToObject(obj, "morph_manual_steps", action->morph_manual_steps);
    }
    if (action->morph_timing_mode != MORPH_TIMING_FEEL) {
      cJSON_AddStringToObject(obj, "morph_timing",
        morph_timing_mode_to_string(action->morph_timing_mode));
    }
    if (action->morph_timing_mode == MORPH_TIMING_FEEL && 
        action->morph_feel != MORPH_FEEL_MEDIUM) {
      cJSON_AddStringToObject(obj, "morph_feel", morph_feel_to_string(action->morph_feel));
    }
    if (action->morph_timing_mode != MORPH_TIMING_FEEL && 
        action->morph_division != MORPH_DIV_BAR) {
      cJSON_AddStringToObject(obj, "morph_division", 
        morph_division_to_string(action->morph_division));
    }
  }

  // Serialize Follow-Up settings (only for eligible hold variants with a
  // non-default mode). JSON keys match the pre-promotion control_hold shape
  // so existing scene files keep working without a key rename.
  if (action_supports_followup_for(action) && action->followup_mode != 0) {
    const char* mode_str = (action->followup_mode == 1) ? "if_held" : "if_quick";
    cJSON_AddStringToObject(obj, "release_mode", mode_str);
    uint16_t thr = action->followup_threshold_ms ? action->followup_threshold_ms : 1000;
    cJSON_AddNumberToObject(obj, "release_threshold_ms", thr);
  }

  return obj;
}

static action_t json_to_action(cJSON* obj) {
  action_t action = {0};
  cJSON* type = cJSON_GetObjectItem(obj, "type");

  if (type) {
    if (cJSON_IsString(type)) {
      // New format: string name. The migration component is consulted
      // inside action_type_from_string when a name is not found in the
      // current table; we additionally probe it here so the variant is
      // populated for legacy single-string types (e.g. "tempo_hold" ->
      // ACTION_TEMPO + VARIANT_HOLD).
      action.type = action_type_from_string(type->valuestring);
      action_variant_t mig_variant = VARIANT_NONE;
      action_type_t mig_type = ACTION_NONE;
      if (action_migration_translate_type(type->valuestring, &mig_type, &mig_variant)) {
        if (mig_variant != VARIANT_NONE) action.variant = mig_variant;
      }
      ESP_LOGD(TAG, "Loaded action: %s -> %d (variant %d)",
        type->valuestring, action.type, action.variant);
    } else if (cJSON_IsNumber(type)) {
      // Legacy format: integer (for backward compatibility)
      action.type = (action_type_t)type->valueint;
      ESP_LOGW(TAG, "Loading action with legacy integer type %d (use string names)", type->valueint);
    } else {
      ESP_LOGE(TAG, "Action type is neither string nor number!");
    }
  } else {
    ESP_LOGE(TAG, "Action missing 'type' field!");
  }

  // Parse explicit variant string if present. Wins over any variant the
  // migration step may have set (new-format files always carry it).
  cJSON* variant_node = cJSON_GetObjectItem(obj, "variant");
  if (variant_node && cJSON_IsString(variant_node)) {
    action_variant_t v = action_variant_from_string(variant_node->valuestring);
    if (v != VARIANT_NONE) action.variant = v;
  }

  // Sensible default for consolidated families when an old file uses the
  // bare canonical type name without a variant (e.g. "type": "control"
  // from before the Control family rename to control_change). This keeps
  // legacy data behaving as it always did rather than landing in the
  // default-case warning path at dispatch.
  if (action.variant == VARIANT_NONE) {
    if (action.type == ACTION_CONTROL)    action.variant = VARIANT_SET;
    if (action.type == ACTION_TEMPO)      action.variant = VARIANT_TAP;
    if (action.type == ACTION_SCENE)      action.variant = VARIANT_SET;
    if (action.type == ACTION_PRESET)     action.variant = VARIANT_SET;
    if (action.type == ACTION_TRANSPORT)  action.variant = VARIANT_PLAY;
    if (action.type == ACTION_TOUCHWHEEL) action.variant = VARIANT_HOLD;
    if (action.type == ACTION_LFO)        action.variant = VARIANT_START;
    if (action.type == ACTION_CLOCK)      action.variant = VARIANT_TOGGLE;
    if (action.type == ACTION_CUT)        action.variant = VARIANT_TOGGLE;
    if (action.type == ACTION_UI)         action.variant = VARIANT_SET;
    if (action.type == ACTION_PARAM)      action.variant = VARIANT_HOLD;
    if (action.type == ACTION_RTG)        action.variant = VARIANT_TOGGLE;
    if (action.type == ACTION_SAMPLE_HOLD) action.variant = VARIANT_TOGGLE;
  }

  // Piano Pedal: dedicated parser so the generic CC parser below doesn't
  // write into the wrong union member. Handles both the new-style
  // {"type":"piano_pedal","cc":<n>} and the legacy {"type":"sustain"} /
  // {"type":"sostenuto"} migration cases.
  if (action.type == ACTION_PIANO_PEDAL) {
    uint8_t cc_seed = 64;  // default to Damper
    cJSON* pp_cc = cJSON_GetObjectItem(obj, "cc");
    if (pp_cc && cJSON_IsNumber(pp_cc)) {
      cc_seed = (uint8_t)pp_cc->valueint;
    } else if (type && cJSON_IsString(type)) {
      // No "cc" field: probably migrated from a legacy "sustain" /
      // "sostenuto" type string. Pick the matching standard CC so
      // behavior is preserved bit-for-bit.
      if (strcmp(type->valuestring, "sostenuto") == 0) cc_seed = 66;
      else cc_seed = 64;
    }
    action = action_create_piano_pedal(cc_seed);
    // action_create_piano_pedal() clamps cc_seed to the whitelist and
    // resets every other field of the struct, so we're done -- skip the
    // generic CC parser below.
    goto piano_pedal_done;
  }


  // Parse CC actions (supports both single and multi-CC formats)
  cJSON* cc = cJSON_GetObjectItem(obj, "cc");
  cJSON* value = cJSON_GetObjectItem(obj, "value");
  cJSON* value2 = cJSON_GetObjectItem(obj, "value2");
  cJSON* values = cJSON_GetObjectItem(obj, "values");

  if (cc) {
    if (cJSON_IsArray(cc)) {
      // Multi-CC format: cc is array
      int num_ccs = cJSON_GetArraySize(cc);
      if (num_ccs > 4) num_ccs = 4;
      action.params.control.num_ccs = num_ccs;
      for (int i = 0; i < num_ccs; i++) {
        cJSON* item = cJSON_GetArrayItem(cc, i);
        if (item) action.params.control.cc_numbers[i] = item->valueint;
      }
      // Parse value array
      if (value && cJSON_IsArray(value)) {
        for (int i = 0; i < num_ccs; i++) {
          cJSON* item = cJSON_GetArrayItem(value, i);
          if (item) action.params.control.values[i] = item->valueint;
        }
      }
      // Parse value2 array (for hold)
      if (value2 && cJSON_IsArray(value2)) {
        for (int i = 0; i < num_ccs; i++) {
          cJSON* item = cJSON_GetArrayItem(value2, i);
          if (item) action.params.control.values2[i] = item->valueint;
        }
      }
      // Parse cycle values (array of arrays)
      if (values && cJSON_IsArray(values)) {
        cJSON* first = cJSON_GetArrayItem(values, 0);
        if (first && cJSON_IsArray(first)) {
          // Multi-CC cycle: values is array of arrays
          int num_steps = cJSON_GetArraySize(first);
          if (num_steps > 8) num_steps = 8;
          action.params.control.num_cycle_steps = num_steps;
          for (int i = 0; i < num_ccs; i++) {
            cJSON* steps = cJSON_GetArrayItem(values, i);
            if (steps && cJSON_IsArray(steps)) {
              for (int j = 0; j < num_steps; j++) {
                cJSON* item = cJSON_GetArrayItem(steps, j);
                if (item) action.params.control.cycle_values[i][j] = item->valueint;
              }
            }
          }
        }
      }
    } else {
      // Single CC format (backward compatible)
      action.params.control.num_ccs = 1;
      action.params.control.cc_numbers[0] = cc->valueint;
      if (value && cJSON_IsNumber(value)) {
        action.params.control.values[0] = value->valueint;
      }
      if (value2 && cJSON_IsNumber(value2)) {
        action.params.control.values2[0] = value2->valueint;
      }
      // Parse single CC cycle values
      if (values && cJSON_IsArray(values)) {
        int num_steps = cJSON_GetArraySize(values);
        if (num_steps > 8) num_steps = 8;
        action.params.control.num_cycle_steps = num_steps;
        for (int i = 0; i < num_steps; i++) {
          cJSON* item = cJSON_GetArrayItem(values, i);
          if (item) action.params.control.cycle_values[0][i] = item->valueint;
        }
      }
    }
  }
  
  // Parse Follow-Up settings. Top-level on action_t after promotion; keyed by
  // legacy names so old control_hold scenes load identically. Read for any
  // action -- non-eligible actions just carry the value harmlessly, but
  // gating on action_supports_followup_for() keeps the field clean.
  if (action_supports_followup_for(&action)) {
    cJSON* fu_mode = cJSON_GetObjectItem(obj, "release_mode");
    if (fu_mode && cJSON_IsString(fu_mode)) {
      const char* mode_str = fu_mode->valuestring;
      if (strcmp(mode_str, "if_held") == 0) action.followup_mode = 1;
      else if (strcmp(mode_str, "if_quick") == 0) action.followup_mode = 2;
      else action.followup_mode = 0;
    }
    cJSON* fu_thr = cJSON_GetObjectItem(obj, "release_threshold_ms");
    if (fu_thr && cJSON_IsNumber(fu_thr)) {
      action.followup_threshold_ms = (uint16_t)fu_thr->valueint;
    } else if (action.followup_mode != 0) {
      action.followup_threshold_ms = 1000;
    }
  }
  
  // Parse randomize CC (uses different struct, always array format)
  if (action.type == ACTION_RANDOMIZE && cc && cJSON_IsArray(cc)) {
    int num_ccs = cJSON_GetArraySize(cc);
    if (num_ccs > 8) num_ccs = 8;
    action.params.randomize.num_ccs = num_ccs;
    for (int i = 0; i < num_ccs; i++) {
      cJSON* item = cJSON_GetArrayItem(cc, i);
      if (item) action.params.randomize.cc_numbers[i] = item->valueint;
    }
  }
  
  // Parse note actions
  if (action.type == ACTION_NOTE) {
    action_note_params_seed(&action);
    cJSON* note = cJSON_GetObjectItem(obj, "note");
    cJSON* velocity = cJSON_GetObjectItem(obj, "velocity");
    if (note) action.params.note.note = (uint8_t)note->valueint;
    if (velocity) action.params.note.velocity = (uint8_t)velocity->valueint;
    cJSON* floor = cJSON_GetObjectItem(obj, "random_floor");
    cJSON* ceiling = cJSON_GetObjectItem(obj, "random_ceiling");
    if (floor && cJSON_IsNumber(floor))
      action.params.note.random_floor = (uint8_t)floor->valueint;
    if (ceiling && cJSON_IsNumber(ceiling))
      action.params.note.random_ceiling = (uint8_t)ceiling->valueint;
    if (action.params.note.random_floor < 36) action.params.note.random_floor = 36;
    if (action.params.note.random_floor > 96) action.params.note.random_floor = 96;
    if (action.params.note.random_ceiling < 36) action.params.note.random_ceiling = 96;
    if (action.params.note.random_ceiling > 96) action.params.note.random_ceiling = 96;
    if (action.params.note.random_floor > action.params.note.random_ceiling)
      action.params.note.random_ceiling = action.params.note.random_floor;
    cJSON* voices = cJSON_GetObjectItem(obj, "voices");
    if (voices && cJSON_IsNumber(voices)) {
      uint8_t v = (uint8_t)voices->valueint;
      if (v < 1) v = 1;
      if (v > 4) v = 4;
      action.params.note.voices = v;
    }
    cJSON* bass = cJSON_GetObjectItem(obj, "bass");
    if (bass && cJSON_IsBool(bass))
      action.params.note.bass = cJSON_IsTrue(bass);
    cJSON* aftertouch = cJSON_GetObjectItem(obj, "aftertouch");
    if (aftertouch && cJSON_IsBool(aftertouch))
      action.params.note.aftertouch = cJSON_IsTrue(aftertouch);
  }
  
  // Parse target/scene/program actions. ACTION_PRESET + VARIANT_SET routes
  // to preset.program (uint16_t) so bank-aware values > 255 survive the load;
  // everything else (ACTION_SCENE today) stays on the uint8_t alias.
  cJSON* number = cJSON_GetObjectItem(obj, "number");
  if (number) {
    if (action.type == ACTION_PRESET) {
      action.params.preset.program = (uint16_t)number->valueint;
    } else {
      action.params.target.number = (uint8_t)number->valueint;
    }
  }

  // Parse preset hold/cycle fields. The variant decides which keys we look
  // for; legacy files (type:"preset_hold", type:"preset_cycle") already
  // resolved to ACTION_PRESET + VARIANT_HOLD/CYCLE via the migration table
  // before we get here, so a single branch covers both old and new shapes.
  if (action.type == ACTION_PRESET && action.variant == VARIANT_HOLD) {
    cJSON* press = cJSON_GetObjectItem(obj, "press_preset");
    cJSON* release = cJSON_GetObjectItem(obj, "release_preset");
    cJSON* original = cJSON_GetObjectItem(obj, "release_to_original");
    if (press) action.params.preset.press_preset = (uint16_t)press->valueint;
    if (release) action.params.preset.release_preset = (uint16_t)release->valueint;
    if (original && cJSON_IsBool(original)) {
      action.params.preset.release_to_original = cJSON_IsTrue(original) ? 1 : 0;
    }
  }
  if (action.type == ACTION_PRESET && action.variant == VARIANT_CYCLE) {
    cJSON* num_presets = cJSON_GetObjectItem(obj, "num_presets");
    cJSON* presets = cJSON_GetObjectItem(obj, "presets");
    if (num_presets) {
      action.params.preset.num_presets = num_presets->valueint;
    }
    if (presets && cJSON_IsArray(presets)) {
      int count = cJSON_GetArraySize(presets);
      if (count > 8) count = 8;
      for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(presets, i);
        if (item) action.params.preset.cycle_presets[i] = (uint16_t)item->valueint;
      }
    }
  }
  
  // Parse tempo actions (consolidated: ACTION_TEMPO + variant)
  if (action.type == ACTION_TEMPO) {
    cJSON* bpm = cJSON_GetObjectItem(obj, "bpm");
    if (bpm) action.params.tempo.bpm = (uint16_t)bpm->valueint;

    if (action.variant == VARIANT_SET && action.params.tempo.bpm == ACTION_TEMPO_BPM_RANDOM) {
      cJSON* floor = cJSON_GetObjectItem(obj, "random_floor");
      cJSON* ceiling = cJSON_GetObjectItem(obj, "random_ceiling");
      action.params.tempo.random_floor = floor ? (uint16_t)floor->valueint : 20;
      action.params.tempo.random_ceiling = ceiling ? (uint16_t)ceiling->valueint : 300;
    }

    if (action.variant == VARIANT_HOLD) {
      cJSON* press_bpm = cJSON_GetObjectItem(obj, "press_bpm");
      cJSON* release_bpm = cJSON_GetObjectItem(obj, "release_bpm");
      if (press_bpm) action.params.tempo.press_bpm = press_bpm->valueint;
      if (release_bpm) action.params.tempo.release_bpm = release_bpm->valueint;
    }
    if (action.variant == VARIANT_CYCLE) {
      cJSON* num_tempos = cJSON_GetObjectItem(obj, "num_tempos");
      cJSON* tempos = cJSON_GetObjectItem(obj, "tempos");
      if (num_tempos) {
        action.params.tempo.num_tempos = num_tempos->valueint;
      }
      if (tempos && cJSON_IsArray(tempos)) {
        int count = cJSON_GetArraySize(tempos);
        if (count > 8) count = 8;
        for (int i = 0; i < count; i++) {
          cJSON* item = cJSON_GetArrayItem(tempos, i);
          if (item) action.params.tempo.cycle_tempos[i] = item->valueint;
        }
      }
    }
    if (action.variant == VARIANT_INCREMENT || action.variant == VARIANT_DECREMENT) {
      cJSON* amt = cJSON_GetObjectItem(obj, "inc_amount");
      // Legacy tempo_inc/tempo_dec scenes won't carry inc_amount; the
      // executor treats 0 as 1, so absence is harmless.
      if (amt) {
        int v = amt->valueint;
        if (v < 1) v = 1;
        if (v > 20) v = 20;
        action.params.tempo.inc_amount = (uint8_t)v;
      }
    }
  }
  
  // Parse touchwheel mode actions. Gated on ACTION_TOUCHWHEEL so other
  // types that happen to use the same JSON key names don't write into the
  // wrong union member. captured_mode is transient and is zeroed by the
  // {0} initializer at the top of the function; on next press the handler
  // resnapshots it from the live scene.
  if (action.type == ACTION_TOUCHWHEEL) {
    cJSON* mode      = cJSON_GetObjectItem(obj, "mode");
    cJSON* mode2     = cJSON_GetObjectItem(obj, "mode2");
    cJSON* rto       = cJSON_GetObjectItem(obj, "release_to_original");
    cJSON* num_modes = cJSON_GetObjectItem(obj, "num_modes");
    cJSON* modes     = cJSON_GetObjectItem(obj, "modes");
    if (mode)  action.params.tw_mode.mode  = mode->valueint;
    if (mode2) action.params.tw_mode.mode2 = mode2->valueint;
    if (rto && cJSON_IsBool(rto)) {
      action.params.tw_mode.release_to_original = cJSON_IsTrue(rto) ? 1 : 0;
    }
    if (num_modes) action.params.tw_mode.num_modes = num_modes->valueint;
    if (modes && cJSON_IsArray(modes)) {
      int count = cJSON_GetArraySize(modes);
      if (count > 8) count = 8;
      for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(modes, i);
        if (item) action.params.tw_mode.modes[i] = item->valueint;
      }
    }
  }
  
  // Parse ACTION_LFO actions
  if (action.type == ACTION_LFO) {
    // Seed every MODIFY override at its "Original" sentinel; the field is
    // only filled in when the JSON object actually carries that key.
    action.params.lfo.waveform        = ACTION_LFO_ORIG_U8;
    action.params.lfo.rate_mode       = ACTION_LFO_ORIG_U8;
    action.params.lfo.rate_hz_x100    = ACTION_LFO_ORIG_U16;
    action.params.lfo.division        = ACTION_LFO_ORIG_U8;
    action.params.lfo.floor           = ACTION_LFO_ORIG_U8;
    action.params.lfo.ceiling         = ACTION_LFO_ORIG_U8;
    action.params.lfo.resolution_mode = ACTION_LFO_ORIG_U8;
    action.params.lfo.manual_steps    = ACTION_LFO_ORIG_STEPS;

    cJSON* slot = cJSON_GetObjectItem(obj, "slot");
    if (slot) action.params.lfo.slot = (uint8_t)slot->valueint;

    if (action.variant == VARIANT_MODIFY) {
      // Modern MODIFY overrides -- absent keys stay at the Original sentinel.
      cJSON* item;
      if ((item = cJSON_GetObjectItem(obj, "waveform")))
        action.params.lfo.waveform = (uint8_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "rate_mode")))
        action.params.lfo.rate_mode = (uint8_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "rate_hz_x100")))
        action.params.lfo.rate_hz_x100 = (uint16_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "division")))
        action.params.lfo.division = (uint8_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "floor")))
        action.params.lfo.floor = (uint8_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "ceiling")))
        action.params.lfo.ceiling = (uint8_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "resolution_mode")))
        action.params.lfo.resolution_mode = (uint8_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "manual_steps")))
        action.params.lfo.manual_steps = (uint8_t)item->valueint;

      // Legacy lfo_shape compatibility: the migration alias rewrote the
      // type/variant to ACTION_LFO + VARIANT_MODIFY but the JSON still
      // carries the old shapes[] cycle. Seed the waveform override from
      // shapes[0] so the legacy file at least flips to its first listed
      // waveform on every press. The other 1-7 entries (and the cycle
      // semantics) are lost; this is the documented regression.
      cJSON* shapes = cJSON_GetObjectItem(obj, "shapes");
      if (action.params.lfo.waveform == ACTION_LFO_ORIG_U8 &&
          shapes && cJSON_IsArray(shapes) && cJSON_GetArraySize(shapes) > 0) {
        cJSON* first = cJSON_GetArrayItem(shapes, 0);
        if (first) action.params.lfo.waveform = (uint8_t)first->valueint;
      }
    }
  }

  // Parse RTG / S+H MODIFY overrides
  if (action.type == ACTION_RTG || action.type == ACTION_SAMPLE_HOLD) {
    action_engine_modify_t* m = (action.type == ACTION_RTG)
      ? &action.params.rtg_modify : &action.params.sh_modify;
    action_engine_modify_seed(m);
    if (action.variant == VARIANT_MODIFY) {
      cJSON* item;
      if ((item = cJSON_GetObjectItem(obj, "rate_mode")))
        m->rate_mode = (uint8_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "rate_hz_x100")))
        m->rate_hz_x100 = (uint16_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "division")))
        m->division = (uint8_t)item->valueint;
      else if ((item = cJSON_GetObjectItem(obj, "sync_mult_x1000")))
        m->sync_mult_x1000 = (uint16_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "glide")))
        m->glide = (uint8_t)item->valueint;
      if ((item = cJSON_GetObjectItem(obj, "probability")))
        m->probability = (uint8_t)item->valueint;
    }
  }
  
  // Parse clock actions (consolidated family)
  if (action.type == ACTION_CLOCK) {
    if (action.variant == VARIANT_TOGGLE || action.variant == VARIANT_HOLD) {
      cJSON* start_enabled = cJSON_GetObjectItem(obj, "start_enabled");
      // Default to false (press disables clock, since clock is running by default)
      action.params.clock.start_enabled = start_enabled ? cJSON_IsTrue(start_enabled) : false;
    }
    if (action.variant == VARIANT_BURST) {
      cJSON* speed = cJSON_GetObjectItem(obj, "speed_percent");
      action.params.clock.speed_percent = speed ? (uint16_t)speed->valueint : 100;
    }
  }
  
  // Parse cut actions
  if (action.type == ACTION_CUT) {
    cJSON* cut_mode = cJSON_GetObjectItem(obj, "cut_mode");
    if (cut_mode && cJSON_IsString(cut_mode)) {
      const char* mode_str = cut_mode->valuestring;
      if (strcmp(mode_str, "local") == 0) {
        action.params.cut.cut_mode = 0;
      } else if (strcmp(mode_str, "passthrough") == 0) {
        action.params.cut.cut_mode = 1;
      } else {
        // Default: "both"
        action.params.cut.cut_mode = 2;
      }
    } else {
      // Default to "both"
      action.params.cut.cut_mode = 2;
    }
  }
  
  // Parse confirm_pending action (Advanced mode target)
  if (action.type == ACTION_CONFIRM_PENDING) {
    cJSON* confirm_target = cJSON_GetObjectItem(obj, "confirm_target");
    if (confirm_target && cJSON_IsString(confirm_target)) {
      if (strcmp(confirm_target->valuestring, "scene") == 0) {
        action.params.confirm.target = CONFIRM_TARGET_SCENE;
      } else {
        action.params.confirm.target = CONFIRM_TARGET_PRESET;
      }
    } else {
      // Default to preset
      action.params.confirm.target = CONFIRM_TARGET_PRESET;
    }
  }
  
  // Parse UI module actions
  if (action.type == ACTION_UI) {
    switch (action.variant) {
      case VARIANT_SET: {
        cJSON* module = cJSON_GetObjectItem(obj, "module");
        if (module) action.params.ui.module = (uint8_t)module->valueint;
        break;
      }
      case VARIANT_HOLD: {
        cJSON* module = cJSON_GetObjectItem(obj, "module");
        cJSON* module2 = cJSON_GetObjectItem(obj, "module2");
        if (module) action.params.ui.module = (uint8_t)module->valueint;
        if (module2) action.params.ui.module2 = (uint8_t)module2->valueint;
        break;
      }
      case VARIANT_CYCLE:
      default: {
        cJSON* num_modules = cJSON_GetObjectItem(obj, "num_modules");
        cJSON* modules = cJSON_GetObjectItem(obj, "modules");
        action.params.ui.num_modules = 2;
        if (num_modules) {
          int nm = num_modules->valueint;
          if (nm < 2) nm = 2;
          if (nm > 8) nm = 8;
          action.params.ui.num_modules = (uint8_t)nm;
        }
        if (modules && cJSON_IsArray(modules)) {
          int count = cJSON_GetArraySize(modules);
          if (count > 8) count = 8;
          for (int i = 0; i < count; i++) {
            cJSON* item = cJSON_GetArrayItem(modules, i);
            if (item) action.params.ui.modules[i] = (uint8_t)item->valueint;
          }
          if (action.params.ui.num_modules > count)
            action.params.ui.num_modules = (uint8_t)count;
        }
        break;
      }
    }
  }
  
  // Parse param actions (scene CC stream retarget)
  if (action.type == ACTION_PARAM) {
    cJSON* target = cJSON_GetObjectItem(obj, "target");
    if (target && cJSON_IsString(target)) {
      action.params.tw_param.target =
        (uint8_t)param_target_from_string(target->valuestring);
    } else {
      action.params.tw_param.target = PARAM_TARGET_TOUCHWHEEL;
    }
    switch (action.variant) {
      case VARIANT_HOLD: {
        cJSON* param = cJSON_GetObjectItem(obj, "param");
        cJSON* param2 = cJSON_GetObjectItem(obj, "param2");
        cJSON* original = cJSON_GetObjectItem(obj, "release_to_original");
        if (param) action.params.tw_param.param = (uint8_t)param->valueint;
        if (original) {
          action.params.tw_param.release_to_original = cJSON_IsTrue(original) ? 1 : 0;
        }
        if (param2) {
          action.params.tw_param.param2 = (uint8_t)param2->valueint;
          action.params.tw_param.release_to_original = 0;
        }
        break;
      }
      case VARIANT_CYCLE:
      default: {
        cJSON* num_params = cJSON_GetObjectItem(obj, "num_params");
        cJSON* params_arr = cJSON_GetObjectItem(obj, "params");
        action.params.tw_param.num_params = 2;
        if (num_params) {
          int np = num_params->valueint;
          if (np < 2) np = 2;
          if (np > 8) np = 8;
          action.params.tw_param.num_params = (uint8_t)np;
        }
        if (params_arr && cJSON_IsArray(params_arr)) {
          int count = cJSON_GetArraySize(params_arr);
          if (count > 8) count = 8;
          for (int i = 0; i < count; i++) {
            cJSON* item = cJSON_GetArrayItem(params_arr, i);
            if (item) action.params.tw_param.params[i] = (uint8_t)item->valueint;
          }
          if (action.params.tw_param.num_params > count)
            action.params.tw_param.num_params = (uint8_t)count;
        }
        break;
      }
    }
  }

  // Parse punch-in action
  if (action.type == ACTION_PUNCH_IN) {
    cJSON* start_cc = cJSON_GetObjectItem(obj, "start_cc");
    cJSON* start_value = cJSON_GetObjectItem(obj, "start_value");
    cJSON* finish_cc = cJSON_GetObjectItem(obj, "finish_cc");
    cJSON* finish_value = cJSON_GetObjectItem(obj, "finish_value");
    cJSON* duration = cJSON_GetObjectItem(obj, "duration");

    if (start_cc) action.params.punch_in.start_cc = (uint8_t)start_cc->valueint;
    if (start_value) action.params.punch_in.start_value = (uint8_t)start_value->valueint;
    if (finish_cc) action.params.punch_in.finish_cc = (uint8_t)finish_cc->valueint;
    if (finish_value) action.params.punch_in.finish_value = (uint8_t)finish_value->valueint;
    if (duration && cJSON_IsString(duration)) {
      action.params.punch_in.duration = punch_in_duration_from_string(duration->valuestring);
    } else {
      action.params.punch_in.duration = PUNCH_IN_1_BAR;  // Default
    }
  }

  // Parse flag ceremony action
  if (action.type == ACTION_FLAG_CEREMONY) {
    cJSON* flag_up_cc = cJSON_GetObjectItem(obj, "flag_up_cc");
    cJSON* flag_up_value = cJSON_GetObjectItem(obj, "flag_up_value");
    cJSON* flag_down_cc = cJSON_GetObjectItem(obj, "flag_down_cc");
    cJSON* flag_down_value = cJSON_GetObjectItem(obj, "flag_down_value");

    if (flag_up_cc) action.params.flag_ceremony.flag_up_cc = (uint8_t)flag_up_cc->valueint;
    if (flag_up_value) action.params.flag_ceremony.flag_up_value = (uint8_t)flag_up_value->valueint;
    if (flag_down_cc) action.params.flag_ceremony.flag_down_cc = (uint8_t)flag_down_cc->valueint;
    if (flag_down_value) action.params.flag_ceremony.flag_down_value = (uint8_t)flag_down_value->valueint;
  }

  // Parse boomerang action
  if (action.type == ACTION_BOOMERANG) {
    cJSON* ot = cJSON_GetObjectItem(obj, "output_type");
    if (ot && cJSON_IsString(ot)) {
      const char* s = ot->valuestring;
      if      (strcmp(s, "lfo_rate") == 0)    action.params.boomerang.output_type = OUTPUT_TYPE_LFO_RATE;
      else if (strcmp(s, "lfo_depth") == 0)   action.params.boomerang.output_type = OUTPUT_TYPE_LFO_DEPTH;
      else if (strcmp(s, "lfo2_rate") == 0)   action.params.boomerang.output_type = OUTPUT_TYPE_LFO2_RATE;
      else if (strcmp(s, "lfo2_depth") == 0)  action.params.boomerang.output_type = OUTPUT_TYPE_LFO2_DEPTH;
      else if (strcmp(s, "lfo1_rate") == 0)   action.params.boomerang.output_type = OUTPUT_TYPE_LFO1_RATE;
      else if (strcmp(s, "lfo1_depth") == 0)  action.params.boomerang.output_type = OUTPUT_TYPE_LFO1_DEPTH;
      else if (strcmp(s, "rtg_rate") == 0)    action.params.boomerang.output_type = OUTPUT_TYPE_RTG_RATE;
      else if (strcmp(s, "sh_rate") == 0)     action.params.boomerang.output_type = OUTPUT_TYPE_SH_RATE;
      else if (strcmp(s, "pitch_bend") == 0)  action.params.boomerang.output_type = OUTPUT_TYPE_PITCH_BEND;
      else if (strcmp(s, "tempo_nudge") == 0) action.params.boomerang.output_type = OUTPUT_TYPE_TEMPO_NUDGE;
      else                                    action.params.boomerang.output_type = OUTPUT_TYPE_CC;
    }

    cJSON* lt = cJSON_GetObjectItem(obj, "lfo_target");
    if (lt && cJSON_IsString(lt)) {
      const char* s = lt->valuestring;
      if      (strcmp(s, "lfo1") == 0) action.params.boomerang.lfo_target = LFO_TARGET_LFO1;
      else if (strcmp(s, "lfo2") == 0) action.params.boomerang.lfo_target = LFO_TARGET_LFO2;
      else                              action.params.boomerang.lfo_target = LFO_TARGET_BOTH;
    }

    cJSON* cc_number = cJSON_GetObjectItem(obj, "cc_number");
    if (cc_number) action.params.boomerang.cc_number = (uint8_t)cc_number->valueint;

    cJSON* target_mode = cJSON_GetObjectItem(obj, "target_mode");
    if (target_mode && cJSON_IsString(target_mode)) {
      action.params.boomerang.target_mode = strcmp(target_mode->valuestring, "random") == 0
        ? BOOMERANG_TARGET_RANDOM : BOOMERANG_TARGET_EXPLICIT;
    }
    cJSON* target_value = cJSON_GetObjectItem(obj, "target_value");
    if (target_value) action.params.boomerang.target_value = (uint16_t)target_value->valueint;

    cJSON* start_mode = cJSON_GetObjectItem(obj, "start_mode");
    if (start_mode && cJSON_IsString(start_mode)) {
      action.params.boomerang.start_mode = strcmp(start_mode->valuestring, "explicit") == 0
        ? BOOMERANG_START_EXPLICIT : BOOMERANG_START_CURRENT;
    }
    cJSON* start_value = cJSON_GetObjectItem(obj, "start_value");
    if (start_value) action.params.boomerang.start_value = (uint16_t)start_value->valueint;

    // Attack
    cJSON* amode = cJSON_GetObjectItem(obj, "attack_mode");
    if (amode && cJSON_IsString(amode)) {
      const char* s = amode->valuestring;
      if      (strcmp(s, "time_ms") == 0)  action.params.boomerang.attack_mode = BOOMERANG_DUR_TIME_MS;
      else if (strcmp(s, "division") == 0) action.params.boomerang.attack_mode = BOOMERANG_DUR_DIVISION;
      else                                  action.params.boomerang.attack_mode = BOOMERANG_DUR_INSTANT;
    }
    cJSON* atime = cJSON_GetObjectItem(obj, "attack_time_ms");
    if (atime) action.params.boomerang.attack_time_ms = (uint16_t)atime->valueint;
    cJSON* adiv = cJSON_GetObjectItem(obj, "attack_division");
    if (adiv && cJSON_IsString(adiv)) {
      action.params.boomerang.attack_division = (uint8_t)morph_division_from_string(adiv->valuestring);
    }
    cJSON* acurve = cJSON_GetObjectItem(obj, "attack_curve");
    if (acurve) action.params.boomerang.attack_curve = (uint8_t)acurve->valueint;
    cJSON* aslope = cJSON_GetObjectItem(obj, "attack_curve_slope");
    if (aslope) action.params.boomerang.attack_curve_slope = (uint8_t)aslope->valueint;

    // Sustain
    cJSON* smode = cJSON_GetObjectItem(obj, "sustain_mode");
    if (smode && cJSON_IsString(smode)) {
      const char* s = smode->valuestring;
      if      (strcmp(s, "time_ms") == 0)  action.params.boomerang.sustain_mode = BOOMERANG_DUR_TIME_MS;
      else if (strcmp(s, "division") == 0) action.params.boomerang.sustain_mode = BOOMERANG_DUR_DIVISION;
      else                                  action.params.boomerang.sustain_mode = BOOMERANG_DUR_INSTANT;
    }
    cJSON* stime = cJSON_GetObjectItem(obj, "sustain_time_ms");
    if (stime) action.params.boomerang.sustain_time_ms = (uint16_t)stime->valueint;
    cJSON* sdiv = cJSON_GetObjectItem(obj, "sustain_division");
    if (sdiv && cJSON_IsString(sdiv)) {
      action.params.boomerang.sustain_division = (uint8_t)morph_division_from_string(sdiv->valuestring);
    }

    // Release
    cJSON* rmode = cJSON_GetObjectItem(obj, "release_mode");
    if (rmode && cJSON_IsString(rmode)) {
      const char* s = rmode->valuestring;
      if      (strcmp(s, "time_ms") == 0)  action.params.boomerang.release_mode = BOOMERANG_DUR_TIME_MS;
      else if (strcmp(s, "division") == 0) action.params.boomerang.release_mode = BOOMERANG_DUR_DIVISION;
      else                                  action.params.boomerang.release_mode = BOOMERANG_DUR_INSTANT;
    }
    cJSON* rtime = cJSON_GetObjectItem(obj, "release_time_ms");
    if (rtime) action.params.boomerang.release_time_ms = (uint16_t)rtime->valueint;
    cJSON* rdiv = cJSON_GetObjectItem(obj, "release_division");
    if (rdiv && cJSON_IsString(rdiv)) {
      action.params.boomerang.release_division = (uint8_t)morph_division_from_string(rdiv->valuestring);
    }
    cJSON* rcurve = cJSON_GetObjectItem(obj, "release_curve");
    if (rcurve) action.params.boomerang.release_curve = (uint8_t)rcurve->valueint;
    cJSON* rslope = cJSON_GetObjectItem(obj, "release_curve_slope");
    if (rslope) action.params.boomerang.release_curve_slope = (uint8_t)rslope->valueint;
  }

  // Parse timing (default: immediate)
  cJSON* timing = cJSON_GetObjectItem(obj, "timing");
  if (timing && cJSON_IsString(timing)) {
    action_timing_from_string(timing->valuestring, &action.timing, &action.timing_beat);
  }
  
  // Parse repeat settings (default: disabled)
  cJSON* repeat = cJSON_GetObjectItem(obj, "repeat");
  if (repeat && cJSON_IsBool(repeat)) {
    action.repeat_enabled = cJSON_IsTrue(repeat);
  }
  cJSON* repeat_div = cJSON_GetObjectItem(obj, "repeat_division");
  if (repeat_div && cJSON_IsString(repeat_div)) {
    action.repeat_division = action_repeat_division_from_string(repeat_div->valuestring);
  }
  
  // Parse probability (default: 100%)
  action.probability = 100;  // Default
  cJSON* prob = cJSON_GetObjectItem(obj, "probability");
  if (prob && cJSON_IsNumber(prob)) {
    int prob_val = prob->valueint;
    if (prob_val >= 10 && prob_val <= 100) {
      action.probability = (uint8_t)prob_val;
    }
  }
  
  // Parse pattern settings (default: disabled)
  action.pattern_length = 0;  // Default: no pattern
  action.pattern_mask = 0xFF; // Default: all steps enabled
  cJSON* pattern_len = cJSON_GetObjectItem(obj, "pattern_length");
  if (pattern_len && cJSON_IsNumber(pattern_len)) {
    int len = pattern_len->valueint;
    if (len >= 2 && len <= 8) {
      action.pattern_length = (uint8_t)len;
    }
  }
  cJSON* pattern_mask = cJSON_GetObjectItem(obj, "pattern_mask");
  if (pattern_mask && cJSON_IsNumber(pattern_mask)) {
    action.pattern_mask = (uint8_t)(pattern_mask->valueint & 0xFF);
  }
  
  // Legacy transport_trigger -> ACTION_TIMING_TRANSPORT_START
  action.transport_trigger = false;
  cJSON* transport_trigger = cJSON_GetObjectItem(obj, "transport_trigger");
  if (transport_trigger && cJSON_IsBool(transport_trigger) &&
      cJSON_IsTrue(transport_trigger)) {
    action.timing = ACTION_TIMING_TRANSPORT_START;
    action.timing_beat = 0;
  }

  // Parse raise_flag (default: disabled)
  action.raise_flag = false;
  cJSON* raise_flag = cJSON_GetObjectItem(obj, "raise_flag");
  if (raise_flag && cJSON_IsBool(raise_flag)) {
    action.raise_flag = cJSON_IsTrue(raise_flag);
  }

  // Parse morph settings (default: disabled; ACTION_CONTROL + HOLD/CYCLE variants, RANDOMIZE)
  action.morph_enabled = false;
  action.morph_steps_mode = MORPH_STEPS_AUTO;
  action.morph_manual_steps = 32;
  action.morph_timing_mode = MORPH_TIMING_FEEL;
  action.morph_feel = MORPH_FEEL_MEDIUM;
  action.morph_division = MORPH_DIV_BAR;
  
  cJSON* morph = cJSON_GetObjectItem(obj, "morph");
  if (morph && cJSON_IsBool(morph)) {
    action.morph_enabled = cJSON_IsTrue(morph);
  }
  cJSON* morph_steps = cJSON_GetObjectItem(obj, "morph_steps");
  if (morph_steps && cJSON_IsString(morph_steps)) {
    action.morph_steps_mode = morph_steps_mode_from_string(morph_steps->valuestring);
  }
  cJSON* morph_manual = cJSON_GetObjectItem(obj, "morph_manual_steps");
  if (morph_manual && cJSON_IsNumber(morph_manual)) {
    int val = morph_manual->valueint;
    if (val >= 8 && val <= 128) {
      action.morph_manual_steps = (uint8_t)val;
    }
  }
  cJSON* morph_timing = cJSON_GetObjectItem(obj, "morph_timing");
  if (morph_timing && cJSON_IsString(morph_timing)) {
    action.morph_timing_mode = morph_timing_mode_from_string(morph_timing->valuestring);
  }
  cJSON* morph_feel = cJSON_GetObjectItem(obj, "morph_feel");
  if (morph_feel && cJSON_IsString(morph_feel)) {
    action.morph_feel = morph_feel_from_string(morph_feel->valuestring);
  }
  cJSON* morph_div = cJSON_GetObjectItem(obj, "morph_division");
  if (morph_div && cJSON_IsString(morph_div)) {
    action.morph_division = morph_division_from_string(morph_div->valuestring);
  }

piano_pedal_done:
  // Post-parse migration hook for future field-shape changes.
  // Currently a stub for the Tempo pilot.
  (void)action_migration_fixup_action(obj, &action);

  return action;
}

// Serialize on_load actions array to JSON
static cJSON* on_load_to_json(const scene_t* scene) {
  cJSON* array = cJSON_CreateArray();
  for (int i = 0; i < scene->num_on_load_actions && i < MAX_ON_LOAD_ACTIONS; i++) {
    cJSON* action_json = action_to_json(&scene->on_load[i]);
    if (action_json) {
      cJSON_AddItemToArray(array, action_json);
    }
  }
  return array;
}

// Parse on_load actions array from JSON
static void json_to_on_load(cJSON* array, scene_t* scene) {
  scene->num_on_load_actions = 0;
  if (!cJSON_IsArray(array)) return;
  
  int count = cJSON_GetArraySize(array);
  
  for (int i = 0; i < count && scene->num_on_load_actions < MAX_ON_LOAD_ACTIONS; i++) {
    action_t action = json_to_action(cJSON_GetArrayItem(array, i));
    if (action.type == ACTION_NONE) continue;
    
    // Validate action is allowed for on_load trigger
    if (!action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_ON_LOAD)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' in on_load",
        action_type_to_string(action.type));
      continue;
    }
    
    scene->on_load[scene->num_on_load_actions++] = action;
  }
}

// Serialize on_play actions array to JSON
static cJSON* on_play_to_json(const scene_t* scene) {
  cJSON* array = cJSON_CreateArray();
  for (int i = 0; i < scene->num_on_play_actions && i < MAX_ON_PLAY_ACTIONS; i++) {
    cJSON* action_json = action_to_json(&scene->on_play[i]);
    if (action_json) {
      cJSON_AddItemToArray(array, action_json);
    }
  }
  return array;
}

// Parse on_play actions array from JSON
static void json_to_on_play(cJSON* array, scene_t* scene) {
  scene->num_on_play_actions = 0;
  if (!cJSON_IsArray(array)) return;
  
  int count = cJSON_GetArraySize(array);
  
  for (int i = 0; i < count && scene->num_on_play_actions < MAX_ON_PLAY_ACTIONS; i++) {
    action_t action = json_to_action(cJSON_GetArrayItem(array, i));
    if (action.type == ACTION_NONE) continue;
    
    // Validate action is allowed for on_play trigger
    if (!action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_ON_PLAY)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' in on_play",
        action_type_to_string(action.type));
      continue;
    }
    
    scene->on_play[scene->num_on_play_actions++] = action;
  }
}

// For backward compatibility: parse array format to single action (takes first action)
static action_t json_array_to_single_action(cJSON* array) {
  if (!cJSON_IsArray(array)) return (action_t){0};
  if (cJSON_GetArraySize(array) == 0) return (action_t){0};
  return json_to_action(cJSON_GetArrayItem(array, 0));
}

static cJSON* cc_triggers_to_json(const scene_t* scene) {
  if (!scene) return NULL;
  cJSON* arr = cJSON_CreateArray();
  for (int i = 0; i < NUM_CC_TRIGGERS; i++) {
    cJSON* slot = cJSON_CreateObject();
    cJSON_AddNumberToObject(slot, "cc_number", scene->cc_triggers[i].cc_number);
    cJSON* action_json = action_to_json(&scene->cc_triggers[i].action);
    if (action_json) cJSON_AddItemToObject(slot, "action", action_json);
    cJSON_AddItemToArray(arr, slot);
  }
  return arr;
}

static void json_to_cc_triggers(cJSON* arr, scene_t* scene) {
  if (!scene) return;
  for (int i = 0; i < NUM_CC_TRIGGERS; i++) {
    scene->cc_triggers[i].cc_number = 0;
    scene->cc_triggers[i].action.type = ACTION_NONE;
    scene->cc_triggers[i].pressing = false;
  }
  if (!arr || !cJSON_IsArray(arr)) return;

  int n = cJSON_GetArraySize(arr);
  if (n > NUM_CC_TRIGGERS) n = NUM_CC_TRIGGERS;
  for (int i = 0; i < n; i++) {
    cJSON* slot = cJSON_GetArrayItem(arr, i);
    if (!slot || !cJSON_IsObject(slot)) continue;

    cJSON* cc = cJSON_GetObjectItem(slot, "cc_number");
    if (cc && cJSON_IsNumber(cc)) {
      int v = cc->valueint;
      if (v < 0) v = 0;
      if (v > 127) v = 127;
      scene->cc_triggers[i].cc_number = (uint8_t)v;
    }

    cJSON* action_obj = cJSON_GetObjectItem(slot, "action");
    if (action_obj) {
      action_t action = json_to_action(action_obj);
      if (action.type != ACTION_NONE &&
          !action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_CC)) {
        ESP_LOGW(TAG, "Ignoring invalid action '%s' in cc_triggers[%d]",
          action_type_to_string(action.type), i);
        action.type = ACTION_NONE;
      }
      scene->cc_triggers[i].action = action;
    }
    scene->cc_triggers[i].pressing = false;
  }
}

// Serialize continuous mapping to JSON
static cJSON* continuous_mapping_to_json(const continuous_mapping_t* mapping) {
  cJSON* obj = cJSON_CreateObject();
  
  cJSON_AddBoolToObject(obj, "enabled", mapping->enabled);
  
  // Serialize output type
  const char* output_type_str;
  switch (mapping->output_type) {
    case OUTPUT_TYPE_CC: output_type_str = "cc"; break;
    case OUTPUT_TYPE_NOTE: output_type_str = "note"; break;
    case OUTPUT_TYPE_LFO_RATE: output_type_str = "lfo_rate"; break;
    case OUTPUT_TYPE_LFO_DEPTH: output_type_str = "lfo_depth"; break;
    case OUTPUT_TYPE_LFO2_RATE: output_type_str = "lfo2_rate"; break;
    case OUTPUT_TYPE_LFO2_DEPTH: output_type_str = "lfo2_depth"; break;
    case OUTPUT_TYPE_LFO1_RATE: output_type_str = "lfo1_rate"; break;
    case OUTPUT_TYPE_LFO1_DEPTH: output_type_str = "lfo1_depth"; break;
    case OUTPUT_TYPE_RTG_RATE: output_type_str = "rtg_rate"; break;
    case OUTPUT_TYPE_SH_RATE: output_type_str = "sh_rate"; break;
    case OUTPUT_TYPE_PITCH_BEND: output_type_str = "pitch_bend"; break;
    case OUTPUT_TYPE_TEMPO_NUDGE: output_type_str = "tempo_nudge"; break;
    default: output_type_str = "cc"; break;
  }
  cJSON_AddStringToObject(obj, "output_type", output_type_str);
  
  // Serialize LFO target (for LFO_RATE/LFO_DEPTH output types)
  const char* lfo_target_str;
  switch (mapping->lfo_target) {
    case LFO_TARGET_LFO1: lfo_target_str = "lfo1"; break;
    case LFO_TARGET_LFO2: lfo_target_str = "lfo2"; break;
    case LFO_TARGET_BOTH: lfo_target_str = "both"; break;
    default: lfo_target_str = "both"; break;
  }
  cJSON_AddStringToObject(obj, "lfo_target", lfo_target_str);
  
  cJSON_AddNumberToObject(obj, "cc_number", mapping->cc_number);
  
  // Multi-CC array - always save all 4 slots to preserve positions
  // (0 values indicate inactive slots)
  if (mapping->num_cc_numbers > 0) {
    cJSON* cc_arr = cJSON_CreateArray();
    for (int i = 0; i < MAX_MULTI_CC; i++) {
      cJSON_AddItemToArray(cc_arr, cJSON_CreateNumber(mapping->cc_numbers[i]));
    }
    cJSON_AddItemToObject(obj, "cc_numbers", cc_arr);
  }
  
  cJSON_AddNumberToObject(obj, "base_note", mapping->base_note);
  cJSON_AddNumberToObject(obj, "note_range", mapping->note_range);
  cJSON_AddNumberToObject(obj, "velocity", mapping->velocity);
  cJSON_AddBoolToObject(obj, "note_latch", mapping->note_latch);
  cJSON_AddNumberToObject(obj, "note_release_ms", mapping->note_release_ms);
  cJSON_AddStringToObject(obj, "polyphony", 
    mapping->polyphony == POLYPHONY_POLY ? "poly" : "mono");
  cJSON_AddNumberToObject(obj, "curve_type", mapping->curve.type);
  cJSON_AddNumberToObject(obj, "polarity", mapping->polarity);
  cJSON_AddNumberToObject(obj, "min_value", mapping->min_value);
  cJSON_AddNumberToObject(obj, "middle_value", mapping->middle_value);
  cJSON_AddNumberToObject(obj, "max_value", mapping->max_value);
  cJSON_AddBoolToObject(obj, "use_idle_value", mapping->use_idle_value);
  cJSON_AddNumberToObject(obj, "idle_value", mapping->idle_value);
  cJSON_AddNumberToObject(obj, "idle_timeout_ms", mapping->idle_timeout_ms);
  
  return obj;
}

// Deserialize continuous mapping from JSON
static void json_to_continuous_mapping(cJSON* obj, continuous_mapping_t* mapping) {
  if (!obj || !mapping) return;
  
  cJSON* enabled = cJSON_GetObjectItem(obj, "enabled");
  if (enabled) mapping->enabled = cJSON_IsTrue(enabled);
  
  cJSON* output_type = cJSON_GetObjectItem(obj, "output_type");
  if (output_type && cJSON_IsString(output_type)) {
    const char* type_str = output_type->valuestring;
    if (strcmp(type_str, "note") == 0) mapping->output_type = OUTPUT_TYPE_NOTE;
    else if (strcmp(type_str, "lfo_rate") == 0) mapping->output_type = OUTPUT_TYPE_LFO_RATE;
    else if (strcmp(type_str, "lfo_depth") == 0) mapping->output_type = OUTPUT_TYPE_LFO_DEPTH;
    else if (strcmp(type_str, "lfo2_rate") == 0) mapping->output_type = OUTPUT_TYPE_LFO2_RATE;
    else if (strcmp(type_str, "lfo2_depth") == 0) mapping->output_type = OUTPUT_TYPE_LFO2_DEPTH;
    else if (strcmp(type_str, "lfo1_rate") == 0) mapping->output_type = OUTPUT_TYPE_LFO1_RATE;
    else if (strcmp(type_str, "lfo1_depth") == 0) mapping->output_type = OUTPUT_TYPE_LFO1_DEPTH;
    else if (strcmp(type_str, "rtg_rate") == 0) mapping->output_type = OUTPUT_TYPE_RTG_RATE;
    else if (strcmp(type_str, "sh_rate") == 0) mapping->output_type = OUTPUT_TYPE_SH_RATE;
    else if (strcmp(type_str, "pitch_bend") == 0) mapping->output_type = OUTPUT_TYPE_PITCH_BEND;
    else if (strcmp(type_str, "tempo_nudge") == 0) mapping->output_type = OUTPUT_TYPE_TEMPO_NUDGE;
    else mapping->output_type = OUTPUT_TYPE_CC;
  }
  
  // Parse LFO target (for LFO_RATE/LFO_DEPTH output types)
  cJSON* lfo_target = cJSON_GetObjectItem(obj, "lfo_target");
  if (lfo_target && cJSON_IsString(lfo_target)) {
    const char* target_str = lfo_target->valuestring;
    if (strcmp(target_str, "lfo1") == 0) mapping->lfo_target = LFO_TARGET_LFO1;
    else if (strcmp(target_str, "lfo2") == 0) mapping->lfo_target = LFO_TARGET_LFO2;
    else mapping->lfo_target = LFO_TARGET_BOTH;
  }
  
  cJSON* cc_num = cJSON_GetObjectItem(obj, "cc_number");
  if (cc_num) mapping->cc_number = cc_num->valueint;
  
  // Multi-CC array - load all slots and recalculate num_cc_numbers
  cJSON* cc_arr = cJSON_GetObjectItem(obj, "cc_numbers");
  if (cc_arr && cJSON_IsArray(cc_arr)) {
    int count = cJSON_GetArraySize(cc_arr);
    if (count > MAX_MULTI_CC) count = MAX_MULTI_CC;
    
    // Load values into slots
    for (int i = 0; i < count; i++) {
      cJSON* item = cJSON_GetArrayItem(cc_arr, i);
      if (item) mapping->cc_numbers[i] = (uint8_t)item->valueint;
    }
    // Clear remaining slots
    for (int i = count; i < MAX_MULTI_CC; i++) {
      mapping->cc_numbers[i] = 0;
    }
    
    // Calculate num_cc_numbers from non-zero entries
    mapping->num_cc_numbers = 0;
    for (int i = 0; i < MAX_MULTI_CC; i++) {
      if (mapping->cc_numbers[i] > 0) {
        mapping->num_cc_numbers++;
      }
    }
  } else {
    mapping->num_cc_numbers = 0;
    for (int i = 0; i < MAX_MULTI_CC; i++) {
      mapping->cc_numbers[i] = 0;
    }
  }
  
  cJSON* base_note = cJSON_GetObjectItem(obj, "base_note");
  if (base_note) mapping->base_note = base_note->valueint;
  
  cJSON* note_range = cJSON_GetObjectItem(obj, "note_range");
  if (note_range) mapping->note_range = note_range->valueint;
  
  cJSON* velocity = cJSON_GetObjectItem(obj, "velocity");
  if (velocity) mapping->velocity = velocity->valueint;
  
  cJSON* note_latch = cJSON_GetObjectItem(obj, "note_latch");
  if (note_latch) mapping->note_latch = cJSON_IsTrue(note_latch);
  
  cJSON* note_release = cJSON_GetObjectItem(obj, "note_release_ms");
  if (note_release) mapping->note_release_ms = note_release->valueint;
  
  cJSON* polyphony_json = cJSON_GetObjectItem(obj, "polyphony");
  if (polyphony_json && cJSON_IsString(polyphony_json)) {
    mapping->polyphony = (strcmp(polyphony_json->valuestring, "poly") == 0) ? 
      POLYPHONY_POLY : POLYPHONY_MONO;
  }
  
  cJSON* curve_type = cJSON_GetObjectItem(obj, "curve_type");
  if (curve_type) mapping->curve = curve_create((curve_type_t)curve_type->valueint);
  
  cJSON* polarity = cJSON_GetObjectItem(obj, "polarity");
  if (polarity) mapping->polarity = (polarity_t)polarity->valueint;
  
  cJSON* min_val = cJSON_GetObjectItem(obj, "min_value");
  if (min_val) mapping->min_value = min_val->valueint;

  cJSON* middle_val = cJSON_GetObjectItem(obj, "middle_value");
  if (middle_val) mapping->middle_value = middle_val->valueint;

  cJSON* max_val = cJSON_GetObjectItem(obj, "max_value");
  if (max_val) mapping->max_value = max_val->valueint;
  
  cJSON* use_idle = cJSON_GetObjectItem(obj, "use_idle_value");
  if (use_idle) mapping->use_idle_value = cJSON_IsTrue(use_idle);
  
  cJSON* idle_val = cJSON_GetObjectItem(obj, "idle_value");
  if (idle_val) mapping->idle_value = idle_val->valueint;
  
  cJSON* idle_timeout = cJSON_GetObjectItem(obj, "idle_timeout_ms");
  if (idle_timeout) mapping->idle_timeout_ms = idle_timeout->valueint;
}

// Serialize LFO config to JSON
static cJSON* lfo_config_to_json(const lfo_config_t* config) {
  cJSON* obj = cJSON_CreateObject();

  cJSON_AddBoolToObject(obj, "enabled", config->enabled);
  cJSON_AddStringToObject(obj, "waveform", lfo_waveform_to_string(config->waveform));
  cJSON_AddStringToObject(obj, "rate_mode", lfo_rate_mode_to_string(config->rate_mode));
  cJSON_AddStringToObject(obj, "start_mode", lfo_start_mode_to_string(config->start_mode));
  cJSON_AddStringToObject(obj, "trigger_timing", lfo_trigger_timing_to_string(config->trigger_timing));
  cJSON_AddBoolToObject(obj, "repeat", config->repeat);
  cJSON_AddBoolToObject(obj, "reset_phase", config->reset_phase);
  cJSON_AddBoolToObject(obj, "restore_on_stop", config->restore_on_stop);
  cJSON_AddNumberToObject(obj, "rate_hz", config->rate_hz_x100 / 100.0);
  cJSON_AddStringToObject(obj, "division", lfo_division_to_string(config->division));
  cJSON_AddNumberToObject(obj, "phase_offset", config->phase_offset);
  cJSON_AddNumberToObject(obj, "duty_cycle", config->duty_cycle);
  cJSON_AddNumberToObject(obj, "floor", config->floor);
  cJSON_AddNumberToObject(obj, "ceiling", config->ceiling);
  cJSON_AddStringToObject(obj, "resolution", lfo_resolution_mode_to_string(config->resolution_mode));
  cJSON_AddNumberToObject(obj, "manual_steps", config->manual_steps);

  return obj;
}

// Deserialize LFO config from JSON
static void json_to_lfo_config(cJSON* obj, lfo_config_t* config) {
  if (!obj || !config) return;
  
  cJSON* enabled = cJSON_GetObjectItem(obj, "enabled");
  if (enabled) config->enabled = cJSON_IsTrue(enabled);
  
  cJSON* waveform = cJSON_GetObjectItem(obj, "waveform");
  if (waveform && cJSON_IsString(waveform)) {
    config->waveform = lfo_waveform_from_string(waveform->valuestring);
  }
  
  cJSON* rate_mode = cJSON_GetObjectItem(obj, "rate_mode");
  if (rate_mode && cJSON_IsString(rate_mode)) {
    config->rate_mode = lfo_rate_mode_from_string(rate_mode->valuestring);
    if (config->rate_mode != LFO_RATE_MODE_FREE &&
        config->rate_mode != LFO_RATE_MODE_TEMPO) {
      config->rate_mode = LFO_RATE_MODE_FREE;
    }
  }

  cJSON* start_mode = cJSON_GetObjectItem(obj, "start_mode");
  if (start_mode && cJSON_IsString(start_mode)) {
    config->start_mode = lfo_start_mode_from_string(start_mode->valuestring);
  }

  cJSON* trigger_timing = cJSON_GetObjectItem(obj, "trigger_timing");
  if (trigger_timing && cJSON_IsString(trigger_timing)) {
    config->trigger_timing = lfo_trigger_timing_from_string(trigger_timing->valuestring);
  }

  cJSON* repeat = cJSON_GetObjectItem(obj, "repeat");
  if (repeat) config->repeat = cJSON_IsTrue(repeat);

  cJSON* reset_phase = cJSON_GetObjectItem(obj, "reset_phase");
  if (reset_phase) config->reset_phase = cJSON_IsTrue(reset_phase);

  cJSON* restore_on_stop = cJSON_GetObjectItem(obj, "restore_on_stop");
  if (restore_on_stop) config->restore_on_stop = cJSON_IsTrue(restore_on_stop);

  cJSON* rate_hz = cJSON_GetObjectItem(obj, "rate_hz");
  if (rate_hz && cJSON_IsNumber(rate_hz)) {
    float hz = (float)rate_hz->valuedouble;
    if (hz < 0.05f) hz = 0.05f;
    if (hz > 20.0f) hz = 20.0f;
    config->rate_hz_x100 = (uint16_t)(hz * 100.0f);
  }
  
  cJSON* division = cJSON_GetObjectItem(obj, "division");
  if (division && cJSON_IsString(division)) {
    config->division = lfo_division_from_string(division->valuestring);
  }
  
  cJSON* phase_offset = cJSON_GetObjectItem(obj, "phase_offset");
  if (phase_offset) config->phase_offset = (uint8_t)phase_offset->valueint;

  cJSON* duty_cycle = cJSON_GetObjectItem(obj, "duty_cycle");
  if (duty_cycle) config->duty_cycle = (uint8_t)duty_cycle->valueint;

  cJSON* floor_val = cJSON_GetObjectItem(obj, "floor");
  if (floor_val) config->floor = (uint8_t)floor_val->valueint;

  cJSON* ceiling_val = cJSON_GetObjectItem(obj, "ceiling");
  if (ceiling_val) config->ceiling = (uint8_t)ceiling_val->valueint;

  cJSON* resolution = cJSON_GetObjectItem(obj, "resolution");
  if (resolution && cJSON_IsString(resolution)) {
    config->resolution_mode = lfo_resolution_mode_from_string(resolution->valuestring);
  }

  cJSON* manual_steps = cJSON_GetObjectItem(obj, "manual_steps");
  if (manual_steps) {
    uint8_t steps = (uint8_t)manual_steps->valueint;
    // Clamp to valid values
    if (steps <= 16) steps = 16;
    else if (steps <= 32) steps = 32;
    else if (steps <= 64) steps = 64;
    else steps = 128;
    config->manual_steps = steps;
  }
}

// Serialize RTG config to JSON
static cJSON* rtg_config_to_json(const rtg_config_t* config) {
  cJSON* obj = cJSON_CreateObject();

  cJSON_AddBoolToObject(obj, "enabled", config->enabled);
  cJSON_AddStringToObject(obj, "mode", rtg_mode_to_string(config->mode));
  cJSON_AddStringToObject(obj, "start_mode", rtg_start_mode_to_string(config->start_mode));
  cJSON_AddStringToObject(obj, "rate_mode", rtg_rate_mode_to_string(config->rate_mode));
  cJSON_AddNumberToObject(obj, "rate_hz", config->rate_hz_x100 / 100.0);
  cJSON_AddStringToObject(obj, "division",
    lfo_division_to_string(config->division));
  cJSON_AddBoolToObject(obj, "glide", config->glide);
  cJSON_AddNumberToObject(obj, "velocity", config->velocity);
  cJSON_AddNumberToObject(obj, "note_min", config->note_min);
  cJSON_AddNumberToObject(obj, "note_max", config->note_max);

  // Only serialize probability if not default (100%)
  if (config->probability > 0 && config->probability < 100) {
    cJSON_AddNumberToObject(obj, "probability", config->probability);
  }
  // Only serialize pattern if enabled (length >= 2)
  if (config->pattern_length >= 2) {
    cJSON_AddNumberToObject(obj, "pattern_length", config->pattern_length);
    cJSON_AddNumberToObject(obj, "pattern_mask", config->pattern_mask);
  }

  // Only serialize Shepard fields when not at defaults so legacy scenes stay terse
  if (config->generator != RTG_GEN_RANDOM) {
    cJSON_AddStringToObject(obj, "generator",
      rtg_generator_to_string(config->generator));
  }
  if (config->shepard_direction != SHEPARD_DIR_RISING) {
    cJSON_AddStringToObject(obj, "shepard_direction",
      shepard_direction_to_string(config->shepard_direction));
  }
  if (config->shepard_layout != SHEPARD_LAYOUT_SINGLE) {
    cJSON_AddStringToObject(obj, "shepard_layout",
      shepard_layout_to_string(config->shepard_layout));
  }
  if (config->shepard_fade != SHEPARD_FADE_NONE) {
    cJSON_AddStringToObject(obj, "shepard_fade",
      shepard_fade_to_string(config->shepard_fade));
  }
  if (config->shepard_style != SHEPARD_STYLE_STREAM) {
    cJSON_AddStringToObject(obj, "shepard_style",
      shepard_style_to_string(config->shepard_style));
  }
  if (config->shepard_style == SHEPARD_STYLE_WIDE &&
      config->shepard_wide_semis != 4) {
    cJSON_AddNumberToObject(obj, "shepard_wide_semis",
      config->shepard_wide_semis);
  }

  return obj;
}

// Deserialize RTG config from JSON
static void json_to_rtg_config(cJSON* obj, rtg_config_t* config) {
  if (!obj || !config) return;

  cJSON* enabled = cJSON_GetObjectItem(obj, "enabled");
  if (enabled) config->enabled = cJSON_IsTrue(enabled);

  cJSON* mode = cJSON_GetObjectItem(obj, "mode");
  if (mode && cJSON_IsString(mode)) {
    config->mode = rtg_mode_from_string(mode->valuestring);
  }

  cJSON* start_mode = cJSON_GetObjectItem(obj, "start_mode");
  if (start_mode && cJSON_IsString(start_mode)) {
    config->start_mode = rtg_start_mode_from_string(start_mode->valuestring);
  }

  cJSON* rate_mode = cJSON_GetObjectItem(obj, "rate_mode");
  if (rate_mode && cJSON_IsString(rate_mode)) {
    config->rate_mode = rtg_rate_mode_from_string(rate_mode->valuestring);
  }

  cJSON* rate_hz = cJSON_GetObjectItem(obj, "rate_hz");
  if (rate_hz && cJSON_IsNumber(rate_hz)) {
    float hz = (float)rate_hz->valuedouble;
    if (hz < 0.5f) hz = 0.5f;
    if (hz > 25.0f) hz = 25.0f;
    config->rate_hz_x100 = (uint16_t)(hz * 100.0f);
  }

  config->division = LFO_DIVISION_QUARTER;
  cJSON* division = cJSON_GetObjectItem(obj, "division");
  if (division && cJSON_IsString(division)) {
    config->division = lfo_division_from_string(division->valuestring);
  } else {
    cJSON* sync_mult = cJSON_GetObjectItem(obj, "sync_mult");
    if (sync_mult && cJSON_IsNumber(sync_mult)) {
      float mult = (float)sync_mult->valuedouble;
      if (mult < 0.125f) mult = 0.125f;
      if (mult > 8.0f) mult = 8.0f;
      config->sync_mult_x1000 = (uint16_t)(mult * 1000.0f);
    }
  }

  cJSON* glide = cJSON_GetObjectItem(obj, "glide");
  if (glide) config->glide = cJSON_IsTrue(glide);

  cJSON* velocity = cJSON_GetObjectItem(obj, "velocity");
  if (velocity) {
    uint8_t vel = (uint8_t)velocity->valueint;
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    config->velocity = vel;
  }

  cJSON* note_min = cJSON_GetObjectItem(obj, "note_min");
  if (note_min) {
    uint8_t n = (uint8_t)note_min->valueint;
    if (n > 127) n = 127;
    config->note_min = n;
  }

  cJSON* note_max = cJSON_GetObjectItem(obj, "note_max");
  if (note_max) {
    uint8_t n = (uint8_t)note_max->valueint;
    if (n > 127) n = 127;
    config->note_max = n;
  }

  // Probability (default 100%)
  config->probability = 100;
  cJSON* prob = cJSON_GetObjectItem(obj, "probability");
  if (prob && cJSON_IsNumber(prob)) {
    int val = prob->valueint;
    if (val >= 10 && val <= 100) config->probability = (uint8_t)val;
  }

  // Pattern (default disabled)
  config->pattern_length = 0;
  config->pattern_mask = 0xFF;
  cJSON* plen = cJSON_GetObjectItem(obj, "pattern_length");
  if (plen && cJSON_IsNumber(plen)) {
    int len = plen->valueint;
    if (len >= 2 && len <= 8) config->pattern_length = (uint8_t)len;
  }
  cJSON* pmask = cJSON_GetObjectItem(obj, "pattern_mask");
  if (pmask && cJSON_IsNumber(pmask)) {
    config->pattern_mask = (uint8_t)(pmask->valueint & 0xFF);
  }

  // Shepard fields - default to legacy random behavior when missing
  config->generator = RTG_GEN_RANDOM;
  config->shepard_direction = SHEPARD_DIR_RISING;
  config->shepard_layout = SHEPARD_LAYOUT_SINGLE;
  config->shepard_fade = SHEPARD_FADE_NONE;
  config->shepard_style = SHEPARD_STYLE_STREAM;
  config->shepard_wide_semis = 4;

  cJSON* gen = cJSON_GetObjectItem(obj, "generator");
  if (gen && cJSON_IsString(gen)) {
    config->generator = rtg_generator_from_string(gen->valuestring);
  }
  cJSON* dir = cJSON_GetObjectItem(obj, "shepard_direction");
  if (dir && cJSON_IsString(dir)) {
    config->shepard_direction = shepard_direction_from_string(dir->valuestring);
  }
  cJSON* layout = cJSON_GetObjectItem(obj, "shepard_layout");
  if (layout && cJSON_IsString(layout)) {
    config->shepard_layout = shepard_layout_from_string(layout->valuestring);
  }
  cJSON* fade = cJSON_GetObjectItem(obj, "shepard_fade");
  if (fade && cJSON_IsString(fade)) {
    config->shepard_fade = shepard_fade_from_string(fade->valuestring);
  }
  cJSON* style = cJSON_GetObjectItem(obj, "shepard_style");
  if (style && cJSON_IsString(style)) {
    config->shepard_style = shepard_style_from_string(style->valuestring);
  }
  cJSON* wide = cJSON_GetObjectItem(obj, "shepard_wide_semis");
  if (wide && cJSON_IsNumber(wide)) {
    int semis = wide->valueint;
    if (semis < 2) semis = 2;
    if (semis > 4) semis = 4;
    config->shepard_wide_semis = (uint8_t)semis;
  }
}

// Serialize Sample+Hold config to JSON
static cJSON* sample_hold_config_to_json(const sample_hold_config_t* config) {
  cJSON* obj = cJSON_CreateObject();

  cJSON_AddBoolToObject(obj, "enabled", config->enabled);
  cJSON_AddStringToObject(obj, "mode", sample_hold_mode_to_string(config->mode));
  cJSON_AddStringToObject(obj, "start_mode", sample_hold_start_mode_to_string(config->start_mode));
  cJSON_AddStringToObject(obj, "rate_mode", sample_hold_rate_mode_to_string(config->rate_mode));
  cJSON_AddNumberToObject(obj, "rate_hz", config->rate_hz_x100 / 100.0);
  cJSON_AddStringToObject(obj, "division",
    lfo_division_to_string(config->division));
  cJSON_AddBoolToObject(obj, "glide", config->glide);

  // Only serialize probability if not default (100%)
  if (config->probability > 0 && config->probability < 100) {
    cJSON_AddNumberToObject(obj, "probability", config->probability);
  }
  // Only serialize pattern if enabled (length >= 2)
  if (config->pattern_length >= 2) {
    cJSON_AddNumberToObject(obj, "pattern_length", config->pattern_length);
    cJSON_AddNumberToObject(obj, "pattern_mask", config->pattern_mask);
  }

  return obj;
}

// Deserialize Sample+Hold config from JSON
static void json_to_sample_hold_config(cJSON* obj, sample_hold_config_t* config) {
  if (!obj || !config) return;

  cJSON* enabled = cJSON_GetObjectItem(obj, "enabled");
  if (enabled) config->enabled = cJSON_IsTrue(enabled);

  cJSON* mode = cJSON_GetObjectItem(obj, "mode");
  if (mode && cJSON_IsString(mode)) {
    config->mode = sample_hold_mode_from_string(mode->valuestring);
  }

  cJSON* start_mode = cJSON_GetObjectItem(obj, "start_mode");
  if (start_mode && cJSON_IsString(start_mode)) {
    config->start_mode = sample_hold_start_mode_from_string(start_mode->valuestring);
  }

  cJSON* rate_mode = cJSON_GetObjectItem(obj, "rate_mode");
  if (rate_mode && cJSON_IsString(rate_mode)) {
    config->rate_mode = sample_hold_rate_mode_from_string(rate_mode->valuestring);
  }

  cJSON* rate_hz = cJSON_GetObjectItem(obj, "rate_hz");
  if (rate_hz && cJSON_IsNumber(rate_hz)) {
    float hz = (float)rate_hz->valuedouble;
    if (hz < 0.5f) hz = 0.5f;
    if (hz > 25.0f) hz = 25.0f;
    config->rate_hz_x100 = (uint16_t)(hz * 100.0f);
  }

  config->division = LFO_DIVISION_QUARTER;
  cJSON* division = cJSON_GetObjectItem(obj, "division");
  if (division && cJSON_IsString(division)) {
    config->division = lfo_division_from_string(division->valuestring);
  } else {
    cJSON* sync_mult = cJSON_GetObjectItem(obj, "sync_mult");
    if (sync_mult && cJSON_IsNumber(sync_mult)) {
      float mult = (float)sync_mult->valuedouble;
      if (mult < 0.125f) mult = 0.125f;
      if (mult > 8.0f) mult = 8.0f;
      config->sync_mult_x1000 = (uint16_t)(mult * 1000.0f);
    }
  }

  cJSON* glide = cJSON_GetObjectItem(obj, "glide");
  if (glide) config->glide = cJSON_IsTrue(glide);

  // Probability (default 100%)
  config->probability = 100;
  cJSON* prob = cJSON_GetObjectItem(obj, "probability");
  if (prob && cJSON_IsNumber(prob)) {
    int val = prob->valueint;
    if (val >= 10 && val <= 100) config->probability = (uint8_t)val;
  }

  // Pattern (default disabled)
  config->pattern_length = 0;
  config->pattern_mask = 0xFF;
  cJSON* plen = cJSON_GetObjectItem(obj, "pattern_length");
  if (plen && cJSON_IsNumber(plen)) {
    int len = plen->valueint;
    if (len >= 2 && len <= 8) config->pattern_length = (uint8_t)len;
  }
  cJSON* pmask = cJSON_GetObjectItem(obj, "pattern_mask");
  if (pmask && cJSON_IsNumber(pmask)) {
    config->pattern_mask = (uint8_t)(pmask->valueint & 0xFF);
  }
}

// Scene JSON serialization
static cJSON* scene_to_json(const scene_t* scene) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", scene->name);

  // Only write ui_module if it's set (non-empty and not the default "beat")
  if (scene->ui_module[0] != '\0' && strcmp(scene->ui_module, "beat") != 0) {
    cJSON_AddStringToObject(root, "ui_module", scene->ui_module);
  }

  // Only write device_id if it's set (non-empty)
  if (scene->device_id[0] != '\0') {
    cJSON_AddStringToObject(root, "device_id", scene->device_id);
  }

  // Only write midi_channel if it's non-zero (has override)
  if (scene->midi_channel > 0) {
    cJSON_AddNumberToObject(root, "midi_channel", scene->midi_channel);
  }

  // Only write note_channel if it's non-zero (has override)
  if (scene->note_channel > 0) {
    cJSON_AddNumberToObject(root, "note_channel", scene->note_channel);
  }

  // Only write trs_type if it's non-zero (has override)
  if (scene->trs_type > 0) {
    cJSON_AddNumberToObject(root, "trs_type", scene->trs_type);
  }

  cJSON_AddNumberToObject(root, "program_number", scene->program_number);
  cJSON_AddBoolToObject(root, "send_pc_on_load", scene->send_pc_on_load);
  
  // Serialize touchwheel mode, style, and continuous mapping
  const char* tw_mode_str;
  switch (scene->touchwheel_mode) {
    case TOUCHWHEEL_MODE_PADS: tw_mode_str = "pads"; break;
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE: tw_mode_str = "program_change"; break;
    case TOUCHWHEEL_MODE_SET_TEMPO: tw_mode_str = "set_tempo"; break;
    case TOUCHWHEEL_MODE_PITCH_BEND: tw_mode_str = "pitch_bend"; break;
    case TOUCHWHEEL_MODE_AFTERTOUCH: tw_mode_str = "aftertouch"; break;
    case TOUCHWHEEL_MODE_DOUBLE_CC: tw_mode_str = "double_cc"; break;
    case TOUCHWHEEL_MODE_VELOCITY: tw_mode_str = "velocity"; break;
    case TOUCHWHEEL_MODE_LFO_RATE: tw_mode_str = "lfo_rate"; break;
    case TOUCHWHEEL_MODE_LFO_DEPTH: tw_mode_str = "lfo_depth"; break;
    case TOUCHWHEEL_MODE_RTG_RATE: tw_mode_str = "rtg_rate"; break;
    default: tw_mode_str = "continuous"; break;
  }
  cJSON_AddStringToObject(root, "touchwheel_mode", tw_mode_str);
  if (scene->touchwheel_mode_prev_valid) {
    cJSON_AddStringToObject(root, "touchwheel_mode_prev",
      touchwheel_mode_json_str(scene->touchwheel_mode_prev));
  }
  const char* tw_style_str = (scene->touchwheel_style == TOUCHWHEEL_STYLE_BIPOLAR) ? "bipolar" :
                             (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) ? "endless" : "odometer";
  cJSON_AddStringToObject(root, "touchwheel_style", tw_style_str);
  cJSON_AddItemToObject(root, "touchwheel", continuous_mapping_to_json(&scene->touchwheel));
  
  // Serialize touchwheel LFO target (for LFO rate/depth modes)
  const char* tw_lfo_target_str;
  switch (scene->touchwheel_lfo_target) {
    case LFO_TARGET_LFO1: tw_lfo_target_str = "lfo1"; break;
    case LFO_TARGET_LFO2: tw_lfo_target_str = "lfo2"; break;
    case LFO_TARGET_BOTH: tw_lfo_target_str = "both"; break;
    default: tw_lfo_target_str = "both"; break;
  }
  cJSON_AddStringToObject(root, "touchwheel_lfo_target", tw_lfo_target_str);
  cJSON_AddNumberToObject(root, "touchwheel_initial_value", scene->touchwheel_initial_value);

  cJSON* touchpads = cJSON_CreateArray();
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    cJSON* pad = cJSON_CreateObject();
    cJSON* action_json = action_to_json(&scene->touchpads[i].action);
    if (action_json) {
      cJSON_AddItemToObject(pad, "action", action_json);
    }
    cJSON_AddItemToArray(touchpads, pad);
  }
  cJSON_AddItemToObject(root, "touchpads", touchpads);
  
  // on_load is an array (up to 4 actions)
  cJSON_AddItemToObject(root, "on_load", on_load_to_json(scene));
  
  // on_play is an array (up to 4 actions, fires when transport starts playing)
  cJSON_AddItemToObject(root, "on_play", on_play_to_json(scene));
  
  // Discrete inputs are single actions
  cJSON* btn_l = action_to_json(&scene->button_left);
  if (btn_l) cJSON_AddItemToObject(root, "button_left", btn_l);
  cJSON* btn_r = action_to_json(&scene->button_right);
  if (btn_r) cJSON_AddItemToObject(root, "button_right", btn_r);
  cJSON* btn_b = action_to_json(&scene->button_both);
  if (btn_b) cJSON_AddItemToObject(root, "button_both", btn_b);
  cJSON* bump_json = action_to_json(&scene->bump);
  if (bump_json) cJSON_AddItemToObject(root, "bump", bump_json);
  
  // Serialize continuous mappings
  cJSON_AddItemToObject(root, "expression", continuous_mapping_to_json(&scene->expression));
  cJSON_AddItemToObject(root, "cv", continuous_mapping_to_json(&scene->cv));
  cJSON_AddItemToObject(root, "proximity", continuous_mapping_to_json(&scene->proximity));
  cJSON_AddItemToObject(root, "als", continuous_mapping_to_json(&scene->als));
  cJSON_AddItemToObject(root, "tilt_x", continuous_mapping_to_json(&scene->tilt_x));
  cJSON_AddItemToObject(root, "tilt_y", continuous_mapping_to_json(&scene->tilt_y));
  cJSON_AddItemToObject(root, "note_track", continuous_mapping_to_json(&scene->note_track));

  cJSON* cc_triggers = cc_triggers_to_json(scene);
  if (cc_triggers) cJSON_AddItemToObject(root, "cc_triggers", cc_triggers);
  
  // Serialize expression jack mode and pedal actions
  const char* mode_str = (scene->expression_mode == EXPRESSION_MODE_NONE) ? "none" :
                         (scene->expression_mode == EXPRESSION_MODE_PEDAL) ? "expression" :
                         (scene->expression_mode == EXPRESSION_MODE_SUSTAIN) ? "sustain" :
                         (scene->expression_mode == EXPRESSION_MODE_SOSTENUTO) ? "sostenuto" :
                         (scene->expression_mode == EXPRESSION_MODE_SWITCH) ? "switch" : "gate";
  cJSON_AddStringToObject(root, "expression_mode", mode_str);
  cJSON* sustain_json = action_to_json(&scene->sustain);
  if (sustain_json) cJSON_AddItemToObject(root, "sustain", sustain_json);
  cJSON* sostenuto_json = action_to_json(&scene->sostenuto);
  if (sostenuto_json) cJSON_AddItemToObject(root, "sostenuto", sostenuto_json);
  cJSON* expr_sw_json = action_to_json(&scene->expr_switch);
  if (expr_sw_json) cJSON_AddItemToObject(root, "expr_switch", expr_sw_json);
  
  // Serialize CV input mode
  const char* cv_mode_str = (scene->cv_input_mode == INPUT_MODE_NONE) ? "none" :
                            (scene->cv_input_mode == INPUT_MODE_CV) ? "cv" :
                            (scene->cv_input_mode == INPUT_MODE_CLOCK_SYNC) ? "clock_sync" :
                            (scene->cv_input_mode == INPUT_MODE_AUDIO) ? "audio" :
                            (scene->cv_input_mode == INPUT_MODE_TRIGGER) ? "trigger" : "note";
  cJSON_AddStringToObject(root, "cv_input_mode", cv_mode_str);
  
  // Serialize velocity mode settings
  cJSON_AddStringToObject(root, "cv_velocity_mode",
    velocity_mode_json_str(scene->cv_velocity_mode));
  cJSON_AddNumberToObject(root, "cv_velocity", scene->cv_velocity);

  // CV Trigger mode configuration
  cJSON_AddNumberToObject(root, "cv_trigger_threshold", scene->cv_trigger_threshold);
  cJSON_AddNumberToObject(root, "cv_trigger_debounce_ms", scene->cv_trigger_debounce_ms);
  cJSON* cv_trigger_action_json = action_to_json(&scene->cv_trigger_action);
  if (cv_trigger_action_json) cJSON_AddItemToObject(root, "cv_trigger_action", cv_trigger_action_json);
  
  // Audio envelope follower configuration
  cJSON* audio_config = cJSON_CreateObject();
  const char* audio_range_str = (scene->audio_config.range == CV_RANGE_BIPOLAR_10V) ? "bi10v" : "bi5v";
  cJSON_AddStringToObject(audio_config, "range", audio_range_str);
  cJSON_AddNumberToObject(audio_config, "sensitivity", scene->audio_config.sensitivity);
  cJSON_AddNumberToObject(audio_config, "attack_ms", scene->audio_config.attack_ms);
  cJSON_AddNumberToObject(audio_config, "release_ms", scene->audio_config.release_ms);
  cJSON_AddNumberToObject(audio_config, "threshold", scene->audio_config.threshold);
  const char* audio_pol_str = (scene->audio_config.polarity == AUDIO_POLARITY_REPEL) ? "repel" : "attract";
  cJSON_AddStringToObject(audio_config, "polarity", audio_pol_str);
  cJSON_AddItemToObject(root, "audio_config", audio_config);
  
  // Other continuous input velocity modes (note-output only: fixed/gate/touchwheel)
  cJSON_AddStringToObject(root, "expression_velocity_mode",
    velocity_mode_json_str_notes_only(scene->expression_velocity_mode));
  cJSON_AddStringToObject(root, "proximity_velocity_mode",
    velocity_mode_json_str_notes_only(scene->proximity_velocity_mode));
  cJSON_AddStringToObject(root, "als_velocity_mode",
    velocity_mode_json_str_notes_only(scene->als_velocity_mode));
  cJSON_AddStringToObject(root, "tilt_x_velocity_mode",
    velocity_mode_json_str_notes_only(scene->tilt_x_velocity_mode));
  cJSON_AddStringToObject(root, "tilt_y_velocity_mode",
    velocity_mode_json_str_notes_only(scene->tilt_y_velocity_mode));
  cJSON_AddNumberToObject(root, "tilt_x_tempo_nudge_pct", scene->tilt_x_tempo_nudge_pct);
  cJSON_AddNumberToObject(root, "tilt_y_tempo_nudge_pct", scene->tilt_y_tempo_nudge_pct);
  cJSON_AddNumberToObject(root, "expression_tempo_nudge_pct", scene->expression_tempo_nudge_pct);
  cJSON_AddNumberToObject(root, "cv_tempo_nudge_pct", scene->cv_tempo_nudge_pct);
  cJSON_AddNumberToObject(root, "proximity_tempo_nudge_pct", scene->proximity_tempo_nudge_pct);
  cJSON_AddNumberToObject(root, "touchwheel_tempo_nudge_pct", scene->touchwheel_tempo_nudge_pct);
  if (scene->touchwheel_tempo_nudge_return != TOUCHWHEEL_NUDGE_RETURN_INSTANT) {
    cJSON_AddNumberToObject(root, "touchwheel_tempo_nudge_return",
      scene->touchwheel_tempo_nudge_return);
  }
  if (scene->touchwheel_aftertouch_return != TOUCHWHEEL_NUDGE_RETURN_FAST) {
    cJSON_AddNumberToObject(root, "touchwheel_aftertouch_return",
      scene->touchwheel_aftertouch_return);
  }
  if (scene->touchwheel_tempo_floor != 20) {
    cJSON_AddNumberToObject(root, "touchwheel_tempo_floor", scene->touchwheel_tempo_floor);
  }
  if (scene->touchwheel_tempo_ceiling != 300) {
    cJSON_AddNumberToObject(root, "touchwheel_tempo_ceiling", scene->touchwheel_tempo_ceiling);
  }
  cJSON_AddNumberToObject(root, "als_tempo_nudge_pct", scene->als_tempo_nudge_pct);
  cJSON_AddNumberToObject(root, "lfo1_tempo_nudge_pct", scene->lfo1_tempo_nudge_pct);
  cJSON_AddNumberToObject(root, "lfo2_tempo_nudge_pct", scene->lfo2_tempo_nudge_pct);

  if (scene->tilt_x_tempo_nudge_direction != TEMPO_NUDGE_DIR_BOTH) {
    cJSON_AddNumberToObject(root, "tilt_x_tempo_nudge_direction",
      scene->tilt_x_tempo_nudge_direction);
  }
  if (scene->tilt_y_tempo_nudge_direction != TEMPO_NUDGE_DIR_BOTH) {
    cJSON_AddNumberToObject(root, "tilt_y_tempo_nudge_direction",
      scene->tilt_y_tempo_nudge_direction);
  }
  if (scene->expression_tempo_nudge_direction != TEMPO_NUDGE_DIR_BOTH) {
    cJSON_AddNumberToObject(root, "expression_tempo_nudge_direction",
      scene->expression_tempo_nudge_direction);
  }
  if (scene->cv_tempo_nudge_direction != TEMPO_NUDGE_DIR_BOTH) {
    cJSON_AddNumberToObject(root, "cv_tempo_nudge_direction", scene->cv_tempo_nudge_direction);
  }
  if (scene->proximity_tempo_nudge_direction != TEMPO_NUDGE_DIR_BOTH) {
    cJSON_AddNumberToObject(root, "proximity_tempo_nudge_direction",
      scene->proximity_tempo_nudge_direction);
  }
  if (scene->touchwheel_tempo_nudge_direction != TEMPO_NUDGE_DIR_BOTH) {
    cJSON_AddNumberToObject(root, "touchwheel_tempo_nudge_direction",
      scene->touchwheel_tempo_nudge_direction);
  }
  if (scene->als_tempo_nudge_direction != TEMPO_NUDGE_DIR_BOTH) {
    cJSON_AddNumberToObject(root, "als_tempo_nudge_direction", scene->als_tempo_nudge_direction);
  }

  // Serialize tempo settings
  cJSON_AddNumberToObject(root, "bpm", scene->bpm);
  
  const char* clock_src_str = (scene->clock_source == CLOCK_SOURCE_INTERNAL) ? "internal" :
                              (scene->clock_source == CLOCK_SOURCE_MIDI) ? "midi" : "sync";
  cJSON_AddStringToObject(root, "clock_source", clock_src_str);
  
  const char* beat_div_str = (scene->beat_divider == DIVIDER_QUARTER) ? "quarter" :
                             (scene->beat_divider == DIVIDER_EIGHTH) ? "eighth" : "sixteenth";
  cJSON_AddStringToObject(root, "beat_divider", beat_div_str);
  
  cJSON* time_sig = cJSON_CreateObject();
  cJSON_AddNumberToObject(time_sig, "numerator", scene->time_signature.numerator);
  cJSON_AddNumberToObject(time_sig, "denominator", scene->time_signature.denominator);
  cJSON_AddItemToObject(root, "time_signature", time_sig);
  
  cJSON_AddBoolToObject(root, "use_transport", scene->use_transport);
  cJSON_AddBoolToObject(root, "send_clock", scene->send_clock);
  
  // Serialize LFO configurations and mappings
  cJSON_AddItemToObject(root, "lfo1_config", lfo_config_to_json(&scene->lfo1_config));
  cJSON_AddItemToObject(root, "lfo2_config", lfo_config_to_json(&scene->lfo2_config));
  cJSON_AddItemToObject(root, "lfo1", continuous_mapping_to_json(&scene->lfo1));
  cJSON_AddItemToObject(root, "lfo2", continuous_mapping_to_json(&scene->lfo2));
  
  // Serialize LFO velocity modes
  const char* lfo1_vel_str = (scene->lfo1_velocity_mode == VELOCITY_MODE_FIXED) ? "fixed" :
                             (scene->lfo1_velocity_mode == VELOCITY_MODE_GATE_VOLTAGE) ? "gate_voltage" : "touchwheel";
  cJSON_AddStringToObject(root, "lfo1_velocity_mode", lfo1_vel_str);
  
  const char* lfo2_vel_str = (scene->lfo2_velocity_mode == VELOCITY_MODE_FIXED) ? "fixed" :
                             (scene->lfo2_velocity_mode == VELOCITY_MODE_GATE_VOLTAGE) ? "gate_voltage" : "touchwheel";
  cJSON_AddStringToObject(root, "lfo2_velocity_mode", lfo2_vel_str);

  // Serialize RTG configuration
  cJSON_AddItemToObject(root, "rtg_config", rtg_config_to_json(&scene->rtg_config));

  // Serialize Sample+Hold configuration
  cJSON_AddItemToObject(root, "sample_hold_config", sample_hold_config_to_json(&scene->sample_hold_config));
  cJSON_AddItemToObject(root, "sample_hold", continuous_mapping_to_json(&scene->sample_hold));
  
  return root;
}

static esp_err_t json_to_scene(cJSON* root, scene_t* scene) {
  if (!root || !scene) return ESP_ERR_INVALID_ARG;

  cJSON* name = cJSON_GetObjectItem(root, "name");
  if (name && cJSON_IsString(name)) {
    strncpy(scene->name, name->valuestring, sizeof(scene->name) - 1);
    scene->name[sizeof(scene->name) - 1] = '\0';
  }

  // Parse ui_module (optional - empty/missing means "beat")
  cJSON* ui_module = cJSON_GetObjectItem(root, "ui_module");
  if (ui_module && cJSON_IsString(ui_module)) {
    strncpy(scene->ui_module, ui_module->valuestring,
      sizeof(scene->ui_module) - 1);
    scene->ui_module[sizeof(scene->ui_module) - 1] = '\0';
  } else {
    scene->ui_module[0] = '\0';  // Default: use "beat"
  }

  // Parse device_id (optional - empty means use global device_config)
  cJSON* device_id = cJSON_GetObjectItem(root, "device_id");
  if (device_id && cJSON_IsString(device_id)) {
    strncpy(scene->device_id, device_id->valuestring, sizeof(scene->device_id) - 1);
    scene->device_id[sizeof(scene->device_id) - 1] = '\0';
  } else {
    scene->device_id[0] = '\0';  // Use global
  }

  // Parse midi_channel (optional - 0 means use global, 1-16 = override)
  cJSON* midi_channel = cJSON_GetObjectItem(root, "midi_channel");
  if (midi_channel && cJSON_IsNumber(midi_channel)) {
    int ch = midi_channel->valueint;
    scene->midi_channel = (ch >= 0 && ch <= 16) ? (uint8_t)ch : 0;
  } else {
    scene->midi_channel = 0;  // Use global
  }

  // Parse note_channel (optional - 0 means use scene channel, 1-16 = override)
  cJSON* note_channel = cJSON_GetObjectItem(root, "note_channel");
  if (note_channel && cJSON_IsNumber(note_channel)) {
    int ch = note_channel->valueint;
    scene->note_channel = (ch >= 0 && ch <= 16) ? (uint8_t)ch : 0;
  } else {
    scene->note_channel = 0;  // Use scene channel
  }

  // Parse trs_type (optional - 0 means use global, 1-4 = A/B/TS/Both)
  cJSON* trs_type = cJSON_GetObjectItem(root, "trs_type");
  if (trs_type && cJSON_IsNumber(trs_type)) {
    int trs = trs_type->valueint;
    scene->trs_type = (trs >= 0 && trs <= 4) ? (uint8_t)trs : 0;
  } else {
    scene->trs_type = 0;  // Use global
  }

  cJSON* program = cJSON_GetObjectItem(root, "program_number");
  if (program) scene->program_number = program->valueint;
  
  // Support both old and new key names for backward compatibility
  cJSON* send_pc = cJSON_GetObjectItem(root, "send_pc_on_load");
  if (!send_pc) send_pc = cJSON_GetObjectItem(root, "send_pc_on_change");  // Legacy
  if (send_pc) scene->send_pc_on_load = cJSON_IsTrue(send_pc);
  
  // Deserialize touchwheel mode
  cJSON* tw_mode = cJSON_GetObjectItem(root, "touchwheel_mode");
  if (tw_mode && cJSON_IsString(tw_mode)) {
    const char* mode_str = tw_mode->valuestring;
    if (strcmp(mode_str, "pads") == 0 || strcmp(mode_str, "buttons") == 0) {
      scene->touchwheel_mode = TOUCHWHEEL_MODE_PADS;  // "buttons" for backwards compatibility
    }
    else if (strcmp(mode_str, "program_change") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_PROGRAM_CHANGE;
    else if (strcmp(mode_str, "continuous") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_CONTINUOUS;
    else if (strcmp(mode_str, "set_tempo") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_SET_TEMPO;
    else if (strcmp(mode_str, "pitch_bend") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_PITCH_BEND;
    else if (strcmp(mode_str, "aftertouch") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_AFTERTOUCH;
    else if (strcmp(mode_str, "double_cc") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_DOUBLE_CC;
    else if (strcmp(mode_str, "velocity") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_VELOCITY;
    else if (strcmp(mode_str, "lfo_rate") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_LFO_RATE;
    else if (strcmp(mode_str, "lfo_depth") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_LFO_DEPTH;
    else if (strcmp(mode_str, "rtg_rate") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_RTG_RATE;
    // Legacy: nrpn/rpn modes removed, map to continuous for backwards compatibility
    else if (strcmp(mode_str, "nrpn") == 0 || strcmp(mode_str, "rpn") == 0) {
      scene->touchwheel_mode = TOUCHWHEEL_MODE_CONTINUOUS;
    }
    else scene->touchwheel_mode = TOUCHWHEEL_MODE_PADS;
  }

  cJSON* tw_mode_prev = cJSON_GetObjectItem(root, "touchwheel_mode_prev");
  if (tw_mode_prev && cJSON_IsString(tw_mode_prev)) {
    scene->touchwheel_mode_prev = touchwheel_mode_from_json_str(tw_mode_prev->valuestring);
    scene->touchwheel_mode_prev_valid = true;
  } else {
    scene->touchwheel_mode_prev_valid = false;
  }
  
  // Deserialize touchwheel style (odometer, endless, or bipolar)
  cJSON* tw_style = cJSON_GetObjectItem(root, "touchwheel_style");
  if (tw_style && cJSON_IsString(tw_style)) {
    const char* style_str = tw_style->valuestring;
    if (strcmp(style_str, "endless") == 0) scene->touchwheel_style = TOUCHWHEEL_STYLE_ENDLESS;
    else if (strcmp(style_str, "bipolar") == 0) scene->touchwheel_style = TOUCHWHEEL_STYLE_BIPOLAR;
    else scene->touchwheel_style = TOUCHWHEEL_STYLE_ODOMETER;
  }
  
  // Deserialize touchwheel continuous mapping
  cJSON* touchwheel = cJSON_GetObjectItem(root, "touchwheel");
  if (touchwheel) json_to_continuous_mapping(touchwheel, &scene->touchwheel);
  
  // Deserialize touchwheel LFO target
  cJSON* tw_lfo_target = cJSON_GetObjectItem(root, "touchwheel_lfo_target");
  if (tw_lfo_target && cJSON_IsString(tw_lfo_target)) {
    const char* target_str = tw_lfo_target->valuestring;
    if (strcmp(target_str, "lfo1") == 0) scene->touchwheel_lfo_target = LFO_TARGET_LFO1;
    else if (strcmp(target_str, "lfo2") == 0) scene->touchwheel_lfo_target = LFO_TARGET_LFO2;
    else scene->touchwheel_lfo_target = LFO_TARGET_BOTH;
  }

  // Deserialize touchwheel initial value (for CC endless mode)
  cJSON* tw_initial = cJSON_GetObjectItem(root, "touchwheel_initial_value");
  if (tw_initial && cJSON_IsNumber(tw_initial)) {
    int val = tw_initial->valueint;
    scene->touchwheel_initial_value = (val >= 0 && val <= 127) ? (uint8_t)val : 0;
  }
  
  cJSON* touchpads = cJSON_GetObjectItem(root, "touchpads");
  if (touchpads && cJSON_IsArray(touchpads)) {
    int count = cJSON_GetArraySize(touchpads);
    for (int i = 0; i < count && i < NUM_TOUCHPADS; i++) {
      cJSON* pad = cJSON_GetArrayItem(touchpads, i);

      // Determine trigger type based on pad index
      action_trigger_type_t trigger = (i <= 7) ?
        ACTION_TRIGGER_TOUCHPAD_0_7 : ACTION_TRIGGER_TOUCHPAD_8_11;
      
      // Try new format first (single "action"), fall back to old format ("actions" array)
      cJSON* action = cJSON_GetObjectItem(pad, "action");
      action_t parsed_action = {0};
      if (action) {
        parsed_action = json_to_action(action);
      } else {
        cJSON* actions = cJSON_GetObjectItem(pad, "actions");
        if (actions) parsed_action = json_array_to_single_action(actions);
      }
      
      // Validate action is allowed for this pad's trigger type
      if (parsed_action.type != ACTION_NONE &&
          !action_is_valid_for_trigger_for(&parsed_action, trigger)) {
        ESP_LOGW(TAG, "Ignoring invalid action '%s' on pad %d",
          action_type_to_string(parsed_action.type), i);
        parsed_action.type = ACTION_NONE;
      }
      scene->touchpads[i].action = parsed_action;
    }
  }
  
  // on_load is an array of up to 4 actions
  cJSON* on_load = cJSON_GetObjectItem(root, "on_load");
  if (on_load) json_to_on_load(on_load, scene);
  
  // on_play is an array of up to 4 actions
  cJSON* on_play = cJSON_GetObjectItem(root, "on_play");
  if (on_play) json_to_on_play(on_play, scene);
  
  // Discrete inputs: try object first (new format), fall back to array (old format)
  cJSON* btn_l = cJSON_GetObjectItem(root, "button_left");
  if (btn_l) {
    action_t action = cJSON_IsArray(btn_l) ?
      json_array_to_single_action(btn_l) : json_to_action(btn_l);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for button_left",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->button_left = action;
  }
  
  cJSON* btn_r = cJSON_GetObjectItem(root, "button_right");
  if (btn_r) {
    action_t action = cJSON_IsArray(btn_r) ?
      json_array_to_single_action(btn_r) : json_to_action(btn_r);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for button_right",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->button_right = action;
  }
  
  cJSON* btn_both = cJSON_GetObjectItem(root, "button_both");
  if (btn_both) {
    action_t action = cJSON_IsArray(btn_both) ?
      json_array_to_single_action(btn_both) : json_to_action(btn_both);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for button_both",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->button_both = action;
  }
  
  cJSON* bump = cJSON_GetObjectItem(root, "bump");
  if (bump) {
    action_t bump_action = cJSON_IsArray(bump) ?
      json_array_to_single_action(bump) : json_to_action(bump);
    if (bump_action.type != ACTION_NONE &&
        !action_is_valid_for_trigger_for(&bump_action, ACTION_TRIGGER_BUMP)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for bump",
        action_type_to_string(bump_action.type));
      bump_action.type = ACTION_NONE;
    }
    scene->bump = bump_action;
  }
  
  // Deserialize continuous mappings
  cJSON* expression = cJSON_GetObjectItem(root, "expression");
  if (expression) json_to_continuous_mapping(expression, &scene->expression);
  
  cJSON* cv = cJSON_GetObjectItem(root, "cv");
  if (cv) json_to_continuous_mapping(cv, &scene->cv);
  
  cJSON* proximity = cJSON_GetObjectItem(root, "proximity");
  if (proximity) json_to_continuous_mapping(proximity, &scene->proximity);
  
  cJSON* als = cJSON_GetObjectItem(root, "als");
  if (als) json_to_continuous_mapping(als, &scene->als);

  cJSON* tilt_x = cJSON_GetObjectItem(root, "tilt_x");
  if (tilt_x) json_to_continuous_mapping(tilt_x, &scene->tilt_x);

  cJSON* tilt_y = cJSON_GetObjectItem(root, "tilt_y");
  if (tilt_y) json_to_continuous_mapping(tilt_y, &scene->tilt_y);

  cJSON* note_track = cJSON_GetObjectItem(root, "note_track");
  if (note_track) json_to_continuous_mapping(note_track, &scene->note_track);

  cJSON* cc_triggers = cJSON_GetObjectItem(root, "cc_triggers");
  if (cc_triggers) json_to_cc_triggers(cc_triggers, scene);

  cJSON* tnpx = cJSON_GetObjectItem(root, "tilt_x_tempo_nudge_pct");
  if (tnpx && cJSON_IsNumber(tnpx)) scene->tilt_x_tempo_nudge_pct = (uint8_t)tnpx->valueint;

  cJSON* tnpy = cJSON_GetObjectItem(root, "tilt_y_tempo_nudge_pct");
  if (tnpy && cJSON_IsNumber(tnpy)) scene->tilt_y_tempo_nudge_pct = (uint8_t)tnpy->valueint;

  cJSON* tnpe = cJSON_GetObjectItem(root, "expression_tempo_nudge_pct");
  if (tnpe && cJSON_IsNumber(tnpe)) scene->expression_tempo_nudge_pct = (uint8_t)tnpe->valueint;

  cJSON* tnpcv = cJSON_GetObjectItem(root, "cv_tempo_nudge_pct");
  if (tnpcv && cJSON_IsNumber(tnpcv)) scene->cv_tempo_nudge_pct = (uint8_t)tnpcv->valueint;

  cJSON* tnpp = cJSON_GetObjectItem(root, "proximity_tempo_nudge_pct");
  if (tnpp && cJSON_IsNumber(tnpp)) scene->proximity_tempo_nudge_pct = (uint8_t)tnpp->valueint;

  cJSON* tnptw = cJSON_GetObjectItem(root, "touchwheel_tempo_nudge_pct");
  if (tnptw && cJSON_IsNumber(tnptw)) scene->touchwheel_tempo_nudge_pct = (uint8_t)tnptw->valueint;

  cJSON* tnptw_ret = cJSON_GetObjectItem(root, "touchwheel_tempo_nudge_return");
  if (tnptw_ret && cJSON_IsNumber(tnptw_ret)) {
    int val = tnptw_ret->valueint;
    scene->touchwheel_tempo_nudge_return = (val >= 0 && val <= TOUCHWHEEL_NUDGE_RETURN_SLOW)
      ? (uint8_t)val : TOUCHWHEEL_NUDGE_RETURN_INSTANT;
  }

  cJSON* tw_at_ret = cJSON_GetObjectItem(root, "touchwheel_aftertouch_return");
  if (tw_at_ret && cJSON_IsNumber(tw_at_ret)) {
    int val = tw_at_ret->valueint;
    scene->touchwheel_aftertouch_return = (val >= 0 && val <= TOUCHWHEEL_NUDGE_RETURN_SLOW)
      ? (uint8_t)val : TOUCHWHEEL_NUDGE_RETURN_INSTANT;
  }

  cJSON* tw_tempo_floor = cJSON_GetObjectItem(root, "touchwheel_tempo_floor");
  if (tw_tempo_floor && cJSON_IsNumber(tw_tempo_floor)) {
    int val = tw_tempo_floor->valueint;
    scene->touchwheel_tempo_floor = (val >= 20 && val <= 300) ? (uint16_t)val : 20;
  }
  cJSON* tw_tempo_ceiling = cJSON_GetObjectItem(root, "touchwheel_tempo_ceiling");
  if (tw_tempo_ceiling && cJSON_IsNumber(tw_tempo_ceiling)) {
    int val = tw_tempo_ceiling->valueint;
    scene->touchwheel_tempo_ceiling = (val >= 20 && val <= 300) ? (uint16_t)val : 300;
  }
  if (scene->touchwheel_tempo_floor < 20 || scene->touchwheel_tempo_floor > 300) {
    scene->touchwheel_tempo_floor = 20;
  }
  if (scene->touchwheel_tempo_ceiling < 20 || scene->touchwheel_tempo_ceiling > 300) {
    scene->touchwheel_tempo_ceiling = 300;
  }
  if (scene->touchwheel_tempo_floor > scene->touchwheel_tempo_ceiling) {
    scene->touchwheel_tempo_ceiling = scene->touchwheel_tempo_floor;
  }

  cJSON* tnpa = cJSON_GetObjectItem(root, "als_tempo_nudge_pct");
  if (tnpa && cJSON_IsNumber(tnpa)) scene->als_tempo_nudge_pct = (uint8_t)tnpa->valueint;

  cJSON* tnpl1 = cJSON_GetObjectItem(root, "lfo1_tempo_nudge_pct");
  if (tnpl1 && cJSON_IsNumber(tnpl1)) scene->lfo1_tempo_nudge_pct = (uint8_t)tnpl1->valueint;

  cJSON* tnpl2 = cJSON_GetObjectItem(root, "lfo2_tempo_nudge_pct");
  if (tnpl2 && cJSON_IsNumber(tnpl2)) scene->lfo2_tempo_nudge_pct = (uint8_t)tnpl2->valueint;

  cJSON* tnd_x = cJSON_GetObjectItem(root, "tilt_x_tempo_nudge_direction");
  if (tnd_x && cJSON_IsNumber(tnd_x)) {
    scene->tilt_x_tempo_nudge_direction = clamp_tempo_nudge_direction((uint8_t)tnd_x->valueint);
  }
  cJSON* tnd_y = cJSON_GetObjectItem(root, "tilt_y_tempo_nudge_direction");
  if (tnd_y && cJSON_IsNumber(tnd_y)) {
    scene->tilt_y_tempo_nudge_direction = clamp_tempo_nudge_direction((uint8_t)tnd_y->valueint);
  }
  cJSON* tnd_e = cJSON_GetObjectItem(root, "expression_tempo_nudge_direction");
  if (tnd_e && cJSON_IsNumber(tnd_e)) {
    scene->expression_tempo_nudge_direction = clamp_tempo_nudge_direction((uint8_t)tnd_e->valueint);
  }
  cJSON* tnd_cv = cJSON_GetObjectItem(root, "cv_tempo_nudge_direction");
  if (tnd_cv && cJSON_IsNumber(tnd_cv)) {
    scene->cv_tempo_nudge_direction = clamp_tempo_nudge_direction((uint8_t)tnd_cv->valueint);
  }
  cJSON* tnd_p = cJSON_GetObjectItem(root, "proximity_tempo_nudge_direction");
  if (tnd_p && cJSON_IsNumber(tnd_p)) {
    scene->proximity_tempo_nudge_direction = clamp_tempo_nudge_direction((uint8_t)tnd_p->valueint);
  }
  cJSON* tnd_tw = cJSON_GetObjectItem(root, "touchwheel_tempo_nudge_direction");
  if (tnd_tw && cJSON_IsNumber(tnd_tw)) {
    scene->touchwheel_tempo_nudge_direction = clamp_tempo_nudge_direction((uint8_t)tnd_tw->valueint);
  }
  cJSON* tnd_a = cJSON_GetObjectItem(root, "als_tempo_nudge_direction");
  if (tnd_a && cJSON_IsNumber(tnd_a)) {
    scene->als_tempo_nudge_direction = clamp_tempo_nudge_direction((uint8_t)tnd_a->valueint);
  }

  // Deserialize expression jack mode
  cJSON* expr_mode = cJSON_GetObjectItem(root, "expression_mode");
  if (expr_mode && cJSON_IsString(expr_mode)) {
    const char* mode_str = expr_mode->valuestring;
    if (strcmp(mode_str, "none") == 0) scene->expression_mode = EXPRESSION_MODE_NONE;
    else if (strcmp(mode_str, "sustain") == 0) scene->expression_mode = EXPRESSION_MODE_SUSTAIN;
    else if (strcmp(mode_str, "sostenuto") == 0) scene->expression_mode = EXPRESSION_MODE_SOSTENUTO;
    else if (strcmp(mode_str, "gate") == 0) scene->expression_mode = EXPRESSION_MODE_GATE;
    else if (strcmp(mode_str, "switch") == 0) scene->expression_mode = EXPRESSION_MODE_SWITCH;
    else scene->expression_mode = EXPRESSION_MODE_PEDAL;
  }
  
  // Note: expression.enabled is loaded from JSON by json_to_continuous_mapping()
  // and should NOT be overwritten here. The auto-management of enabled only
  // happens when the mode is actively changed via scene_set_expression_mode().
  
  // Deserialize pedal actions (try object first, fall back to array for backward compat)
  // Sustain/sostenuto are essentially button-like triggers (press/release)
  cJSON* sustain = cJSON_GetObjectItem(root, "sustain");
  if (sustain) {
    action_t action = cJSON_IsArray(sustain) ?
      json_array_to_single_action(sustain) : json_to_action(sustain);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for sustain",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->sustain = action;
  }
  
  cJSON* sostenuto = cJSON_GetObjectItem(root, "sostenuto");
  if (sostenuto) {
    action_t action = cJSON_IsArray(sostenuto) ?
      json_array_to_single_action(sostenuto) : json_to_action(sostenuto);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for sostenuto",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->sostenuto = action;
  }
  
  cJSON* expr_switch_json = cJSON_GetObjectItem(root, "expr_switch");
  if (expr_switch_json) {
    action_t action = cJSON_IsArray(expr_switch_json) ?
      json_array_to_single_action(expr_switch_json) : json_to_action(expr_switch_json);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_EXPR_SWITCH)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for expr_switch",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->expr_switch = action;
  }
  
  // Deserialize CV input mode
  cJSON* cv_mode = cJSON_GetObjectItem(root, "cv_input_mode");
  if (cv_mode && cJSON_IsString(cv_mode)) {
    const char* mode_str = cv_mode->valuestring;
    if (strcmp(mode_str, "none") == 0) scene->cv_input_mode = INPUT_MODE_NONE;
    else if (strcmp(mode_str, "clock_sync") == 0) scene->cv_input_mode = INPUT_MODE_CLOCK_SYNC;
    else if (strcmp(mode_str, "audio") == 0) scene->cv_input_mode = INPUT_MODE_AUDIO;
    else if (strcmp(mode_str, "note") == 0) scene->cv_input_mode = INPUT_MODE_NOTE;
    else if (strcmp(mode_str, "trigger") == 0) scene->cv_input_mode = INPUT_MODE_TRIGGER;
    else scene->cv_input_mode = INPUT_MODE_CV;
  }
  
  // Auto-manage cv.enabled based on cv_input_mode (same logic as scene_set_cv_input_mode)
  // This ensures cv_input_mode is the source of truth, regardless of what the JSON cv.enabled says
  if (scene->cv_input_mode == INPUT_MODE_NONE) {
    scene->cv.enabled = false;
  } else if (scene->cv_input_mode == INPUT_MODE_CV) {
    scene->cv.enabled = true;
  } else if (scene->cv_input_mode == INPUT_MODE_TRIGGER) {
    scene->cv.enabled = false;
    scene->cv_trigger_pressing = false;
  }
  // INPUT_MODE_NOTE: enabled is managed by input_manager at runtime
  // INPUT_MODE_CLOCK_SYNC: CV is used for tempo, not continuous routing
  
  // Deserialize CV velocity mode settings (check both old and new field names)
  cJSON* cv_vel_mode = cJSON_GetObjectItem(root, "cv_velocity_mode");
  if (!cv_vel_mode) cv_vel_mode = cJSON_GetObjectItem(root, "note_velocity_mode");  // Backward compat
  if (cv_vel_mode && cJSON_IsString(cv_vel_mode)) {
    scene->cv_velocity_mode = velocity_mode_from_json_str(cv_vel_mode->valuestring);
  }
  
  cJSON* cv_vel = cJSON_GetObjectItem(root, "cv_velocity");
  if (!cv_vel) cv_vel = cJSON_GetObjectItem(root, "note_fixed_velocity");  // Backward compat
  if (cv_vel && cJSON_IsNumber(cv_vel)) {
    int vel = cv_vel->valueint;
    if (vel >= 1 && vel <= 127) scene->cv_velocity = (uint8_t)vel;
  }

  // Deserialize CV Trigger mode configuration
  cJSON* cv_trigger_thresh = cJSON_GetObjectItem(root, "cv_trigger_threshold");
  if (cv_trigger_thresh && cJSON_IsNumber(cv_trigger_thresh)) {
    int val = cv_trigger_thresh->valueint;
    if (val < 0) val = 0;
    if (val > 100) val = 100;
    scene->cv_trigger_threshold = (uint8_t)val;
  }
  cJSON* cv_trigger_debounce = cJSON_GetObjectItem(root, "cv_trigger_debounce_ms");
  if (cv_trigger_debounce && cJSON_IsNumber(cv_trigger_debounce)) {
    int val = cv_trigger_debounce->valueint;
    if (val < 0) val = 0;
    if (val > 2000) val = 2000;
    scene->cv_trigger_debounce_ms = (uint16_t)val;
  }
  cJSON* cv_trigger_action_obj = cJSON_GetObjectItem(root, "cv_trigger_action");
  if (cv_trigger_action_obj) {
    action_t action = json_to_action(cv_trigger_action_obj);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_CC)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for cv_trigger_action",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->cv_trigger_action = action;
  }
  
  // Deserialize audio envelope follower configuration
  cJSON* audio_cfg = cJSON_GetObjectItem(root, "audio_config");
  if (audio_cfg && cJSON_IsObject(audio_cfg)) {
    cJSON* range_json = cJSON_GetObjectItem(audio_cfg, "range");
    if (range_json && cJSON_IsString(range_json)) {
      if (strcmp(range_json->valuestring, "bi10v") == 0) {
        scene->audio_config.range = CV_RANGE_BIPOLAR_10V;
      } else {
        scene->audio_config.range = CV_RANGE_BIPOLAR_5V;
      }
    }
    cJSON* sens = cJSON_GetObjectItem(audio_cfg, "sensitivity");
    if (sens && cJSON_IsNumber(sens)) {
      scene->audio_config.sensitivity = (uint8_t)sens->valueint;
    }
    cJSON* attack = cJSON_GetObjectItem(audio_cfg, "attack_ms");
    if (attack && cJSON_IsNumber(attack)) {
      int val = attack->valueint;
      if (val < 5) val = 5;
      if (val > 100) val = 100;
      scene->audio_config.attack_ms = (uint16_t)val;
    }
    cJSON* release = cJSON_GetObjectItem(audio_cfg, "release_ms");
    if (release && cJSON_IsNumber(release)) {
      int val = release->valueint;
      if (val < 50) val = 50;
      if (val > 2000) val = 2000;
      scene->audio_config.release_ms = (uint16_t)val;
    }
    cJSON* thresh = cJSON_GetObjectItem(audio_cfg, "threshold");
    if (thresh && cJSON_IsNumber(thresh)) {
      int val = thresh->valueint;
      if (val < 0) val = 0;
      if (val > 127) val = 127;
      scene->audio_config.threshold = (uint8_t)val;
    }
    cJSON* pol = cJSON_GetObjectItem(audio_cfg, "polarity");
    if (pol && cJSON_IsString(pol)) {
      if (strcmp(pol->valuestring, "repel") == 0) {
        scene->audio_config.polarity = AUDIO_POLARITY_REPEL;
      } else {
        scene->audio_config.polarity = AUDIO_POLARITY_ATTRACT;
      }
    }
  }
  
  // Deserialize other velocity modes (note-output modules: fixed/gate/touchwheel only)
  #define PARSE_NOTE_VEL_MODE(json_obj, target) do { \
    if ((json_obj) && cJSON_IsString(json_obj)) { \
      (target) = velocity_mode_from_json_str_notes_only((json_obj)->valuestring); \
    } \
  } while(0)

  PARSE_NOTE_VEL_MODE(cJSON_GetObjectItem(root, "expression_velocity_mode"), scene->expression_velocity_mode);
  PARSE_NOTE_VEL_MODE(cJSON_GetObjectItem(root, "proximity_velocity_mode"), scene->proximity_velocity_mode);
  PARSE_NOTE_VEL_MODE(cJSON_GetObjectItem(root, "als_velocity_mode"), scene->als_velocity_mode);
  PARSE_NOTE_VEL_MODE(cJSON_GetObjectItem(root, "tilt_x_velocity_mode"), scene->tilt_x_velocity_mode);
  PARSE_NOTE_VEL_MODE(cJSON_GetObjectItem(root, "tilt_y_velocity_mode"), scene->tilt_y_velocity_mode);

  #undef PARSE_NOTE_VEL_MODE
  
  // Deserialize tempo settings
  cJSON* bpm_json = cJSON_GetObjectItem(root, "bpm");
  if (bpm_json && cJSON_IsNumber(bpm_json)) {
    int bpm = bpm_json->valueint;
    if (bpm >= 20 && bpm <= 300) scene->bpm = (uint16_t)bpm;
  }
  
  cJSON* clock_src = cJSON_GetObjectItem(root, "clock_source");
  if (clock_src && cJSON_IsString(clock_src)) {
    const char* src_str = clock_src->valuestring;
    if (strcmp(src_str, "midi") == 0) scene->clock_source = CLOCK_SOURCE_MIDI;
    else if (strcmp(src_str, "sync") == 0) scene->clock_source = CLOCK_SOURCE_SYNC;
    else scene->clock_source = CLOCK_SOURCE_INTERNAL;
  }
  
  cJSON* beat_div = cJSON_GetObjectItem(root, "beat_divider");
  if (beat_div && cJSON_IsString(beat_div)) {
    const char* div_str = beat_div->valuestring;
    if (strcmp(div_str, "eighth") == 0) scene->beat_divider = DIVIDER_EIGHTH;
    else if (strcmp(div_str, "sixteenth") == 0) scene->beat_divider = DIVIDER_SIXTEENTH;
    else scene->beat_divider = DIVIDER_QUARTER;
  }
  
  cJSON* time_sig = cJSON_GetObjectItem(root, "time_signature");
  if (time_sig && cJSON_IsObject(time_sig)) {
    cJSON* numerator = cJSON_GetObjectItem(time_sig, "numerator");
    cJSON* denominator = cJSON_GetObjectItem(time_sig, "denominator");
    if (numerator) scene->time_signature.numerator = numerator->valueint;
    if (denominator) scene->time_signature.denominator = denominator->valueint;
  }
  
  cJSON* use_transport = cJSON_GetObjectItem(root, "use_transport");
  if (use_transport && cJSON_IsBool(use_transport)) {
    scene->use_transport = cJSON_IsTrue(use_transport);
  }

  cJSON* send_clock = cJSON_GetObjectItem(root, "send_clock");
  if (send_clock && cJSON_IsBool(send_clock)) {
    scene->send_clock = cJSON_IsTrue(send_clock);
  }

  // Deserialize LFO configurations and mappings
  cJSON* lfo1_config = cJSON_GetObjectItem(root, "lfo1_config");
  if (lfo1_config) json_to_lfo_config(lfo1_config, &scene->lfo1_config);
  
  cJSON* lfo2_config = cJSON_GetObjectItem(root, "lfo2_config");
  if (lfo2_config) json_to_lfo_config(lfo2_config, &scene->lfo2_config);
  
  cJSON* lfo1_mapping = cJSON_GetObjectItem(root, "lfo1");
  if (lfo1_mapping) json_to_continuous_mapping(lfo1_mapping, &scene->lfo1);

  cJSON* lfo2_mapping = cJSON_GetObjectItem(root, "lfo2");
  if (lfo2_mapping) json_to_continuous_mapping(lfo2_mapping, &scene->lfo2);

  // Force LFO curves to LINEAR (curves don't apply to LFO waveforms)
  // Also force min/max to 0/127 - floor/ceiling is now applied in lfo_config at full resolution
  scene->lfo1.curve.type = CURVE_LINEAR;
  scene->lfo1.min_value = 0;
  scene->lfo1.max_value = 127;
  scene->lfo2.curve.type = CURVE_LINEAR;
  scene->lfo2.min_value = 0;
  scene->lfo2.max_value = 127;

  // Deserialize LFO velocity modes
  cJSON* lfo1_vel_mode = cJSON_GetObjectItem(root, "lfo1_velocity_mode");
  if (lfo1_vel_mode && cJSON_IsString(lfo1_vel_mode)) {
    const char* s = lfo1_vel_mode->valuestring;
    if (strcmp(s, "fixed") == 0) scene->lfo1_velocity_mode = VELOCITY_MODE_FIXED;
    else if (strcmp(s, "gate_voltage") == 0) scene->lfo1_velocity_mode = VELOCITY_MODE_GATE_VOLTAGE;
    else if (strcmp(s, "touchwheel") == 0) scene->lfo1_velocity_mode = VELOCITY_MODE_TOUCHWHEEL;
  }
  
  cJSON* lfo2_vel_mode = cJSON_GetObjectItem(root, "lfo2_velocity_mode");
  if (lfo2_vel_mode && cJSON_IsString(lfo2_vel_mode)) {
    const char* s = lfo2_vel_mode->valuestring;
    if (strcmp(s, "fixed") == 0) scene->lfo2_velocity_mode = VELOCITY_MODE_FIXED;
    else if (strcmp(s, "gate_voltage") == 0) scene->lfo2_velocity_mode = VELOCITY_MODE_GATE_VOLTAGE;
    else if (strcmp(s, "touchwheel") == 0) scene->lfo2_velocity_mode = VELOCITY_MODE_TOUCHWHEEL;
  }

  // Deserialize RTG configuration
  cJSON* rtg_cfg = cJSON_GetObjectItem(root, "rtg_config");
  if (rtg_cfg) {
    json_to_rtg_config(rtg_cfg, &scene->rtg_config);
  } else {
    scene->rtg_config = rtg_config_create_default();
  }

  // Deserialize Sample+Hold configuration
  cJSON* sh_cfg = cJSON_GetObjectItem(root, "sample_hold_config");
  if (sh_cfg) {
    json_to_sample_hold_config(sh_cfg, &scene->sample_hold_config);
  } else {
    scene->sample_hold_config = sample_hold_config_create_default();
  }

  cJSON* sh_mapping = cJSON_GetObjectItem(root, "sample_hold");
  if (sh_mapping) {
    json_to_continuous_mapping(sh_mapping, &scene->sample_hold);
  } else {
    scene->sample_hold = continuous_mapping_create(1);  // Default CC1
  }
  // Output mapping has no separate enable UI; keep it in sync with the engine.
  scene->sample_hold.enabled = scene->sample_hold_config.enabled;

  scene_fixup_touchwheel_orphan(scene);

  return ESP_OK;
}

// Scene storage implementation
esp_err_t scene_load_from_flash(uint8_t scene_index) {
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));

  FILE* f = fopen(filepath, "r");
  if (!f) return ESP_ERR_NOT_FOUND;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return ESP_ERR_NO_MEM; }

  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';

  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return ESP_ERR_INVALID_ARG;

  // Snapshot migration hit count so we can tell whether THIS load touched
  // any legacy aliases (action_to_json/json_to_action delegate to the
  // action_migration component for legacy translations).
  uint32_t mig_before = action_migration_hit_count();

  // Load into cache
  int cache_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;
  // Initialize with defaults first to ensure all fields have valid values,
  // even if JSON file is missing newer fields (e.g., velocity modes)
  scene_init_defaults(&g_scene_manager.cache[cache_idx].scene, scene_index);
  esp_err_t ret = json_to_scene(root, &g_scene_manager.cache[cache_idx].scene);
  cJSON_Delete(root);

  if (ret == ESP_OK) {
    g_scene_manager.cache[cache_idx].index = scene_index;
    g_scene_manager.cache[cache_idx].valid = true;

    // Report migration coverage so we can tell when the migration
    // component can safely be deleted (see plans/action_consolidation
    // pilot's "Migration component lifecycle"). The scene is NOT
    // batch-saved here -- any subsequent user edit triggers
    // scene_save_to_flash, which writes the new format unconditionally
    // (action_to_json always uses the consolidated 'tempo'+variant form).
    uint32_t mig_after = action_migration_hit_count();
    if (mig_after > mig_before) {
      ESP_LOGI(TAG, "Scene %d: migrated %lu legacy action alias(es) on load; "
        "next save will persist new format",
        scene_index, (unsigned long)(mig_after - mig_before));
    }
  }

  return ret;
}

// Read persisted touchwheel_mode from JSON without loading entire scene
// Used by menu to show configured (not runtime) value
touchwheel_mode_t scene_get_persisted_touchwheel_mode(uint8_t scene_index) {
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  
  FILE* f = fopen(filepath, "r");
  if (!f) return TOUCHWHEEL_MODE_PADS;  // Default if file not found
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return TOUCHWHEEL_MODE_PADS; }
  
  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';
  
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return TOUCHWHEEL_MODE_PADS;
  
  touchwheel_mode_t mode = TOUCHWHEEL_MODE_PADS;
  cJSON* tw_mode = cJSON_GetObjectItem(root, "touchwheel_mode");
  if (tw_mode && cJSON_IsString(tw_mode)) {
    const char* mode_str = tw_mode->valuestring;
    if (strcmp(mode_str, "pads") == 0 || strcmp(mode_str, "buttons") == 0) {
      mode = TOUCHWHEEL_MODE_PADS;
    } else if (strcmp(mode_str, "program_change") == 0) {
      mode = TOUCHWHEEL_MODE_PROGRAM_CHANGE;
    } else if (strcmp(mode_str, "continuous") == 0) {
      mode = TOUCHWHEEL_MODE_CONTINUOUS;
    } else if (strcmp(mode_str, "set_tempo") == 0) {
      mode = TOUCHWHEEL_MODE_SET_TEMPO;
    } else if (strcmp(mode_str, "pitch_bend") == 0) {
      mode = TOUCHWHEEL_MODE_PITCH_BEND;
    } else if (strcmp(mode_str, "aftertouch") == 0) {
      mode = TOUCHWHEEL_MODE_AFTERTOUCH;
    } else if (strcmp(mode_str, "double_cc") == 0) {
      mode = TOUCHWHEEL_MODE_DOUBLE_CC;
    } else if (strcmp(mode_str, "velocity") == 0) {
      mode = TOUCHWHEEL_MODE_VELOCITY;
    } else if (strcmp(mode_str, "lfo_rate") == 0) {
      mode = TOUCHWHEEL_MODE_LFO_RATE;
    } else if (strcmp(mode_str, "lfo_depth") == 0) {
      mode = TOUCHWHEEL_MODE_LFO_DEPTH;
    } else if (strcmp(mode_str, "rtg_rate") == 0) {
      mode = TOUCHWHEEL_MODE_RTG_RATE;
    }
  }

  cJSON_Delete(root);
  return mode;
}

// Read persisted touchwheel output_type from JSON without loading entire scene
output_type_t scene_get_persisted_touchwheel_output_type(uint8_t scene_index) {
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  
  FILE* f = fopen(filepath, "r");
  if (!f) return OUTPUT_TYPE_CC;  // Default
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return OUTPUT_TYPE_CC; }
  
  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';
  
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return OUTPUT_TYPE_CC;
  
  output_type_t output_type = OUTPUT_TYPE_CC;
  cJSON* tw = cJSON_GetObjectItem(root, "touchwheel");
  if (tw) {
    cJSON* ot = cJSON_GetObjectItem(tw, "output_type");
    if (ot && cJSON_IsString(ot)) {
      const char* type_str = ot->valuestring;
      if (strcmp(type_str, "note") == 0) {
        output_type = OUTPUT_TYPE_NOTE;
      } else if (strcmp(type_str, "lfo_rate") == 0) {
        output_type = OUTPUT_TYPE_LFO_RATE;
      } else if (strcmp(type_str, "lfo_depth") == 0) {
        output_type = OUTPUT_TYPE_LFO_DEPTH;
      } else if (strcmp(type_str, "lfo2_rate") == 0) {
        output_type = OUTPUT_TYPE_LFO2_RATE;
      } else if (strcmp(type_str, "lfo2_depth") == 0) {
        output_type = OUTPUT_TYPE_LFO2_DEPTH;
      } else if (strcmp(type_str, "lfo1_rate") == 0) {
        output_type = OUTPUT_TYPE_LFO1_RATE;
      } else if (strcmp(type_str, "lfo1_depth") == 0) {
        output_type = OUTPUT_TYPE_LFO1_DEPTH;
      } else if (strcmp(type_str, "rtg_rate") == 0) {
        output_type = OUTPUT_TYPE_RTG_RATE;
      } else if (strcmp(type_str, "sh_rate") == 0) {
        output_type = OUTPUT_TYPE_SH_RATE;
      } else if (strcmp(type_str, "pitch_bend") == 0) {
        output_type = OUTPUT_TYPE_PITCH_BEND;
      } else if (strcmp(type_str, "tempo_nudge") == 0) {
        output_type = OUTPUT_TYPE_TEMPO_NUDGE;
      } else {
        output_type = OUTPUT_TYPE_CC;
      }
    }
  }
  
  cJSON_Delete(root);
  return output_type;
}

esp_err_t scene_save_to_flash(uint8_t scene_index) {
  scene_t* scene = NULL;
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid && g_scene_manager.cache[i].index == scene_index) {
      scene = &g_scene_manager.cache[i].scene;
      break;
    }
  }
  if (!scene) return ESP_ERR_NOT_FOUND;
  
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  
  cJSON* root = scene_to_json(scene);
  char* json_str = cJSON_Print(root);
  cJSON_Delete(root);
  if (!json_str) return ESP_ERR_NO_MEM;
  
  FILE* f = fopen(filepath, "w");
  if (!f) { free(json_str); return ESP_ERR_NOT_FOUND; }
  
  fwrite(json_str, 1, strlen(json_str), f);
  fclose(f);
  free(json_str);
  
  ESP_LOGI(TAG, "Saved scene %d to flash", scene_index);
  if (scene_index == g_scene_manager.current_scene_index)
    scene_post_updated_event(scene_index);
  return ESP_OK;
}

bool scene_index_in_manifest(uint8_t scene_index) {
  if (!g_scene_manager.initialized) return false;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index)
      return true;
  }
  return false;
}

static void scene_invalidate_cache_index(uint8_t scene_index) {
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid &&
        g_scene_manager.cache[i].index == scene_index) {
      g_scene_manager.cache[i].valid = false;
    }
  }
}

static esp_err_t scene_load_into_cache_slot(int cache_idx, uint8_t scene_index) {
  if (cache_idx < 0 || cache_idx >= SCENE_CACHE_SIZE)
    return ESP_ERR_INVALID_ARG;

  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));

  FILE *f = fopen(filepath, "r");
  if (!f) return ESP_ERR_NOT_FOUND;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize <= 0 || fsize > 256 * 1024) {
    fclose(f);
    return ESP_ERR_INVALID_SIZE;
  }

  char *json_str = malloc((size_t)fsize + 1);
  if (!json_str) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  size_t nread = fread(json_str, 1, (size_t)fsize, f);
  fclose(f);
  json_str[nread] = '\0';

  cJSON *root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return ESP_ERR_INVALID_ARG;

  bool preserve_send_clock = g_scene_manager.cache[cache_idx].valid &&
    g_scene_manager.cache[cache_idx].index == scene_index;
  bool prev_send_clock = preserve_send_clock
    ? g_scene_manager.cache[cache_idx].scene.send_clock : true;

  scene_init_defaults(&g_scene_manager.cache[cache_idx].scene, scene_index);
  if (preserve_send_clock)
    g_scene_manager.cache[cache_idx].scene.send_clock = prev_send_clock;

  esp_err_t ret = json_to_scene(root, &g_scene_manager.cache[cache_idx].scene);
  cJSON_Delete(root);

  if (ret == ESP_OK) {
    g_scene_manager.cache[cache_idx].index = scene_index;
    g_scene_manager.cache[cache_idx].valid = true;
  }
  return ret;
}

static void scene_reapply_runtime(uint8_t scene_index, scene_t *scene) {
  if (!scene) return;

  midi_local_output_release_all();
  action_clear_pending();
  action_clear_morphs();

  if (s_cached_device) {
    assets_free_device(s_cached_device);
    s_cached_device = NULL;
    s_cached_device_slug[0] = '\0';
  }

  if (scene->cv_input_mode != INPUT_MODE_NOTE &&
      scene->expression_mode == EXPRESSION_MODE_GATE) {
    scene->expression_mode = EXPRESSION_MODE_NONE;
    scene->expression.enabled = false;
  }
  input_manager_cv_trigger_scene_changed();
  input_set_mode(scene->cv_input_mode);
  expression_set_mode(scene->expression_mode);

  tempo_set_bpm(scene->bpm);
  tempo_set_source(scene->clock_source);
  tempo_set_note_divider(scene->beat_divider);
  tempo_set_time_signature(scene->time_signature.numerator,
    scene->time_signature.denominator);
  action_validate_scene_timings(scene);
  midi_out_reset_cut();

  midi_trs_type_t trs = scene_get_effective_trs_type(scene_index);
  midi_transmit_mode_t trs_mode =
    (midi_transmit_mode_t)assets_trs_type_to_transmit_mode(trs);
  midi_set_uart_transmit_mode(trs_mode);

  lfo_apply_config(0, &scene->lfo1_config);
  lfo_apply_config(1, &scene->lfo2_config);
  rtg_apply_config(&scene->rtg_config);
  sample_hold_apply_config(&scene->sample_hold_config);
  scene->sample_hold.enabled = scene->sample_hold_config.enabled;

  if (!ui_is_in_programming_mode()) {
    uint8_t program;
    if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) {
      int ordinal = 0;
      for (int i = 0; i < g_scene_manager.num_scenes; i++) {
        if (g_scene_manager.manifest[i].index == scene_index) break;
        if (g_scene_manager.manifest[i].active) ordinal++;
      }
      program = (uint8_t)(ordinal + device_config_get_min_preset());
    } else {
      program = scene->program_number;
    }

    if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC || scene->send_pc_on_load) {
      device_config_set_program(program);
    }
    if (scene->num_on_load_actions > 0) {
      for (int i = 0; i < scene->num_on_load_actions; i++)
        action_execute(&scene->on_load[i], 127, true);
    }
    lfo_apply_start_modes();
    rtg_apply_start_mode();
    sample_hold_apply_start_mode();
    s_needs_deferred_init = false;
  } else {
    s_needs_deferred_init = true;
  }

  tilt_axis_set_enabled(TILT_AXIS_X, scene->tilt_x.enabled);
  tilt_axis_set_enabled(TILT_AXIS_Y, scene->tilt_y.enabled);

  scene_apply_ui_module_for_performance(scene->ui_module);

  scene_cleanup_touchwheel();
  scene_setup_touchwheel_for_mode(scene);

  scene_post_updated_event(scene_index);
}

esp_err_t scene_reload_index(uint8_t scene_index) {
  if (!g_scene_manager.initialized) return ESP_ERR_INVALID_STATE;
  if (!scene_index_in_manifest(scene_index)) return ESP_ERR_NOT_FOUND;

  if (scene_index != g_scene_manager.current_scene_index) {
    scene_invalidate_cache_index(scene_index);
    return ESP_OK;
  }

  int cache_idx = g_scene_manager.current_cache_idx;
  esp_err_t ret = scene_load_into_cache_slot(cache_idx, scene_index);
  if (ret != ESP_OK) {
    scene_invalidate_cache_index(scene_index);
    ESP_LOGW(TAG, "scene_reload_index: load failed for %u", (unsigned)scene_index);
    return ret;
  }

  scene_reapply_runtime(scene_index, &g_scene_manager.cache[cache_idx].scene);
  ESP_LOGI(TAG, "Reloaded current scene %u from flash", (unsigned)scene_index);
  return ESP_OK;
}

static esp_err_t scene_parse_from_flash(uint8_t scene_index, scene_t *out) {
  if (!out) return ESP_ERR_INVALID_ARG;

  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));

  FILE *f = fopen(filepath, "r");
  if (!f) return ESP_ERR_NOT_FOUND;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize <= 0 || fsize > 256 * 1024) {
    fclose(f);
    return ESP_ERR_INVALID_SIZE;
  }

  char *json_str = malloc((size_t)fsize + 1);
  if (!json_str) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  size_t nread = fread(json_str, 1, (size_t)fsize, f);
  fclose(f);
  json_str[nread] = '\0';

  cJSON *root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return ESP_ERR_INVALID_ARG;

  scene_init_defaults(out, scene_index);
  esp_err_t ret = json_to_scene(root, out);
  cJSON_Delete(root);
  return ret;
}

static const scene_t *scene_find_cached(uint8_t scene_index) {
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid &&
        g_scene_manager.cache[i].index == scene_index) {
      return &g_scene_manager.cache[i].scene;
    }
  }
  return NULL;
}

esp_err_t scene_inspect_at_index(uint8_t scene_index, char *buf, size_t cap,
    bool *truncated_out) {
  if (!buf || cap == 0) return ESP_ERR_INVALID_ARG;
  if (!g_scene_manager.initialized) return ESP_ERR_INVALID_STATE;
  if (!scene_index_in_manifest(scene_index)) return ESP_ERR_NOT_FOUND;

  const scene_t *cached = scene_find_cached(scene_index);
  scene_t *heap_scene = NULL;
  const scene_t *scene = cached;
  esp_err_t ret = ESP_OK;

  if (!scene) {
    heap_scene = heap_caps_malloc(sizeof(scene_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!heap_scene) return ESP_ERR_NO_MEM;
    ret = scene_parse_from_flash(scene_index, heap_scene);
    if (ret != ESP_OK) {
      heap_caps_free(heap_scene);
      return ret;
    }
    scene = heap_scene;
  }

  bool complete = scene_inspect_build(scene, scene_index, buf, cap);
  if (truncated_out) *truncated_out = !complete;

  if (heap_scene) heap_caps_free(heap_scene);
  return ESP_OK;
}

esp_err_t scene_get_json(uint8_t scene_index, char **json_out) {
  if (!json_out) return ESP_ERR_INVALID_ARG;
  *json_out = NULL;
  if (!g_scene_manager.initialized) return ESP_ERR_INVALID_STATE;
  if (!scene_index_in_manifest(scene_index)) return ESP_ERR_NOT_FOUND;

  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid &&
        g_scene_manager.cache[i].index == scene_index) {
      cJSON *root = scene_to_json(&g_scene_manager.cache[i].scene);
      if (!root) return ESP_ERR_NO_MEM;
      *json_out = cJSON_PrintUnformatted(root);
      cJSON_Delete(root);
      return *json_out ? ESP_OK : ESP_ERR_NO_MEM;
    }
  }

  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  FILE *f = fopen(filepath, "r");
  if (!f) return ESP_ERR_NOT_FOUND;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (fsize <= 0 || fsize > 256 * 1024) {
    fclose(f);
    return ESP_ERR_INVALID_SIZE;
  }

  char *buf = heap_caps_malloc((size_t)fsize + 1, MALLOC_CAP_SPIRAM);
  if (!buf) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  size_t nread = fread(buf, 1, (size_t)fsize, f);
  fclose(f);
  buf[nread] = '\0';
  *json_out = buf;
  return ESP_OK;
}

esp_err_t scene_put_json(uint8_t scene_index, const char *json, size_t len) {
  if (!json || len == 0) return ESP_ERR_INVALID_ARG;
  if (!g_scene_manager.initialized) return ESP_ERR_INVALID_STATE;
  if (!scene_index_in_manifest(scene_index)) return ESP_ERR_NOT_FOUND;
  if (len > 256 * 1024) return ESP_ERR_INVALID_SIZE;

  char *parse_buf = malloc(len + 1);
  if (!parse_buf) return ESP_ERR_NO_MEM;
  memcpy(parse_buf, json, len);
  parse_buf[len] = '\0';
  cJSON *root = cJSON_Parse(parse_buf);
  free(parse_buf);
  if (!root) return ESP_ERR_INVALID_ARG;

  scene_t *scratch =
    heap_caps_malloc(sizeof(scene_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!scratch) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  scene_init_defaults(scratch, scene_index);
  esp_err_t ret = json_to_scene(root, scratch);
  cJSON_Delete(root);
  if (ret != ESP_OK) {
    heap_caps_free(scratch);
    return ret;
  }

  scene_trim_name(scratch->name);
  if (scratch->name[0] == '\0') {
    ESP_LOGW(TAG, "SCENE_PUT rejected: scene name is required");
    heap_caps_free(scratch);
    return ESP_ERR_INVALID_ARG;
  }

  char saved_name[sizeof(scratch->name)];
  strncpy(saved_name, scratch->name, sizeof(saved_name) - 1);
  saved_name[sizeof(saved_name) - 1] = '\0';

  cJSON *out = scene_to_json(scratch);
  heap_caps_free(scratch);
  if (!out) return ESP_ERR_NO_MEM;

  char *json_str = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!json_str) return ESP_ERR_NO_MEM;

  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  FILE *f = fopen(filepath, "w");
  if (!f) {
    free(json_str);
    return ESP_ERR_NOT_FOUND;
  }
  fwrite(json_str, 1, strlen(json_str), f);
  fclose(f);
  free(json_str);

  ESP_LOGI(TAG, "SCENE_PUT wrote scene %u (%s)", (unsigned)scene_index, filepath);
  ret = scene_reload_index(scene_index);
  if (ret == ESP_OK) {
    esp_err_t name_err = scene_update_manifest_name(scene_index, saved_name);
    if (name_err != ESP_OK && name_err != ESP_ERR_NOT_FOUND) {
      ESP_LOGW(TAG, "SCENE_PUT manifest name sync failed: %s",
        esp_err_to_name(name_err));
    }
  }
  if (ret == ESP_OK && scene_index != g_scene_manager.current_scene_index)
    scene_post_updated_event(scene_index);
  return ret;
}

static bool scene_json_leaf_is_skipped(const char *fname) {
  if (!fname || fname[0] == '\0') return true;
  if (fname[0] == '.') return true;
  size_t flen = strlen(fname);
  if (flen < 6 || strcmp(fname + flen - 5, ".json") != 0) return true;
  return strcasecmp(fname, "manifest.json") == 0;
}

static bool scene_filename_to_index(const char *fname, uint8_t *out_index) {
  if (strncmp(fname, "scene_", 6) != 0) return false;
  const char *num_start = fname + 6;
  const char *dot = strstr(num_start, ".json");
  if (!dot || dot[5] != '\0') return false;

  char num_buf[8];
  size_t num_len = (size_t)(dot - num_start);
  if (num_len == 0 || num_len >= sizeof(num_buf)) return false;
  memcpy(num_buf, num_start, num_len);
  num_buf[num_len] = '\0';

  int file_num = atoi(num_buf);
  if (file_num < 1 || file_num > MAX_SCENE_INDEX + 1) return false;
  *out_index = (uint8_t)(file_num - 1);
  return true;
}

static int scene_count_json_files_on_disk(void) {
  DIR *dir = opendir(SCENES_BASE_PATH);
  if (!dir) return 0;

  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (!scene_json_leaf_is_skipped(entry->d_name)) count++;
  }
  closedir(dir);
  return count;
}

static esp_err_t scene_read_json_name(const char *filepath, char *name, size_t name_size) {
  if (!filepath || !name || name_size == 0) return ESP_ERR_INVALID_ARG;
  name[0] = '\0';

  FILE *f = fopen(filepath, "r");
  if (!f) return ESP_ERR_NOT_FOUND;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (fsize <= 0 || fsize > 256 * 1024) {
    fclose(f);
    return ESP_ERR_INVALID_SIZE;
  }

  char *json_buf = malloc((size_t)fsize + 1);
  if (!json_buf) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  size_t nread = fread(json_buf, 1, (size_t)fsize, f);
  fclose(f);
  json_buf[nread] = '\0';

  cJSON *root = cJSON_Parse(json_buf);
  free(json_buf);
  if (!root) return ESP_ERR_INVALID_ARG;

  cJSON *name_item = cJSON_GetObjectItem(root, "name");
  if (name_item && cJSON_IsString(name_item) && name_item->valuestring) {
    strncpy(name, name_item->valuestring, name_size - 1);
    name[name_size - 1] = '\0';
    scene_trim_name(name);
  }
  cJSON_Delete(root);
  return ESP_OK;
}

static esp_err_t scene_update_manifest_name(uint8_t scene_index, const char *name) {
  if (!name || name[0] == '\0') return ESP_ERR_INVALID_ARG;
  if (!g_scene_manager.manifest) return ESP_ERR_INVALID_STATE;

  int pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) {
      pos = i;
      break;
    }
  }
  if (pos < 0) return ESP_ERR_NOT_FOUND;
  if (strcmp(g_scene_manager.manifest[pos].name, name) == 0) return ESP_OK;

  strncpy(g_scene_manager.manifest[pos].name, name,
    sizeof(g_scene_manager.manifest[pos].name) - 1);
  g_scene_manager.manifest[pos].name[
    sizeof(g_scene_manager.manifest[pos].name) - 1] = '\0';

  esp_err_t err = scene_save_manifest();
  if (err == ESP_OK) scene_post_list_changed_event(scene_index);
  return err;
}

static void scene_sync_all_manifest_names_from_json(void) {
  if (!g_scene_manager.manifest || g_scene_manager.num_scenes == 0) return;

  bool changed = false;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    char filepath[320];
    snprintf(filepath, sizeof(filepath), "%s/%s", SCENES_BASE_PATH,
      g_scene_manager.manifest[i].filename);

    char json_name[sizeof(g_scene_manager.manifest[i].name)];
    if (scene_read_json_name(filepath, json_name, sizeof(json_name)) != ESP_OK ||
        json_name[0] == '\0') {
      continue;
    }

    if (strcmp(g_scene_manager.manifest[i].name, json_name) == 0) continue;

    strncpy(g_scene_manager.manifest[i].name, json_name,
      sizeof(g_scene_manager.manifest[i].name) - 1);
    g_scene_manager.manifest[i].name[
      sizeof(g_scene_manager.manifest[i].name) - 1] = '\0';
    changed = true;
  }

  if (!changed) return;

  esp_err_t err = scene_save_manifest();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to sync manifest names from JSON: %s",
      esp_err_to_name(err));
  }
}

static esp_err_t scene_write_manifest_entries(const scene_manifest_entry_t *entries,
  int count) {
  if (!entries || count <= 0) return ESP_ERR_INVALID_ARG;

  cJSON *root = cJSON_CreateObject();
  cJSON *scenes = cJSON_CreateArray();
  if (!root || !scenes) {
    cJSON_Delete(root);
    cJSON_Delete(scenes);
    return ESP_ERR_NO_MEM;
  }

  for (int i = 0; i < count; i++) {
    cJSON *entry = cJSON_CreateObject();
    if (!entry) continue;
    cJSON_AddNumberToObject(entry, "index", entries[i].index);
    cJSON_AddStringToObject(entry, "name", entries[i].name);
    cJSON_AddStringToObject(entry, "filename", entries[i].filename);
    cJSON_AddBoolToObject(entry, "active", entries[i].active);
    cJSON_AddItemToArray(scenes, entry);
  }

  cJSON_AddItemToObject(root, "scenes", scenes);
  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json_str) return ESP_ERR_NO_MEM;

  FILE *f = fopen(MANIFEST_PATH, "w");
  if (!f) {
    cJSON_free(json_str);
    return ESP_ERR_NOT_FOUND;
  }
  fputs(json_str, f);
  fclose(f);
  cJSON_free(json_str);
  return ESP_OK;
}

static bool manifest_build_has_filename(const scene_manifest_entry_t *entries, int count,
  const char *fname) {
  for (int i = 0; i < count; i++) {
    if (strcmp(entries[i].filename, fname) == 0) return true;
  }
  return false;
}

static bool manifest_build_has_index(const scene_manifest_entry_t *entries, int count,
  uint8_t index) {
  for (int i = 0; i < count; i++) {
    if (entries[i].index == index) return true;
  }
  return false;
}

static uint8_t manifest_find_free_index(const scene_manifest_entry_t *entries, int count) {
  for (uint8_t i = 0; i <= MAX_SCENE_INDEX; i++) {
    if (!manifest_build_has_index(entries, count, i)) return i;
  }
  return MAX_SCENE_INDEX + 1;
}

esp_err_t scene_rebuild_manifest_from_disk(bool reload_runtime) {
  scene_manifest_entry_t *old_entries = NULL;
  int old_count = 0;

  FILE *mf = fopen(MANIFEST_PATH, "r");
  if (mf) {
    fseek(mf, 0, SEEK_END);
    long mfsize = ftell(mf);
    fseek(mf, 0, SEEK_SET);
    if (mfsize > 0 && mfsize <= 64 * 1024) {
      char *json_str = malloc((size_t)mfsize + 1);
      if (json_str) {
        size_t nread = fread(json_str, 1, (size_t)mfsize, mf);
        fclose(mf);
        mf = NULL;
        json_str[nread] = '\0';

        cJSON *root = cJSON_Parse(json_str);
        free(json_str);
        if (root) {
          cJSON *scenes = cJSON_GetObjectItem(root, "scenes");
          if (scenes && cJSON_IsArray(scenes)) {
            old_count = cJSON_GetArraySize(scenes);
            if (old_count > 0) {
              old_entries = calloc((size_t)old_count, sizeof(scene_manifest_entry_t));
              if (old_entries) {
                for (int i = 0; i < old_count; i++) {
                  cJSON *entry = cJSON_GetArrayItem(scenes, i);
                  cJSON *idx = cJSON_GetObjectItem(entry, "index");
                  cJSON *name = cJSON_GetObjectItem(entry, "name");
                  cJSON *filename = cJSON_GetObjectItem(entry, "filename");
                  cJSON *active = cJSON_GetObjectItem(entry, "active");

                  if (idx && cJSON_IsNumber(idx))
                    old_entries[i].index = (uint8_t)idx->valueint;
                  if (name && cJSON_IsString(name)) {
                    strncpy(old_entries[i].name, name->valuestring,
                      sizeof(old_entries[i].name) - 1);
                  }
                  if (filename && cJSON_IsString(filename)) {
                    strncpy(old_entries[i].filename, filename->valuestring,
                      sizeof(old_entries[i].filename) - 1);
                    old_entries[i].filename[sizeof(old_entries[i].filename) - 1] = '\0';
                  }
                  old_entries[i].active = active ? cJSON_IsTrue(active) : true;
                }
              } else {
                old_count = 0;
              }
            }
          }
          cJSON_Delete(root);
        }
      } else {
        fclose(mf);
        mf = NULL;
      }
    }
    if (mf) fclose(mf);
  }

  int capacity = 16;
  int count = 0;
  scene_manifest_entry_t *built =
    malloc_prefer_psram((size_t)capacity * sizeof(scene_manifest_entry_t));
  if (!built) {
    free(old_entries);
    return ESP_ERR_NO_MEM;
  }

  // Keep existing manifest entries whose files still exist (preserve order).
  for (int i = 0; i < old_count; i++) {
    if (old_entries[i].filename[0] == '\0') continue;

    char filepath[320];
    snprintf(filepath, sizeof(filepath), "%s/%s", SCENES_BASE_PATH,
      old_entries[i].filename);
    struct stat st;
    if (stat(filepath, &st) != 0) continue;

    scene_manifest_entry_t entry = old_entries[i];
    char read_name[sizeof(entry.name)];
    if (scene_read_json_name(filepath, read_name, sizeof(read_name)) == ESP_OK &&
        read_name[0] != '\0') {
      strncpy(entry.name, read_name, sizeof(entry.name) - 1);
      entry.name[sizeof(entry.name) - 1] = '\0';
    } else if (entry.name[0] == '\0') {
      snprintf(entry.name, sizeof(entry.name), "Scene %u",
        (unsigned)entry.index + 1);
    }

    if (count >= capacity) {
      capacity *= 2;
      scene_manifest_entry_t *grown = realloc_prefer_psram(built,
        (size_t)capacity * sizeof(scene_manifest_entry_t));
      if (!grown) {
        free(built);
        free(old_entries);
        return ESP_ERR_NO_MEM;
      }
      built = grown;
    }
    built[count++] = entry;
  }

  DIR *dir = opendir(SCENES_BASE_PATH);
  if (!dir) {
    free(built);
    free(old_entries);
    return ESP_ERR_NOT_FOUND;
  }

  struct dirent *dent;
  while ((dent = readdir(dir)) != NULL) {
    const char *fname = dent->d_name;
    if (scene_json_leaf_is_skipped(fname)) continue;
    if (manifest_build_has_filename(built, count, fname)) continue;

    char filepath[320];
    snprintf(filepath, sizeof(filepath), "%s/%s", SCENES_BASE_PATH, fname);

    scene_manifest_entry_t entry = {0};
    strncpy(entry.filename, fname, sizeof(entry.filename) - 1);
    entry.filename[sizeof(entry.filename) - 1] = '\0';
    entry.active = true;

    uint8_t derived_index = 0;
    if (scene_filename_to_index(fname, &derived_index) &&
        !manifest_build_has_index(built, count, derived_index)) {
      entry.index = derived_index;
    } else {
      uint8_t free_index = manifest_find_free_index(built, count);
      if (free_index > MAX_SCENE_INDEX) continue;
      entry.index = free_index;
    }

    if (scene_read_json_name(filepath, entry.name, sizeof(entry.name)) != ESP_OK ||
        entry.name[0] == '\0') {
      snprintf(entry.name, sizeof(entry.name), "Scene %u", (unsigned)entry.index + 1);
    }

    if (count >= capacity) {
      capacity *= 2;
      scene_manifest_entry_t *grown = realloc_prefer_psram(built,
        (size_t)capacity * sizeof(scene_manifest_entry_t));
      if (!grown) continue;
      built = grown;
    }
    built[count++] = entry;
    ESP_LOGI(TAG, "Recovered orphan scene file '%s' as index %u",
      fname, (unsigned)entry.index);
  }
  closedir(dir);
  free(old_entries);

  if (count == 0) {
    free(built);
    return ESP_ERR_NOT_FOUND;
  }

  esp_err_t ret = scene_write_manifest_entries(built, count);
  if (ret != ESP_OK) {
    free(built);
    return ret;
  }

  ESP_LOGI(TAG, "Rebuilt scenes manifest with %d entr%s", count, count == 1 ? "y" : "ies");

  if (reload_runtime && g_scene_manager.initialized) {
    if (g_scene_manager.manifest) {
      free(g_scene_manager.manifest);
      g_scene_manager.manifest = NULL;
    }
    g_scene_manager.manifest = built;
    g_scene_manager.num_scenes = (uint16_t)count;
    scene_post_list_changed_event(0);
    return ESP_OK;
  }

  free(built);
  return ESP_OK;
}

esp_err_t scene_load_manifest(void) {
  if (g_scene_manager.manifest) {
    free(g_scene_manager.manifest);
    g_scene_manager.manifest = NULL;
    g_scene_manager.num_scenes = 0;
  }

  FILE* f = fopen(MANIFEST_PATH, "r");
  if (!f) return ESP_ERR_NOT_FOUND;
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return ESP_ERR_NO_MEM; }
  
  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';
  
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return ESP_ERR_INVALID_ARG;
  
  cJSON* scenes = cJSON_GetObjectItem(root, "scenes");
  if (!scenes || !cJSON_IsArray(scenes)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
  
  int count = cJSON_GetArraySize(scenes);
  g_scene_manager.manifest = malloc_prefer_psram(count * sizeof(scene_manifest_entry_t));
  if (!g_scene_manager.manifest) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
  
  g_scene_manager.num_scenes = count;
  for (int i = 0; i < count; i++) {
    cJSON* entry = cJSON_GetArrayItem(scenes, i);
    cJSON* idx = cJSON_GetObjectItem(entry, "index");
    cJSON* name = cJSON_GetObjectItem(entry, "name");
    cJSON* filename = cJSON_GetObjectItem(entry, "filename");
    
    if (idx) g_scene_manager.manifest[i].index = idx->valueint;
    if (name && cJSON_IsString(name)) {
      strncpy(g_scene_manager.manifest[i].name, name->valuestring,
        sizeof(g_scene_manager.manifest[i].name) - 1);
      g_scene_manager.manifest[i].name[sizeof(g_scene_manager.manifest[i].name) - 1] = '\0';
    }
    if (filename && cJSON_IsString(filename)) {
      strncpy(g_scene_manager.manifest[i].filename, filename->valuestring, 63);
      g_scene_manager.manifest[i].filename[63] = '\0';
    }
    
    // Default to active if field is missing (backward compatibility)
    cJSON* active = cJSON_GetObjectItem(entry, "active");
    g_scene_manager.manifest[i].active = active ? cJSON_IsTrue(active) : true;
  }
  
  cJSON_Delete(root);
  return ESP_OK;
}

esp_err_t scene_save_manifest(void) {
  cJSON* root = cJSON_CreateObject();
  cJSON* scenes = cJSON_CreateArray();
  
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    cJSON* entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "index", g_scene_manager.manifest[i].index);
    cJSON_AddStringToObject(entry, "name", g_scene_manager.manifest[i].name);
    cJSON_AddStringToObject(entry, "filename", g_scene_manager.manifest[i].filename);
    cJSON_AddBoolToObject(entry, "active", g_scene_manager.manifest[i].active);
    cJSON_AddItemToArray(scenes, entry);
  }
  
  cJSON_AddItemToObject(root, "scenes", scenes);
  char* json_str = cJSON_Print(root);
  cJSON_Delete(root);
  if (!json_str) return ESP_ERR_NO_MEM;
  
  FILE* f = fopen(MANIFEST_PATH, "w");
  if (!f) { free(json_str); return ESP_ERR_NOT_FOUND; }
  
  fwrite(json_str, 1, strlen(json_str), f);
  fclose(f);
  free(json_str);
  
  return ESP_OK;
}

uint16_t scene_get_count(void) {
  uint16_t count = 0;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].active) count++;
  }
  return count;
}

bool scene_get_active_slot(uint8_t scene_index, uint16_t *ordinal_1based,
  uint16_t *active_total) {
  uint16_t total = scene_get_count();
  if (active_total) *active_total = total;

  uint16_t ordinal = 0;
  for (uint16_t i = 0; i < g_scene_manager.num_scenes; i++) {
    if (!g_scene_manager.manifest[i].active) continue;
    ordinal++;
    if (g_scene_manager.manifest[i].index == scene_index) {
      if (ordinal_1based) *ordinal_1based = ordinal;
      return true;
    }
  }

  if (ordinal_1based) *ordinal_1based = 0;
  return false;
}

static void scene_post_updated_event(uint8_t scene_index) {
  event_t event = {
    .type = EVENT_SCENE_UPDATED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = scene_index}
  };
  event_bus_post(&event);
}

static void scene_post_reordered_event(uint8_t scene_index) {
  event_t event = {
    .type = EVENT_SCENE_REORDERED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = scene_index}
  };
  event_bus_post(&event);
}

static void scene_post_list_changed_event(uint8_t scene_index) {
  event_t event = {
    .type = EVENT_SCENE_LIST_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = scene_index}
  };
  event_bus_post(&event);
}

uint16_t scene_get_total_count(void) {
  return g_scene_manager.num_scenes;
}

bool scene_is_active(uint8_t scene_index) {
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index)
      return g_scene_manager.manifest[i].active;
  }
  return false;
}

esp_err_t scene_set_active(uint8_t scene_index, bool active) {
  // Find entry in manifest
  int pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) {
      pos = i;
      break;
    }
  }
  if (pos == -1) return ESP_ERR_NOT_FOUND;
  
  if (!active) {
    // Cannot deactivate the current scene
    if (scene_index == g_scene_manager.current_scene_index) {
      ESP_LOGW(TAG, "Cannot deactivate the current scene");
      return ESP_ERR_INVALID_STATE;
    }
    // Cannot deactivate the last active scene
    if (scene_get_count() <= 1) {
      ESP_LOGW(TAG, "Cannot deactivate the last active scene");
      return ESP_ERR_INVALID_STATE;
    }
  }
  
  g_scene_manager.manifest[pos].active = active;
  ESP_LOGI(TAG, "Scene %d (%s) %s",
    scene_index + 1, g_scene_manager.manifest[pos].name,
    active ? "activated" : "deactivated");
  esp_err_t err = scene_save_manifest();
  if (err == ESP_OK) scene_post_list_changed_event(scene_index);
  return err;
}

const char* scene_get_name_by_position(uint16_t position) {
  if (position >= g_scene_manager.num_scenes || !g_scene_manager.manifest) {
    return NULL;
  }
  return g_scene_manager.manifest[position].name;
}

uint8_t scene_get_index_by_position(uint16_t position) {
  if (position >= g_scene_manager.num_scenes || !g_scene_manager.manifest) {
    return 0;
  }
  return g_scene_manager.manifest[position].index;
}

bool scene_is_active_by_position(uint16_t position) {
  if (position >= g_scene_manager.num_scenes || !g_scene_manager.manifest) {
    return false;
  }
  return g_scene_manager.manifest[position].active;
}

bool scene_is_factory_by_position(uint16_t position) {
  if (position >= g_scene_manager.num_scenes || !g_scene_manager.manifest) {
    return false;
  }
  return factory_source_exists(g_scene_manager.manifest[position].filename);
}

esp_err_t scene_create_new(const char* name) {
  return scene_create_new_at_position(name, g_scene_manager.num_scenes);
}

esp_err_t scene_create_new_at_position(const char* name, uint16_t position) {
  if (!name || name[0] == '\0') return ESP_ERR_INVALID_ARG;
  
  // Check for name uniqueness
  if (scene_name_exists(name, -1)) {
    ESP_LOGW(TAG, "Cannot create scene: name '%s' already exists", name);
    return ESP_ERR_INVALID_ARG;
  }
  if (scene_name_is_reserved(name)) {
    ESP_LOGW(TAG, "Cannot create scene: name '%s' is reserved", name);
    return ESP_ERR_INVALID_ARG;
  }
  
  // Find next available index
  uint8_t new_index = 0;
  for (uint8_t i = 0; i <= MAX_SCENE_INDEX; i++) {
    bool exists = false;
    for (int j = 0; j < g_scene_manager.num_scenes; j++) {
      if (g_scene_manager.manifest[j].index == i) { exists = true; break; }
    }
    if (!exists) { new_index = i; break; }
  }
  
  // Expand manifest (use PSRAM for persistent data)
  scene_manifest_entry_t* new_manifest = realloc_prefer_psram(g_scene_manager.manifest, 
                                                               (g_scene_manager.num_scenes + 1) * sizeof(scene_manifest_entry_t));
  if (!new_manifest) return ESP_ERR_NO_MEM;
  g_scene_manager.manifest = new_manifest;
  
  // Clamp position to valid range
  if (position > g_scene_manager.num_scenes) position = g_scene_manager.num_scenes;
  
  // Shift entries down to make room at position
  for (int i = g_scene_manager.num_scenes; i > (int)position; i--) {
    g_scene_manager.manifest[i] = g_scene_manager.manifest[i - 1];
  }
  
  // Generate slug filename from scene name
  char slug[64];
  scene_name_to_slug(name, slug, sizeof(slug));
  
  // Insert new entry at position
  g_scene_manager.manifest[position].index = new_index;
  strncpy(g_scene_manager.manifest[position].name, name,
    sizeof(g_scene_manager.manifest[position].name) - 1);
  g_scene_manager.manifest[position].name[sizeof(g_scene_manager.manifest[position].name) - 1] = '\0';
  strncpy(g_scene_manager.manifest[position].filename, slug,
    sizeof(g_scene_manager.manifest[position].filename) - 1);
  g_scene_manager.manifest[position].filename[sizeof(g_scene_manager.manifest[position].filename) - 1] = '\0';
  g_scene_manager.manifest[position].active = true;
  g_scene_manager.num_scenes++;
  
  // Create and save default scene - initialize directly in cache to avoid stack allocation
  int temp_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;
  scene_init_defaults(&g_scene_manager.cache[temp_idx].scene, new_index);
  strncpy(g_scene_manager.cache[temp_idx].scene.name, name,
    sizeof(g_scene_manager.cache[temp_idx].scene.name) - 1);
  g_scene_manager.cache[temp_idx].scene.name[sizeof(g_scene_manager.cache[temp_idx].scene.name) - 1] = '\0';
  g_scene_manager.cache[temp_idx].index = new_index;
  g_scene_manager.cache[temp_idx].valid = true;
  
  scene_save_to_flash(new_index);
  scene_save_manifest();

  scene_post_list_changed_event(new_index);

  ESP_LOGI(TAG, "Created scene '%s' (file: %s)", name, slug);
  return ESP_OK;
}

esp_err_t scene_delete(uint8_t scene_index) {
  if (g_scene_manager.num_scenes == 1) return ESP_ERR_INVALID_STATE;
  
  int pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) { pos = i; break; }
  }
  if (pos == -1) return ESP_ERR_NOT_FOUND;

  if (scene_index == g_scene_manager.current_scene_index) {
    ESP_LOGW(TAG, "Cannot delete the current scene");
    return ESP_ERR_INVALID_STATE;
  }

  // If the scene being deleted originated as a factory preset (same basename
  // is shipped under /assets/scenes/factory/), record a tombstone so the
  // merge pass on future assets updates doesn't resurrect it.
  const char *del_fname = g_scene_manager.manifest[pos].filename;
  if (del_fname[0] != '\0' && factory_source_exists(del_fname)) {
    esp_err_t tret = factory_tombstones_add(del_fname);
    if (tret != ESP_OK)
      ESP_LOGW(TAG, "Failed to tombstone factory preset '%s': %s",
        del_fname, esp_err_to_name(tret));
  }

  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  remove(filepath);
  
  for (int i = pos; i < g_scene_manager.num_scenes - 1; i++) {
    g_scene_manager.manifest[i] = g_scene_manager.manifest[i + 1];
  }
  g_scene_manager.num_scenes--;

  scene_save_manifest();
  scene_post_list_changed_event(g_scene_manager.current_scene_index);
  return ESP_OK;
}

esp_err_t scene_duplicate(uint8_t source_index, const char* new_name) {
  if (!new_name || new_name[0] == '\0') return ESP_ERR_INVALID_ARG;
  
  // Check for name uniqueness
  if (scene_name_exists(new_name, -1)) {
    ESP_LOGW(TAG, "Cannot duplicate scene: name '%s' already exists", new_name);
    return ESP_ERR_INVALID_ARG;
  }
  if (scene_name_is_reserved(new_name)) {
    ESP_LOGW(TAG, "Cannot duplicate scene: name '%s' is reserved", new_name);
    return ESP_ERR_INVALID_ARG;
  }
  
  // Find source in manifest
  int source_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == source_index) { source_pos = i; break; }
  }
  if (source_pos == -1) return ESP_ERR_NOT_FOUND;

  // Find next available index
  uint8_t new_index = 0;
  for (uint8_t i = 0; i <= MAX_SCENE_INDEX; i++) {
    bool exists = false;
    for (int j = 0; j < g_scene_manager.num_scenes; j++) {
      if (g_scene_manager.manifest[j].index == i) { exists = true; break; }
    }
    if (!exists) { new_index = i; break; }
  }

  // Expand manifest
  scene_manifest_entry_t* new_manifest = realloc_prefer_psram(g_scene_manager.manifest,
    (g_scene_manager.num_scenes + 1) * sizeof(scene_manifest_entry_t));
  if (!new_manifest) return ESP_ERR_NO_MEM;
  g_scene_manager.manifest = new_manifest;

  // Generate slug filename from scene name
  char slug[64];
  scene_name_to_slug(new_name, slug, sizeof(slug));

  // Insert right after source
  uint16_t insert_pos = source_pos + 1;
  for (int i = g_scene_manager.num_scenes; i > (int)insert_pos; i--) {
    g_scene_manager.manifest[i] = g_scene_manager.manifest[i - 1];
  }

  g_scene_manager.manifest[insert_pos].index = new_index;
  strncpy(g_scene_manager.manifest[insert_pos].name, new_name,
    sizeof(g_scene_manager.manifest[insert_pos].name) - 1);
  g_scene_manager.manifest[insert_pos].name[
    sizeof(g_scene_manager.manifest[insert_pos].name) - 1] = '\0';
  strncpy(g_scene_manager.manifest[insert_pos].filename, slug,
    sizeof(g_scene_manager.manifest[insert_pos].filename) - 1);
  g_scene_manager.manifest[insert_pos].filename[
    sizeof(g_scene_manager.manifest[insert_pos].filename) - 1] = '\0';
  g_scene_manager.manifest[insert_pos].active = true;
  g_scene_manager.num_scenes++;

  // Use a temp cache slot for the new scene
  int temp_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;

  // Try to copy source from cache first
  bool copied = false;
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid &&
      g_scene_manager.cache[i].index == source_index) {
      g_scene_manager.cache[temp_idx].scene = g_scene_manager.cache[i].scene;
      copied = true;
      break;
    }
  }

  // If not cached, load source from flash into temp slot
  if (!copied) {
    char filepath[128];
    get_scene_filename(source_index, filepath, sizeof(filepath));
    FILE* f = fopen(filepath, "r");
    if (!f) {
      // Rollback manifest change
      for (int i = (int)insert_pos; i < g_scene_manager.num_scenes - 1; i++) {
        g_scene_manager.manifest[i] = g_scene_manager.manifest[i + 1];
      }
      g_scene_manager.num_scenes--;
      return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* json_str = malloc(fsize + 1);
    if (!json_str) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(json_str, 1, fsize, f);
    fclose(f);
    json_str[fsize] = '\0';
    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return ESP_ERR_INVALID_ARG;
    scene_init_defaults(&g_scene_manager.cache[temp_idx].scene, new_index);
    json_to_scene(root, &g_scene_manager.cache[temp_idx].scene);
    cJSON_Delete(root);
  }

  // Update the copy with new identity
  strncpy(g_scene_manager.cache[temp_idx].scene.name, new_name,
    sizeof(g_scene_manager.cache[temp_idx].scene.name) - 1);
  g_scene_manager.cache[temp_idx].scene.name[
    sizeof(g_scene_manager.cache[temp_idx].scene.name) - 1] = '\0';
  g_scene_manager.cache[temp_idx].index = new_index;
  g_scene_manager.cache[temp_idx].valid = true;

  scene_save_to_flash(new_index);
  scene_save_manifest();

  scene_post_list_changed_event(new_index);

  return ESP_OK;
}

esp_err_t scene_reorder(uint8_t from_index, uint8_t to_index) {
  int from_pos = -1, to_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == from_index) from_pos = i;
    if (g_scene_manager.manifest[i].index == to_index) to_pos = i;
  }
  if (from_pos == -1 || to_pos == -1) return ESP_ERR_INVALID_ARG;
  
  scene_manifest_entry_t temp = g_scene_manager.manifest[from_pos];
  
  if (from_pos < to_pos) {
    for (int i = from_pos; i < to_pos; i++) g_scene_manager.manifest[i] = g_scene_manager.manifest[i + 1];
  } else {
    for (int i = from_pos; i > to_pos; i--) g_scene_manager.manifest[i] = g_scene_manager.manifest[i - 1];
  }
  
  g_scene_manager.manifest[to_pos] = temp;
  scene_save_manifest();

  scene_post_list_changed_event(g_scene_manager.current_scene_index);
  scene_post_reordered_event(g_scene_manager.current_scene_index);

  return ESP_OK;
}

// ============================================================================
// Input suspension for programming mode
// ============================================================================

// Track LFO running state before entering programming mode
static bool s_lfo_was_running[2] = {false, false};

esp_err_t scene_suspend_input(void) {
  if (s_input_suspended) return ESP_OK;  // Already suspended

  ESP_LOGD(TAG, "Suspending scene input (entering programming mode)");

  // Unregister scene touchwheel so it doesn't receive input
  if (s_scene_touchwheel) {
    touch_unregister_touchwheel_instance(s_scene_touchwheel);
    ESP_LOGD(TAG, "Scene touchwheel unregistered");
  }

  // Save LFO running state, then stop the loops so they don't emit fresh
  // values into the silence path. The held NOTE-output mapping notes are
  // released by midi_local_output_release_all() called next from ui.c.
  // LFO state lives here (not in midi_local_output) because restoration
  // has to coordinate with scene_apply_deferred_init when a scene change
  // happens during programming mode.
  s_lfo_was_running[0] = lfo_is_enabled(0);
  s_lfo_was_running[1] = lfo_is_enabled(1);
  lfo_enable(0, false);
  lfo_enable(1, false);

  s_input_suspended = true;
  return ESP_OK;
}

esp_err_t scene_resume_input(void) {
  if (!s_input_suspended) return ESP_OK;  // Not suspended

  ESP_LOGD(TAG, "Resuming scene input (leaving programming mode)");

  // Re-register scene touchwheel if it exists
  if (s_scene_touchwheel) {
    touch_register_touchwheel_instance(s_scene_touchwheel);
    ESP_LOGD(TAG, "Scene touchwheel re-registered");
  }

  // Restore LFO state (skip if a scene change is pending; scene_apply_deferred_init
  // will run lfo_apply_start_modes() against the new scene's config).
  //
  // Two cases per slot:
  //   1. Slot was already enabled before programming mode and the user did NOT
  //      change its configured-enabled state -> restore previous runtime state.
  //   2. Slot was disabled before programming mode and the user enabled it
  //      while in programming mode -> respect start_mode (PAUSED / RUNNING /
  //      TRANSPORT) instead of leaving the LFO running just because
  //      lfo_apply_config() flipped config.enabled to true.
  // Case 3 (was enabled, user disabled) just naturally stays off: lfo_apply_config()
  // wrote config.enabled=false, suspend's lfo_enable(i, false) was a no-op,
  // and we don't re-enable here.
  if (!s_needs_deferred_init) {
    scene_t* scene = scene_get_current();
    bool now_configured_enabled[2] = {
      scene ? scene->lfo1_config.enabled : false,
      scene ? scene->lfo2_config.enabled : false
    };

    for (int i = 0; i < 2; i++) {
      bool was_enabled = s_lfo_was_running[i];
      bool is_enabled = now_configured_enabled[i];

      if (!was_enabled && is_enabled) {
        lfo_apply_start_mode_one((uint8_t)i);
      } else if (was_enabled && is_enabled) {
        lfo_enable(i, true);
      }
    }
  }

  s_input_suspended = false;
  return ESP_OK;
}

