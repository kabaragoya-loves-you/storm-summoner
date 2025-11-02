/**
 * MIDI Device Identity Handler
 * 
 * Responds to Universal System Exclusive Identity Request messages.
 */

#include "midi_identity.h"
#include "midi_messages.h"
#include "esp_log.h"
#include "revision.h"

#define TAG "MIDI_IDENTITY"

// Universal SysEx Identity Request: F0 7E 7F 06 01 F7
#define IDENTITY_REQUEST_LEN 6
static const uint8_t IDENTITY_REQUEST[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};

// Our manufacturer ID (using educational/development ID)
// 0x00 0x00 0x00 is reserved for educational/non-commercial use
#define MANUFACTURER_ID_1 0x00
#define MANUFACTURER_ID_2 0x00
#define MANUFACTURER_ID_3 0x00

// Device family and member (arbitrary for custom device)
#define DEVICE_FAMILY_LSB 0x01
#define DEVICE_FAMILY_MSB 0x00
#define DEVICE_MEMBER_LSB 0x01
#define DEVICE_MEMBER_MSB 0x00

bool midi_identity_is_request(const uint8_t *sysex_data, size_t length) {
  if (!sysex_data || length != IDENTITY_REQUEST_LEN) return false;
  
  // Check if it matches the identity request pattern
  for (size_t i = 0; i < IDENTITY_REQUEST_LEN; i++) {
    if (sysex_data[i] != IDENTITY_REQUEST[i]) return false;
  }
  
  return true;
}

void midi_identity_send_reply(void) {
  // Get software version from revision component
  const char* version_str = revision_get_string();
  
  // Build Identity Reply message:
  // F0 7E 7F 06 02 [Manufacturer ID (3 bytes)] [Device Family (2 bytes)] 
  // [Device Member (2 bytes)] [Software Revision (4 bytes)] F7
  
  uint8_t reply[15];
  int idx = 0;
  
  reply[idx++] = 0xF0;  // SysEx start
  reply[idx++] = 0x7E;  // Universal Non-Real Time
  reply[idx++] = 0x7F;  // Device ID (broadcast)
  reply[idx++] = 0x06;  // General Information
  reply[idx++] = 0x02;  // Identity Reply (not request)
  
  // Manufacturer ID (3 bytes) - Educational/Development use
  reply[idx++] = MANUFACTURER_ID_1;
  reply[idx++] = MANUFACTURER_ID_2;
  reply[idx++] = MANUFACTURER_ID_3;
  
  // Device Family (2 bytes, LSB first)
  reply[idx++] = DEVICE_FAMILY_LSB;
  reply[idx++] = DEVICE_FAMILY_MSB;
  
  // Device Family Member (2 bytes, LSB first)
  reply[idx++] = DEVICE_MEMBER_LSB;
  reply[idx++] = DEVICE_MEMBER_MSB;
  
  // Software Revision (4 bytes)
  // Parse version string (e.g., "Rev A", "Rev B")
  // For simplicity, use first 4 characters or pad with zeros
  for (int i = 0; i < 4; i++) {
    if (version_str && version_str[i] != '\0') {
      // Convert to valid data byte (0-127)
      reply[idx++] = (uint8_t)(version_str[i] & 0x7F);
    } else {
      reply[idx++] = 0x00;
    }
  }
  
  reply[idx++] = 0xF7;  // SysEx end
  
  // Send the reply using send_sysex
  send_sysex(reply, idx);
  
  ESP_LOGI(TAG, "Sent Identity Reply: Storm Summoner %s", version_str ? version_str : "unknown");
}

void midi_identity_handle_request(const uint8_t *sysex_data, size_t length) {
  if (midi_identity_is_request(sysex_data, length)) {
    ESP_LOGI(TAG, "Received Identity Request - responding...");
    midi_identity_send_reply();
  }
}

