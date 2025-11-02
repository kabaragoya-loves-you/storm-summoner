#include "usb_mode_manager.h"
#include "midi_out_usb.h"
#include "midi_in_usb.h"
#include "firmware_update.h"
#include "usb_descriptors.h"  // For descriptor switching
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "tusb.h"
#include <string.h>

#define TAG "USB_MODE"

// MSC disk configuration
#define MSC_DISK_SIZE_MB 12
#define MSC_BLOCK_SIZE 512
#define MSC_BLOCK_COUNT ((MSC_DISK_SIZE_MB * 1024 * 1024) / MSC_BLOCK_SIZE)

static usb_mode_t s_current_mode = USB_MODE_MIDI;
static SemaphoreHandle_t s_mode_mutex = NULL;
static bool s_initialized = false;

// PSRAM-backed virtual disk for MSC mode
static uint8_t *s_disk_buffer = NULL;

// Forward declarations
static void init_fat_filesystem(void);

esp_err_t usb_mode_manager_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "USB mode manager already initialized");
    return ESP_OK;
  }

  s_mode_mutex = xSemaphoreCreateMutex();
  if (!s_mode_mutex) {
    ESP_LOGE(TAG, "Failed to create mode mutex");
    return ESP_ERR_NO_MEM;
  }

  // Allocate PSRAM buffer for MSC disk
  s_disk_buffer = heap_caps_malloc(MSC_BLOCK_SIZE * MSC_BLOCK_COUNT, MALLOC_CAP_SPIRAM);
  if (!s_disk_buffer) {
    ESP_LOGE(TAG, "Failed to allocate %dMB PSRAM for MSC disk", MSC_DISK_SIZE_MB);
    vSemaphoreDelete(s_mode_mutex);
    return ESP_ERR_NO_MEM;
  }

  // Initialize with empty FAT filesystem
  init_fat_filesystem();

  s_initialized = true;
  ESP_LOGI(TAG, "USB mode manager initialized in MIDI mode (MSC buffer: %dMB @ %p)",
    MSC_DISK_SIZE_MB, s_disk_buffer);
  return ESP_OK;
}

usb_mode_t usb_mode_get_current(void) {
  usb_mode_t mode = USB_MODE_MIDI;
  if (s_mode_mutex && xSemaphoreTake(s_mode_mutex, portMAX_DELAY) == pdPASS) {
    mode = s_current_mode;
    xSemaphoreGive(s_mode_mutex);
  }
  return mode;
}

esp_err_t usb_switch_to_midi(void) {
  if (!s_initialized) return ESP_ERR_INVALID_STATE;

  if (xSemaphoreTake(s_mode_mutex, portMAX_DELAY) != pdPASS) return ESP_ERR_TIMEOUT;

  if (s_current_mode == USB_MODE_MIDI) {
    xSemaphoreGive(s_mode_mutex);
    ESP_LOGI(TAG, "Already in MIDI mode");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Switching from MSC to MIDI mode...");

  // Step 1: Disconnect USB
  if (tud_mounted()) {
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100)); // Give time for disconnect
  }

  // Step 2: Switch to MIDI descriptor
  usb_descriptors_set_midi_mode();

  // Step 3: Reinitialize MIDI interfaces
  midi_out_usb_init();
  midi_in_usb_init();

  // Step 4: Reconnect USB with MIDI descriptor
  tud_connect();
  vTaskDelay(pdMS_TO_TICKS(100)); // Give time for enumeration

  s_current_mode = USB_MODE_MIDI;
  xSemaphoreGive(s_mode_mutex);

  ESP_LOGI(TAG, "Switched to MIDI mode");
  return ESP_OK;
}

esp_err_t usb_switch_to_msc(void) {
  if (!s_initialized) return ESP_ERR_INVALID_STATE;

  if (xSemaphoreTake(s_mode_mutex, portMAX_DELAY) != pdPASS) return ESP_ERR_TIMEOUT;

  if (s_current_mode == USB_MODE_MSC) {
    xSemaphoreGive(s_mode_mutex);
    ESP_LOGI(TAG, "Already in MSC mode");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Switching from MIDI to MSC mode...");

  // Step 1: Disconnect USB
  if (tud_mounted()) {
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100)); // Give time for disconnect
  }

  // Step 2: Deinitialize USB MIDI
  midi_out_usb_deinit();
  midi_in_usb_deinit();

  // Step 3: Switch to MSC descriptor
  usb_descriptors_set_msc_mode();

  // Step 4: Reinitialize FAT filesystem for new session
  init_fat_filesystem();

  // Step 5: Reconnect USB with MSC descriptor
  tud_connect();
  vTaskDelay(pdMS_TO_TICKS(100)); // Give time for enumeration

  s_current_mode = USB_MODE_MSC;
  xSemaphoreGive(s_mode_mutex);

  ESP_LOGI(TAG, "Switched to MSC mode - UART MIDI remains active");
  return ESP_OK;
}

