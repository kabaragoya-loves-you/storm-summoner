#ifndef _NAME_EDIT_H
#define _NAME_EDIT_H

#include "ui.h"
#include "text_edit.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Name edit UI module
 * Provides a text editor with character gallery for editing scene names
 * This is a convenience wrapper around text_edit for scene name editing.
 */
extern ui_draw_module_t name_edit_module;

/**
 * Set the name to edit (scene-specific convenience function)
 * Must be called before activating the module
 * @param name Current name (will be copied)
 * @param scene_index Scene index to save to when confirmed
 */
void name_edit_set_name(const char* name, uint8_t scene_index);

/**
 * Handle pad input from ui_event_handler
 * @param pad_id Logical pad ID (8, 9, 10, 11, 12)
 * @param pressed True for press, false for release
 * @return True if handled
 */
bool name_edit_handle_pad(uint8_t pad_id, bool pressed);

/**
 * Handle touchwheel delta from ui_event_handler
 * @param delta Wheel delta (-1 or +1)
 */
void name_edit_handle_wheel(int delta);

/**
 * Check if name editor is currently active
 */
bool name_edit_is_active(void);

#endif /* _NAME_EDIT_H */
