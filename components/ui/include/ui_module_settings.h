#ifndef UI_MODULE_SETTINGS_H
#define UI_MODULE_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Setting value types
typedef enum {
  UI_SETTING_BOOL,
  UI_SETTING_U8,
  UI_SETTING_U16,
  UI_SETTING_I16,
  UI_SETTING_I32,
  UI_SETTING_FLOAT,
  UI_SETTING_ENUM      // String enum - uses enum_values array
} ui_setting_type_t;

// Optional callback when setting changes
typedef void (*ui_setting_callback_t)(void);

// Individual setting definition
typedef struct {
  const char* name;              // Setting name (e.g., "pulse", "body_ratio")
  ui_setting_type_t type;        // Value type
  void* value_ptr;               // Pointer to the actual value
  const char* description;       // Human-readable description
  ui_setting_callback_t on_change; // Optional: called after value is set
  const char** enum_values;      // For ENUM type: NULL-terminated array of valid strings
} ui_module_setting_t;

// Maximum settings per module
#define UI_MAX_SETTINGS_PER_MODULE 16
// Maximum number of modules that can register settings
#define UI_MAX_MODULES_WITH_SETTINGS 8

// Module registration (called from module init)
// Settings array must remain valid for the lifetime of the module
void ui_module_register_settings(const char* module_name,
                                  ui_module_setting_t* settings,
                                  size_t count);

// Unregister settings (called from module teardown)
void ui_module_unregister_settings(const char* module_name);

// Query API (for console)
// Returns pointer to settings array and sets count, or NULL if module not found
ui_module_setting_t* ui_module_get_settings(const char* module_name, size_t* count);

// Set a setting value from string
// Returns true on success, false on error (invalid module, setting, or value)
bool ui_module_set_setting(const char* module_name, const char* setting_name, 
                            const char* value);

// Get a setting value as string (for display)
// Returns true and fills buffer on success, false on error
bool ui_module_get_setting_str(const char* module_name, const char* setting_name,
                                char* buffer, size_t buffer_size);

// List all registered modules
// Returns count and fills names array (up to max_count)
size_t ui_module_list_registered(const char** names, size_t max_count);

#endif // UI_MODULE_SETTINGS_H

