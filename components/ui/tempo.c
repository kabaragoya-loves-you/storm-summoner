#include "lvgl.h"
#include "ui.h"
#include "lv_vector_art.h"
#include "event_bus.h"
#include "tempo.h"
#include "transport.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

#define TAG "TEMPO_UI"

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

// Animated vector art path
#define TEMPO_ANIM_PATH "/assets/images/kabaragoya_anim.bin.z"

// Animation frames
#define ANIM_FRAME_COUNT 24

// Interpolation timer period (ms) - ~60 FPS
#define INTERP_TIMER_PERIOD_MS 16

//=============================================================================
// STATE
//=============================================================================

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_vector_art = NULL;
static lv_obj_t *g_bpm_label = NULL;
static lv_obj_t *g_time_sig_label = NULL;
static lv_obj_t *g_beat_label = NULL;
static lv_grad_dsc_t g_grad;
static lv_timer_t *g_interp_timer = NULL;

static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;

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
  if (is_beat_one) {
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
  
  // Only update state when transport is actively playing
  // This prevents state drift while stopped/paused
  if (g_transport_state != TRANSPORT_PLAYING && g_transport_state != TRANSPORT_RECORDING) {
    return;
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
  
  // Handle transport state changes
  if (g_transport_dirty) {
    g_transport_dirty = false;
    
    switch (g_transport_state) {
      case TRANSPORT_PLAYING:
      case TRANSPORT_RECORDING:
        // Reset timing to start clean - prevents "catch up" animation
        g_last_beat_time_ms = esp_timer_get_time() / 1000;
        break;
        
      case TRANSPORT_STOPPED:
        // Reset to frame 0
        if (g_vector_art && lv_vector_art_is_animated(g_vector_art)) {
          lv_vector_art_set_frame(g_vector_art, 0);
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
  }
  
  // Don't animate when stopped or paused
  if (g_transport_state == TRANSPORT_STOPPED || g_transport_state == TRANSPORT_PAUSED) {
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
  uint32_t elapsed = now - g_last_beat_time_ms;
  float beat_phase = (float)elapsed / (float)g_beat_duration_ms;
  if (beat_phase > 1.0f) beat_phase = 1.0f;
  
  // Calculate bar phase (0-1 over entire bar) for gradient pulse
  float bar_phase = ((float)(g_current_beat - 1) + beat_phase) / (float)g_bar_length;
  
  // Update gradient with pulse (one full cycle per bar)
  update_gradient(bar_phase, g_current_beat == 1 && beat_phase < 0.5f);
  
  // Update label if dirty
  if (g_beat_dirty || g_tempo_dirty) {
    g_beat_dirty = false;
    g_tempo_dirty = false;
    update_tempo_labels();
    // Note: LED flash is handled by tempo component in publish_beat_event()
  }
  
  // Smooth animation frame interpolation
  if (g_vector_art && lv_vector_art_is_animated(g_vector_art)) {
    // Calculate frame based on bar phase (already computed above)
    uint16_t frame = (uint16_t)(bar_phase * ANIM_FRAME_COUNT);
    
    // Clamp to valid range
    if (frame >= ANIM_FRAME_COUNT) frame = ANIM_FRAME_COUNT - 1;
    
    lv_vector_art_set_frame(g_vector_art, frame);
  }
}

//=============================================================================
// MODULE CALLBACKS
//=============================================================================

static void tempo_draw_deferred_cb(lv_timer_t *timer) {
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
  
  // Layer 2: Animated vector art
  g_vector_art = lv_vector_art_create(g_screen);
  lv_obj_set_size(g_vector_art, g_disp_width, g_disp_height);
  lv_obj_center(g_vector_art);
  
  // Scale to fit display (source is 198x198)
  float scale = (float)g_disp_width / 198.0f;
  lv_vector_art_set_scale(g_vector_art, scale);
  
  // Load the animated vector art
  if (lv_vector_art_set_src(g_vector_art, TEMPO_ANIM_PATH)) {
    if (lv_vector_art_is_animated(g_vector_art)) {
      ESP_LOGI(TAG, "Loaded animated vector art: %d frames",
               lv_vector_art_get_frame_count(g_vector_art));
      
      // Don't auto-play - we control frames manually based on beat
      lv_vector_art_pause(g_vector_art);
      lv_vector_art_set_frame(g_vector_art, 0);
    } else {
      ESP_LOGW(TAG, "Loaded static vector art (expected animated)");
    }
  } else {
    ESP_LOGE(TAG, "Failed to load animated vector art from %s", TEMPO_ANIM_PATH);
  }
  
  // Layer 3: Tempo display labels
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
  
  // Force label update
  g_last_label_beat = 0;
  g_last_label_bpm = 0;
  update_tempo_labels();
  
  // Create interpolation timer (always running - checks state internally)
  g_interp_timer = lv_timer_create(interp_timer_cb, INTERP_TIMER_PERIOD_MS, NULL);
  
  // Mark module as active
  g_module_active = true;
  
  ESP_LOGI(TAG, "Tempo UI screen created - BPM: %d, Time Sig: %d/%d",
           g_current_bpm, sig.numerator, sig.denominator);
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(tempo, tempo_draw_deferred_cb)

static void tempo_teardown(void) {
  // Mark module as inactive first
  g_module_active = false;
  
  // Unsubscribe from events to prevent duplicate handlers on re-init
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
    g_vector_art = NULL;
    g_bpm_label = NULL;
    g_time_sig_label = NULL;
    g_beat_label = NULL;
  }
  
  // Reset cached state
  g_last_label_beat = 0;
  g_last_label_bpm = 0;
  g_last_label_state = TRANSPORT_STOPPED;
}

static void tempo_ui_init(void) {
  // Subscribe to events
  event_bus_subscribe(EVENT_BEAT, beat_event_handler, NULL);
  event_bus_subscribe(EVENT_TEMPO_CHANGED, tempo_changed_handler, NULL);
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, transport_state_handler, NULL);
  
  ESP_LOGI(TAG, "Tempo UI module initialized");
}

ui_draw_module_t tempo_module = {
  .draw_func = tempo_draw,
  .teardown_func = tempo_teardown,
  .init_func = tempo_ui_init,
  .name = "tempo"
};
