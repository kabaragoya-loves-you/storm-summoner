#include "scene_test.h"
#include "scene.h"
#include "device_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "scene_test";

// Test functions that can be called from monitor
void scene_test_info(void) {
  uint8_t index = scene_get_current_index();
  scene_t* scene = scene_get_current();
  
  if (!scene) {
    ESP_LOGE(TAG, "Scene manager not initialized!");
    return;
  }
  
  scene_mode_t mode = scene_get_mode();
  scene_change_mode_t change_mode = scene_get_change_mode();
  uint8_t device_channel = device_config_get_channel();
  
  const char* mode_str = (mode == SCENE_MODE_SINGLE) ? "Single" :
                         (mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  const char* change_str = (change_mode == CHANGE_MODE_IMMEDIATE) ? "Immediate" : "Pending";
  
  ESP_LOGI(TAG, "====== SCENE INFO ======");
  ESP_LOGI(TAG, "Scene mode: %s", mode_str);
  ESP_LOGI(TAG, "Change mode: %s", change_str);
  ESP_LOGI(TAG, "Device MIDI channel: %d", device_channel);
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Current scene: %d - %s", index + 1, scene->name);
  ESP_LOGI(TAG, "Program number: %d (send PC on load: %s)", scene->program_number, 
           scene->send_pc_on_load ? "yes" : "no");
  ESP_LOGI(TAG, "Touchwheel: %s mode", 
           scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS ? "pads" : "encoder");
  
  if (scene_has_pending_change()) {
    ESP_LOGI(TAG, "PENDING CHANGE to scene %d", scene_get_pending_index() + 1);
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Touchpad mappings:");
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    touchpad_mapping_t* map = &scene->touchpads[i];
    if (map->enabled) {
      if (map->action.type != ACTION_NONE) {
        const char* action_name = action_type_to_string(map->action.type);
        
        if (map->action.type == ACTION_SEND_CC) {
          uint8_t num_ccs = map->action.params.cc.num_ccs;
          if (num_ccs == 0) num_ccs = 1;
          ESP_LOGI(TAG, "  Pad %2d: %s (CC%d=%d)", 
                   i, action_name, map->action.params.cc.cc_numbers[0], 
                   map->action.params.cc.values[0]);
        } else {
          ESP_LOGI(TAG, "  Pad %2d: %s", i, action_name);
        }
      } else {
        ESP_LOGI(TAG, "  Pad %2d: no action", i);
      }
    } else {
      ESP_LOGI(TAG, "  Pad %2d: disabled", i);
    }
  }
  ESP_LOGI(TAG, "========================");
}

void scene_test_next(void) {
  esp_err_t ret = scene_next();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Switched to next scene");
    scene_test_info();
  } else {
    ESP_LOGE(TAG, "Failed to switch scene: %s", esp_err_to_name(ret));
  }
}

void scene_test_previous(void) {
  esp_err_t ret = scene_previous();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Switched to previous scene");
    scene_test_info();
  } else {
    ESP_LOGE(TAG, "Failed to switch scene: %s", esp_err_to_name(ret));
  }
}

void scene_test_set_cc(uint8_t pad, uint8_t cc, uint8_t value) {
  uint8_t scene_index = scene_get_current_index();
  esp_err_t ret = scene_set_touchpad_cc(scene_index, pad, cc, value);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Set pad %d: CC%d=%d", pad, cc, value);
  } else {
    ESP_LOGE(TAG, "Failed to set CC mapping");
  }
}

void scene_test_demo(void) {
  ESP_LOGI(TAG, "Running scene demo...");
  
  // Configure some interesting mappings
  uint8_t scene_index = scene_get_current_index();
  
  // Set up a filter cutoff on pad 0
  scene_set_touchpad_cc(scene_index, 0, 74, 127);  // CC74 = Filter Cutoff
  
  // Set up modulation on pad 1
  scene_set_touchpad_cc(scene_index, 1, 1, 64);    // CC1 = Modulation
  
  // Set up volume on pad 2  
  scene_set_touchpad_cc(scene_index, 2, 7, 100);   // CC7 = Volume
  
  // Set scene name
  scene_set_name(scene_index, "Demo Scene");
  
  ESP_LOGI(TAG, "Demo configuration applied!");
  scene_test_info();
}

