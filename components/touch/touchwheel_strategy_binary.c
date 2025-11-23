#include "touchwheel_strategy_binary.h"
#include "event_bus.h"
#include "ui.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>

#define TAG "TOUCHWHEEL_BINARY"

// Configuration
#define SPEED_THRESHOLD_FAST       150   // ms (2x) - Brisk scrolling
#define SPEED_THRESHOLD_VERY_FAST  110    // ms (3x) - Fast scrolling
#define SPEED_THRESHOLD_SUPER_FAST 75    // ms (4x) - Very fast scrolling
#define SPEED_THRESHOLD_ULTRA_FAST 60    // ms (5x) - Maximum velocity
#define STICKY_RELEASE_MS 100            // ms - Time to hold dual value after releasing one finger

// Mappings for absolute modes
// Pad 4 is 0, Pad 3 is MAX
// Clockwise path: 4 -> 5 -> 6 -> 7 -> 0 -> 1 -> 2 -> 3
// Index mapping: 0=Pad 4, 1=Pad 5, ..., 7=Pad 3
static const uint8_t PAD_TO_INDEX[NUM_WHEEL_PADS] = {
    4, // Pad 0 is index 4 (12 o'clock)
    5, // Pad 1 is index 5
    6, // Pad 2 is index 6
    7, // Pad 3 is index 7 (End)
    0, // Pad 4 is index 0 (Start)
    1, // Pad 5 is index 1
    2, // Pad 6 is index 2
    3  // Pad 7 is index 3
};

// Odometer: 0-100 range
static const int ODOMETER_MAP[NUM_WHEEL_PADS] = {
    0,   // Pad 4 (Start)
    14,  // Pad 5
    29,  // Pad 6
    43,  // Pad 7
    57,  // Pad 0
    71,  // Pad 1
    86,  // Pad 2
    100  // Pad 3 (End)
};

// Bipolar: -100 to +100 range
static const int BIPOLAR_MAP[NUM_WHEEL_PADS] = {
    -100, // Pad 4 (Start)
    -71,  // Pad 5
    -43,  // Pad 6
    -14,  // Pad 7
    14,   // Pad 0
    43,   // Pad 1
    71,   // Pad 2
    100   // Pad 3 (End)
};

// Helper to calculate interpolated value between two pads
static int interpolate_value(int val1, int val2) {
    return (val1 + val2) / 2;
}

