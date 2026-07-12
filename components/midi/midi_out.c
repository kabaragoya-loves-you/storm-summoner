#include "midi_out.h"
#include "midi_out_uart.h"
#include "midi_out_usb.h"
#include "device_config.h"
#include "assets_manager.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs.h"
#include "app_settings.h"
#include "task_priorities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define TAG "MIDI_OUT"
#define MIDI_QUEUE_LENGTH   50
#define MIDI_QUEUE_ITEM_SIZE sizeof(midi_out_job_t *)

// Staleness thresholds (queue age at dequeue)
#define MIDI_STALE_CLOCK_US   20000
#define MIDI_STALE_NOTE_ON_US 50000

// NVS Keys
#define NVS_KEY_MIDI_MODE "midi_mode"
#define NVS_KEY_OUT_INTERFACE "midi_out_iface"
#define NVS_KEY_UART_TEMPO "midi_uart_tempo"
#define NVS_KEY_UART_TRANS "midi_uart_trans"
#define NVS_KEY_USB_TEMPO "midi_usb_tempo"
#define NVS_KEY_USB_TRANS "midi_usb_trans"

static QueueHandle_t   midi_out_queue  = NULL;
static SemaphoreHandle_t midi_out_mutex = NULL;
static midi_out_config_t s_config = {0};

// Cut state (temporary runtime mute, not persisted)
static bool s_cut_local = false;       // Cut locally-generated MIDI messages
static bool s_cut_passthrough = false; // Cut passthrough MIDI messages
static midi_out_cdc_mirror_fn s_cdc_mirror_fn = NULL;

static uint32_t s_queue_hwm = 0;
static uint32_t s_drop_clock = 0;
static uint32_t s_drop_note_on = 0;
static uint32_t s_drop_overflow = 0;
static uint32_t s_max_job_age_us = 0;

static void midi_out_task(void *pvParameters);
static void load_config_from_nvs(void);
static void save_config_to_nvs(void);

void midi_out_init(void) {
  if (midi_out_queue != NULL) {
    ESP_LOGW(TAG, "MIDI queue already initialized");
    return;
  }

  // Load configuration from NVS
  load_config_from_nvs();

  // Initialize UART MIDI if enabled
  if (s_config.active_interfaces & MIDI_OUT_INTERFACE_UART) {
    esp_err_t ret = midi_out_uart_init();
    if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to initialize UART MIDI: %s", esp_err_to_name(ret));
    
    // Get TRS type from device_config (source of truth) and apply to UART
    midi_trs_type_t trs_type = device_config_get_trs_type();
    midi_transmit_mode_t mode = (midi_transmit_mode_t)assets_trs_type_to_transmit_mode(trs_type);
    midi_out_uart_set_mode(mode);
    ESP_LOGI(TAG, "UART TRS mode set from device_config: %d", mode);
  }

  // Initialize USB MIDI if enabled
  if (s_config.active_interfaces & MIDI_OUT_INTERFACE_USB) {
    esp_err_t ret = midi_out_usb_init();
    if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to initialize USB MIDI: %s", esp_err_to_name(ret));
  }

  midi_out_queue = xQueueCreate(MIDI_QUEUE_LENGTH, MIDI_QUEUE_ITEM_SIZE);
  if (midi_out_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create MIDI queue");
    return;
  }

  midi_out_mutex = xSemaphoreCreateMutex();
  if (midi_out_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create MIDI mutex");
    vQueueDelete(midi_out_queue);
    midi_out_queue = NULL;
    return;
  }

  BaseType_t ret = xTaskCreate(midi_out_task, "midi_out", 4096, NULL, TASK_PRIORITY_MIDI_OUT, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create MIDI task");
    vQueueDelete(midi_out_queue);
    midi_out_queue = NULL;
    vSemaphoreDelete(midi_out_mutex);
    midi_out_mutex = NULL;
    return;
  }

  ESP_LOGI(TAG, "MIDI OUT initialized - UART: %s, USB: %s, UART tempo: %s, UART transport: %s",
    (s_config.active_interfaces & MIDI_OUT_INTERFACE_UART) ? "ON" : "OFF",
    (s_config.active_interfaces & MIDI_OUT_INTERFACE_USB) ? "ON" : "OFF",
    s_config.uart_send_tempo ? "ON" : "OFF",
    s_config.uart_send_transport ? "ON" : "OFF");
}

