#include "midi_in_usb.h"
#include "midi_in.h"
#include "midi_passthrough.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

#define TAG "MIDI_IN_USB"

static bool s_initialized = false;

esp_err_t midi_in_usb_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "USB MIDI IN already initialized");
    return ESP_OK;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "USB MIDI IN initialized");
  return ESP_OK;
}

void midi_in_usb_deinit(void) {
  if (!s_initialized) return;
  
  s_initialized = false;
  ESP_LOGI(TAG, "USB MIDI IN deinitialized");
}

bool midi_in_usb_is_initialized(void) {
  return s_initialized;
}

void midi_in_usb_process_packet(const uint8_t *packet, size_t len) {
  if (!s_initialized || len == 0) return;
  
  // USB MIDI packets are in 4-byte format
  // Byte 0: Cable Number (4 bits) + Code Index (4 bits)
  // Bytes 1-3: MIDI data
  
  // For stream_read, TinyUSB already extracts just the MIDI bytes
  // so we can pass directly to the MIDI IN processor with USB source
  midi_in_process_stream(packet, len, MIDI_SOURCE_USB);
}

// TinyUSB callback for incoming MIDI data
void tud_midi_rx_cb(uint8_t itf) {
  if (!s_initialized) return;
  
  uint8_t packet[64];
  while (tud_midi_available()) {
    uint32_t bytes_read = tud_midi_stream_read(packet, sizeof(packet));
    if (bytes_read > 0) {
      ESP_LOGD(TAG, "Received %lu MIDI bytes from USB", (unsigned long)bytes_read);
      midi_in_usb_process_packet(packet, bytes_read);
    }
  }
}