void touchwheel_strategy_binary_process_press(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms) {
    if (!core) return;

    int current_logical_wheel_pos = pad_id;
    
    // Logic for constrained modes (Odometer/Bipolar)
    if (core->mode_type == TOUCHWHEEL_MODE_ODOMETER || core->mode_type == TOUCHWHEEL_MODE_BIPOLAR) {
        // Determine absolute position
        int target_value = 0;
        bool valid_touch = false;
        
        // Check for multi-touch to interpolate
        int active_pad_count = 0;
        int first_pad_idx = -1;
        int second_pad_idx = -1;
        int first_physical_pad = -1;
        int second_physical_pad = -1;
        
        for (int i = 0; i < NUM_WHEEL_PADS; i++) {
            if (core->pad_pressed_states[i]) {
                active_pad_count++;
                if (active_pad_count == 1) {
                    first_pad_idx = PAD_TO_INDEX[i];
                    first_physical_pad = i;
                }
                else if (active_pad_count == 2) {
                    second_pad_idx = PAD_TO_INDEX[i];
                    second_physical_pad = i;
                }
            }
        }
        
        if (active_pad_count == 1 && first_pad_idx != -1) {
            // Check for sticky release: if any OTHER pad was released recently, ignore this single-pad update
            // This prevents "snap back" when releasing one finger of a dual touch
            bool recent_release = false;
            for (int i = 0; i < NUM_WHEEL_PADS; i++) {
                if (i != first_physical_pad && core->pad_release_times[i] > 0) {
                    uint32_t age = timestamp_ms - core->pad_release_times[i];
                    if (age < STICKY_RELEASE_MS) {
                        recent_release = true;
                        break;
                    }
                }
            }
            
            if (recent_release) {
                ESP_LOGI(TAG, "%s: Holding dual value (recent release)", 
                    core->mode_type == TOUCHWHEEL_MODE_ODOMETER ? "Odometer" : "Bipolar");
                return;
            }

            // Boundary check: don't allow crossing 3 <-> 4
            if (core->last_logical_wheel_pos != -1 && 
                touchwheel_core_is_boundary_violation(core, core->last_logical_wheel_pos, first_physical_pad)) {
                ESP_LOGI(TAG, "%s: Boundary violation (Pad %d -> %d) - Blocked", 
                    core->mode_type == TOUCHWHEEL_MODE_ODOMETER ? "Odometer" : "Bipolar",
                    core->last_logical_wheel_pos, first_physical_pad);
                return;
            }

            // Single pad touch
            if (core->mode_type == TOUCHWHEEL_MODE_ODOMETER) {
                target_value = ODOMETER_MAP[first_pad_idx];
            } else {
                target_value = BIPOLAR_MAP[first_pad_idx];
            }
            valid_touch = true;
        } else if (active_pad_count >= 2 && first_pad_idx != -1 && second_pad_idx != -1) {
            // Dual (or more) pad touch - only interpolate if adjacent
            // We use the first two detected pads to determine position
            int diff = abs(first_pad_idx - second_pad_idx);
            if (diff == 1) {
                // Adjacent in our linear mapping
                if (core->mode_type == TOUCHWHEEL_MODE_ODOMETER) {
                    target_value = interpolate_value(ODOMETER_MAP[first_pad_idx], ODOMETER_MAP[second_pad_idx]);
                } else {
                    target_value = interpolate_value(BIPOLAR_MAP[first_pad_idx], BIPOLAR_MAP[second_pad_idx]);
                }
                valid_touch = true;
            }
        }
        
        if (valid_touch) {
            ESP_LOGI(TAG, "%s: Pads [%d%s] -> Value %d", 
                core->mode_type == TOUCHWHEEL_MODE_ODOMETER ? "Odometer" : "Bipolar",
                first_physical_pad, 
                valid_touch && active_pad_count >= 2 ? (char[]){',', (char)('0' + second_physical_pad), '\0'} : "", 
                target_value);

            // For absolute modes, we send the absolute position update via position callback
            if (core->position_callback) {
                core->position_callback(target_value, timestamp_ms, core->callback_user_data);
            }
            
            // Also calculate delta for compatibility if needed, but usually position is enough
            // If delta callback is registered, we can send delta from previous transmitted position
            // NOTE: Core doesn't track last transmitted position value, so we rely on position callback
        }
        
        return; // Skip relative logic
    }
    
    // Relative logic for Endless mode
    if (core->last_logical_wheel_pos != -1 && core->interaction_active && 
        core->last_logical_wheel_pos != current_logical_wheel_pos) {
        
        // Calculate delta
        int delta = current_logical_wheel_pos - core->last_logical_wheel_pos;
        
        // Handle wrap-around
        if (delta > (NUM_WHEEL_PADS / 2)) {
            delta -= NUM_WHEEL_PADS;
        } else if (delta < -(NUM_WHEEL_PADS / 2)) {
            delta += NUM_WHEEL_PADS;
        }
        
        if (delta != 0) {
            uint32_t time_diff_ms = timestamp_ms - core->last_wheel_interaction_time;
            int speed_multiplier = 1;
            
            if (time_diff_ms < SPEED_THRESHOLD_ULTRA_FAST) speed_multiplier = 5;
            else if (time_diff_ms < SPEED_THRESHOLD_SUPER_FAST) speed_multiplier = 4;
            else if (time_diff_ms < SPEED_THRESHOLD_VERY_FAST) speed_multiplier = 3;
            else if (time_diff_ms < SPEED_THRESHOLD_FAST) speed_multiplier = 2;
            
            int effective_delta = delta * speed_multiplier;
            
            ESP_LOGI(TAG, "Touchwheel: %d x %dx = %d (time: %lu ms)", delta, speed_multiplier, effective_delta, time_diff_ms);
            
            if (ui_get_app_mode() != APP_MODE_PROGRAMMING) {
                event_t haptic_event = {
                    .type = EVENT_HAPTIC_REQUEST,
                    .priority = EVENT_PRIORITY_NORMAL,
                    .timestamp = event_bus_get_current_timestamp(),
                    .data.haptic = { 
                        .pattern = (delta > 0) ? HAPTIC_INCREMENT : HAPTIC_DECREMENT
                    }
                };
                event_bus_post(&haptic_event);
            }
            
            if (core->delta_callback) {
                core->delta_callback(effective_delta, current_logical_wheel_pos, timestamp_ms, core->callback_user_data);
            }
        }
    }
}
