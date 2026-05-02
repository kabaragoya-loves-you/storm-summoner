/**
 * MIDI Passthrough Handler
 * 
 * Provides bidirectional MIDI passthrough:
 * - USB IN → UART OUT
 * - UART IN → USB OUT
 * 
 * This allows the Storm Summoner to act as a MIDI interface/bridge.
 */

#include "midi_passthrough.h"
#include "midi_out.h"
#include "midi_out_uart.h"
#include "midi_out_usb.h"
#include "app_settings.h"
#include "event_bus.h"
#include "note_track_config.h"
#include "esp_log.h"

#define TAG "MIDI_PASSTHROUGH"

// NVS keys
#define NVS_KEY_USB_TO_UART "pt_usb_uart"
#define NVS_KEY_UART_TO_USB "pt_uart_usb"

static bool s_usb_to_uart_enabled = false;
static bool s_uart_to_usb_enabled = false;
static bool s_initialized = false;

// Track statistics
static struct {
  uint32_t usb_to_uart_bytes;
  uint32_t uart_to_usb_bytes;
  uint32_t usb_to_uart_messages;
  uint32_t uart_to_usb_messages;
} passthrough_stats = {0};

esp_err_t midi_passthrough_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "MIDI passthrough already initialized");
    return ESP_OK;
  }

  // Load settings from NVS
  bool usb_to_uart = false;
  bool uart_to_usb = false;
  
  esp_err_t ret = app_settings_load_bool(NVS_KEY_USB_TO_UART, &usb_to_uart);
  if (ret == ESP_OK) {
    s_usb_to_uart_enabled = usb_to_uart;
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    // Default: disabled
    s_usb_to_uart_enabled = false;
  } else {
    ESP_LOGW(TAG, "Failed to load USB→UART setting: %s", esp_err_to_name(ret));
  }
  
  ret = app_settings_load_bool(NVS_KEY_UART_TO_USB, &uart_to_usb);
  if (ret == ESP_OK) {
    s_uart_to_usb_enabled = uart_to_usb;
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    // Default: disabled
    s_uart_to_usb_enabled = false;
  } else {
    ESP_LOGW(TAG, "Failed to load UART→USB setting: %s", esp_err_to_name(ret));
  }

  s_initialized = true;
  
  ESP_LOGI(TAG, "MIDI passthrough initialized - USB→UART: %s, UART→USB: %s",
    s_usb_to_uart_enabled ? "ON" : "OFF",
    s_uart_to_usb_enabled ? "ON" : "OFF");
  
  return ESP_OK;
}

void midi_passthrough_usb_to_uart_enable(bool enable) {
  s_usb_to_uart_enabled = enable;
  
  // Save to NVS
  esp_err_t ret = app_settings_save_bool(NVS_KEY_USB_TO_UART, enable);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to save USB→UART setting to NVS: %s", esp_err_to_name(ret));
  }
  
  ESP_LOGI(TAG, "USB → UART passthrough: %s", enable ? "ENABLED" : "DISABLED");
}

void midi_passthrough_uart_to_usb_enable(bool enable) {
  s_uart_to_usb_enabled = enable;
  
  // Save to NVS
  esp_err_t ret = app_settings_save_bool(NVS_KEY_UART_TO_USB, enable);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to save UART→USB setting to NVS: %s", esp_err_to_name(ret));
  }
  
  ESP_LOGI(TAG, "UART → USB passthrough: %s", enable ? "ENABLED" : "DISABLED");
}

bool midi_passthrough_usb_to_uart_is_enabled(void) {
  return s_usb_to_uart_enabled;
}

bool midi_passthrough_uart_to_usb_is_enabled(void) {
  return s_uart_to_usb_enabled;
}

