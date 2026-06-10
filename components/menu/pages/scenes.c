#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "scene_name_gen.h"
#include "event_bus.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_SCENES"
#define MAX_SCENE_ITEMS 136  // 128 scenes + 2 headings + 2 dividers + Add + slack

// Forward declarations
static lv_obj_t* scene_action_menu_create(void);

// State
static uint16_t s_selected_position = 0;  // Position in manifest (not scene index)
static menu_item_t s_scene_items[MAX_SCENE_ITEMS];
static char s_scene_labels[128][32];  // "1. SceneName" format

// Reorder roller state
static char s_reorder_options[2048];  // Roller options string
static bool s_list_subscribed = false;

static int manifest_position_to_clickable_focus(uint16_t manifest_pos) {
  uint16_t total = scene_get_total_count();
  int focus = 0;

  for (uint16_t i = 0; i < total; i++) {
    if (!scene_is_active_by_position(i)) continue;
    if (i == manifest_pos) return focus;
    focus++;
  }

  for (uint16_t i = 0; i < total; i++) {
    if (scene_is_active_by_position(i)) continue;
    if (scene_is_factory_by_position(i)) continue;
    if (i == manifest_pos) return focus;
    focus++;
  }

  for (uint16_t i = 0; i < total; i++) {
    if (scene_is_active_by_position(i)) continue;
    if (!scene_is_factory_by_position(i)) continue;
    if (i == manifest_pos) return focus;
    focus++;
  }

  return focus > 0 ? focus - 1 : 0;
}

static void scene_list_refresh_handler(const event_t *event, void *context) {
  (void)event;
  (void)context;
  if (!menu_current_page_is("Scenes")) return;

  void *focused = menu_get_focused_item_user_data();
  if (focused) {
    uint16_t old_pos = (uint16_t)(uintptr_t)focused;
    uint8_t scene_index = scene_get_index_by_position(old_pos);
    uint16_t total = scene_get_total_count();
    for (uint16_t i = 0; i < total; i++) {
      if (scene_get_index_by_position(i) == scene_index) {
        menu_set_restore_focus(manifest_position_to_clickable_focus(i));
        break;
      }
    }
  }

  menu_replace_current_deferred("Scenes", menu_page_scenes_create);
}

static void ensure_list_subscribed(void) {
  if (s_list_subscribed) return;
  event_bus_subscribe(EVENT_SCENE_LIST_CHANGED, scene_list_refresh_handler, NULL);
  event_bus_subscribe(EVENT_SCENE_REORDERED, scene_list_refresh_handler, NULL);
  s_list_subscribed = true;
}

// ============================================================================
// Helpers
// ============================================================================

// Count inactive user-created scenes (excludes factory presets)
static uint16_t count_inactive_user_scenes(void) {
  uint16_t total = scene_get_total_count();
  uint16_t inactive = 0;
  for (uint16_t i = 0; i < total; i++) {
    if (scene_is_active_by_position(i)) continue;
    if (scene_is_factory_by_position(i)) continue;
    inactive++;
  }
  return inactive;
}

// Count inactive factory preset scenes
static uint16_t count_factory_scenes(void) {
  uint16_t total = scene_get_total_count();
  uint16_t factory = 0;
  for (uint16_t i = 0; i < total; i++) {
    if (scene_is_active_by_position(i)) continue;
    if (!scene_is_factory_by_position(i)) continue;
    factory++;
  }
  return factory;
}

// ============================================================================
// Reorder Roller
// ============================================================================

static void reorder_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  uint16_t target_position = (uint16_t)selected_index;
  if (target_position == s_selected_position) {
    // No change needed
    menu_navigate_back_then_to(3, "Scenes", menu_page_scenes_create);
    return;
  }
  
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  uint8_t target_index = scene_get_index_by_position(target_position);
  
  esp_err_t ret = scene_reorder(scene_index, target_index);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Reordered scene from position %u to %u",
      (unsigned)(s_selected_position + 1), (unsigned)(target_position + 1));
    s_selected_position = target_position;
  } else {
    ESP_LOGW(TAG, "Failed to reorder scene: %s", esp_err_to_name(ret));
  }

  menu_set_restore_focus(manifest_position_to_clickable_focus(target_position));
  menu_navigate_back_then_to(3, "Scenes", menu_page_scenes_create);
}

