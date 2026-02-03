#include "touchwheel_outputs.h"
#include "event_bus.h"
#include "esp_log.h"
#include "ui.h"
#include "text_edit.h"
#include <stdlib.h>
#include <string.h>

#define TAG "TOUCHWHEEL_OUTPUTS"

// LVGL encoder read callback
static void lvgl_encoder_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
  void* driver_data = lv_indev_get_driver_data(indev);
  if (!driver_data) {
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  
  touchwheel_output_t* output = (touchwheel_output_t*)driver_data;
  if (!output || output->type != TOUCHWHEEL_OUTPUT_LVGL) {
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  
  // Check if we're in Programming mode with a focused group object
  bool suppress_haptic = false;
  if (ui_get_app_mode() == APP_MODE_PROGRAMMING && output->data.lvgl.accumulated_diff != 0) {
    lv_group_t* group = lv_indev_get_group(indev);
    if (group) {
      // Skip boundary detection if in editing mode (e.g., scrolling within a roller)
      // In editing mode, encoder controls the widget internally, not menu navigation
      if (!lv_group_get_editing(group)) {
        lv_obj_t* focused = lv_group_get_focused(group);
        if (focused) {
          // Get the parent container to check boundaries
          lv_obj_t* container = lv_obj_get_parent(focused);
          // Check if parent is a scrollable container (our menu containers)
          if (container && lv_obj_has_flag(container, LV_OBJ_FLAG_SCROLLABLE)) {
            uint32_t child_cnt = lv_obj_get_child_count(container);
            uint32_t focused_index = lv_obj_get_index(focused);
            
            // Check if at boundaries
            if (output->data.lvgl.accumulated_diff < 0 && focused_index == 0) {
              // Trying to go up from first item
              suppress_haptic = true;
              output->data.lvgl.accumulated_diff = 0;  // Suppress navigation too
            } else if (output->data.lvgl.accumulated_diff > 0 && focused_index == child_cnt - 1) {
              // Trying to go down from last item
              suppress_haptic = true;
              output->data.lvgl.accumulated_diff = 0;  // Suppress navigation too
            }
          }
        }
      }
    }
  }
  
  // Return accumulated diff and clear it
  data->enc_diff = output->data.lvgl.accumulated_diff;
  int32_t diff_to_report = output->data.lvgl.accumulated_diff;
  output->data.lvgl.accumulated_diff = 0;
  data->state = LV_INDEV_STATE_RELEASED;  // Always released (no button)
  
  // Generate haptic feedback only if navigation will actually happen
  if (diff_to_report != 0 && !suppress_haptic) {
    event_t haptic_event = {
      .type = EVENT_HAPTIC_REQUEST,
      .priority = EVENT_PRIORITY_NORMAL,
      .timestamp = event_bus_get_current_timestamp(),
      .data.haptic = { 
        .pattern = (diff_to_report > 0) ? HAPTIC_INCREMENT : HAPTIC_DECREMENT
      }
    };
    event_bus_post(&haptic_event);
  }
}

touchwheel_output_t* touchwheel_output_eventbus_create(void) {
  touchwheel_output_t* output = (touchwheel_output_t*)malloc(sizeof(touchwheel_output_t));
  if (!output) return NULL;
  
  memset(output, 0, sizeof(touchwheel_output_t));
  output->type = TOUCHWHEEL_OUTPUT_EVENTBUS;
  
  ESP_LOGI(TAG, "Created event bus output adapter");
  return output;
}

touchwheel_output_t* touchwheel_output_lvgl_create(lv_display_t* disp) {
  touchwheel_output_t* output = (touchwheel_output_t*)malloc(sizeof(touchwheel_output_t));
  if (!output) return NULL;
  
  memset(output, 0, sizeof(touchwheel_output_t));
  output->type = TOUCHWHEEL_OUTPUT_LVGL;
  output->data.lvgl.accumulated_diff = 0;
  
  // Create LVGL encoder input device
  lv_indev_t* indev = lv_indev_create();
  if (!indev) {
    free(output);
    return NULL;
  }
  
  lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(indev, lvgl_encoder_read_cb);
  lv_indev_set_driver_data(indev, output);
  
  if (disp) {
    lv_indev_set_display(indev, disp);
  } else {
    lv_indev_set_display(indev, lv_display_get_default());
  }
  
  output->data.lvgl.indev = indev;
  
  ESP_LOGD(TAG, "Created LVGL encoder output adapter");
  return output;
}

touchwheel_output_t* touchwheel_output_callback_create(touchwheel_value_cb_t callback, void* user_data) {
  if (!callback) return NULL;
  
  touchwheel_output_t* output = (touchwheel_output_t*)malloc(sizeof(touchwheel_output_t));
  if (!output) return NULL;
  
  memset(output, 0, sizeof(touchwheel_output_t));
  output->type = TOUCHWHEEL_OUTPUT_CALLBACK;
  output->data.callback.callback = callback;
  output->data.callback.user_data = user_data;
  
  ESP_LOGI(TAG, "Created callback output adapter");
  return output;
}

void touchwheel_output_send(touchwheel_output_t* output, int value) {
  if (!output) return;
  
  // Intercept wheel events for text editor when active
  if (output->type == TOUCHWHEEL_OUTPUT_LVGL && text_edit_is_active()) {
    text_edit_handle_wheel(value);
    return;
  }
  
  switch (output->type) {
    case TOUCHWHEEL_OUTPUT_EVENTBUS: {
      event_t event = {
        .type = EVENT_TOUCHWHEEL_VALUE,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = event_bus_get_current_timestamp(),
        .data.touchwheel_value = {
          .value = value
        }
      };
      event_bus_post(&event);
      break;
    }
    
    case TOUCHWHEEL_OUTPUT_LVGL: {
      // Accumulate encoder diff for LVGL
      output->data.lvgl.accumulated_diff += value;
      break;
    }
    
    case TOUCHWHEEL_OUTPUT_CALLBACK: {
      if (output->data.callback.callback) {
        output->data.callback.callback(value, output->data.callback.user_data);
      }
      break;
    }
  }
}

lv_indev_t* touchwheel_output_get_lvgl_indev(touchwheel_output_t* output) {
  if (!output || output->type != TOUCHWHEEL_OUTPUT_LVGL) return NULL;
  return output->data.lvgl.indev;
}

void touchwheel_output_destroy(touchwheel_output_t* output) {
  if (!output) return;
  
  if (output->type == TOUCHWHEEL_OUTPUT_LVGL && output->data.lvgl.indev) {
    lv_indev_t* indev = output->data.lvgl.indev;
    
    // Clear our reference first to prevent any callbacks from using stale pointer
    output->data.lvgl.indev = NULL;
    
    // Detach from group before deletion to prevent stale group references
    lv_group_t* group = lv_indev_get_group(indev);
    if (group) lv_indev_set_group(indev, NULL);
    
    // Clear the driver data to prevent read callback from using stale output pointer
    lv_indev_set_driver_data(indev, NULL);
    
    lv_indev_delete(indev);
  }
  
  free(output);
}

void touchwheel_output_set_release_callback(touchwheel_output_t* output, touchwheel_release_cb_t callback) {
  if (!output || output->type != TOUCHWHEEL_OUTPUT_CALLBACK) return;
  output->data.callback.release_callback = callback;
}

void touchwheel_output_send_release(touchwheel_output_t* output) {
  if (!output) return;
  
  if (output->type == TOUCHWHEEL_OUTPUT_CALLBACK && output->data.callback.release_callback) {
    output->data.callback.release_callback(output->data.callback.user_data);
  }
}

