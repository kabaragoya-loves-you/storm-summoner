#include "expression.h"
#include "io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "adc2.h"
#include "midi_messages.h"
#include "midi_out.h"
#include <math.h>
#include <string.h>
#include "task_priorities.h"

#define TAG "EXPRESSION"
#define MIN_MIDI_INTERVAL_MS 15  // Increased from 20ms to 30ms for more conservative rate limiting
#define DEADZONE_THRESHOLD 4    // Minimum change required to send MIDI
#define FLOATING_THRESHOLD 3    // Maximum allowed variation in floating state
#define FLOATING_SAMPLES 10     // Number of samples to analyze for floating state
#define FLOATING_IIR_ALPHA 0.1f // More aggressive filtering for floating state
#define MAX_LATENCY_MS 50       // Maximum allowed latency before clearing queue
#define STABLE_SAMPLES_REQUIRED 5  // Number of consecutive stable samples required before sending
#define INITIALIZATION_PERIOD_MS 2000  // Wait 2 seconds after startup before sending any messages

static TaskHandle_t task_handle = NULL;

static int samples[MOVING_AVG_LENGTH] = {0};
static int sample_index = 0;
static int sum_samples = 0;
static int num_samples = 0;
static float expression_value = 0.0f;
static uint8_t midi_value = 0;
static uint8_t last_midi_value = 0;
static TickType_t last_queue_clear_time = 0;
static bool has_valid_reading = false;  // Flag to track if we have valid readings

// Floating state detection
static float recent_values[FLOATING_SAMPLES] = {0};
static int recent_index = 0;
static bool is_floating = false;
static bool first_message_sent = false;  // Flag to track if we've sent the first message
static int stable_sample_count = 0;  // Count of consecutive stable samples
static TickType_t initialization_start_time = 0;  // Track when initialization started

static void expression_task(void *arg);

void expression_init(void) {
  esp_err_t err;
  adc_oneshot_chan_cfg_t chan_config = {
    .bitwidth = ADC_BITWIDTH_DEFAULT, // Use default bit width
    .atten = ADC_ATTEN_DB_12,           // Attenuation for proper input range
  };
// commented for refactor
  // err = adc_oneshot_config_channel(adc2_handle(), EXPRESSION_ADC_CHANNEL, &chan_config);
  // if (err != ESP_OK) {
  //   ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %d", err);
  //   return;
  // }
  ESP_LOGI(TAG, "Expression pedal ADC initialized");
}

void expression_disable(void) {
  if (task_handle != NULL) {
    vTaskSuspend(task_handle);
    ESP_LOGI(TAG, "Expression task suspended");
  }
}

void expression_enable(void) {
  if (task_handle != NULL) {
    vTaskResume(task_handle);
    ESP_LOGI(TAG, "Expression task resumed");
  } else {
    // Reset state variables before creating task
    has_valid_reading = false;
    last_midi_value = 0;
    midi_value = 0;
    expression_value = 0.0f;
    num_samples = 0;
    sum_samples = 0;
    sample_index = 0;
    first_message_sent = false;  // Reset first message flag
    stable_sample_count = 0;  // Reset stable sample count
    initialization_start_time = xTaskGetTickCount();  // Record initialization start time
    memset(samples, 0, sizeof(samples));
    
    // Add a small delay before creating the task to ensure ADC is stable
    vTaskDelay(pdMS_TO_TICKS(100));
    
    BaseType_t ret = xTaskCreate(expression_task, "expression", 4096, NULL, TASK_PRIORITY_EXPRESSION, &task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create Expression task");
      return;
    }
    ESP_LOGI(TAG, "Expression task started");
  }
}

float expression_get_value(void) {
  return expression_value;
}

uint8_t expression_get_midi_value(void) {
  return midi_value;
}

static bool detect_floating_state(float new_value) {
  // Update circular buffer
  recent_values[recent_index] = new_value;
  recent_index = (recent_index + 1) % FLOATING_SAMPLES;
  
  // Calculate mean and standard deviation
  float mean = 0.0f;
  for (int i = 0; i < FLOATING_SAMPLES; i++) {
    mean += recent_values[i];
  }
  mean /= FLOATING_SAMPLES;
  
  float variance = 0.0f;
  for (int i = 0; i < FLOATING_SAMPLES; i++) {
    float diff = recent_values[i] - mean;
    variance += diff * diff;
  }
  variance /= FLOATING_SAMPLES;
  float std_dev = sqrtf(variance);
  
  // Add debug logging for floating state detection
  // if (std_dev < FLOATING_THRESHOLD) ESP_LOGI(TAG, "Floating state detected - std_dev: %.2f, mean: %.2f", std_dev, mean);
  
  return std_dev < FLOATING_THRESHOLD;
}

