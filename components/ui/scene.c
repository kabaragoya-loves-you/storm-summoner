#include "lvgl.h"
#include "ui.h"
#include "lv_vector_art.h"
#include "ui_module_settings.h"
#include "event_bus.h"
#include "tempo.h"
#include "transport.h"
#include "scene.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

#define TAG "SCENE_UI"

//=============================================================================
// CONFIGURATION
//=============================================================================

// Sunburst base colors (gold center to darker gold edge)
#define BASE_CENTER_R  255
#define BASE_CENTER_G  200
#define BASE_CENTER_B  100

#define BASE_EDGE_R    180
#define BASE_EDGE_G    100
#define BASE_EDGE_B    40

// Red shift colors for beat 1
#define RED_CENTER_R   255
#define RED_CENTER_G   60
#define RED_CENTER_B   40

#define RED_EDGE_R     180
#define RED_EDGE_G     30
#define RED_EDGE_B     20

// Pulse depth (0.0 = no pulse, 1.0 = full black between beats)
#define PULSE_DEPTH    0.2f

// Asset paths
#define STATIC_PATH "/assets/images/mountain.bin.z"
#define BODY_PATH   "/assets/images/body.bin.z"
#define TAIL_PATH   "/assets/images/tail.bin.z"

// Animation frame counts
#define BODY_FRAME_COUNT 12
#define TAIL_FRAME_COUNT 48

// Body-to-tail ratio: body plays this many times per tail cycle
// Default 4 reflects common 4/4 time signature
#define DEFAULT_BODY_RATIO 4

// Interpolation timer period (ms) - ~60 FPS
#define INTERP_TIMER_PERIOD_MS 16

//=============================================================================
// BOP SOLVER - Calculates body animation rate from time signature
//=============================================================================

typedef enum {
  METER_SIMPLE,
  METER_COMPOUND
} meter_type_t;

// Classify meter type based on time signature
// Compound meters: 6/8, 9/8, 12/8 (numerator divisible by 3, denominator is 8)
static meter_type_t classify_meter(uint8_t numerator, uint8_t denominator) {
  if ((numerator == 6 || numerator == 9 || numerator == 12) && denominator == 8) {
    return METER_COMPOUND;
  }
  return METER_SIMPLE;
}

// Calculate felt beats per bar based on meter type
// Simple meter: beats = numerator (4/4 -> 4, 3/4 -> 3)
// Compound meter: beats = numerator / 3 (6/8 -> 2, 9/8 -> 3, 12/8 -> 4)
static uint8_t get_beats_per_bar(uint8_t numerator, uint8_t denominator) {
  if (classify_meter(numerator, denominator) == METER_COMPOUND) {
    return numerator / 3;
  }
  return numerator;
}

// Calculate sub-bops per beat based on beat divider and meter type
// Simple meter: 8ths = 2 per beat (for quarter-note beats), 16ths = 4
// Compound meter: 8ths = 3 per beat (dotted note = 3 eighths), 16ths = 6
static uint8_t get_sub_bops_per_beat(uint8_t denominator, tempo_note_divider_t divider, meter_type_t meter) {
  if (divider == DIVIDER_QUARTER) {
    return 1;  // One bop per felt beat
  }
  
  if (meter == METER_SIMPLE) {
    // Simple meter: beat = denominator note value
    if (divider == DIVIDER_EIGHTH) {
      return (denominator >= 8) ? 1 : 8 / denominator;  // 4/4: 8/4 = 2 eighths per quarter
    }
    if (divider == DIVIDER_SIXTEENTH) {
      return (denominator >= 16) ? 1 : 16 / denominator;  // 4/4: 16/4 = 4 sixteenths per quarter
    }
  } else {
    // Compound meter: beat = dotted note (3 x denominator notes)
    if (divider == DIVIDER_EIGHTH) {
      return 3;  // Each dotted beat has 3 eighths
    }
    if (divider == DIVIDER_SIXTEENTH) {
      return 6;  // Each dotted beat has 6 sixteenths (3 eighths x 2)
    }
  }
  
  return 1;  // Fallback
}

