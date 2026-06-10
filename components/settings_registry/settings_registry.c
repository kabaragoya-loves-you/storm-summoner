#include "settings_registry.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

// Component headers
#include "buttons.h"
#include "bump.h"
#include "clock_sync.h"
#include "config.h"
#include "cv.h"
#include "display_console.h"
#include "expression.h"
#include "input_mode.h"
#include "midi_loopback.h"
#include "midi_out.h"
#include "midi_passthrough.h"
#include "scene.h"
#include "screensaver.h"
#include "sensor.h"
#include "tempo.h"
#include "touch.h"
#include "ui.h"
#include "tilt.h"
#include "note_track_config.h"
#include "midi_control.h"
#include "menu_theme.h"
#include "inspect_config.h"

static const char* TAG = "settings_registry";

// ============================================================================
// Getter/Setter function types
// ============================================================================

typedef uint32_t (*setting_getter_t)(void);
typedef esp_err_t (*setting_setter_t)(uint32_t value);

typedef struct {
  const char* id;
  setting_getter_t getter;
  setting_setter_t setter;
} setting_entry_t;

// ============================================================================
// Wrapper functions for type conversion and special cases
// ============================================================================

// Config wrappers
static uint32_t get_scene_mode(void) { return (uint32_t)scene_get_mode(); }
static esp_err_t set_scene_mode(uint32_t v) { return scene_set_mode((scene_mode_t)v); }

static uint32_t get_confirm_change(void) { return (uint32_t)scene_get_change_mode(); }
static esp_err_t set_confirm_change(uint32_t v) { return scene_set_change_mode((scene_change_mode_t)v); }

static uint32_t get_preset_wrap(void) { return config_get_preset_wrap() ? 1 : 0; }
static esp_err_t set_preset_wrap(uint32_t v) { return config_set_preset_wrap(v != 0); }

static uint32_t get_persist_scene(void) { return config_get_persist_scene() ? 1 : 0; }
static esp_err_t set_persist_scene(uint32_t v) { return config_set_persist_scene(v != 0); }

static uint32_t get_device_mode(void) { return (uint32_t)config_get_device_mode(); }
static esp_err_t set_device_mode(uint32_t v) { return config_set_device_mode((device_mode_t)v); }

static uint32_t get_flag_enabled(void) { return config_get_flag_enabled() ? 1 : 0; }
static esp_err_t set_flag_enabled(uint32_t v) { return config_set_flag_enabled(v != 0); }

// Touch wrappers
static uint32_t get_stuck_timeout(void) { return touch_get_stuck_timeout_ms(); }
static esp_err_t set_stuck_timeout(uint32_t v) {
  touch_set_stuck_timeout_ms(v);
  return ESP_OK;
}

static uint32_t get_idle_calibration(void) { return touch_get_idle_calibration_interval_ms(); }
static esp_err_t set_idle_calibration(uint32_t v) {
  touch_set_idle_calibration_interval_ms(v);
  return ESP_OK;
}

static uint32_t get_menu_hold_time(void) { return ui_get_button13_long_press_ms(); }
static esp_err_t set_menu_hold_time(uint32_t v) {
  ui_set_button13_long_press_ms(v);
  return ESP_OK;
}

// MIDI wrappers - interface combines to bitmask
static uint32_t get_midi_interface(void) { return (uint32_t)midi_out_get_interfaces(); }
static esp_err_t set_midi_interface(uint32_t v) {
  midi_out_set_interfaces((midi_out_interface_t)v);
  return ESP_OK;
}

// MIDI passthrough - combines two booleans into bitmask (bit0 = usb_to_uart, bit1 = uart_to_usb)
static uint32_t get_midi_passthrough(void) {
  uint32_t val = 0;
  if (midi_passthrough_usb_to_uart_is_enabled()) val |= 1;
  if (midi_passthrough_uart_to_usb_is_enabled()) val |= 2;
  return val;
}
static esp_err_t set_midi_passthrough(uint32_t v) {
  midi_passthrough_usb_to_uart_enable((v & 1) != 0);
  midi_passthrough_uart_to_usb_enable((v & 2) != 0);
  return ESP_OK;
}

