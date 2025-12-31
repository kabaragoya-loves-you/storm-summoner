#ifndef ACTION_CONFIG_H
#define ACTION_CONFIG_H

#include "menu.h"
#include "action.h"

// Forward declaration
typedef struct action_config_context action_config_context_t;

// Callback when action configuration is complete
// context: the configuration context
// action: the configured action (already stored in target)
typedef void (*action_config_complete_cb_t)(action_config_context_t* ctx, action_t* action);

// Action configuration context
struct action_config_context {
  action_t* target_action;           // Where to store the configured action
  const char* source_title;          // Title for return navigation (e.g., "Alpha", "Switch")
  const char* detail_title;          // Title for the detail page (e.g., "Alpha", "Switch")
  menu_page_builder_t return_page;   // Page builder to return to after config
  uint8_t return_depth;              // How many menu levels to pop when returning
  action_config_complete_cb_t on_complete; // Optional callback when done
  void* user_data;                   // Optional user data
  bool exclude_hold_actions;         // Filter out hold-requiring actions (for bump)
  bool on_load_filter;               // Only show actions valid for on-load (CC, transport, etc.)
};

// Initialize action configuration module
void action_config_init(void);

// Start action configuration flow - navigates to detail page
// ctx: configuration context (caller must keep this alive until flow completes)
void action_config_start(action_config_context_t* ctx);

// Get the current action config context (for use in rollers)
action_config_context_t* action_config_get_context(void);

// Create the action detail page (shows action type + parameters)
lv_obj_t* action_config_detail_page_create(void);

// Create page for the action type roller
lv_obj_t* action_config_type_roller_create(void);

// Get display name for an action type
const char* action_config_get_display_name(action_type_t type);

// Cleanup action configuration resources
void action_config_cleanup(void);

#endif // ACTION_CONFIG_H