// Main bop solver: calculate total bops per bar from current tempo settings
// Returns beats_per_bar * sub_bops_per_beat
static uint8_t calculate_bops_per_bar(void) {
  time_signature_t sig = tempo_get_time_signature();
  tempo_note_divider_t divider = tempo_get_note_divider();
  meter_type_t meter = classify_meter(sig.numerator, sig.denominator);
  
  uint8_t beats = get_beats_per_bar(sig.numerator, sig.denominator);
  uint8_t subs = get_sub_bops_per_beat(sig.denominator, divider, meter);
  
  uint8_t bops = beats * subs;
  
  // Sanity check: ensure at least 1 bop per bar
  return (bops > 0) ? bops : 1;
}

//=============================================================================
// STATE
//=============================================================================

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_static_art = NULL;   // Bottom layer (static)
static lv_obj_t *g_body_art = NULL;     // Middle layer (12 frames)
static lv_obj_t *g_tail_art = NULL;     // Top layer (48 frames)
static lv_obj_t *g_bpm_label = NULL;
static lv_obj_t *g_time_sig_label = NULL;
static lv_obj_t *g_beat_label = NULL;
static lv_grad_dsc_t g_grad;
static lv_timer_t *g_interp_timer = NULL;

static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;

// Body-to-tail animation ratio (configurable)
static uint8_t g_body_ratio = DEFAULT_BODY_RATIO;

// Module settings (registered with ui_module_settings)
static bool g_animation_enabled = true;
static bool g_pulse_enabled = true;

static ui_module_setting_t scene_settings[] = {
  { "animation", UI_SETTING_BOOL, &g_animation_enabled, "Enable/disable animation", NULL, NULL },
  { "pulse", UI_SETTING_BOOL, &g_pulse_enabled, "Red pulse on beat 1", NULL, NULL },
  { "body_ratio", UI_SETTING_U8, &g_body_ratio, "Body loops per tail cycle", NULL, NULL },
};
static const size_t scene_settings_count = sizeof(scene_settings) / sizeof(scene_settings[0]);

// Beat tracking (volatile - updated from event handler context)
static volatile uint8_t g_current_beat = 1;      // 1-based beat position
static volatile uint8_t g_bar_length = 4;        // Beats per bar
static volatile uint16_t g_current_bpm = 120;    // Current BPM
static volatile uint32_t g_last_beat_time_ms = 0;
static volatile uint32_t g_beat_duration_ms = 500; // 60000 / BPM

// Transport state (volatile - updated from event handler context)
static volatile transport_state_t g_transport_state = TRANSPORT_STOPPED;

// Dirty flags - set by event handlers, cleared by timer
static volatile bool g_beat_dirty = false;
static volatile bool g_tempo_dirty = false;
static volatile bool g_transport_dirty = false;

// Module active flag (only process events when screen is active)
static volatile bool g_module_active = false;

// Cached values for label update
static uint8_t g_last_label_beat = 0;
static uint16_t g_last_label_bpm = 0;
static transport_state_t g_last_label_state = TRANSPORT_STOPPED;

// External font declarations
LV_FONT_DECLARE(flyer_venice_20);
LV_FONT_DECLARE(flyer_venice_24);

//=============================================================================
// FORWARD DECLARATIONS
//=============================================================================

static void beat_event_handler(const event_t* event, void* context);
static void tempo_changed_handler(const event_t* event, void* context);
static void transport_state_handler(const event_t* event, void* context);
static void interp_timer_cb(lv_timer_t *timer);
static void update_gradient(float phase, bool is_beat_one);
static void update_tempo_labels(void);
static lv_color_t get_transport_color(void);

//=============================================================================
// GRADIENT HELPERS
//=============================================================================