// MIDI loopback - combines two booleans into bitmask (bit0 = uart, bit1 = usb)
static uint32_t get_midi_loopback(void) {
  uint32_t val = 0;
  if (midi_loopback_uart_is_enabled()) val |= 1;
  if (midi_loopback_usb_is_enabled()) val |= 2;
  return val;
}
static esp_err_t set_midi_loopback(uint32_t v) {
  midi_loopback_uart_enable((v & 1) != 0);
  midi_loopback_usb_enable((v & 2) != 0);
  return ESP_OK;
}

// Expression wrappers
static uint32_t get_expr_polarity(void) { return (uint32_t)expression_get_polarity(); }
static esp_err_t set_expr_polarity(uint32_t v) {
  expression_set_polarity((expression_polarity_t)v);
  return ESP_OK;
}

static uint32_t get_expr_switch_type(void) { return (uint32_t)expression_get_pedal_switch_type(); }
static esp_err_t set_expr_switch_type(uint32_t v) {
  expression_set_pedal_switch_type((pedal_switch_type_t)v);
  return ESP_OK;
}

static uint32_t get_expr_slow_delay(void) { return expression_get_slow_delay(); }
static esp_err_t set_expr_slow_delay(uint32_t v) {
  expression_set_slow_delay((uint8_t)v);
  return ESP_OK;
}

static uint32_t get_expr_menu_nav(void) { return (uint32_t)expression_get_menu_nav_mode(); }
static esp_err_t set_expr_menu_nav(uint32_t v) {
  expression_set_menu_nav_mode((expression_menu_nav_mode_t)v);
  return ESP_OK;
}

// CV wrappers
static uint32_t get_cv_range(void) { return (uint32_t)cv_get_range(); }
static esp_err_t set_cv_range(uint32_t v) {
  cv_set_range((cv_range_t)v);
  return ESP_OK;
}

static uint32_t get_cv_pitch_standard(void) { return (uint32_t)cv_get_pitch_standard(); }
static esp_err_t set_cv_pitch_standard(uint32_t v) {
  cv_set_pitch_standard((cv_pitch_standard_t)v);
  return ESP_OK;
}

static uint32_t get_cv_deadzone(void) { return cv_get_deadzone(); }
static esp_err_t set_cv_deadzone(uint32_t v) {
  cv_set_deadzone((uint8_t)v);
  return ESP_OK;
}

// Proximity wrappers
static uint32_t get_prox_hysteresis(void) { return proximity_get_hysteresis_enabled() ? 1 : 0; }
static esp_err_t set_prox_hysteresis(uint32_t v) {
  proximity_set_hysteresis_enabled(v != 0);
  return ESP_OK;
}

static uint32_t get_prox_rest_position(void) { return proximity_get_rest_position(); }
static esp_err_t set_prox_rest_position(uint32_t v) {
  proximity_set_rest_position((uint8_t)v);
  return ESP_OK;
}

static uint32_t get_prox_return_speed(void) { return (uint32_t)proximity_get_return_speed(); }
static esp_err_t set_prox_return_speed(uint32_t v) {
  proximity_set_return_speed((proximity_return_speed_t)v);
  return ESP_OK;
}

static uint32_t get_prox_note_silence(void) { return proximity_get_note_silence_on_low() ? 1 : 0; }
static esp_err_t set_prox_note_silence(uint32_t v) {
  proximity_set_note_silence_on_low(v != 0);
  return ESP_OK;
}

static uint32_t get_prox_timeout(void) { return (uint32_t)proximity_get_timeout(); }
static esp_err_t set_prox_timeout(uint32_t v) {
  proximity_set_timeout((proximity_timeout_t)v);
  return ESP_OK;
}

static uint32_t get_prox_deadzone(void) { return proximity_get_deadzone(); }
static esp_err_t set_prox_deadzone(uint32_t v) {
  proximity_set_deadzone((uint8_t)v);
  return ESP_OK;
}

static uint32_t get_prox_ir_rejection(void) { return proximity_get_sunlight_cancel() ? 1 : 0; }
static esp_err_t set_prox_ir_rejection(uint32_t v) {
  proximity_set_sunlight_cancel(v != 0);
  return ESP_OK;
}

