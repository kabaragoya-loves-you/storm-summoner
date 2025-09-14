#pragma once

#include "ui.h"

/**
 * @brief Template UI module
 * 
 * This is a minimal example of a UI module that demonstrates:
 * - Proper LVGL screen creation and management
 * - Deferred drawing (waits for LVGL to be ready)
 * - Resource cleanup in teardown
 * - Module registration structure
 * 
 * To create your own UI module:
 * 1. Copy this template
 * 2. Replace "template" with your module name throughout
 * 3. Add your LVGL widgets in the draw_deferred_cb function
 * 4. Add any custom logic/timers/animations as needed
 * 5. Clean up all resources in teardown
 * 6. Add your module to ui.h and register it in main.c
 */

extern ui_draw_module_t template_module;