bool usb_mode_is_ready(void) {
  if (!s_initialized) return false;
  return tud_mounted();
}

// Initialize a basic FAT12 filesystem in the disk buffer
static void init_fat_filesystem(void) {
  if (!s_disk_buffer) return;

  // Clear the buffer
  memset(s_disk_buffer, 0, MSC_BLOCK_SIZE * MSC_BLOCK_COUNT);

  // Create a minimal FAT12 boot sector
  // This is a simplified implementation - for production, use a proper FAT library
  uint8_t *boot_sector = s_disk_buffer;
  
  // Jump instruction and OEM name
  boot_sector[0] = 0xEB; boot_sector[1] = 0x3C; boot_sector[2] = 0x90;
  memcpy(&boot_sector[3], "STORM   ", 8);
  
  // Bytes per sector (512)
  boot_sector[11] = 0x00; boot_sector[12] = 0x02;
  
  // Sectors per cluster (1)
  boot_sector[13] = 0x01;
  
  // Reserved sectors (1)
  boot_sector[14] = 0x01; boot_sector[15] = 0x00;
  
  // Number of FATs (2)
  boot_sector[16] = 0x02;
  
  // Root entries (224)
  boot_sector[17] = 0xE0; boot_sector[18] = 0x00;
  
  // Total sectors
  uint16_t total_sectors = MSC_BLOCK_COUNT;
  boot_sector[19] = total_sectors & 0xFF;
  boot_sector[20] = (total_sectors >> 8) & 0xFF;
  
  // Media descriptor (0xF8 = hard disk)
  boot_sector[21] = 0xF8;
  
  // Sectors per FAT
  boot_sector[22] = 0x30; boot_sector[23] = 0x00;
  
  // Boot signature
  boot_sector[510] = 0x55; boot_sector[511] = 0xAA;

  ESP_LOGI(TAG, "Initialized FAT12 filesystem (%d blocks, %d KB)",
    MSC_BLOCK_COUNT, (MSC_BLOCK_COUNT * MSC_BLOCK_SIZE) / 1024);
}

// TinyUSB MSC callbacks
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
  const char vid[] = "Storm";
  const char pid[] = "Summoner";
  const char rev[] = "1.0";

  memcpy(vendor_id, vid, strlen(vid));
  memcpy(product_id, pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  return s_current_mode == USB_MODE_MSC && s_disk_buffer != NULL;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
  *block_count = MSC_BLOCK_COUNT;
  *block_size = MSC_BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (s_current_mode != USB_MODE_MSC || !s_disk_buffer) return -1;
  
  uint32_t addr = lba * MSC_BLOCK_SIZE + offset;
  if (addr + bufsize > MSC_BLOCK_SIZE * MSC_BLOCK_COUNT) return -1;

  memcpy(buffer, s_disk_buffer + addr, bufsize);
  return bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
  return s_current_mode == USB_MODE_MSC;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  if (s_current_mode != USB_MODE_MSC || !s_disk_buffer) return -1;
  
  uint32_t addr = lba * MSC_BLOCK_SIZE + offset;
  if (addr + bufsize > MSC_BLOCK_SIZE * MSC_BLOCK_COUNT) return -1;

  memcpy(s_disk_buffer + addr, buffer, bufsize);
  
  // TODO: Detect file writes and process firmware/assets updates
  // This requires parsing FAT filesystem structures to detect file closes
  
  return bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
  void const *response = NULL;
  int32_t resplen = 0;

  switch (scsi_cmd[0]) {
    default:
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
      resplen = -1;
      break;
  }

  if (resplen > bufsize) resplen = bufsize;
  if (response && (resplen > 0)) {
    memcpy(buffer, response, resplen);
  }

  return resplen;
}