static uint32_t get_prox_gamma(void) { return proximity_get_gamma(); }
static esp_err_t set_prox_gamma(uint32_t v) {
  proximity_set_gamma((uint8_t)v);
  return ESP_OK;
}

// Tilt wrappers
static uint32_t get_tilt_forgive_middle(void) { return tilt_get_forgive_middle() ? 1 : 0; }
static esp_err_t set_tilt_forgive_middle(uint32_t v) {
  tilt_set_forgive_middle(v != 0);
  return ESP_OK;
}

static uint32_t get_tilt_middle_width(void) { return tilt_get_middle_width(); }
static esp_err_t set_tilt_middle_width(uint32_t v) {
  tilt_set_middle_width((uint8_t)v);
  return ESP_OK;
}

static uint32_t get_tilt_deadzone(void) { return tilt_get_deadzone(); }
static esp_err_t set_tilt_deadzone(uint32_t v) {
  tilt_set_deadzone((uint8_t)v);
  return ESP_OK;
}

static uint32_t get_tilt_rate_hz(void) { return tilt_get_rate_hz(); }
static esp_err_t set_tilt_rate_hz(uint32_t v) {
  tilt_set_rate_hz((uint8_t)v);
  return ESP_OK;
}

static uint32_t get_tilt_x_invert(void) { return tilt_get_axis_inverted(TILT_AXIS_X) ? 1 : 0; }
static esp_err_t set_tilt_x_invert(uint32_t v) {
  tilt_set_axis_inverted(TILT_AXIS_X, v != 0);
  return ESP_OK;
}

static uint32_t get_tilt_y_invert(void) { return tilt_get_axis_inverted(TILT_AXIS_Y) ? 1 : 0; }
static esp_err_t set_tilt_y_invert(uint32_t v) {
  tilt_set_axis_inverted(TILT_AXIS_Y, v != 0);
  return ESP_OK;
}

static uint32_t get_tilt_note_off_mode(void) { return (uint32_t)tilt_get_note_off_mode(); }
static esp_err_t set_tilt_note_off_mode(uint32_t v) {
  if (v >= TILT_NOTE_OFF_NUM_MODES) return ESP_ERR_INVALID_ARG;
  tilt_set_note_off_mode((tilt_note_off_mode_t)v);
  return ESP_OK;
}

// MIDI Control wrappers
static uint32_t get_midi_control_enabled(void) { return midi_control_is_enabled() ? 1 : 0; }
static esp_err_t set_midi_control_enabled(uint32_t v) {
  return midi_control_set_enabled(v != 0);
}

static uint32_t get_midi_control_channel(void) { return midi_control_get_channel(); }
static esp_err_t set_midi_control_channel(uint32_t v) {
  if (v < 1 || v > 16) return ESP_ERR_INVALID_ARG;
  return midi_control_set_channel((uint8_t)v);
}

static uint32_t get_midi_control_input(void) { return (uint32_t)midi_control_get_input(); }
static esp_err_t set_midi_control_input(uint32_t v) {
  if (v > MIDI_CONTROL_INPUT_BOTH) return ESP_ERR_INVALID_ARG;
  return midi_control_set_input((midi_control_input_t)v);
}

// Note Track wrappers
static uint32_t get_note_track_low_note(void) { return note_track_get_low_note(); }
static esp_err_t set_note_track_low_note(uint32_t v) {
  return note_track_set_low_note((uint8_t)v);
}

static uint32_t get_note_track_high_note(void) { return note_track_get_high_note(); }
static esp_err_t set_note_track_high_note(uint32_t v) {
  return note_track_set_high_note((uint8_t)v);
}

static uint32_t get_note_track_channel(void) { return note_track_get_channel(); }
static esp_err_t set_note_track_channel(uint32_t v) {
  if (v > 16) return ESP_ERR_INVALID_ARG;
  return note_track_set_channel((uint8_t)v);
}

static uint32_t get_note_track_filter_mode(void) { return (uint32_t)note_track_get_filter_mode(); }
static esp_err_t set_note_track_filter_mode(uint32_t v) {
  if (v > NOTE_TRACK_FILTER_KILL) return ESP_ERR_INVALID_ARG;
  return note_track_set_filter_mode((note_track_filter_mode_t)v);
}