static uint8_t lerp_u8(uint8_t a, uint8_t b, float t) {
  return (uint8_t)(a + (b - a) * t);
}

static void update_gradient(float phase, bool is_beat_one) {
  if (!g_screen) return;
  
  // Calculate brightness based on phase (0 = on beat, 0.5 = between beats, 1 = next beat)
  // Use full cosine cycle: brightest at phase=0 and 1, darkest at phase=0.5
  // Formula: (1 - cos(phase * 2π)) / 2 gives 0->1->0 as phase goes 0->0.5->1
  float pulse = (1.0f - cosf(phase * 2.0f * M_PI)) / 2.0f;
  float brightness = 1.0f - PULSE_DEPTH * pulse;
  
  // Base colors
  uint8_t center_r = BASE_CENTER_R;
  uint8_t center_g = BASE_CENTER_G;
  uint8_t center_b = BASE_CENTER_B;
  uint8_t edge_r = BASE_EDGE_R;
  uint8_t edge_g = BASE_EDGE_G;
  uint8_t edge_b = BASE_EDGE_B;
  
  // Red shift on beat 1 - fade in at start of beat, fade out at end
  if (is_beat_one && g_pulse_enabled) {
    // Red blend factor: 1.0 at phase=0, fades to 0 as phase approaches 1
    float red_blend = 1.0f - phase;
    red_blend = red_blend * red_blend; // Ease out for smoother transition
    
    center_r = lerp_u8(BASE_CENTER_R, RED_CENTER_R, red_blend);
    center_g = lerp_u8(BASE_CENTER_G, RED_CENTER_G, red_blend);
    center_b = lerp_u8(BASE_CENTER_B, RED_CENTER_B, red_blend);
    edge_r = lerp_u8(BASE_EDGE_R, RED_EDGE_R, red_blend);
    edge_g = lerp_u8(BASE_EDGE_G, RED_EDGE_G, red_blend);
    edge_b = lerp_u8(BASE_EDGE_B, RED_EDGE_B, red_blend);
  }
  
  // Apply brightness
  center_r = (uint8_t)(center_r * brightness);
  center_g = (uint8_t)(center_g * brightness);
  center_b = (uint8_t)(center_b * brightness);
  edge_r = (uint8_t)(edge_r * brightness);
  edge_g = (uint8_t)(edge_g * brightness);
  edge_b = (uint8_t)(edge_b * brightness);
  
  // Update gradient colors
  lv_color_t colors[2] = {
    lv_color_make(center_r, center_g, center_b),
    lv_color_make(edge_r, edge_g, edge_b)
  };
  lv_opa_t opas[2] = { LV_OPA_COVER, LV_OPA_COVER };
  uint8_t fracs[2] = { 0, 255 };
  lv_grad_init_stops(&g_grad, colors, opas, fracs, 2);
  
  lv_obj_set_style_bg_grad(g_screen, &g_grad, 0);
  lv_obj_invalidate(g_screen);
}

//=============================================================================
// LABEL HELPERS
//=============================================================================

static lv_color_t get_transport_color(void) {
  // When not using transport controls, always show white (animation is running)
  bool use_transport = scene_get_use_transport(scene_get_current_index());
  if (!use_transport) {
    return lv_color_make(255, 255, 255);  // White
  }
  
  switch (g_transport_state) {
    case TRANSPORT_STOPPED:
      return lv_color_make(0, 0, 0);        // Black
    case TRANSPORT_PAUSED:
      return lv_color_make(128, 128, 128);  // Grey
    case TRANSPORT_PLAYING:
      return lv_color_make(255, 255, 255);  // White
    case TRANSPORT_RECORDING:
      return lv_color_make(255, 0, 0);      // Red
    default:
      return lv_color_make(255, 255, 255);
  }
}

