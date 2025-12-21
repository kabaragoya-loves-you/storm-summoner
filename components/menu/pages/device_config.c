#include "menu.h"
#include "menu_pages.h"
#include "device_config.h"
#include "assets_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define TAG "MENU_DEVICE_CONFIG"

// Static storage for main menu (16 items max: name, refresh, midi ch, preset lock, divider, 10 info labels)
#define MAX_DEVICE_CONFIG_ITEMS 16
static menu_item_t s_device_config_items[MAX_DEVICE_CONFIG_ITEMS];
static char s_current_pedal_label[80];
static char s_midi_ch_label[24];
static char s_preset_lock_label[32];
static char s_info_labels[10][48];  // TRS, CC count, Clock, Notes, Transmits, Slots, Bank, First preset, etc.

// Dynamic storage - allocated in PSRAM only when needed
typedef struct {
  menu_item_t* items;
  char (*labels)[64];
  uint32_t* indices;
  uint32_t count;
  uint32_t capacity;
} dynamic_menu_t;

static dynamic_menu_t s_vendor_menu = {0};
static dynamic_menu_t s_pedal_menu = {0};
static char s_selected_vendor[64];
static char s_pedal_title[80];

// Forward declarations
static lv_obj_t* menu_page_vendor_select_create(void);
static lv_obj_t* menu_page_pedal_select_create(void);

// ============================================================================
// Dynamic Menu Allocation (PSRAM)
// ============================================================================

static void dynamic_menu_free(dynamic_menu_t* menu) {
  if (menu->items) {
    heap_caps_free(menu->items);
    menu->items = NULL;
  }
  if (menu->labels) {
    heap_caps_free(menu->labels);
    menu->labels = NULL;
  }
  if (menu->indices) {
    heap_caps_free(menu->indices);
    menu->indices = NULL;
  }
  menu->count = 0;
  menu->capacity = 0;
}

static bool dynamic_menu_alloc(dynamic_menu_t* menu, uint32_t count) {
  // Free any existing allocation
  dynamic_menu_free(menu);
  
  if (count == 0) return true;
  
  // Allocate in PSRAM
  menu->items = heap_caps_calloc(count, sizeof(menu_item_t), MALLOC_CAP_SPIRAM);
  menu->labels = heap_caps_calloc(count, 64, MALLOC_CAP_SPIRAM);
  menu->indices = heap_caps_calloc(count, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
  
  if (!menu->items || !menu->labels || !menu->indices) {
    ESP_LOGE(TAG, "Failed to allocate dynamic menu for %lu items", (unsigned long)count);
    dynamic_menu_free(menu);
    return false;
  }
  
  menu->capacity = count;
  menu->count = count;
  ESP_LOGD(TAG, "Allocated dynamic menu: %lu items in PSRAM", (unsigned long)count);
  return true;
}

// ============================================================================
// MIDI Channel Selection (Roller)
// ============================================================================

// Roller confirmation callback - save channel to NVS and rebuild menu
static void midi_channel_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint8_t new_channel = (uint8_t)(selected_index + 1);  // Convert to 1-16
  
  ESP_LOGI(TAG, "MIDI channel changed to: %u", (unsigned)new_channel);
  device_config_set_channel(new_channel);
  device_config_save();
  
  // Navigate back 2 levels (roller -> old Pedal Setup -> Index) 
  // then navigate to fresh Pedal Setup to show new channel
  menu_navigate_back_then_to(2, "Pedal Setup", menu_page_device_config_create);
}

// Builder function for roller page
static lv_obj_t* midi_channel_roller_create(void) {
  const device_config_t* cfg = device_config_get();
  uint32_t current_channel = cfg->midi_channel;
  
  return menu_create_roller_page("MIDI Channel",
    "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16",
    current_channel - 1,  // 0-based index
    midi_channel_confirm_cb, NULL);
}

static void nav_to_midi_channel_select(void* user_data) {
  (void)user_data;
  menu_navigate_to("MIDI Channel", midi_channel_roller_create);
}

// ============================================================================
// Preset Lock Toggle
// ============================================================================