// Theme wrappers
static uint32_t get_theme(void) { return (uint32_t)menu_theme_get(); }
static esp_err_t set_theme(uint32_t v) {
  if (v >= MENU_THEME_COUNT) return ESP_ERR_INVALID_ARG;
  return menu_theme_set((menu_theme_t)v);
}

// Scene Inspect wrappers
static uint32_t get_scene_inspect_scroll_speed(void) { return (uint32_t)inspect_config_get_scroll_speed(); }
static esp_err_t set_scene_inspect_scroll_speed(uint32_t v) {
  if (v >= INSPECT_SCROLL_SPEED_MAX) return ESP_ERR_INVALID_ARG;
  return inspect_config_set_scroll_speed((inspect_scroll_speed_t)v);
}

static uint32_t get_scene_inspect_scroll_mode(void) { return (uint32_t)inspect_config_get_scroll_mode(); }
static esp_err_t set_scene_inspect_scroll_mode(uint32_t v) {
  if (v >= INSPECT_SCROLL_MODE_MAX) return ESP_ERR_INVALID_ARG;
  return inspect_config_set_scroll_mode((inspect_scroll_mode_t)v);
}

// ALS wrappers
static uint32_t get_als_filter_mode(void) { return als_get_raw_mode() ? 1 : 0; }
static esp_err_t set_als_filter_mode(uint32_t v) {
  als_set_raw_mode(v != 0);
  return ESP_OK;
}

static uint32_t get_als_source(void) { return als_get_use_white_channel() ? 1 : 0; }
static esp_err_t set_als_source(uint32_t v) {
  als_set_use_white_channel(v != 0);
  return ESP_OK;
}

static uint32_t get_als_deadzone(void) { return als_get_deadzone(); }
static esp_err_t set_als_deadzone(uint32_t v) {
  als_set_deadzone((uint8_t)v);
  return ESP_OK;
}

// Tempo wrappers
static uint32_t get_sync_pulse_mode(void) { return (uint32_t)clock_sync_get_mode(); }
static esp_err_t set_sync_pulse_mode(uint32_t v) {
  clock_sync_set_mode((clock_sync_mode_t)v);
  return ESP_OK;
}

static uint32_t get_clock_output(void) { return (uint32_t)tempo_get_clock_output(); }
static esp_err_t set_clock_output(uint32_t v) {
  tempo_set_clock_output((clock_output_t)v);
  return ESP_OK;
}

static uint32_t get_clock_standard(void) { return (uint32_t)tempo_get_clock_standard(); }
static esp_err_t set_clock_standard(uint32_t v) {
  tempo_set_clock_standard((tempo_clock_standard_t)v);
  return ESP_OK;
}

static uint32_t get_always_send(void) { return tempo_get_clock_always_send() ? 1 : 0; }
static esp_err_t set_always_send(uint32_t v) {
  tempo_set_clock_always_send(v != 0);
  return ESP_OK;
}

static uint32_t get_disable_on_passthrough(void) { return tempo_get_disable_clock_on_passthrough() ? 1 : 0; }
static esp_err_t set_disable_on_passthrough(uint32_t v) {
  tempo_set_disable_clock_on_passthrough(v != 0);
  return ESP_OK;
}

static uint32_t get_bpm_deadzone(void) { return tempo_get_bpm_deadzone(); }
static esp_err_t set_bpm_deadzone(uint32_t v) {
  tempo_set_bpm_deadzone((uint8_t)v);
  return ESP_OK;
}

static uint32_t get_led_sync(void) { return tempo_get_led_sync() ? 1 : 0; }
static esp_err_t set_led_sync(uint32_t v) {
  tempo_set_led_sync(v != 0);
  return ESP_OK;
}

static uint32_t get_flash_duration(void) { return tempo_get_led_flash_ratio(); }
static esp_err_t set_flash_duration(uint32_t v) {
  tempo_set_led_flash_ratio((uint8_t)v);
  return ESP_OK;
}

// LED wrappers
static uint32_t get_led_enable(void) { return led_get_enabled() ? 1 : 0; }
static esp_err_t set_led_enable(uint32_t v) {
  led_set_enabled(v != 0);
  return ESP_OK;
}

