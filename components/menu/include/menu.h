#ifndef MENU_H
#define MENU_H

#include "lvgl.h"

#define MAX_MENU_STACK 8
#define MAX_MENU_ITEMS 32

// Menu item callback function type
typedef void (*menu_item_cb_t)(void);

// Menu item structure
typedef struct {
  const char* label;
  menu_item_cb_t callback;
  bool has_submenu;
} menu_item_t;

// Menu page builder function type
// Returns: screen object with list widget
typedef lv_obj_t* (*menu_page_builder_t)(void);

// Initialize menu system
void menu_init(void);

// Create and show top-level menu
void menu_create(void);

// Navigate to a sub-menu (pass builder function that creates the page)
void menu_navigate_to(const char* menu_name, menu_page_builder_t builder);

// Navigate back one level
void menu_navigate_back(void);

// Handle enter key (activate selected item)
void menu_handle_enter(void);

// Handle back key
void menu_handle_back(void);

// Cleanup menu widgets
void menu_cleanup(void);

// Check if menu is at top level
bool menu_is_top_level(void);

// Get menu group for encoder attachment
lv_group_t* menu_get_group(void);

// Get current menu screen (for restoration after screensaver)
lv_obj_t* menu_get_current_screen(void);

// Helper: Create a menu screen with title and items
lv_obj_t* menu_create_page(const char* title, const menu_item_t* items, int item_count);

// Helper: Navigate to an info page
void menu_navigate_to_info(const char* title, const char* info_text);

// Helper: Create a scrollable info page with formatted text
lv_obj_t* menu_create_info_page(const char* title, const char* info_text);

// Helper: Create a page with action buttons
lv_obj_t* menu_create_action_page(const char* title, const menu_item_t* items, int item_count);

#endif // MENU_H

