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

// Small static storage for the main menu (always needed, minimal size)
static menu_item_t s_device_config_items[4];
static char s_current_pedal_label[80];

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
// Pedal Info Display
// ============================================================================

static void show_pedal_info(void* user_data) {
  (void)user_data;
  const device_config_t* cfg = device_config_get();
  
  // Load the device to get full info
  device_def_t* device = assets_load_device(cfg->pedal_slug);
  
  // Allocate info text in PSRAM
  char* info_text = heap_caps_malloc(512, MALLOC_CAP_SPIRAM);
  if (!info_text) {
    ESP_LOGE(TAG, "Failed to allocate info text");
    return;
  }
  
  if (!device) {
    snprintf(info_text, 512,
      "PEDAL INFO\n\n"
      "Slug: %s\n\n"
      "Device not found in database",
      cfg->pedal_slug[0] ? cfg->pedal_slug : "(none)");
    menu_navigate_to_info("Pedal Info", info_text);
    heap_caps_free(info_text);
    return;
  }
  
  // Build TRS type string
  const char* trs_str;
  switch (device->trs_type) {
    case MIDI_TRS_TYPE_A: trs_str = "Type A"; break;
    case MIDI_TRS_TYPE_B: trs_str = "Type B"; break;
    case MIDI_TRS_TYPE_TS: trs_str = "TS"; break;
    case MIDI_TRS_TYPE_BOTH: trs_str = "Both"; break;
    case MIDI_TRS_UNKNOWN:
    default: trs_str = "Not specified"; break;
  }
  
  // Build bank mode string
  const char* bank_str = "None";
  if (device->pc_info) {
    switch (device->pc_info->bank_mode) {
      case PC_BANK_SELECT_CC0: bank_str = "CC0"; break;
      case PC_BANK_SELECT_CC0_CC32: bank_str = "CC0+CC32"; break;
      default: break;
    }
  }
  
  snprintf(info_text, 512,
    "%s\n"
    "%s\n\n"
    "TRS: %s\n"
    "MIDI Ch: %d\n"
    "CC commands: %u\n"
    "Clock: %s\n"
    "Notes: %s\n"
    "Transmits: %s\n"
    "Preset slots: %u\n"
    "Bank mode: %s\n"
    "First preset: %d",
    device->name[0] ? device->name : "Unknown",
    device->vendor[0] ? device->vendor : "Unknown",
    trs_str,
    cfg->midi_channel,
    (unsigned)device->control_count,
    device->receives_clock ? "supported" : "unsupported",
    device->receives_notes ? "supported" : "unsupported",
    device->transmits_pc ? "yes" : "no",
    device->pc_info ? (unsigned)device->pc_info->count : 128,
    bank_str,
    device->pc_info ? (int)device->pc_info->index_base : 0);
  
  assets_free_device(device);
  menu_navigate_to_info("Pedal Info", info_text);
  heap_caps_free(info_text);
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
    
    // Free both menus before navigating
    dynamic_menu_free(&s_pedal_menu);
    dynamic_menu_free(&s_vendor_menu);
    
    // Navigate back 3 levels (Pedal -> Vendor -> Pedal Setup -> Menu)
    // then forward to rebuild Pedal Setup with the new pedal name
    menu_navigate_back_then_to(3, "Pedal Setup", menu_page_device_config_create);
  }
}

static lv_obj_t* menu_page_pedal_select_create(void) {
  ESP_LOGI(TAG, "Creating pedal select page for vendor: %s", s_selected_vendor);
  
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
  ESP_LOGI(TAG, "Creating device config page");
  
  // Clean up any leftover submenu allocations from previous navigation
  cleanup_submenus();
  
  const device_config_t* cfg = device_config_get();
  
  // Get display name for current pedal (no "Pedal:" prefix)
  device_def_t* device = assets_load_device(cfg->pedal_slug);
  if (device && device->name[0]) {
    strncpy(s_current_pedal_label, device->name, sizeof(s_current_pedal_label) - 1);
    s_current_pedal_label[sizeof(s_current_pedal_label) - 1] = '\0';
  } else {
    strncpy(s_current_pedal_label, "(no pedal)", sizeof(s_current_pedal_label) - 1);
    s_current_pedal_label[sizeof(s_current_pedal_label) - 1] = '\0';
  }
  if (device) assets_free_device(device);
  
  // Build menu items (small static array)
  s_device_config_items[0] = (menu_item_t){ s_current_pedal_label, nav_to_vendor_select, NULL, true };
  s_device_config_items[1] = (menu_item_t){ "Pedal Info", show_pedal_info, NULL, false };
  s_device_config_items[2] = (menu_item_t){ "Refresh", refresh_pedal, NULL, false };
  
  return menu_create_page("Pedal Setup", s_device_config_items, 3);
}
