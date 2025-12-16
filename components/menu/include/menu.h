#ifndef MENU_H
#define MENU_H

#include "lvgl.h"

#define MAX_MENU_STACK 8
#define MAX_MENU_ITEMS 32

// Menu item callback function type (receives optional user data)
typedef void (*menu_item_cb_t)(void* user_data);

// Menu item structure
typedef struct {
  const char* label;
  menu_item_cb_t callback;
  void* user_data;         // Optional data passed to callback
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

// Navigate back N levels, then optionally navigate to a new page
// If builder is NULL, just navigates back N levels
void menu_navigate_back_then_to(int levels, const char* menu_name,
  menu_page_builder_t builder);

// Replace the current menu page with a new one (synchronous, for use in callbacks)
// Removes current page from stack and pushes a new one
void menu_replace_current(const char* menu_name, menu_page_builder_t builder);

// Handle enter key (activate selected item)
// Returns true if an action was performed (item was clickable)
bool menu_handle_enter(void);

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

// Roller page callback - called when value is confirmed
// Parameters: selected_index (0-based), user_data
typedef void (*menu_roller_confirm_cb_t)(uint32_t selected_index, void* user_data);

// Helper: Create a roller selection page
// options: newline-separated string of options (e.g., "1\n2\n3")
// initial_index: 0-based index of initially selected item
// confirm_cb: called when user confirms selection (pad 8)
// user_data: passed to confirm_cb
lv_obj_t* menu_create_roller_page(const char* title, const char* options, 
  uint32_t initial_index, menu_roller_confirm_cb_t confirm_cb, void* user_data);

#endif // MENU_H