static lv_obj_t* reorder_roller_create(void) {
  uint16_t total = scene_get_total_count();
  
  // Build options string with ordinal + scene name for each position
  s_reorder_options[0] = '\0';
  char* pos = s_reorder_options;
  size_t remaining = sizeof(s_reorder_options);
  
  for (uint16_t i = 0; i < total && remaining > 40; i++) {
    const char* name = scene_get_name_by_position(i);
    if (!name) name = "Untitled";
    
    int written = snprintf(pos, remaining, "%s%u. %.20s",
      i > 0 ? "\n" : "", (unsigned)(i + 1), name);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
  }
  
  return menu_create_roller_page("Move To", s_reorder_options, s_selected_position,
    reorder_confirm_cb, NULL);
}

static void nav_to_reorder(void* user_data) {
  (void)user_data;
  menu_navigate_to("Reorder", reorder_roller_create);
}

// ============================================================================
// Scene Actions
// ============================================================================

static void action_delete_scene(void* user_data) {
  (void)user_data;
  uint16_t total = scene_get_total_count();
  if (total <= 1) {
    ESP_LOGW(TAG, "Cannot delete last scene");
    menu_navigate_back();
    return;
  }
  
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  
  esp_err_t ret = scene_delete(scene_index);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Deleted scene at position %u", (unsigned)s_selected_position);
    // Focus on scene above (or first scene if deleted first)
    if (s_selected_position > 0) s_selected_position--;
  } else {
    ESP_LOGW(TAG, "Failed to delete scene: %s", esp_err_to_name(ret));
  }
  
  // Refresh list
  menu_set_restore_focus((int)s_selected_position);
  menu_navigate_back_then_to(2, "Scenes", menu_page_scenes_create);
}

static void action_edit_scene(void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  
  // Switch to this scene and open its editor
  esp_err_t ret = scene_set_current(scene_index);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to switch to scene: %s", esp_err_to_name(ret));
  }
  
  // Navigate to scene editor (back 2 from action menu, push scene page)
  // Focus first item, not the scenes list position
  menu_set_restore_focus(0);
  menu_navigate_back_then_to(2, "Scene", menu_page_current_scene_create);
}

static void action_duplicate_scene(void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  const char* source_name = scene_get_name_by_position(s_selected_position);

  // Build "Copy of <name>", truncated to fit 16 chars
  // If collision, try "Copy of X 2", "Copy of X 3", etc.
  char dup_name[17];
  snprintf(dup_name, sizeof(dup_name), "Copy of %.8s",
    source_name ? source_name : "Scene");

  esp_err_t ret = scene_duplicate(scene_index, dup_name);
  
  // Retry with numeric suffix if name collision
  for (int suffix = 2; suffix <= 99 && ret == ESP_ERR_INVALID_ARG; suffix++) {
    snprintf(dup_name, sizeof(dup_name), "Copy of %.5s %d",
      source_name ? source_name : "Scene", suffix);
    ret = scene_duplicate(scene_index, dup_name);
  }

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Duplicated scene %u as \"%s\"",
      (unsigned)(s_selected_position + 1), dup_name);
    // Focus on the new copy (inserted right after source)
    s_selected_position++;
  } else {
    ESP_LOGW(TAG, "Failed to duplicate scene: %s", esp_err_to_name(ret));
  }

  menu_set_restore_focus((int)s_selected_position);
  menu_navigate_back_then_to(2, "Scenes", menu_page_scenes_create);
}

static void action_activate_scene(void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  esp_err_t ret = scene_set_active(scene_index, true);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Activated scene at position %u", (unsigned)s_selected_position);
  } else {
    ESP_LOGW(TAG, "Failed to activate scene: %s", esp_err_to_name(ret));
  }
  menu_set_restore_focus((int)s_selected_position);
  menu_navigate_back_then_to(2, "Scenes", menu_page_scenes_create);
}

