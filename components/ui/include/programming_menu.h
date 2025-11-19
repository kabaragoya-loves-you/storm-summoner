#ifndef PROGRAMMING_MENU_H
#define PROGRAMMING_MENU_H

#include "lvgl.h"
#include "ui.h"

// Menu item callback function type
typedef void (*programming_menu_item_cb_t)(void);

// Menu item structure
typedef struct {
  const char* label;
  programming_menu_item_cb_t callback;
  bool has_submenu;
} programming_menu_item_t;

// Initialize Programming menu system
void programming_menu_init(void);

// Create top-level menu
void programming_menu_create(void);

// Navigate to a sub-menu
void programming_menu_navigate_to(const char* menu_name, const programming_menu_item_t* items, int item_count);

// Navigate back one level (breadcrumb)
void programming_menu_navigate_back(void);

// Handle pad 8 (enter/confirm)
void programming_menu_handle_enter(void);

// Handle pad 12 (back/cancel)
void programming_menu_handle_back(void);

// Cleanup menu widgets
void programming_menu_cleanup(void);

// Check if menu is at top level
bool programming_menu_is_top_level(void);

// Get menu group for encoder attachment
lv_group_t* programming_menu_get_group(void);

#endif // PROGRAMMING_MENU_H