static void preset_lock_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  bool lock = (selected_index == 0);  // On=0, Off=1
  
  ESP_LOGI(TAG, "Preset lock set to: %s", lock ? "On" : "Off");
  device_config_set_lock_preset_range(lock);
  
  // Navigate back to rebuilt Pedal Setup
  menu_navigate_back_then_to(2, "Pedal Setup", menu_page_device_config_create);
}

static lv_obj_t* preset_lock_roller_create(void) {
  bool current_lock = device_config_get_lock_preset_range();
  
  return menu_create_roller_page("Preset Lock",
    "On\nOff",
    current_lock ? 0 : 1,  // On=0, Off=1
    preset_lock_confirm_cb, NULL);
}

static void nav_to_preset_lock(void* user_data) {
  (void)user_data;
  menu_navigate_to("Preset Lock", preset_lock_roller_create);
}

// ============================================================================
// Pedal Selection - Single callback using user_data for index
// ============================================================================

static void select_pedal_callback(void* user_data) {
  uint32_t idx = *(uint32_t*)user_data;
  
  const char* slug = NULL;
  const char* name = NULL;
  
  if (assets_get_device_for_vendor(s_selected_vendor, idx, &slug, &name) == ESP_OK && slug) {
    ESP_LOGI(TAG, "Selected pedal: %s", slug);
    device_config_set_pedal(slug);
    device_config_save();

    // Navigate back 3 levels (Pedal -> Vendor -> Pedal Setup -> Menu)
    // then forward to rebuild Pedal Setup with the new pedal name
    // Note: don't free menus here - cleanup_submenus() handles it safely
    // after old screens are deleted to avoid use-after-free
    menu_navigate_back_then_to(3, "Pedal Setup", menu_page_device_config_create);
  }
}

static lv_obj_t* menu_page_pedal_select_create(void) {
  ESP_LOGD(TAG, "Creating pedal select page for vendor: %s", s_selected_vendor);
  
  uint32_t pedal_count = assets_get_device_count_for_vendor(s_selected_vendor);
  
  // Allocate dynamic menu in PSRAM
  if (!dynamic_menu_alloc(&s_pedal_menu, pedal_count)) {
    ESP_LOGE(TAG, "Failed to allocate pedal menu");
    // Return a fallback error page
    static menu_item_t error_items[] = {{ "Memory Error", NULL, NULL, false }};
    return menu_create_page("Error", error_items, 1);
  }
  
  // Build menu items for each pedal
  for (uint32_t i = 0; i < pedal_count; i++) {
    const char* slug = NULL;
    const char* name = NULL;
    
    if (assets_get_device_for_vendor(s_selected_vendor, i, &slug, &name) == ESP_OK) {
      strncpy(s_pedal_menu.labels[i], name ? name : "Unknown", 63);
      s_pedal_menu.labels[i][63] = '\0';
    } else {
      snprintf(s_pedal_menu.labels[i], 64, "Pedal %u", (unsigned)(i + 1));
    }
    
    // Store index for callback
    s_pedal_menu.indices[i] = i;
    
    s_pedal_menu.items[i].label = s_pedal_menu.labels[i];
    s_pedal_menu.items[i].callback = select_pedal_callback;
    s_pedal_menu.items[i].user_data = &s_pedal_menu.indices[i];
    s_pedal_menu.items[i].has_submenu = false;
  }
  
  // Build title with vendor name
  snprintf(s_pedal_title, sizeof(s_pedal_title), "%s", s_selected_vendor);
  
  return menu_create_page(s_pedal_title, s_pedal_menu.items, pedal_count);
}

// ============================================================================
// Vendor Selection - Single callback using user_data for index
// ============================================================================

static void select_vendor_callback(void* user_data) {
  uint32_t idx = *(uint32_t*)user_data;
  
  const char* vendor = assets_get_vendor_by_index(idx);
  if (vendor) {
    strncpy(s_selected_vendor, vendor, sizeof(s_selected_vendor) - 1);
    s_selected_vendor[sizeof(s_selected_vendor) - 1] = '\0';
    ESP_LOGI(TAG, "Selected vendor: %s", s_selected_vendor);
    menu_navigate_to("Select Pedal", menu_page_pedal_select_create);
  }
}

