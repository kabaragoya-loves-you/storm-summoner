#include "midi_sysex_update.h"
#include "firmware_update.h"
#include "midi_messages.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#define TAG "SYSEX_UPDATE"

// Maximum chunk size (SysEx payload without header/footer)
#define MAX_CHUNK_SIZE 128

// Buffer for accumulating firmware/assets data
#define UPDATE_BUFFER_SIZE (1024 * 1024)  // 1MB buffer for chunks
static uint8_t *s_update_buffer = NULL;
static size_t s_buffer_used = 0;
static size_t s_expected_total = 0;
static bool s_update_in_progress = false;
static bool s_is_firmware = false;  // true=firmware, false=assets

esp_err_t midi_sysex_update_init(void) {
  ESP_LOGI(TAG, "SysEx update handler initialized");
  
  // Allocate update buffer
  s_update_buffer = malloc(UPDATE_BUFFER_SIZE);
  if (!s_update_buffer) {
    ESP_LOGE(TAG, "Failed to allocate update buffer");
    return ESP_ERR_NO_MEM;
  }
  
  return ESP_OK;
}

esp_err_t midi_sysex_update_process(const uint8_t *data, size_t len) {
  if (!data || len < 6) {
    ESP_LOGW(TAG, "Invalid SysEx message length");
    return ESP_ERR_INVALID_ARG;
  }

  // Verify SysEx start/end bytes
  if (data[0] != 0xF0 || data[len-1] != 0xF7) {
    ESP_LOGW(TAG, "Invalid SysEx framing");
    return ESP_ERR_INVALID_ARG;
  }

  // Check manufacturer ID
  if (data[1] != SYSEX_MANUFACTURER_ID) {
    ESP_LOGD(TAG, "SysEx not for us (ID: 0x%02X)", data[1]);
    return ESP_OK;  // Not an error, just not our message
  }

  // Extract device ID (data[2]) and command (data[3])
  uint8_t device_id = data[2];
  uint8_t cmd = data[3];

  ESP_LOGI(TAG, "SysEx update command: 0x%02X, device: 0x%02X, len: %u",
    cmd, device_id, (unsigned)len);

  switch (cmd) {
    case SYSEX_CMD_FIRMWARE_CHUNK: {
      // Format: F0 7D [dev] 01 [seq_hi] [seq_lo] [data...] F7
      if (len < 8) {
        ESP_LOGW(TAG, "Firmware chunk too short");
        return ESP_ERR_INVALID_ARG;
      }

      uint16_t seq = (data[4] << 7) | data[5];
      size_t chunk_len = len - 7;  // Subtract header + footer
      
      ESP_LOGI(TAG, "Firmware chunk seq=%u, len=%u", seq, (unsigned)chunk_len);

      if (!s_update_in_progress) {
        s_update_in_progress = true;
        s_is_firmware = true;
        s_buffer_used = 0;
        ESP_LOGI(TAG, "Starting firmware update via SysEx");
      }

      // Copy chunk to buffer
      if (s_buffer_used + chunk_len <= UPDATE_BUFFER_SIZE) {
        memcpy(s_update_buffer + s_buffer_used, &data[6], chunk_len);
        s_buffer_used += chunk_len;
      } else {
        ESP_LOGE(TAG, "Update buffer overflow");
        s_update_in_progress = false;
        return ESP_ERR_NO_MEM;
      }

      midi_sysex_send_status(0x00);  // ACK
      break;
    }

    case SYSEX_CMD_ASSETS_CHUNK: {
      // Format: F0 7D [dev] 02 [seq_hi] [seq_lo] [data...] F7
      if (len < 8) {
        ESP_LOGW(TAG, "Assets chunk too short");
        return ESP_ERR_INVALID_ARG;
      }

      uint16_t seq = (data[4] << 7) | data[5];
      size_t chunk_len = len - 7;
      
      ESP_LOGI(TAG, "Assets chunk seq=%u, len=%u", seq, (unsigned)chunk_len);

      if (!s_update_in_progress) {
        s_update_in_progress = true;
        s_is_firmware = false;
        s_buffer_used = 0;
        ESP_LOGI(TAG, "Starting assets update via SysEx");
      }

      // Copy chunk to buffer
      if (s_buffer_used + chunk_len <= UPDATE_BUFFER_SIZE) {
        memcpy(s_update_buffer + s_buffer_used, &data[6], chunk_len);
        s_buffer_used += chunk_len;
      } else {
        ESP_LOGE(TAG, "Update buffer overflow");
        s_update_in_progress = false;
        return ESP_ERR_NO_MEM;
      }

      midi_sysex_send_status(0x00);  // ACK
      break;
    }

    case SYSEX_CMD_FIRMWARE_COMMIT: {
      // Format: F0 7D [dev] 03 00 00 F7
      if (!s_update_in_progress || !s_is_firmware) {
        ESP_LOGW(TAG, "No firmware update in progress");
        midi_sysex_send_status(0x02);  // NAK - wrong state
        return ESP_ERR_INVALID_STATE;
      }

      ESP_LOGI(TAG, "Committing firmware update (%u bytes)", (unsigned)s_buffer_used);

      // Perform firmware update
      esp_err_t err = firmware_update_start(s_update_buffer, s_buffer_used);
      if (err == ESP_OK) {
        err = firmware_update_write(s_update_buffer, s_buffer_used);
      }
      if (err == ESP_OK) {
        err = firmware_update_finalize();
      }

      if (err == ESP_OK) {
        ESP_LOGI(TAG, "Firmware update successful!");
        midi_sysex_send_status(0x01);  // Success
      } else {
        ESP_LOGE(TAG, "Firmware update failed: %s", esp_err_to_name(err));
        midi_sysex_send_status(0x03);  // Error
      }

      s_update_in_progress = false;
      s_buffer_used = 0;
      break;
    }

    case SYSEX_CMD_ASSETS_COMMIT: {
      // Format: F0 7D [dev] 04 00 00 F7
      if (!s_update_in_progress || s_is_firmware) {
        ESP_LOGW(TAG, "No assets update in progress");
        midi_sysex_send_status(0x02);  // NAK - wrong state
        return ESP_ERR_INVALID_STATE;
      }

      ESP_LOGI(TAG, "Committing assets update (%u bytes)", (unsigned)s_buffer_used);

      // Perform assets update
      esp_err_t err = assets_update_start(s_update_buffer, s_buffer_used);
      if (err == ESP_OK) {
        err = assets_update_write(s_update_buffer, s_buffer_used);
      }
      if (err == ESP_OK) {
        err = assets_update_finalize();
      }

      if (err == ESP_OK) {
        ESP_LOGI(TAG, "Assets update successful!");
        midi_sysex_send_status(0x01);  // Success
      } else {
        ESP_LOGE(TAG, "Assets update failed: %s", esp_err_to_name(err));
        midi_sysex_send_status(0x03);  // Error
      }

      s_update_in_progress = false;
      s_buffer_used = 0;
      break;
    }

    case SYSEX_CMD_STATUS_REQUEST: {
      // Format: F0 7D [dev] 05 00 00 F7
      uint8_t progress = midi_sysex_update_get_progress();
      ESP_LOGI(TAG, "Status request - progress: %u%%", progress);
      midi_sysex_send_status(progress);
      break;
    }

    default:
      ESP_LOGW(TAG, "Unknown SysEx command: 0x%02X", cmd);
      return ESP_ERR_NOT_SUPPORTED;
  }

  return ESP_OK;
}

uint8_t midi_sysex_update_get_progress(void) {
  if (!s_update_in_progress) return 0;
  if (s_expected_total == 0) return 0;
  return (uint8_t)((s_buffer_used * 100) / s_expected_total);
}

esp_err_t midi_sysex_send_status(uint8_t status_code) {
  // Format: F0 7D 00 06 [status] F7
  uint8_t response[] = {
    0xF0,
    SYSEX_MANUFACTURER_ID,
    0x00,  // Device ID (broadcast)
    SYSEX_CMD_STATUS_RESPONSE,
    status_code & 0x7F,
    0xF7
  };
  
  send_sysex(&response[1], sizeof(response) - 2);  // send_sysex adds F0/F7
  return ESP_OK;
}

