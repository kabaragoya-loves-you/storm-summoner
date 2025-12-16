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
#define STICKY_RELEASE_MS 0              // ms - Disabled to allow single-pad values during slide

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
        
        // Check for multi-touch to interpolate using active_pads stack (newest is last)
        // This is robust against stuck pads because we prioritize the most recent touch
        int p1_physical = -1;
        int p2_physical = -1;
        
        if (core->num_active_pads > 0) {
            p1_physical = core->active_pads[core->num_active_pads - 1];
            if (p1_physical >= NUM_WHEEL_PADS) p1_physical = -1; // Safety check
            
            if (core->num_active_pads > 1) {
                p2_physical = core->active_pads[core->num_active_pads - 2];
                if (p2_physical >= NUM_WHEEL_PADS) p2_physical = -1;
            }
        }
        
        if (p1_physical != -1) {
            int p1_idx = PAD_TO_INDEX[p1_physical];
            
            // Try dual touch first
            if (p2_physical != -1) {
                int p2_idx = PAD_TO_INDEX[p2_physical];
                int diff = abs(p1_idx - p2_idx);
                
                if (diff == 1) {
                    // Adjacent! Interpolate.
                    if (core->mode_type == TOUCHWHEEL_MODE_ODOMETER) {
                        target_value = interpolate_value(ODOMETER_MAP[p1_idx], ODOMETER_MAP[p2_idx]);
                    } else {
                        target_value = interpolate_value(BIPOLAR_MAP[p1_idx], BIPOLAR_MAP[p2_idx]);
                    }
                    valid_touch = true;
                    
                    ESP_LOGI(TAG, "%s: Pads [%d,%d] -> Value %d", 
                        core->mode_type == TOUCHWHEEL_MODE_ODOMETER ? "Odometer" : "Bipolar",
                        p2_physical, p1_physical, target_value); // Log older,newer
                }
            }
            
            // Fallback to single touch if dual failed (not adjacent) or only 1 pad
            if (!valid_touch) {
                // Boundary check: don't allow crossing 3 <-> 4
                if (core->last_logical_wheel_pos != -1 && 
                    touchwheel_core_is_boundary_violation(core, core->last_logical_wheel_pos, p1_physical)) {
                    ESP_LOGI(TAG, "%s: Boundary violation (Pad %d -> %d) - Blocked", 
                        core->mode_type == TOUCHWHEEL_MODE_ODOMETER ? "Odometer" : "Bipolar",
                        core->last_logical_wheel_pos, p1_physical);
                    return;
                }

                if (core->mode_type == TOUCHWHEEL_MODE_ODOMETER) {
                    target_value = ODOMETER_MAP[p1_idx];
                } else {
                    target_value = BIPOLAR_MAP[p1_idx];
                }
                valid_touch = true;
                
                ESP_LOGI(TAG, "%s: Pad [%d] -> Value %d", 
                    core->mode_type == TOUCHWHEEL_MODE_ODOMETER ? "Odometer" : "Bipolar",
                    p1_physical, target_value);
            }
        }
        
        if (valid_touch) {
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
            
            ESP_LOGD(TAG, "Touchwheel: %d x %dx = %d (time: %lu ms)", delta, speed_multiplier, effective_delta, time_diff_ms);
            
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

void touchwheel_strategy_binary_process_release(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms) {
    // Re-run the press logic to recalculate value based on remaining pads
    // Pass the pad_id that remains pressed (if any), or just use the first active pad
    
    if (core->mode_type == TOUCHWHEEL_MODE_ODOMETER || core->mode_type == TOUCHWHEEL_MODE_BIPOLAR) {
        // Find any active pad to serve as the "current" pad for calculation
        int active_pad = -1;
        for (int i = 0; i < NUM_WHEEL_PADS; i++) {
            if (core->pad_pressed_states[i]) {
                active_pad = i;
                break;
            }
        }
        
        if (active_pad != -1) {
            touchwheel_strategy_binary_process_press(core, active_pad, timestamp_ms);
        }
    }
}