// Monitor keyboard handler
void scene_test_monitor_handler(char key) {
  switch (key) {
    case 'i':
    case 'I':
      scene_test_info();
      break;
      
    case 'n':
    case 'N':
      scene_test_next();
      break;
      
    case 'p':
    case 'P':
      scene_test_previous();
      break;
      
    case 'd':
    case 'D':
      scene_test_demo();
      break;
      
    case '1':
      scene_set_current(0);
      ESP_LOGI(TAG, "Switched to scene 1");
      break;
      
    case '2':
      scene_set_current(1);
      ESP_LOGI(TAG, "Switched to scene 2");
      break;
      
    case '3':
      scene_set_current(2);
      ESP_LOGI(TAG, "Switched to scene 3");
      break;
      
    case 'm':
    case 'M':
      // Cycle through scene modes
      {
        scene_mode_t current = scene_get_mode();
        scene_mode_t next = (current == SCENE_MODE_SINGLE) ? SCENE_MODE_PRESET_SYNC :
                            (current == SCENE_MODE_PRESET_SYNC) ? SCENE_MODE_ADVANCED :
                            SCENE_MODE_SINGLE;
        scene_set_mode(next);
        const char* mode_str = (next == SCENE_MODE_SINGLE) ? "Single" :
                               (next == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
        ESP_LOGI(TAG, "Scene mode: %s", mode_str);
      }
      break;
      
    case 'c':
    case 'C':
      // Toggle change mode
      {
        scene_change_mode_t current = scene_get_change_mode();
        scene_change_mode_t next = (current == CHANGE_MODE_IMMEDIATE) ? 
                                   CHANGE_MODE_PENDING : CHANGE_MODE_IMMEDIATE;
        scene_set_change_mode(next);
        ESP_LOGI(TAG, "Change mode: %s", next == CHANGE_MODE_IMMEDIATE ? "Immediate" : "Pending");
      }
      break;
      
    case 'y':
    case 'Y':
      // Confirm pending change
      if (scene_has_pending_change()) {
        scene_confirm_change();
        ESP_LOGI(TAG, "Confirmed pending change");
      } else {
        ESP_LOGI(TAG, "No pending change");
      }
      break;
      
    case 'x':
    case 'X':
      // Cancel pending change
      if (scene_has_pending_change()) {
        scene_cancel_pending();
        ESP_LOGI(TAG, "Cancelled pending change");
      } else {
        ESP_LOGI(TAG, "No pending change");
      }
      break;
      
    case '+':
      // Increase device MIDI channel
      {
        uint8_t ch = device_config_get_channel();
        if (ch < 16) {
          device_config_set_channel(ch + 1);
          ESP_LOGI(TAG, "Device MIDI channel: %d", ch + 1);
        }
      }
      break;
      
    case '-':
      // Decrease device MIDI channel
      {
        uint8_t ch = device_config_get_channel();
        if (ch > 1) {
          device_config_set_channel(ch - 1);
          ESP_LOGI(TAG, "Device MIDI channel: %d", ch - 1);
        }
      }
      break;
      
    case 'h':
    case 'H':
      ESP_LOGI(TAG, "Scene Test Commands:");
      ESP_LOGI(TAG, "  i - Show scene info");
      ESP_LOGI(TAG, "  n - Next scene");
      ESP_LOGI(TAG, "  p - Previous scene");
      ESP_LOGI(TAG, "  d - Apply demo configuration");
      ESP_LOGI(TAG, "  1-3 - Switch to scene 1-3");
      ESP_LOGI(TAG, "  m - Cycle scene mode (Single/Preset Sync/Advanced)");
      ESP_LOGI(TAG, "  c - Toggle change mode (Immediate/Pending)");
      ESP_LOGI(TAG, "  y - Confirm pending change");
      ESP_LOGI(TAG, "  x - Cancel pending change");
      ESP_LOGI(TAG, "  +/- - Adjust device MIDI channel");
      ESP_LOGI(TAG, "  h - Show this help");
      break;
  }
}