void midi_passthrough_forward_from_usb(const uint8_t *data, size_t len) {
  if (!s_usb_to_uart_enabled || !data || len == 0) return;
  
  // Check if passthrough is cut
  if (midi_out_get_cut_passthrough()) return;
  
  // Forward USB MIDI data to UART
  midi_out_uart_send(data, len);
  
  passthrough_stats.usb_to_uart_bytes += len;
  passthrough_stats.usb_to_uart_messages++;
  
  ESP_LOGD(TAG, "Forwarded %d bytes from USB → UART", len);
}

void midi_passthrough_forward_from_uart(const uint8_t *data, size_t len) {
  if (!s_uart_to_usb_enabled || !data || len == 0) return;
  
  // Check if passthrough is cut
  if (midi_out_get_cut_passthrough()) return;
  
  // Forward UART MIDI data to USB
  if (midi_out_usb_is_connected()) {
    midi_out_usb_send(data, len);
    
    passthrough_stats.uart_to_usb_bytes += len;
    passthrough_stats.uart_to_usb_messages++;
    
    ESP_LOGD(TAG, "Forwarded %d bytes from UART → USB", len);
  }
}

// Per-message-filter state (one per source direction).
//
// When Note Track is in "kill" mode we cannot just blast the raw input chunk to
// the other interface, because we may need to drop a 3-byte note message that
// straddles a chunk boundary or that piggybacks on running status. This little
// state machine walks the byte stream, identifies complete channel-voice
// messages, and decides whether to emit them. It always emits explicit status
// bytes (collapsing running status), so dropped messages don't break the next
// receiver-side running-status assumption.
typedef struct {
  // Channel-voice message in flight
  uint8_t cv_status;       // 0x80..0xEF, or 0 when not in a channel message
  uint8_t cv_data[2];
  uint8_t cv_count;
  uint8_t cv_expected;

  // System common in flight (forwarded as-is, but we need to know expected len)
  uint8_t sc_status;       // 0xF1..0xF3, or 0
  uint8_t sc_count;
  uint8_t sc_expected;

  bool in_sysex;
} pt_filter_state_t;

static pt_filter_state_t s_uart_filter_state;  // bytes coming from UART
static pt_filter_state_t s_usb_filter_state;   // bytes coming from USB

static void filter_emit(uint8_t source, const uint8_t* bytes, size_t n) {
  if (n == 0) return;
  if (midi_out_get_cut_passthrough()) return;

  if (source == MIDI_SOURCE_UART) {
    if (!s_uart_to_usb_enabled) return;
    if (midi_out_usb_is_connected()) {
      midi_out_usb_send(bytes, n);
      passthrough_stats.uart_to_usb_bytes += n;
      passthrough_stats.uart_to_usb_messages++;
    }
  } else if (source == MIDI_SOURCE_USB) {
    if (!s_usb_to_uart_enabled) return;
    midi_out_uart_send(bytes, n);
    passthrough_stats.usb_to_uart_bytes += n;
    passthrough_stats.usb_to_uart_messages++;
  }
}