static void update_tempo_labels(void) {
  // Only update if something changed
  uint8_t beat = g_current_beat;
  uint16_t bpm = g_current_bpm;
  transport_state_t state = g_transport_state;
  
  if (beat == g_last_label_beat && bpm == g_last_label_bpm && state == g_last_label_state) {
    return;
  }
  
  g_last_label_beat = beat;
  g_last_label_bpm = bpm;
  g_last_label_state = state;
  
  lv_color_t color = get_transport_color();
  time_signature_t sig = tempo_get_time_signature();
  
  // Update BPM label
  if (g_bpm_label) {
    lv_label_set_text_fmt(g_bpm_label, "%d BPM", bpm);
    lv_obj_set_style_text_color(g_bpm_label, color, 0);
  }
  
  // Update time signature label
  if (g_time_sig_label) {
    lv_label_set_text_fmt(g_time_sig_label, "%d/%d", sig.numerator, sig.denominator);
    lv_obj_set_style_text_color(g_time_sig_label, color, 0);
  }
  
  // Update beat label
  if (g_beat_label) {
    lv_label_set_text_fmt(g_beat_label, "%d", beat);
    lv_obj_set_style_text_color(g_beat_label, color, 0);
  }
}

//=============================================================================
// EVENT HANDLERS (lightweight - just update state, no LVGL calls)
//=============================================================================

static void beat_event_handler(const event_t* event, void* context) {
  if (!event || !g_module_active) return;
  
  // Check if transport controls are in use
  bool use_transport = scene_get_use_transport(scene_get_current_index());
  
  // Only filter by transport state when use_transport is enabled
  if (use_transport) {
    // Only update state when transport is actively playing
    // This prevents state drift while stopped/paused
    if (g_transport_state != TRANSPORT_PLAYING && g_transport_state != TRANSPORT_RECORDING) {
      return;
    }
  }
  
  // Just update volatile state - LVGL updates happen in timer callback
  g_current_beat = event->data.beat.beat_in_bar;
  g_bar_length = event->data.beat.bar_length;
  g_last_beat_time_ms = esp_timer_get_time() / 1000;
  g_beat_dirty = true;
}

static void tempo_changed_handler(const event_t* event, void* context) {
  if (!event || !g_module_active) return;
  
  g_current_bpm = event->data.tempo.bpm;
  g_beat_duration_ms = 60000 / g_current_bpm;
  g_tempo_dirty = true;
  
  // Update bar length from current time signature
  time_signature_t sig = tempo_get_time_signature();
  g_bar_length = sig.numerator;
  
  // Recalculate bops per bar in case time signature or beat divider changed
  g_body_ratio = calculate_bops_per_bar();
}

static void transport_state_handler(const event_t* event, void* context) {
  if (!event || !g_module_active) return;
  
  g_transport_state = (transport_state_t)event->data.transport.state;
  g_transport_dirty = true;
}

//=============================================================================
// INTERPOLATION TIMER (runs in LVGL context - safe for LVGL calls)
//=============================================================================

