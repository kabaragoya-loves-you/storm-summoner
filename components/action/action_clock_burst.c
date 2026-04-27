#include "action_internal.h"
#include "midi_out.h"
#include "tempo.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "action_clock_burst";

static esp_timer_handle_t s_clock_burst_timer = NULL;
static uint8_t s_clock_burst_speed_percent = 100;
static bool s_clock_burst_active = false;

static void clock_burst_timer_callback(void* arg) {
  (void)arg;
  if (s_clock_burst_active) {
    // Send an extra clock pulse (0xF8) directly
    // Note: This bypasses the scene's send_clock check intentionally
    // because the burst is a deliberate performance effect
    const uint8_t message = 0xF8;
    midi_send_message(&message, 1);
  }
}

esp_err_t action_clock_burst_init(void) {
  esp_timer_create_args_t timer_args = {
    .callback = clock_burst_timer_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "clock_burst"
  };
  esp_err_t ret = esp_timer_create(&timer_args, &s_clock_burst_timer);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create clock burst timer: %s", esp_err_to_name(ret));
  }
  return ret;
}

void action_clock_burst_start(uint8_t speed_percent) {
  if (s_clock_burst_active) return;  // Already running

  if (!s_clock_burst_timer) {
    ESP_LOGW(TAG, "Clock Burst timer not initialized");
    return;
  }

  uint16_t current_bpm = tempo_get_bpm();

  // Calculate the base tick interval (for 24 PPQN)
  // Base ticks per second = (bpm * 24) / 60 = bpm * 0.4
  // Base tick interval = 60000000 / (bpm * 24) microseconds
  // For the burst, we need to add (speed_percent / 100) of that rate
  // So burst interval = base_interval * 100 / speed_percent

  // If speed_percent is 100%, we match the existing tempo (double the clocks)
  // If speed_percent is 200%, we send twice as many extra clocks (triple total)
  // If speed_percent is 50%, we send half as many extra clocks (1.5x total)

  uint64_t base_interval_us = (60 * 1000000ULL) / (current_bpm * 24);
  uint64_t burst_interval_us = (base_interval_us * 100) / speed_percent;

  if (burst_interval_us < 1000) burst_interval_us = 1000;

  s_clock_burst_speed_percent = speed_percent;

  esp_err_t ret = esp_timer_start_periodic(s_clock_burst_timer, burst_interval_us);
  if (ret == ESP_OK) {
    s_clock_burst_active = true;
    ESP_LOGI(TAG, "Clock Burst started: %d%% speed, interval %llu us",
      speed_percent, (unsigned long long)burst_interval_us);
  } else {
    ESP_LOGE(TAG, "Failed to start Clock Burst timer: %s", esp_err_to_name(ret));
  }
}

void action_clock_burst_stop(void) {
  if (!s_clock_burst_active) return;

  s_clock_burst_active = false;
  if (s_clock_burst_timer) {
    esp_timer_stop(s_clock_burst_timer);
    ESP_LOGI(TAG, "Clock Burst stopped");
  }
}
