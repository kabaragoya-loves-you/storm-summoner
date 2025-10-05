#include "scene_test.h"
#include "scene.h"
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
  
  ESP_LOGI(TAG, "====== SCENE INFO ======");
  ESP_LOGI(TAG, "Current scene: %d - %s", index + 1, scene->name);
  ESP_LOGI(TAG, "MIDI channel: %d", scene->midi_channel);
  ESP_LOGI(TAG, "Touchwheel: %s mode", scene->touchwheel_mode == TOUCHWHEEL_MODE_BUTTONS ? "button" : "encoder");
  
  ESP_LOGI(TAG, "Touchpad mappings:");
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    touchpad_mapping_t* map = &scene->touchpads[i];
    if (map->enabled) {
      ESP_LOGI(TAG, "  Pad %2d: CC%-3d value=%-3d ch=%d", 
        i, map->cc.cc_number, map->cc.value,
        map->cc.channel ? map->cc.channel : scene->midi_channel);
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
  esp_err_t ret = scene_set_touchpad_cc(scene_index, pad, cc, value, 0);
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
  scene_set_touchpad_cc(scene_index, 0, 74, 127, 0);  // CC74 = Filter Cutoff
  
  // Set up modulation on pad 1
  scene_set_touchpad_cc(scene_index, 1, 1, 64, 0);    // CC1 = Modulation
  
  // Set up volume on pad 2  
  scene_set_touchpad_cc(scene_index, 2, 7, 100, 0);   // CC7 = Volume
  
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
      
    case 'h':
    case 'H':
      ESP_LOGI(TAG, "Scene Test Commands:");
      ESP_LOGI(TAG, "  i - Show scene info");
      ESP_LOGI(TAG, "  n - Next scene");
      ESP_LOGI(TAG, "  p - Previous scene");
      ESP_LOGI(TAG, "  d - Apply demo configuration");
      ESP_LOGI(TAG, "  1-3 - Switch to scene 1-3");
      ESP_LOGI(TAG, "  h - Show this help");
      break;
  }
}
