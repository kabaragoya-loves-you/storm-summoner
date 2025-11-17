#include "touchwheel_outputs.h"
#include "event_bus.h"
#include "esp_log.h"
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
  
  // Return accumulated diff and clear it
  data->enc_diff = output->data.lvgl.accumulated_diff;
  output->data.lvgl.accumulated_diff = 0;
  data->state = LV_INDEV_STATE_RELEASED;  // Always released (no button)
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
  
  ESP_LOGI(TAG, "Created LVGL encoder output adapter");
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
    lv_indev_delete(output->data.lvgl.indev);
  }
  
  free(output);
}