static void action_deactivate_scene(void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  esp_err_t ret = scene_set_active(scene_index, false);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Deactivated scene at position %u", (unsigned)s_selected_position);
  } else {
    ESP_LOGW(TAG, "Failed to deactivate scene: %s", esp_err_to_name(ret));
  }
  menu_set_restore_focus((int)s_selected_position);
  menu_navigate_back_then_to(2, "Scenes", menu_page_scenes_create);
}

static lv_obj_t* scene_action_menu_create(void) {
  const char* scene_name = scene_get_name_by_position(s_selected_position);
  bool is_active = scene_is_active_by_position(s_selected_position);
  uint16_t active_count = scene_get_count();
  uint16_t total = scene_get_total_count();
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  bool is_current = (scene_index == scene_get_current_index());
  
  static menu_item_t action_items[6];
  int idx = 0;
  
  if (is_active) {
    // Active scene: Edit, Duplicate, Reorder, Deactivate, Delete
    action_items[idx++] = (menu_item_t){
      "Edit", action_edit_scene, NULL, true, MENU_ITEM_KIND_SUBMENU
    };
    action_items[idx++] = (menu_item_t){
      "Duplicate", action_duplicate_scene, NULL, true, MENU_ITEM_KIND_ACTION
    };

    if (active_count > 1) {
      action_items[idx++] = (menu_item_t){
        "Reorder", nav_to_reorder, NULL, true, MENU_ITEM_KIND_ROLLER
      };
    }

    // Deactivate (not if current scene or last active)
    if (!is_current && active_count > 1) {
      action_items[idx++] = (menu_item_t){
        "Deactivate", action_deactivate_scene, NULL, false, MENU_ITEM_KIND_ACTION
      };
    }

    // Delete (not if only one scene total)
    if (total > 1 && !is_current) {
      action_items[idx++] = (menu_item_t){
        "Delete", action_delete_scene, NULL, false, MENU_ITEM_KIND_ACTION
      };
    }
  } else {
    // Inactive scene: Activate, Delete
    action_items[idx++] = (menu_item_t){
      "Activate", action_activate_scene, NULL, false, MENU_ITEM_KIND_ACTION
    };
    action_items[idx++] = (menu_item_t){
      "Duplicate", action_duplicate_scene, NULL, true, MENU_ITEM_KIND_ACTION
    };

    if (total > 1) {
      action_items[idx++] = (menu_item_t){
        "Delete", action_delete_scene, NULL, false, MENU_ITEM_KIND_ACTION
      };
    }
  }
  
  // Build title - show ordinal for active scenes, just name for inactive
  static char title[48];
  if (is_active) {
    // Compute active ordinal for this scene
    uint16_t ordinal = 0;
    for (uint16_t i = 0; i <= s_selected_position; i++) {
      if (scene_is_active_by_position(i)) ordinal++;
    }
    snprintf(title, sizeof(title), "%u. %.24s",
      (unsigned)ordinal, scene_name ? scene_name : "Untitled");
  } else {
    snprintf(title, sizeof(title), "%.30s",
      scene_name ? scene_name : "Untitled");
  }
  
  return menu_create_page(title, action_items, idx);
}

// ============================================================================
// Add Scene
// ============================================================================

static void action_add_scene(void* user_data) {
  (void)user_data;
  
  // Generate a unique name (retry if collision)
  char name[17];
  esp_err_t ret = ESP_ERR_INVALID_ARG;
  for (int attempt = 0; attempt < 10 && ret == ESP_ERR_INVALID_ARG; attempt++) {
    scene_name_generate(name, sizeof(name));
    ret = scene_create_new(name);
  }
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Created new scene: %s", name);
    // Focus on new scene (at the end of active scenes)
    s_selected_position = scene_get_total_count() - 1;
  } else {
    ESP_LOGW(TAG, "Failed to create scene: %s", esp_err_to_name(ret));
  }
  
  // Refresh list with focus on new scene
  menu_set_restore_focus((int)s_selected_position);
  menu_replace_current("Scenes", menu_page_scenes_create);
}