static uint32_t get_led_mode(void) { return (uint32_t)led_get_mode(); }
static esp_err_t set_led_mode(uint32_t v) { return led_set_mode((led_mode_t)v); }

static uint32_t get_led_sundial(void) { return led_get_sundial_mode() ? 1 : 0; }
static esp_err_t set_led_sundial(uint32_t v) { return led_set_sundial_mode(v != 0); }

// Buttons wrappers
static uint32_t get_btn_debounce_delay(void) { return buttons_get_debounce(); }
static esp_err_t set_btn_debounce_delay(uint32_t v) { return buttons_set_debounce((uint16_t)v); }

static uint32_t get_btn_debounce_mode(void) { return buttons_get_debounce_mode(); }
static esp_err_t set_btn_debounce_mode(uint32_t v) { return buttons_set_debounce_mode((uint8_t)v); }

static uint32_t get_btn_release_debounce(void) { return buttons_get_debounce_release(); }
static esp_err_t set_btn_release_debounce(uint32_t v) { return buttons_set_debounce_release((uint16_t)v); }

static uint32_t get_btn_chord_window(void) { return buttons_get_chord_window(); }
static esp_err_t set_btn_chord_window(uint32_t v) { return buttons_set_chord_window((uint16_t)v); }

static uint32_t get_btn_long_press(void) { return buttons_get_long_press_threshold(); }
static esp_err_t set_btn_long_press(uint32_t v) { return buttons_set_long_press_threshold((uint16_t)v); }

static uint32_t get_btn_glitch_filter(void) { return buttons_get_glitch_filter_mode(); }
static esp_err_t set_btn_glitch_filter(uint32_t v) {
  // When setting mode, use current window value (or default if switching to Flex)
  uint32_t window = buttons_get_glitch_filter_window_ns();
  if (v == BUTTON_GLITCH_FILTER_MODE_FLEX && (window < 100 || window > 4000)) {
    window = 1000;  // Default for Flex mode
  }
  return buttons_set_glitch_filter((uint8_t)v, window);
}

static uint32_t get_btn_glitch_window(void) { return buttons_get_glitch_filter_window_ns(); }
static esp_err_t set_btn_glitch_window(uint32_t v) {
  return buttons_set_glitch_filter(buttons_get_glitch_filter_mode(), v);
}

// Bump wrappers
static uint32_t get_bump_sensitivity(void) { return bump_get_sensitivity_level(); }
static esp_err_t set_bump_sensitivity(uint32_t v) {
  bump_set_sensitivity_level((uint8_t)v);
  return ESP_OK;
}

static uint32_t get_bump_debounce(void) { return bump_get_debounce(); }
static esp_err_t set_bump_debounce(uint32_t v) {
  bump_set_debounce(v);
  return ESP_OK;
}

// Display wrappers
static uint32_t get_brightness(void) { return display_get_brightness(); }
static esp_err_t set_brightness(uint32_t v) { return display_set_brightness((uint8_t)v); }

static uint32_t get_screensaver_delay(void) {
  if (!screensaver_is_enabled()) return 0;
  return (uint32_t)screensaver_get_delay() * 1000;  // Convert seconds to ms
}
static esp_err_t set_screensaver_delay(uint32_t v) {
  if (v == 0) {
    screensaver_disable();
  } else {
    screensaver_set_delay((uint16_t)(v / 1000));  // Convert ms to seconds
    screensaver_enable();
  }
  return ESP_OK;
}

static uint32_t get_screensaver_mode(void) { return (uint32_t)screensaver_get_mode(); }
static esp_err_t set_screensaver_mode(uint32_t v) {
  screensaver_set_mode((screensaver_mode_t)v);
  return ESP_OK;
}

// ============================================================================
// Dispatch table
// ============================================================================