static void expression_task(void *arg) {
  // int raw = 0;
  // while (1) {
  //   esp_err_t err = adc_oneshot_read(adc2_handle(), EXPRESSION_ADC_CHANNEL, &raw);
  //   if (err != ESP_OK) {
  //     ESP_LOGE(TAG, "adc_oneshot_read failed: %d", err);
  //   } else {
  //     if (num_samples < MOVING_AVG_LENGTH) {
  //       samples[sample_index] = raw;
  //       sum_samples += raw;
  //       num_samples++;
  //     } else {
  //       sum_samples = sum_samples - samples[sample_index] + raw;
  //       samples[sample_index] = raw;
  //     }
  //     sample_index = (sample_index + 1) % MOVING_AVG_LENGTH;
  //     int moving_avg = sum_samples / num_samples;

  //     // Detect if we're in a floating state
  //     is_floating = detect_floating_state(moving_avg);
      
  //     // Use different IIR alpha based on state
  //     float alpha = is_floating ? FLOATING_IIR_ALPHA : IIR_ALPHA;
  //     expression_value = alpha * moving_avg + (1.0f - alpha) * expression_value;

  //     // Scale the processed value (0 - 4095) linearly to MIDI CC range (0 - 127).
  //     float scaled = ((float)expression_value - (float)EXPRESSION_MIN) * 127.0f / ((float)EXPRESSION_MAX - (float)EXPRESSION_MIN);
  //     // Ensure we can hit 127 by using ceilf for values very close to 127
  //     uint8_t new_midi_value = (scaled > 126.5f) ? 127 : (uint8_t)(scaled + 0.5f);
      
  //     // Clamp to valid MIDI range (0-127)
  //     if (new_midi_value > 127) new_midi_value = 127;
      
  //     // Check if we need to clear the queue due to latency
  //     TickType_t current_time = xTaskGetTickCount();
  //     if ((current_time - last_queue_clear_time) >= pdMS_TO_TICKS(MAX_LATENCY_MS)) {
  //       midi_clear_queue();
  //       last_queue_clear_time = current_time;
  //     }
      
  //     // Update stable sample count
  //     if (is_floating) {
  //       stable_sample_count = 0;  // Reset count if we detect floating
  //     } else {
  //       stable_sample_count++;    // Increment count if stable
  //     }
      
  //     // Check if we're still in initialization period
  //     bool in_initialization = (current_time - initialization_start_time) < pdMS_TO_TICKS(INITIALIZATION_PERIOD_MS);
      
  //     // Only send MIDI if:
  //     // 1. We have enough samples for a valid reading
  //     // 2. The value has changed beyond the deadzone
  //     // 3. We're not in a floating state
  //     // 4. We've already sent our first message
  //     // 5. We have enough consecutive stable samples
  //     // 6. We're not in initialization period
  //     if (num_samples >= MOVING_AVG_LENGTH && 
  //         abs(new_midi_value - last_midi_value) >= DEADZONE_THRESHOLD &&
  //         !is_floating &&
  //         first_message_sent &&
  //         stable_sample_count >= STABLE_SAMPLES_REQUIRED &&
  //         !in_initialization) {
  //       ESP_LOGI(TAG, "Expression pedal sent MIDI value %d", new_midi_value);
  //       // ESP_LOGI(TAG, "Debug - num_samples: %d, last_midi: %d, new_midi: %d, is_floating: %d, stable_count: %d", 
  //               //  num_samples, last_midi_value, new_midi_value, is_floating, stable_sample_count);
  //       send_control_change(0, 4, new_midi_value);
  //       last_midi_value = new_midi_value;
  //     } else if (!first_message_sent && num_samples >= MOVING_AVG_LENGTH && !in_initialization) {
  //       // If this would have been our first message and we're past initialization, just mark it as sent
  //       first_message_sent = true;
  //       last_midi_value = new_midi_value;  // Update last_midi_value to prevent a jump on next message
  //       // ESP_LOGI(TAG, "Skipped first MIDI message with value %d", new_midi_value);
  //     }
  //     midi_value = new_midi_value;
      
  //     // Mark that we have valid readings after collecting enough samples
  //     if (num_samples >= MOVING_AVG_LENGTH) has_valid_reading = true;
  //   }
  //   vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
  // }
}
