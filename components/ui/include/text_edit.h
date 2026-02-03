#ifndef _TEXT_EDIT_H
#define _TEXT_EDIT_H

#include "ui.h"
#include <stdint.h>
#include <stdbool.h>

// Maximum text length supported by the editor
#define TEXT_EDIT_MAX_LEN 32

/**
 * Callback invoked when text editing is confirmed
 * @param text The final edited text
 * @param user_data Context passed to text_edit_start
 */
typedef void (*text_edit_confirm_cb_t)(const char* text, void* user_data);

/**
 * Callback invoked when text editing is cancelled
 * @param user_data Context passed to text_edit_start
 */
typedef void (*text_edit_cancel_cb_t)(void* user_data);

/**
 * Text edit configuration
 */
typedef struct {
  const char* title;              // Screen title (e.g., "Edit Name")
  const char* initial_text;       // Initial text to edit
  uint8_t max_length;             // Maximum allowed length (1-32)
  text_edit_confirm_cb_t on_confirm;  // Called when user confirms
  text_edit_cancel_cb_t on_cancel;    // Called when user cancels (optional)
  void* user_data;                // Context passed to callbacks
} text_edit_config_t;

/**
 * Text edit UI module
 * Provides a text editor with character gallery
 */
extern ui_draw_module_t text_edit_module;

/**
 * Start text editing with the given configuration
 * Automatically activates the text_edit_module
 * @param config Editor configuration (copied internally)
 */
void text_edit_start(const text_edit_config_t* config);

/**
 * Handle pad input from ui_event_handler
 * @param pad_id Logical pad ID
 * @param pressed True for press, false for release
 * @return True if handled
 */
bool text_edit_handle_pad(uint8_t pad_id, bool pressed);

/**
 * Handle touchwheel delta from ui_event_handler
 * @param delta Wheel delta (-1 or +1)
 */
void text_edit_handle_wheel(int delta);

/**
 * Check if text editor is currently active
 */
bool text_edit_is_active(void);

/**
 * Check if text editor exit is pending (used to consume release events)
 */
bool text_edit_exit_pending(void);

#endif /* _TEXT_EDIT_H */