static const setting_entry_t s_settings[] = {
  // Config category
  {"config.scene_mode", get_scene_mode, set_scene_mode},
  {"config.device_mode", get_device_mode, set_device_mode},
  {"config.confirm_change", get_confirm_change, set_confirm_change},
  {"config.preset_wrap", get_preset_wrap, set_preset_wrap},
  {"config.persist_scene", get_persist_scene, set_persist_scene},
  {"config.flag_enabled", get_flag_enabled, set_flag_enabled},
  
  // Touch category
  {"touch.stuck_timeout", get_stuck_timeout, set_stuck_timeout},
  {"touch.idle_calibration", get_idle_calibration, set_idle_calibration},
  {"touch.menu_hold_time", get_menu_hold_time, set_menu_hold_time},
  
  // MIDI category
  {"midi.interface", get_midi_interface, set_midi_interface},
  {"midi.passthrough", get_midi_passthrough, set_midi_passthrough},
  {"midi.loopback", get_midi_loopback, set_midi_loopback},
  
  // Expression category
  {"expression.trs_polarity", get_expr_polarity, set_expr_polarity},
  {"expression.switch_type", get_expr_switch_type, set_expr_switch_type},
  {"expression.slow_delay", get_expr_slow_delay, set_expr_slow_delay},
  {"expression.menu_nav_mode", get_expr_menu_nav, set_expr_menu_nav},
  
  // CV category
  {"cv.range", get_cv_range, set_cv_range},
  {"cv.pitch_standard", get_cv_pitch_standard, set_cv_pitch_standard},
  {"cv.deadzone", get_cv_deadzone, set_cv_deadzone},
  
  // Proximity category
  {"proximity.hysteresis", get_prox_hysteresis, set_prox_hysteresis},
  {"proximity.rest_position", get_prox_rest_position, set_prox_rest_position},
  {"proximity.return_speed", get_prox_return_speed, set_prox_return_speed},
  {"proximity.note_silence", get_prox_note_silence, set_prox_note_silence},
  {"proximity.timeout", get_prox_timeout, set_prox_timeout},
  {"proximity.deadzone", get_prox_deadzone, set_prox_deadzone},
  {"proximity.ir_rejection", get_prox_ir_rejection, set_prox_ir_rejection},
  {"proximity.gamma", get_prox_gamma, set_prox_gamma},

  // Tilt category
  {"tilt.forgive_middle", get_tilt_forgive_middle, set_tilt_forgive_middle},
  {"tilt.middle_width", get_tilt_middle_width, set_tilt_middle_width},
  {"tilt.deadzone", get_tilt_deadzone, set_tilt_deadzone},
  {"tilt.rate_hz", get_tilt_rate_hz, set_tilt_rate_hz},
  {"tilt.x_invert", get_tilt_x_invert, set_tilt_x_invert},
  {"tilt.y_invert", get_tilt_y_invert, set_tilt_y_invert},
  {"tilt.note_off_mode", get_tilt_note_off_mode, set_tilt_note_off_mode},

  // MIDI Control category
  {"midi_control.enabled", get_midi_control_enabled, set_midi_control_enabled},
  {"midi_control.channel", get_midi_control_channel, set_midi_control_channel},
  {"midi_control.input", get_midi_control_input, set_midi_control_input},

  // Note Track category
  {"note_track.low_note", get_note_track_low_note, set_note_track_low_note},
  {"note_track.high_note", get_note_track_high_note, set_note_track_high_note},
  {"note_track.channel", get_note_track_channel, set_note_track_channel},
  {"note_track.filter_mode", get_note_track_filter_mode, set_note_track_filter_mode},

  // Theme category
  {"theme.theme", get_theme, set_theme},

  // Scene Inspect category
  {"scene_inspect.scroll_speed", get_scene_inspect_scroll_speed, set_scene_inspect_scroll_speed},
  {"scene_inspect.scroll_mode", get_scene_inspect_scroll_mode, set_scene_inspect_scroll_mode},
  
  // ALS category
  {"als.filter_mode", get_als_filter_mode, set_als_filter_mode},
  {"als.source", get_als_source, set_als_source},
  {"als.deadzone", get_als_deadzone, set_als_deadzone},
  
  // Tempo category
  {"tempo.sync_pulse_mode", get_sync_pulse_mode, set_sync_pulse_mode},
  {"tempo.clock_output", get_clock_output, set_clock_output},
  {"tempo.clock_standard", get_clock_standard, set_clock_standard},
  {"tempo.always_send", get_always_send, set_always_send},
  {"tempo.disable_on_passthrough", get_disable_on_passthrough, set_disable_on_passthrough},
  {"tempo.bpm_deadzone", get_bpm_deadzone, set_bpm_deadzone},
  {"tempo.led_sync", get_led_sync, set_led_sync},
  {"tempo.flash_duration", get_flash_duration, set_flash_duration},
  
  // LED category
  {"led.enable", get_led_enable, set_led_enable},
  {"led.mode", get_led_mode, set_led_mode},
  {"led.sundial", get_led_sundial, set_led_sundial},
  
  // Buttons category
  {"buttons.debounce_delay", get_btn_debounce_delay, set_btn_debounce_delay},
  {"buttons.debounce_mode", get_btn_debounce_mode, set_btn_debounce_mode},
  {"buttons.release_debounce", get_btn_release_debounce, set_btn_release_debounce},
  {"buttons.chord_window", get_btn_chord_window, set_btn_chord_window},
  {"buttons.long_press", get_btn_long_press, set_btn_long_press},
  {"buttons.glitch_filter", get_btn_glitch_filter, set_btn_glitch_filter},
  {"buttons.glitch_window", get_btn_glitch_window, set_btn_glitch_window},
  
  // Bump category
  {"bump.sensitivity", get_bump_sensitivity, set_bump_sensitivity},
  {"bump.debounce", get_bump_debounce, set_bump_debounce},
  
  // Display category
  {"display.brightness", get_brightness, set_brightness},
  {"display.screensaver_delay", get_screensaver_delay, set_screensaver_delay},
  {"display.screensaver_mode", get_screensaver_mode, set_screensaver_mode},
};