static lv_obj_t* menu_page_vendor_select_create(void) {
  ESP_LOGI(TAG, "Creating vendor select page");
  
  uint32_t vendor_count = assets_get_vendor_count();
  
  // Allocate dynamic menu in PSRAM
  if (!dynamic_menu_alloc(&s_vendor_menu, vendor_count)) {
    ESP_LOGE(TAG, "Failed to allocate vendor menu");
    // Return a fallback error page
    static menu_item_t error_items[] = {{ "Memory Error", NULL, NULL, false }};
    return menu_create_page("Error", error_items, 1);
  }
  
  // Build menu items for each vendor
  for (uint32_t i = 0; i < vendor_count; i++) {
    const char* vendor = assets_get_vendor_by_index(i);
    if (vendor) {
      strncpy(s_vendor_menu.labels[i], vendor, 63);
      s_vendor_menu.labels[i][63] = '\0';
    } else {
      snprintf(s_vendor_menu.labels[i], 64, "Vendor %u", (unsigned)(i + 1));
    }
    
    // Store index for callback
    s_vendor_menu.indices[i] = i;
    
    s_vendor_menu.items[i].label = s_vendor_menu.labels[i];
    s_vendor_menu.items[i].callback = select_vendor_callback;
    s_vendor_menu.items[i].user_data = &s_vendor_menu.indices[i];
    s_vendor_menu.items[i].has_submenu = true;
  }
  
  return menu_create_page("Select Vendor", s_vendor_menu.items, vendor_count);
}

// ============================================================================
// Navigation to vendor select
// ============================================================================

static void nav_to_vendor_select(void* user_data) {
  (void)user_data;
  menu_navigate_to("Select Vendor", menu_page_vendor_select_create);
}

// ============================================================================
// Cleanup - free any allocated submenus
// ============================================================================

static void cleanup_submenus(void) {
  if (s_vendor_menu.items || s_pedal_menu.items) {
    ESP_LOGD(TAG, "Cleaning up submenu allocations");
  }
  dynamic_menu_free(&s_vendor_menu);
  dynamic_menu_free(&s_pedal_menu);
}

// Public cleanup function - call after menu teardown to free PSRAM
void menu_page_device_config_cleanup(void) {
  cleanup_submenus();
}

// ============================================================================
// Refresh - reload pedal data from JSON
// ============================================================================

static void refresh_pedal(void* user_data) {
  (void)user_data;
  const device_config_t* cfg = device_config_get();
  
  ESP_LOGI(TAG, "Refreshing pedal data for: %s", cfg->pedal_slug);
  
  // Delete cache and force reload from JSON
  esp_err_t err = assets_manager_reload_device(cfg->pedal_slug);
  if (err == ESP_OK) {
    // Load the fresh device to verify and log
    device_def_t* device = assets_load_device(cfg->pedal_slug);
    if (device) {
      ESP_LOGI(TAG, "Pedal refreshed: %s by %s", device->name, device->vendor);
      assets_free_device(device);
    }
  } else {
    ESP_LOGW(TAG, "Failed to reload pedal: %s (err=%d)", cfg->pedal_slug, err);
  }
  
  // Replace current page with a fresh one (synchronous, no deferred navigation)
  menu_replace_current("Pedal Setup", menu_page_device_config_create);
}

// ============================================================================
// Main Device Config Page
// ============================================================================

