#include "menu.h"
#include "menu_pages.h"
#include "version.h"
#include "revision.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_ABOUT"

// Static storage for menu items and labels
#define MAX_ABOUT_ITEMS 9
static menu_item_t s_about_items[MAX_ABOUT_ITEMS];
static char s_labels[MAX_ABOUT_ITEMS][48];

lv_obj_t* menu_page_about_create(void) {
  ESP_LOGI(TAG, "Creating about page");
  
  int idx = 0;
  
  // Version (major.minor only)
  snprintf(s_labels[idx], sizeof(s_labels[0]), "Version\n%u.%u",
    version_get_major(), version_get_minor());
  s_about_items[idx] = (menu_item_t){ s_labels[idx], NULL, NULL, false };
  idx++;
  
  // Git Commit
  snprintf(s_labels[idx], sizeof(s_labels[0]), "Git Commit\n%s", version_get_git_hash());
  s_about_items[idx] = (menu_item_t){ s_labels[idx], NULL, NULL, false };
  idx++;
  
  // Serial
  snprintf(s_labels[idx], sizeof(s_labels[0]), "Serial\n%s", version_get_serial());
  s_about_items[idx] = (menu_item_t){ s_labels[idx], NULL, NULL, false };
  idx++;
  
  // Build number
  snprintf(s_labels[idx], sizeof(s_labels[0]), "Build\n%lu", (unsigned long)version_get_build());
  s_about_items[idx] = (menu_item_t){ s_labels[idx], NULL, NULL, false };
  idx++;
  
  // Assets checksum
  snprintf(s_labels[idx], sizeof(s_labels[0]), "Assets\n%s", version_get_assets_checksum());
  s_about_items[idx] = (menu_item_t){ s_labels[idx], NULL, NULL, false };
  idx++;
  
  // Hardware revision
  snprintf(s_labels[idx], sizeof(s_labels[0]), "Hardware\n%s", revision_get_string());
  s_about_items[idx] = (menu_item_t){ s_labels[idx], NULL, NULL, false };
  idx++;
  
  // LittleFS storage
  size_t total_bytes = 0, used_bytes = 0;
  if (esp_littlefs_info("assets", &total_bytes, &used_bytes) == ESP_OK) {
    // Convert to KB for readability
    snprintf(s_labels[idx], sizeof(s_labels[0]), "Storage\n%uK / %uK", 
      (unsigned)(used_bytes / 1024), (unsigned)(total_bytes / 1024));
  } else {
    snprintf(s_labels[idx], sizeof(s_labels[0]), "Storage\nunavailable");
  }
  s_about_items[idx] = (menu_item_t){ s_labels[idx], NULL, NULL, false };
  idx++;

  // Identity
  s_about_items[idx] = (menu_item_t){ "Lizard\nVoronox", NULL, NULL, false };
  idx++;
  
  ESP_LOGI(TAG, "About page: %d items", idx);
  return menu_create_page_2line("About", s_about_items, idx);
}