static void interp_timer_cb(lv_timer_t *timer) {
  if (!g_module_active) return;
  
  // Check if animation is enabled at all
  if (!g_animation_enabled) {
    // Still update labels
    if (g_beat_dirty || g_tempo_dirty || g_transport_dirty) {
      g_beat_dirty = false;
      g_tempo_dirty = false;
      g_transport_dirty = false;
      update_tempo_labels();
    }
    return;
  }
  
  // Check scene's use_transport setting
  bool use_transport = scene_get_use_transport(scene_get_current_index());
  
  // Handle transport state changes (only if use_transport is enabled)
  if (use_transport && g_transport_dirty) {
    g_transport_dirty = false;
    
    switch (g_transport_state) {
      case TRANSPORT_PLAYING:
      case TRANSPORT_RECORDING:
        // Reset timing to start clean - prevents "catch up" animation
        g_last_beat_time_ms = esp_timer_get_time() / 1000;
        break;
        
      case TRANSPORT_STOPPED:
        // Reset to frame 0
        if (g_body_art && lv_vector_art_is_animated(g_body_art)) {
          lv_vector_art_set_frame(g_body_art, 0);
        }
        if (g_tail_art && lv_vector_art_is_animated(g_tail_art)) {
          lv_vector_art_set_frame(g_tail_art, 0);
        }
        g_current_beat = 1;
        update_gradient(0.0f, false);
        update_tempo_labels();
        return; // Don't do further updates when stopped
        
      case TRANSPORT_PAUSED:
        // Just update label, keep current visual state
        update_tempo_labels();
        return;
    }
  } else if (!use_transport) {
    g_transport_dirty = false; // Clear without processing
  }
  
  // Check if we should animate
  bool should_animate = use_transport
    ? (g_transport_state == TRANSPORT_PLAYING || g_transport_state == TRANSPORT_RECORDING)
    : true; // Always animate when use_transport is false
  
  if (!should_animate) {
    // Still update label if dirty
    if (g_beat_dirty || g_tempo_dirty) {
      g_beat_dirty = false;
      g_tempo_dirty = false;
      update_tempo_labels();
    }
    return;
  }
  
  // Calculate phase within current beat
  uint32_t now = esp_timer_get_time() / 1000;
  float bar_phase;
  
  if (!use_transport) {
    // When not using transport, calculate phase from continuous time
    // This makes animation self-driving, not dependent on beat events
    uint32_t bar_duration_ms = g_beat_duration_ms * g_bar_length;
    bar_phase = fmodf((float)now / (float)bar_duration_ms, 1.0f);
    
    // Derive current beat from bar_phase for label display
    uint8_t derived_beat = 1 + (uint8_t)(bar_phase * g_bar_length);
    if (derived_beat > g_bar_length) derived_beat = g_bar_length;
    if (derived_beat != g_current_beat) {
      g_current_beat = derived_beat;
      g_beat_dirty = true;
    }
  } else {
    // When using transport, use beat events for timing
    uint32_t elapsed = now - g_last_beat_time_ms;
    float beat_phase = (float)elapsed / (float)g_beat_duration_ms;
    if (beat_phase > 1.0f) beat_phase = 1.0f;
    
    // Calculate bar phase (0-1 over entire bar) for gradient pulse and tail animation
    bar_phase = ((float)(g_current_beat - 1) + beat_phase) / (float)g_bar_length;
  }
  
  // Update gradient with pulse (one full cycle per bar)
  // Note: is_beat_one detection uses bar_phase for non-transport mode
  bool is_beat_one = use_transport
    ? (g_current_beat == 1 && bar_phase < (1.0f / g_bar_length / 2.0f))
    : (bar_phase < (0.5f / g_bar_length));
  update_gradient(bar_phase, is_beat_one);
  
  // Update label if dirty
  if (g_beat_dirty || g_tempo_dirty) {
    g_beat_dirty = false;
    g_tempo_dirty = false;
    update_tempo_labels();
  }
  
  // Tail animation: plays once per bar cycle (48 frames over bar_phase 0-1)
  if (g_tail_art && lv_vector_art_is_animated(g_tail_art)) {
    uint16_t tail_frame = (uint16_t)(bar_phase * TAIL_FRAME_COUNT);
    if (tail_frame >= TAIL_FRAME_COUNT) tail_frame = TAIL_FRAME_COUNT - 1;
    lv_vector_art_set_frame(g_tail_art, tail_frame);
  }
  
  // Body animation: plays g_body_ratio times per bar cycle
  // Multiply phase by ratio and wrap using fmodf
  if (g_body_art && lv_vector_art_is_animated(g_body_art)) {
    float body_phase = fmodf(bar_phase * g_body_ratio, 1.0f);
    uint16_t body_frame = (uint16_t)(body_phase * BODY_FRAME_COUNT);
    if (body_frame >= BODY_FRAME_COUNT) body_frame = BODY_FRAME_COUNT - 1;
    lv_vector_art_set_frame(g_body_art, body_frame);
  }
}