lv_obj_t* menu_page_device_config_create(void) {
  ESP_LOGD(TAG, "Creating device config page");
  
  // Note: Don't call cleanup_submenus() here! Old screens (being deleted after this)
  // still have event handlers pointing to that memory. The submenus will be freed
  // when recreated via dynamic_menu_alloc() (which frees first) or on menu teardown.
  
  const device_config_t* cfg = device_config_get();
  device_def_t* device = assets_load_device(cfg->pedal_slug);
  
  int item_idx = 0;
  
  // Item 0: Current pedal name (clickable -> vendor select)
  if (device && device->name[0]) {
    strncpy(s_current_pedal_label, device->name, sizeof(s_current_pedal_label) - 1);
    s_current_pedal_label[sizeof(s_current_pedal_label) - 1] = '\0';
  } else {
    strncpy(s_current_pedal_label, "(no pedal)", sizeof(s_current_pedal_label) - 1);
  }
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_current_pedal_label, nav_to_vendor_select, NULL, true };
  
  // Item 1: MIDI Channel (clickable -> roller)
  snprintf(s_midi_ch_label, sizeof(s_midi_ch_label), "MIDI Ch: %d", cfg->midi_channel);
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_midi_ch_label, nav_to_midi_channel_select, NULL, false };
  
  // Item 2: Preset Lock toggle (clickable -> roller)
  bool preset_lock = device_config_get_lock_preset_range();
  snprintf(s_preset_lock_label, sizeof(s_preset_lock_label), "Preset lock: %s",
    preset_lock ? "On" : "Off");
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_preset_lock_label, nav_to_preset_lock, NULL, false };
  
  // Divider
  s_device_config_items[item_idx++] = (menu_item_t){ "---", NULL, NULL, false };
  
  // Build info labels (read-only, NULL callback)
  int info_idx = 0;
  
  // TRS Type
  const char* trs_str = "Not specified";
  if (device) {
    switch (device->trs_type) {
      case MIDI_TRS_TYPE_A: trs_str = "Type A"; break;
      case MIDI_TRS_TYPE_B: trs_str = "Type B"; break;
      case MIDI_TRS_TYPE_TS: trs_str = "TS"; break;
      case MIDI_TRS_TYPE_BOTH: trs_str = "Both"; break;
      default: break;
    }
  }
  snprintf(s_info_labels[info_idx], sizeof(s_info_labels[0]), "TRS: %s", trs_str);
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_info_labels[info_idx++], NULL, NULL, false };
  
  // CC commands count
  unsigned cc_count = device ? (unsigned)device->control_count : 0;
  snprintf(s_info_labels[info_idx], sizeof(s_info_labels[0]), "CC commands: %u", cc_count);
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_info_labels[info_idx++], NULL, NULL, false };
  
  // Clock support
  bool clock = device ? device->receives_clock : false;
  snprintf(s_info_labels[info_idx], sizeof(s_info_labels[0]), 
    "Clock: %s", clock ? "supported" : "unsupported");
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_info_labels[info_idx++], NULL, NULL, false };
  
  // Notes support
  bool notes = device ? device->receives_notes : false;
  snprintf(s_info_labels[info_idx], sizeof(s_info_labels[0]), 
    "Notes: %s", notes ? "supported" : "unsupported");
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_info_labels[info_idx++], NULL, NULL, false };
  
  // Transmits PC
  bool transmits = device ? device->transmits_pc : false;
  snprintf(s_info_labels[info_idx], sizeof(s_info_labels[0]), 
    "Transmits: %s", transmits ? "yes" : "no");
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_info_labels[info_idx++], NULL, NULL, false };
  
  // Preset slots
  unsigned slots = (device && device->pc_info) ? (unsigned)device->pc_info->count : 128;
  snprintf(s_info_labels[info_idx], sizeof(s_info_labels[0]), "Preset slots: %u", slots);
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_info_labels[info_idx++], NULL, NULL, false };
  
  // Bank mode
  const char* bank_str = "None";
  if (device && device->pc_info) {
    switch (device->pc_info->bank_mode) {
      case PC_BANK_SELECT_CC0: bank_str = "CC0"; break;
      case PC_BANK_SELECT_CC0_CC32: bank_str = "CC0+CC32"; break;
      default: break;
    }
  }
  snprintf(s_info_labels[info_idx], sizeof(s_info_labels[0]), "Bank mode: %s", bank_str);
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_info_labels[info_idx++], NULL, NULL, false };
  
  // First preset
  int first_preset = (device && device->pc_info) ? (int)device->pc_info->index_base : 0;
  snprintf(s_info_labels[info_idx], sizeof(s_info_labels[0]), "First preset: %d", first_preset);
  s_device_config_items[item_idx++] = 
    (menu_item_t){ s_info_labels[info_idx++], NULL, NULL, false };
  
  // Second divider after info labels
  s_device_config_items[item_idx++] = (menu_item_t){ "---", NULL, NULL, false };
  
  // Refresh command at bottom (clickable)
  s_device_config_items[item_idx++] = 
    (menu_item_t){ "Refresh", refresh_pedal, NULL, false };
  
  if (device) assets_free_device(device);
  
  ESP_LOGD(TAG, "Device config page: %d items", item_idx);
  return menu_create_page("Pedal Setup", s_device_config_items, item_idx);
}