static void load_config_from_nvs(void) {
  // Default configuration: both interfaces, all messages enabled
  s_config.active_interfaces = MIDI_OUT_INTERFACE_BOTH;
  s_config.uart_send_tempo = true;
  s_config.uart_send_transport = true;
  s_config.usb_send_tempo = true;
  s_config.usb_send_transport = true;

  uint8_t iface_val;
  if (app_settings_load_u8(NVS_KEY_OUT_INTERFACE, &iface_val) == ESP_OK) {
    s_config.active_interfaces = (midi_out_interface_t)iface_val;
  } else {
    app_settings_save_u8(NVS_KEY_OUT_INTERFACE, (uint8_t)s_config.active_interfaces);
  }

  bool temp_bool;
  if (app_settings_load_bool(NVS_KEY_UART_TEMPO, &temp_bool) == ESP_OK) s_config.uart_send_tempo = temp_bool;
  else app_settings_save_bool(NVS_KEY_UART_TEMPO, s_config.uart_send_tempo);

  if (app_settings_load_bool(NVS_KEY_UART_TRANS, &temp_bool) == ESP_OK) s_config.uart_send_transport = temp_bool;
  else app_settings_save_bool(NVS_KEY_UART_TRANS, s_config.uart_send_transport);

  if (app_settings_load_bool(NVS_KEY_USB_TEMPO, &temp_bool) == ESP_OK) s_config.usb_send_tempo = temp_bool;
  else app_settings_save_bool(NVS_KEY_USB_TEMPO, s_config.usb_send_tempo);

  if (app_settings_load_bool(NVS_KEY_USB_TRANS, &temp_bool) == ESP_OK) s_config.usb_send_transport = temp_bool;
  else app_settings_save_bool(NVS_KEY_USB_TRANS, s_config.usb_send_transport);
}

static void save_config_to_nvs(void) {
  app_settings_save_u8(NVS_KEY_OUT_INTERFACE, (uint8_t)s_config.active_interfaces);
  app_settings_save_bool(NVS_KEY_UART_TEMPO, s_config.uart_send_tempo);
  app_settings_save_bool(NVS_KEY_UART_TRANS, s_config.uart_send_transport);
  app_settings_save_bool(NVS_KEY_USB_TEMPO, s_config.usb_send_tempo);
  app_settings_save_bool(NVS_KEY_USB_TRANS, s_config.usb_send_transport);
}

static void midi_out_track_depth(void) {
  UBaseType_t depth = uxQueueMessagesWaiting(midi_out_queue);
  if ((uint32_t)depth > s_queue_hwm) s_queue_hwm = (uint32_t)depth;
}

void midi_send_message(const uint8_t *stream, size_t len) {
  if (midi_out_queue == NULL) {
    ESP_LOGE(TAG, "MIDI queue not initialized! Call midi_out_init() first");
    return;
  }

  // Check cut state for locally-generated messages
  if (s_cut_local) return;

  midi_out_job_t *job = malloc(sizeof(midi_out_job_t));
  if (!job) {
    ESP_LOGE(TAG, "Failed to allocate job structure");
    return;
  }

  job->data = malloc(len);
  if (!job->data) {
    ESP_LOGE(TAG, "Failed to allocate job data");
    free(job);
    return;
  }

  memcpy(job->data, stream, len);
  job->len = len;
  job->enqueue_time_us = esp_timer_get_time();

  // FIFO: preserve chronological order. On overflow, drop oldest and retry.
  if (xQueueSend(midi_out_queue, &job, 0) != pdPASS) {
    midi_out_job_t *old_job;
    if (xQueueReceive(midi_out_queue, &old_job, 0) == pdPASS) {
      free(old_job->data);
      free(old_job);
      s_drop_overflow++;
    }
    if (xQueueSend(midi_out_queue, &job, 0) != pdPASS) {
      ESP_LOGW(TAG, "Failed to send MIDI message - queue full even after clearing");
      free(job->data);
      free(job);
      s_drop_overflow++;
      return;
    }
  }
  midi_out_track_depth();
}

void midi_clear_queue(void) {
  if (midi_out_queue == NULL) return;
  
  midi_out_job_t *job;
  while (xQueueReceive(midi_out_queue, &job, 0) == pdPASS) {
    free(job->data);
    free(job);
  }
}

void midi_set_uart_transmit_mode(midi_transmit_mode_t mode) {
  if (midi_out_uart_is_override_active()) {
    ESP_LOGD(TAG, "UART mode change blocked by REPL override");
    return;
  }
  if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
    midi_out_uart_set_mode(mode);
    app_settings_save_u16(NVS_KEY_MIDI_MODE, (uint16_t)mode);
    ESP_LOGD(TAG, "UART transmit mode: %d", mode);
    xSemaphoreGive(midi_out_mutex);
  }
}