static const size_t s_settings_count = sizeof(s_settings) / sizeof(s_settings[0]);

// ============================================================================
// Public API implementation
// ============================================================================

static const setting_entry_t* find_setting(const char* id) {
  for (size_t i = 0; i < s_settings_count; i++) {
    if (strcmp(s_settings[i].id, id) == 0) {
      return &s_settings[i];
    }
  }
  return NULL;
}

esp_err_t settings_registry_get_value(const char* id, uint32_t* value) {
  if (!id || !value) return ESP_ERR_INVALID_ARG;
  
  const setting_entry_t* entry = find_setting(id);
  if (!entry) {
    ESP_LOGW(TAG, "Unknown setting: %s", id);
    return ESP_ERR_NOT_FOUND;
  }
  
  *value = entry->getter();
  return ESP_OK;
}

esp_err_t settings_registry_set_value(const char* id, uint32_t value) {
  if (!id) return ESP_ERR_INVALID_ARG;
  
  const setting_entry_t* entry = find_setting(id);
  if (!entry) {
    ESP_LOGW(TAG, "Unknown setting: %s", id);
    return ESP_ERR_NOT_FOUND;
  }
  
  return entry->setter(value);
}

esp_err_t settings_registry_get_all_values(char* buffer, size_t buffer_size, size_t* written) {
  if (!buffer || buffer_size < 3) return ESP_ERR_INVALID_ARG;
  
  size_t pos = 0;
  buffer[pos++] = '{';
  
  for (size_t i = 0; i < s_settings_count; i++) {
    const setting_entry_t* entry = &s_settings[i];
    uint32_t value = entry->getter();
    
    // Calculate required space: "id":value, (or "id":value} for last)
    char temp[128];
    int len = snprintf(temp, sizeof(temp), "%s\"%s\":%lu",
      (i > 0) ? "," : "",
      entry->id,
      (unsigned long)value);
    
    if (pos + len + 2 >= buffer_size) {
      ESP_LOGE(TAG, "Buffer too small for settings JSON");
      return ESP_ERR_NO_MEM;
    }
    
    memcpy(buffer + pos, temp, len);
    pos += len;
  }
  
  buffer[pos++] = '}';
  buffer[pos] = '\0';
  
  if (written) *written = pos;
  return ESP_OK;
}

bool settings_registry_exists(const char* id) {
  return find_setting(id) != NULL;
}

size_t settings_registry_count(void) {
  return s_settings_count;
}