static void filter_byte(pt_filter_state_t* s, uint8_t source, uint8_t byte) {
  // Realtime: forward immediately, do not affect any state machines.
  if (byte >= 0xF8) {
    filter_emit(source, &byte, 1);
    return;
  }

  // Inside SysEx: pass through every byte until 0xF7.
  if (s->in_sysex) {
    filter_emit(source, &byte, 1);
    if (byte == 0xF7) s->in_sysex = false;
    return;
  }

  // SysEx start: clear any running channel-voice state, forward F0.
  if (byte == 0xF0) {
    s->in_sysex = true;
    s->cv_status = 0;
    s->cv_count = 0;
    s->sc_status = 0;
    filter_emit(source, &byte, 1);
    return;
  }

  // System Common (0xF1..0xF7).
  if (byte >= 0xF0) {
    s->cv_status = 0;
    s->cv_count = 0;

    if (byte == 0xF7) {
      // Stray EOX outside SysEx: forward as-is, drop state.
      s->sc_status = 0;
      filter_emit(source, &byte, 1);
      return;
    }

    s->sc_status = byte;
    s->sc_count = 0;
    switch (byte) {
      case 0xF1: case 0xF3: s->sc_expected = 1; break;
      case 0xF2: s->sc_expected = 2; break;
      default:   s->sc_expected = 0; s->sc_status = 0; break;
    }
    filter_emit(source, &byte, 1);
    return;
  }

  // Channel-voice status byte.
  if (byte & 0x80) {
    s->cv_status = byte;
    s->cv_count = 0;
    s->sc_status = 0;
    uint8_t nibble = byte & 0xF0;
    switch (nibble) {
      case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0:
        s->cv_expected = 2; break;
      case 0xC0: case 0xD0:
        s->cv_expected = 1; break;
      default:
        s->cv_expected = 0; break;
    }
    // Don't emit yet; wait for full message so we can decide drop/keep.
    return;
  }

  // System common data byte.
  if (s->sc_status) {
    filter_emit(source, &byte, 1);
    if (s->sc_count < 2) s->sc_count++;
    if (s->sc_count >= s->sc_expected) {
      s->sc_status = 0;
    }
    return;
  }

  // Channel-voice data byte (uses running status when cv_status persists).
  if (s->cv_status) {
    if (s->cv_count < 2) s->cv_data[s->cv_count++] = byte;

    if (s->cv_count >= s->cv_expected) {
      uint8_t nibble = s->cv_status & 0xF0;
      uint8_t channel0 = s->cv_status & 0x0F;

      bool drop = false;
      if (nibble == 0x80 || nibble == 0x90) {
        if (note_track_message_matches(channel0, s->cv_data[0])) {
          drop = true;
        }
      }

      if (!drop) {
        // Always emit explicit status (collapses running status). This keeps
        // us correct when previous messages were dropped.
        uint8_t out[3];
        out[0] = s->cv_status;
        for (uint8_t i = 0; i < s->cv_expected; i++) out[1 + i] = s->cv_data[i];
        filter_emit(source, out, 1u + s->cv_expected);
      }

      // Stay in running status (cv_status preserved); reset data count.
      s->cv_count = 0;
    }
    return;
  }

  // Stray data byte without status: drop silently (parser also discards).
}

void midi_passthrough_forward_filtered(uint8_t source, const uint8_t* data, size_t len) {
  if (!data || len == 0) return;

  pt_filter_state_t* s;
  bool enabled;
  if (source == MIDI_SOURCE_UART) {
    s = &s_uart_filter_state;
    enabled = s_uart_to_usb_enabled;
  } else if (source == MIDI_SOURCE_USB) {
    s = &s_usb_filter_state;
    enabled = s_usb_to_uart_enabled;
  } else {
    return;
  }

  // When the destination direction is disabled, still walk the bytes so the
  // filter state stays coherent for when it gets re-enabled mid-stream.
  if (!enabled) {
    for (size_t i = 0; i < len; i++) (void)data[i];
    return;
  }

  for (size_t i = 0; i < len; i++) filter_byte(s, source, data[i]);
}

void midi_passthrough_get_stats(
  uint32_t *usb_to_uart_bytes,
  uint32_t *uart_to_usb_bytes,
  uint32_t *usb_to_uart_messages,
  uint32_t *uart_to_usb_messages
) {
  if (usb_to_uart_bytes) *usb_to_uart_bytes = passthrough_stats.usb_to_uart_bytes;
  if (uart_to_usb_bytes) *uart_to_usb_bytes = passthrough_stats.uart_to_usb_bytes;
  if (usb_to_uart_messages) *usb_to_uart_messages = passthrough_stats.usb_to_uart_messages;
  if (uart_to_usb_messages) *uart_to_usb_messages = passthrough_stats.uart_to_usb_messages;
}

void midi_passthrough_reset_stats(void) {
  passthrough_stats.usb_to_uart_bytes = 0;
  passthrough_stats.uart_to_usb_bytes = 0;
  passthrough_stats.usb_to_uart_messages = 0;
  passthrough_stats.uart_to_usb_messages = 0;
  ESP_LOGI(TAG, "Statistics reset");
}