void midi_out_set_interfaces(midi_out_interface_t interfaces) {
  if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
    s_config.active_interfaces = interfaces;
    save_config_to_nvs();
    
    // Initialize USB if newly enabled and not already initialized
    if ((interfaces & MIDI_OUT_INTERFACE_USB) && !midi_out_usb_is_initialized()) {
      esp_err_t ret = midi_out_usb_init();
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB MIDI: %s", esp_err_to_name(ret));
      }
    }
    
    // Initialize UART if newly enabled and not already initialized
    if ((interfaces & MIDI_OUT_INTERFACE_UART) && !midi_out_uart_is_initialized()) {
      esp_err_t ret = midi_out_uart_init();
      if (ret == ESP_OK) {
        midi_trs_type_t trs_type = device_config_get_trs_type();
        midi_transmit_mode_t mode = (midi_transmit_mode_t)assets_trs_type_to_transmit_mode(trs_type);
        midi_out_uart_set_mode(mode);
      } else {
        ESP_LOGE(TAG, "Failed to initialize UART MIDI: %s", esp_err_to_name(ret));
      }
    }
    
    ESP_LOGI(TAG, "Active interfaces set to: UART=%s USB=%s",
      (interfaces & MIDI_OUT_INTERFACE_UART) ? "ON" : "OFF",
      (interfaces & MIDI_OUT_INTERFACE_USB) ? "ON" : "OFF");
    xSemaphoreGive(midi_out_mutex);
  }
}

midi_out_interface_t midi_out_get_interfaces(void) {
  midi_out_interface_t interfaces;
  if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
    interfaces = s_config.active_interfaces;
    xSemaphoreGive(midi_out_mutex);
  } else {
    interfaces = MIDI_OUT_INTERFACE_NONE;
  }
  return interfaces;
}

void midi_out_set_tempo_enabled(midi_out_interface_t interface, bool enabled) {
  if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
    if (interface & MIDI_OUT_INTERFACE_UART) {
      s_config.uart_send_tempo = enabled;
      app_settings_save_bool(NVS_KEY_UART_TEMPO, enabled);
    }
    if (interface & MIDI_OUT_INTERFACE_USB) {
      s_config.usb_send_tempo = enabled;
      app_settings_save_bool(NVS_KEY_USB_TEMPO, enabled);
    }
    ESP_LOGI(TAG, "Tempo messages: UART=%s USB=%s",
      s_config.uart_send_tempo ? "ON" : "OFF",
      s_config.usb_send_tempo ? "ON" : "OFF");
    xSemaphoreGive(midi_out_mutex);
  }
}

void midi_out_set_transport_enabled(midi_out_interface_t interface, bool enabled) {
  if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
    if (interface & MIDI_OUT_INTERFACE_UART) {
      s_config.uart_send_transport = enabled;
      app_settings_save_bool(NVS_KEY_UART_TRANS, enabled);
    }
    if (interface & MIDI_OUT_INTERFACE_USB) {
      s_config.usb_send_transport = enabled;
      app_settings_save_bool(NVS_KEY_USB_TRANS, enabled);
    }
    ESP_LOGI(TAG, "Transport messages: UART=%s USB=%s",
      s_config.uart_send_transport ? "ON" : "OFF",
      s_config.usb_send_transport ? "ON" : "OFF");
    xSemaphoreGive(midi_out_mutex);
  }
}

bool midi_out_get_tempo_enabled(midi_out_interface_t interface) {
  bool enabled = false;
  if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
    if (interface == MIDI_OUT_INTERFACE_UART) {
      enabled = s_config.uart_send_tempo;
    } else if (interface == MIDI_OUT_INTERFACE_USB) {
      enabled = s_config.usb_send_tempo;
    }
    xSemaphoreGive(midi_out_mutex);
  }
  return enabled;
}

bool midi_out_get_transport_enabled(midi_out_interface_t interface) {
  bool enabled = false;
  if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
    if (interface == MIDI_OUT_INTERFACE_UART) {
      enabled = s_config.uart_send_transport;
    } else if (interface == MIDI_OUT_INTERFACE_USB) {
      enabled = s_config.usb_send_transport;
    }
    xSemaphoreGive(midi_out_mutex);
  }
  return enabled;
}

midi_out_config_t midi_out_get_config(void) {
  midi_out_config_t config;
  if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
    config = s_config;
    xSemaphoreGive(midi_out_mutex);
  } else {
    memset(&config, 0, sizeof(config));
  }
  return config;
}