// ============================================================================
// Scene List Navigation
// ============================================================================

static void nav_to_scene_action(void* user_data) {
  // user_data is the manifest position cast to pointer
  s_selected_position = (uint16_t)(uintptr_t)user_data;
  menu_navigate_to("Scene", scene_action_menu_create);
}

// ============================================================================
// Main Scenes Page
// ============================================================================

lv_obj_t* menu_page_scenes_create(void) {
  ensure_list_subscribed();
  ESP_LOGI(TAG, "Creating scenes page");
  
  uint16_t total = scene_get_total_count();
  int idx = 0;
  int label_idx = 0;
  uint16_t ordinal = 1;
  uint16_t inactive_user_count = count_inactive_user_scenes();
  uint16_t factory_count = count_factory_scenes();
  
  // First pass: active scenes with ordinal prefixes
  for (uint16_t i = 0; i < total && label_idx < 128; i++) {
    if (!scene_is_active_by_position(i)) continue;
    
    const char* name = scene_get_name_by_position(i);
    snprintf(s_scene_labels[label_idx], sizeof(s_scene_labels[label_idx]),
      "%u. %.24s", (unsigned)ordinal, name ? name : "Untitled");
    ordinal++;
    
    s_scene_items[idx++] = (menu_item_t){
      s_scene_labels[label_idx],
      nav_to_scene_action,
      (void*)(uintptr_t)i,
      true,
      MENU_ITEM_KIND_SUBMENU
    };
    label_idx++;
  }
  
  // Inactive user-created scenes section (if any)
  if (inactive_user_count > 0) {
    s_scene_items[idx++] = (menu_item_t){ "---", NULL, NULL, false, MENU_ITEM_KIND_DISPLAY };
    s_scene_items[idx++] = (menu_item_t){
      "Inactive", NULL, NULL, false, MENU_ITEM_KIND_HEADING
    };

    for (uint16_t i = 0; i < total && label_idx < 128; i++) {
      if (scene_is_active_by_position(i)) continue;
      if (scene_is_factory_by_position(i)) continue;

      const char* name = scene_get_name_by_position(i);
      snprintf(s_scene_labels[label_idx], sizeof(s_scene_labels[label_idx]),
        "%.28s", name ? name : "Untitled");

      s_scene_items[idx++] = (menu_item_t){
        s_scene_labels[label_idx],
        nav_to_scene_action,
        (void*)(uintptr_t)i,
        true,
        MENU_ITEM_KIND_SUBMENU
      };
      label_idx++;
    }
  }

  // Factory preset scenes section (if any)
  if (factory_count > 0) {
    if (inactive_user_count == 0) {
      s_scene_items[idx++] = (menu_item_t){ "---", NULL, NULL, false, MENU_ITEM_KIND_DISPLAY };
    }
    s_scene_items[idx++] = (menu_item_t){
      "Factory", NULL, NULL, false, MENU_ITEM_KIND_HEADING
    };

    for (uint16_t i = 0; i < total && label_idx < 128; i++) {
      if (scene_is_active_by_position(i)) continue;
      if (!scene_is_factory_by_position(i)) continue;

      const char* name = scene_get_name_by_position(i);
      snprintf(s_scene_labels[label_idx], sizeof(s_scene_labels[label_idx]),
        "%.28s", name ? name : "Untitled");

      s_scene_items[idx++] = (menu_item_t){
        s_scene_labels[label_idx],
        nav_to_scene_action,
        (void*)(uintptr_t)i,
        true,
        MENU_ITEM_KIND_SUBMENU
      };
      label_idx++;
    }
  }
  
  // Divider before Add Scene
  s_scene_items[idx++] = (menu_item_t){ "---", NULL, NULL, false, MENU_ITEM_KIND_DISPLAY };
  
  // Add Scene option
  s_scene_items[idx++] = (menu_item_t){
    "Add Scene", action_add_scene, NULL, false, MENU_ITEM_KIND_ACTION
  };
  
  return menu_create_page("Scenes", s_scene_items, idx);
}