//=============================================================================
// MODULE CALLBACKS
//=============================================================================

static void scene_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen != NULL) {
    lv_screen_load(g_screen);
    lv_timer_delete(timer);
    return;
  }
  
  lv_display_t *disp = lv_display_get_default();
  g_disp_width = lv_display_get_horizontal_resolution(disp);
  g_disp_height = lv_display_get_vertical_resolution(disp);
  
  // Create screen with radial gradient background
  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, g_disp_width, g_disp_height);
  lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
  
  // Layer 1: Radial gradient background (sunburst)
  lv_grad_radial_init(&g_grad, 
    LV_PCT(50), LV_PCT(50),
    LV_PCT(100), LV_PCT(50),
    LV_GRAD_EXTEND_PAD);
  
  lv_color_t colors[2] = {
    lv_color_make(BASE_CENTER_R, BASE_CENTER_G, BASE_CENTER_B),
    lv_color_make(BASE_EDGE_R, BASE_EDGE_G, BASE_EDGE_B)
  };
  lv_opa_t opas[2] = { LV_OPA_COVER, LV_OPA_COVER };
  uint8_t fracs[2] = { 0, 255 };
  lv_grad_init_stops(&g_grad, colors, opas, fracs, 2);
  
  lv_obj_set_style_bg_grad(g_screen, &g_grad, 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  
  // Scale factor for 240x240 source art
  float scale = (float)g_disp_width / 240.0f;
  
  // Layer 2: Static vector art (bottom layer)
  g_static_art = lv_vector_art_create(g_screen);
  lv_obj_set_size(g_static_art, g_disp_width, g_disp_height);
  lv_obj_center(g_static_art);
  lv_vector_art_set_scale(g_static_art, scale);
  
  if (lv_vector_art_set_src(g_static_art, STATIC_PATH)) {
    ESP_LOGD(TAG, "Loaded static layer from %s", STATIC_PATH);
  } else {
    ESP_LOGE(TAG, "Failed to load static layer from %s", STATIC_PATH);
  }
  
  // Layer 3: Body animation (middle layer - 12 frames)
  g_body_art = lv_vector_art_create(g_screen);
  lv_obj_set_size(g_body_art, g_disp_width, g_disp_height);
  lv_obj_center(g_body_art);
  lv_vector_art_set_scale(g_body_art, scale);
  
  if (lv_vector_art_set_src(g_body_art, BODY_PATH)) {
    if (lv_vector_art_is_animated(g_body_art)) {
      ESP_LOGD(TAG, "Loaded body animation: %d frames",
               lv_vector_art_get_frame_count(g_body_art));
      lv_vector_art_pause(g_body_art);
      lv_vector_art_set_frame(g_body_art, 0);
    } else {
      ESP_LOGW(TAG, "Body layer is static (expected animated)");
    }
  } else {
    ESP_LOGE(TAG, "Failed to load body layer from %s", BODY_PATH);
  }
  
  // Layer 4: Tail animation (top layer - 48 frames)
  g_tail_art = lv_vector_art_create(g_screen);
  lv_obj_set_size(g_tail_art, g_disp_width, g_disp_height);
  lv_obj_center(g_tail_art);
  lv_vector_art_set_scale(g_tail_art, scale);
  
  if (lv_vector_art_set_src(g_tail_art, TAIL_PATH)) {
    if (lv_vector_art_is_animated(g_tail_art)) {
      ESP_LOGD(TAG, "Loaded tail animation: %d frames",
               lv_vector_art_get_frame_count(g_tail_art));
      lv_vector_art_pause(g_tail_art);
      lv_vector_art_set_frame(g_tail_art, 0);
    } else {
      ESP_LOGW(TAG, "Tail layer is static (expected animated)");
    }
  } else {
    ESP_LOGE(TAG, "Failed to load tail layer from %s", TAIL_PATH);
  }
  
  // Layer 5: Tempo display labels
  // Time signature label
  g_time_sig_label = lv_label_create(g_screen);
  lv_obj_set_style_text_font(g_time_sig_label, &flyer_venice_20, 0);
  lv_obj_set_pos(g_time_sig_label, 30, 31);
  
  // BPM label
  g_bpm_label = lv_label_create(g_screen);
  lv_obj_set_style_text_font(g_bpm_label, &flyer_venice_20, 0);
  lv_obj_set_pos(g_bpm_label, 12, 55);
  
  // Beat label - top right area, larger font
  g_beat_label = lv_label_create(g_screen);
  lv_obj_set_style_text_font(g_beat_label, &flyer_venice_24, 0);
  lv_obj_set_pos(g_beat_label, 135, 30);
  
  // Initialize state from tempo/transport
  g_current_bpm = tempo_get_bpm();
  g_beat_duration_ms = 60000 / g_current_bpm;
  g_transport_state = transport_get_state();
  time_signature_t sig = tempo_get_time_signature();
  g_bar_length = sig.numerator;
  g_current_beat = transport_get_current_beat();
  if (g_current_beat == 0) g_current_beat = 1;
  
  // Calculate body animation rate from time signature
  g_body_ratio = calculate_bops_per_bar();
  ESP_LOGD(TAG, "Bop solver: %d/%d -> %d bops per bar", 
           sig.numerator, sig.denominator, g_body_ratio);
  
  // Force label update
  g_last_label_beat = 0;
  g_last_label_bpm = 0;
  update_tempo_labels();
  
  // Create interpolation timer (always running - checks state internally)
  g_interp_timer = lv_timer_create(interp_timer_cb, INTERP_TIMER_PERIOD_MS, NULL);

  // Subscribe to events now that we're active
  event_bus_subscribe(EVENT_BEAT, beat_event_handler, NULL);
  event_bus_subscribe(EVENT_TEMPO_CHANGED, tempo_changed_handler, NULL);
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, transport_state_handler, NULL);

  // Mark module as active
  g_module_active = true;
  
  ESP_LOGI(TAG, "Scene UI screen created - BPM: %d, Time Sig: %d/%d, Body ratio: %d",
           g_current_bpm, sig.numerator, sig.denominator, g_body_ratio);
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(scene_ui, scene_draw_deferred_cb)