// Returns true if the job should be dropped as stale.
static bool midi_out_job_is_stale(const midi_out_job_t* job, uint32_t age_us) {
  if (!job || !job->data || job->len == 0) return true;

  uint8_t status = job->data[0];

  // Clock: drop if older than threshold (stale clock = catch-up torrent)
  if (status == 0xF8) {
    if (age_us > MIDI_STALE_CLOCK_US) {
      s_drop_clock++;
      return true;
    }
    return false;
  }

  // Note On with velocity > 0: drop if too late
  if ((status & 0xF0) == 0x90) {
    uint8_t velocity = (job->len >= 3) ? job->data[2] : 0;
    if (velocity > 0 && age_us > MIDI_STALE_NOTE_ON_US) {
      s_drop_note_on++;
      return true;
    }
    // Note On vel 0 is Note Off — always send
    return false;
  }

  // Note Off, transport, MMC/SysEx, SPP, CC, PC, pitch bend, aftertouch:
  // always send even late (state / hang prevention).
  return false;
}

static void midi_out_task(void *pvParameters) {
  midi_out_job_t *job;
  for (;;) {
    if (xQueueReceive(midi_out_queue, &job, portMAX_DELAY) == pdPASS) {
      int64_t now_us = esp_timer_get_time();
      uint32_t age_us = 0;
      if (now_us > job->enqueue_time_us)
        age_us = (uint32_t)(now_us - job->enqueue_time_us);
      if (age_us > s_max_job_age_us) s_max_job_age_us = age_us;

      if (midi_out_job_is_stale(job, age_us)) {
        free(job->data);
        free(job);
        continue;
      }

      if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
        // Detect message type
        uint8_t status = job->data[0];
        bool is_tempo = (status == 0xF8);  // Clock
        bool is_transport = (status >= 0xFA && status <= 0xFC);  // Start/Stop/Continue
        
        // Determine which interfaces should receive this message
        bool send_to_uart = (s_config.active_interfaces & MIDI_OUT_INTERFACE_UART) &&
                           midi_out_uart_is_initialized();
        bool send_to_usb = (s_config.active_interfaces & MIDI_OUT_INTERFACE_USB) && midi_out_usb_is_initialized();
        
        // Apply filters
        if (is_tempo) {
          if (!s_config.uart_send_tempo) send_to_uart = false;
          if (!s_config.usb_send_tempo) send_to_usb = false;
        }
        if (is_transport) {
          if (!s_config.uart_send_transport) send_to_uart = false;
          if (!s_config.usb_send_transport) send_to_usb = false;
        }
        
        // Send to enabled interfaces
        if (send_to_uart) midi_out_uart_send(job->data, job->len);
        if (send_to_usb) midi_out_usb_send(job->data, job->len);

        if (s_cdc_mirror_fn) s_cdc_mirror_fn(job->data, job->len);

        xSemaphoreGive(midi_out_mutex);
      }
      free(job->data);
      free(job);
    }
  }
}

// Cut control functions (temporary runtime mute)
void midi_out_set_cut_local(bool cut) {
  s_cut_local = cut;
  ESP_LOGI(TAG, "Cut local: %s", cut ? "active" : "inactive");
}

void midi_out_set_cut_passthrough(bool cut) {
  s_cut_passthrough = cut;
  ESP_LOGI(TAG, "Cut passthrough: %s", cut ? "active" : "inactive");
}

bool midi_out_get_cut_local(void) {
  return s_cut_local;
}

bool midi_out_get_cut_passthrough(void) {
  return s_cut_passthrough;
}

void midi_out_reset_cut(void) {
  s_cut_local = false;
  s_cut_passthrough = false;
  ESP_LOGD(TAG, "Cut states reset");
}

void midi_out_set_cdc_mirror_fn(midi_out_cdc_mirror_fn fn) {
  s_cdc_mirror_fn = fn;
}

uint32_t midi_out_get_queue_high_watermark(void) {
  return s_queue_hwm;
}

void midi_out_reset_stats(void) {
  s_queue_hwm = 0;
  s_drop_clock = 0;
  s_drop_note_on = 0;
  s_drop_overflow = 0;
  s_max_job_age_us = 0;
}

void midi_out_print_stats(void) {
  uint32_t depth = 0;
  if (midi_out_queue)
    depth = (uint32_t)uxQueueMessagesWaiting(midi_out_queue);

  ESP_LOGI(TAG, "========== MIDI OUT STATS ==========");
  ESP_LOGI(TAG, "Queue depth: %lu / %d  HWM: %lu",
    (unsigned long)depth, MIDI_QUEUE_LENGTH, (unsigned long)s_queue_hwm);
  ESP_LOGI(TAG, "Max job age: %lu us", (unsigned long)s_max_job_age_us);
  ESP_LOGI(TAG, "Drops: clock=%lu note_on=%lu overflow=%lu",
    (unsigned long)s_drop_clock,
    (unsigned long)s_drop_note_on,
    (unsigned long)s_drop_overflow);
  ESP_LOGI(TAG, "====================================");
}
