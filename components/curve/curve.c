#include "curve.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char* TAG = "curve";

static bool s_initialized = false;

// Curve type names
static const char* curve_type_names[] = {
  [CURVE_LINEAR] = "Linear",
  [CURVE_EXPONENTIAL] = "Exponential",
  [CURVE_LOGARITHMIC] = "Logarithmic",
  [CURVE_S_CURVE] = "S-Curve",
  [CURVE_INVERSE_S] = "Inverse S",
  [CURVE_QUADRATIC] = "Quadratic",
  [CURVE_SQUARE_ROOT] = "Square Root",
  [CURVE_SINE] = "Sine",
  [CURVE_CUSTOM] = "Custom"
};

esp_err_t curve_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Curve component already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing curve component");
  s_initialized = true;
  
  return ESP_OK;
}

const char* curve_type_to_string(curve_type_t type) {
  return (type < CURVE_MAX) ? curve_type_names[type] : "Unknown";
}

// Helper: Apply exponential curve with configurable slope
static uint8_t apply_exponential(uint8_t input, curve_slope_t slope) {
  float normalized = input / 127.0f;
  float exponent = (slope == CURVE_SLOPE_GENTLE) ? 1.5f :
                   (slope == CURVE_SLOPE_MEDIUM) ? 2.0f : 3.0f;
  float result = powf(normalized, exponent);
  return (uint8_t)(result * 127.0f);
}

// Helper: Apply logarithmic curve
static uint8_t apply_logarithmic(uint8_t input, curve_slope_t slope) {
  if (input == 0) return 0;
  float normalized = input / 127.0f;
  float base = (slope == CURVE_SLOPE_GENTLE) ? 1.5f :
               (slope == CURVE_SLOPE_MEDIUM) ? 2.0f : 3.0f;
  float result = logf(1.0f + normalized * (base - 1.0f)) / logf(base);
  return (uint8_t)(result * 127.0f);
}

// Helper: Apply S-curve (sigmoid)
static uint8_t apply_s_curve(uint8_t input, curve_slope_t slope) {
  float normalized = (input / 127.0f) * 2.0f - 1.0f;  // -1 to 1
  float steepness = (slope == CURVE_SLOPE_GENTLE) ? 3.0f :
                    (slope == CURVE_SLOPE_MEDIUM) ? 5.0f : 8.0f;
  float sigmoid = 1.0f / (1.0f + expf(-steepness * normalized));
  return (uint8_t)(sigmoid * 127.0f);
}

uint8_t curve_apply(const curve_t* curve, uint8_t input) {
  if (!curve) return input;  // No curve = passthrough
  
  if (input > 127) input = 127;  // Clamp input
  
  switch (curve->type) {
    case CURVE_LINEAR:
      return input;
      
    case CURVE_EXPONENTIAL:
      return apply_exponential(input, curve->slope);
      
    case CURVE_LOGARITHMIC:
      return apply_logarithmic(input, curve->slope);
      
    case CURVE_S_CURVE:
      return apply_s_curve(input, curve->slope);
      
    case CURVE_INVERSE_S:
      // Inverse S: invert input, apply S-curve, invert output
      return 127 - apply_s_curve(127 - input, curve->slope);
      
    case CURVE_QUADRATIC:
      {
        float normalized = input / 127.0f;
        float result = normalized * normalized;
        return (uint8_t)(result * 127.0f);
      }
      
    case CURVE_SQUARE_ROOT:
      {
        float normalized = input / 127.0f;
        float result = sqrtf(normalized);
        return (uint8_t)(result * 127.0f);
      }
      
    case CURVE_SINE:
      {
        // Map 0-127 to 0-90 degrees (0 to pi/2 radians)
        float radians = (input / 127.0f) * (M_PI / 2.0f);
        float result = sinf(radians);
        return (uint8_t)(result * 127.0f);
      }
      
    case CURVE_CUSTOM:
      if (curve->custom_data && curve->custom_data->valid) {
        return curve->custom_data->values[input];
      }
      return input;  // Fallback to linear if custom not available
      
    default:
      ESP_LOGW(TAG, "Unknown curve type %d, using linear", curve->type);
      return input;
  }
}

curve_t curve_create(curve_type_t type) {
  curve_t curve = {
    .type = type,
    .slope = CURVE_SLOPE_MEDIUM,
    .custom_data = NULL
  };
  return curve;
}

curve_t curve_create_with_slope(curve_type_t type, curve_slope_t slope) {
  curve_t curve = {
    .type = type,
    .slope = slope,
    .custom_data = NULL
  };
  return curve;
}

// Curve recording implementation
esp_err_t curve_start_recording(curve_recorder_t* recorder) {
  if (!recorder) return ESP_ERR_INVALID_ARG;
  
  memset(recorder, 0, sizeof(curve_recorder_t));
  recorder->recording = true;
  recorder->start_time_ms = esp_log_timestamp();
  
  ESP_LOGI(TAG, "Started curve recording");
  return ESP_OK;
}

esp_err_t curve_add_sample(curve_recorder_t* recorder, uint8_t value) {
  if (!recorder || !recorder->recording) {
    return ESP_ERR_INVALID_STATE;
  }
  
  if (recorder->sample_count >= 256) {
    ESP_LOGW(TAG, "Sample buffer full");
    return ESP_ERR_NO_MEM;
  }
  
  recorder->samples[recorder->sample_count++] = value;
  return ESP_OK;
}

esp_err_t curve_finish_recording(curve_recorder_t* recorder, custom_curve_t* output) {
  if (!recorder || !output || !recorder->recording) {
    return ESP_ERR_INVALID_STATE;
  }
  
  if (recorder->sample_count < 10) {
    ESP_LOGE(TAG, "Not enough samples (need at least 10, got %d)", recorder->sample_count);
    return ESP_ERR_INVALID_ARG;
  }
  
  ESP_LOGI(TAG, "Finishing curve recording: %d samples collected", recorder->sample_count);
  
  // Interpolate samples into 128-point curve
  // Simple linear interpolation
  for (int i = 0; i < CURVE_RESOLUTION; i++) {
    // Map curve index to sample index
    float sample_pos = (i / 127.0f) * (recorder->sample_count - 1);
    int sample_idx = (int)sample_pos;
    float fraction = sample_pos - sample_idx;
    
    if (sample_idx >= recorder->sample_count - 1) {
      output->values[i] = recorder->samples[recorder->sample_count - 1];
    } else {
      // Linear interpolation between samples
      uint8_t val1 = recorder->samples[sample_idx];
      uint8_t val2 = recorder->samples[sample_idx + 1];
      output->values[i] = (uint8_t)(val1 + (val2 - val1) * fraction);
    }
  }
  
  output->valid = true;
  recorder->recording = false;
  
  uint32_t duration_ms = esp_log_timestamp() - recorder->start_time_ms;
  ESP_LOGI(TAG, "Curve recording complete: %d samples -> 128 points (%lu ms)", 
           recorder->sample_count, (unsigned long)duration_ms);
  
  return ESP_OK;
}

void curve_cancel_recording(curve_recorder_t* recorder) {
  if (recorder) {
    recorder->recording = false;
    ESP_LOGI(TAG, "Curve recording cancelled");
  }
}

void curve_free_custom(custom_curve_t* custom) {
  if (custom) {
    free(custom);
  }
}