static void scene_ui_teardown(void) {
  // Mark module as inactive first
  g_module_active = false;

  // Unsubscribe from events (we subscribe when activated, unsubscribe when deactivated)
  event_bus_unsubscribe(EVENT_BEAT, beat_event_handler);
  event_bus_unsubscribe(EVENT_TEMPO_CHANGED, tempo_changed_handler);
  event_bus_unsubscribe(EVENT_TRANSPORT_STATE_CHANGED, transport_state_handler);
  
  // Clean up interpolation timer
  if (g_interp_timer) {
    lv_timer_delete(g_interp_timer);
    g_interp_timer = NULL;
  }
  
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_static_art = NULL;
    g_body_art = NULL;
    g_tail_art = NULL;
    g_bpm_label = NULL;
    g_time_sig_label = NULL;
    g_beat_label = NULL;
  }
  
  // Reset cached state
  g_last_label_beat = 0;
  g_last_label_bpm = 0;
  g_last_label_state = TRANSPORT_STOPPED;
}

static void scene_ui_init(void) {
  static bool s_settings_registered = false;
  if (s_settings_registered) return;
  s_settings_registered = true;
  
  // Register module settings (once at startup, so they're always available)
  ui_module_register_settings("scene", scene_settings, scene_settings_count);
  ESP_LOGD(TAG, "Scene UI module settings registered");
}

ui_draw_module_t scene_ui_module = {
  .draw_func = scene_ui_draw,
  .teardown_func = scene_ui_teardown,
  .init_func = scene_ui_init,
  .name = "scene"
};

