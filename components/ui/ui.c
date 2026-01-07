#include "lvgl.h"
#include "ui.h"
#include "shared_canvas_buffer.h"
#include "touchwheel.h"
#include "touchwheel_outputs.h"
#include "touch.h"
#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "input_manager.h"
#include "midi_cv_scene_handler.h"
#include "midi_expression_scene_handler.h"
#include "midi_als_scene_handler.h"
#include "midi_proximity_scene_handler.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

void ui_event_handler_init(void);

lv_obj_t *canvas = NULL;
static lv_timer_t *g_ui_refresh_timer = NULL;
static ui_draw_module_t* current_draw_module = NULL;

// Buffer handoff state - no allocation/deallocation, just coordination
static bool g_ui_has_buffer = false;  // True when UI is actively using the shared buffer
static bool g_handoff_in_progress = false;
static lv_timer_t *g_handoff_timer = NULL;
static void (*g_post_release_callback)(void) = NULL;

#define TAG "UI"

app_mode_t g_app_mode = APP_MODE_PERFORMANCE;
bool g_at_programming_top_level_menu = false;

// Touchwheel instance for LVGL encoder in programming mode
static touchwheel_instance_t* s_ui_touchwheel = NULL;
static touchwheel_output_t* s_ui_touchwheel_output = NULL;

// Saved Performance mode draw module when entering Programming mode
static ui_draw_module_t* saved_draw_module = NULL;

void lvgl_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  if (canvas != NULL) lv_obj_invalidate(canvas);
}

// Pending module for deferred switch
static ui_draw_module_t* pending_module = NULL;

// Deferred callback to switch modules in LVGL context
static void deferred_module_switch_cb(lv_timer_t *timer) {
  lv_timer_delete(timer);
  
  ui_draw_module_t* module = pending_module;
  pending_module = NULL;
  
  if (!module) {
    ESP_LOGW(TAG, "No pending module to switch to");
    return;
  }

  // Switch to default screen BEFORE teardown to avoid deleting active screen
  if (canvas) {
    lv_obj_t *default_screen = lv_obj_get_parent(canvas);
    if (default_screen && lv_obj_is_valid(default_screen)) {
      lv_screen_load(default_screen);
    }
  }

  // Now safe to teardown - module's screen is no longer active
  if (current_draw_module && current_draw_module->teardown_func) {
    current_draw_module->teardown_func();
  }

  // Clear canvas for new module
  if (canvas) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    if (layer.draw_buf) {
      lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
      lv_canvas_finish_layer(canvas, &layer);
    }
  }

  current_draw_module = module;
  ESP_LOGI(TAG, "Switched to module: %s", module->name);

  if (module->init_func) module->init_func();
  if (module->draw_func) module->draw_func();
}

void ui_set_draw_module(ui_draw_module_t* module) {
  if (!module) {
    ESP_LOGW(TAG, "Attempted to set NULL module");
    return;
  }

  // Store pending module and defer switch to LVGL context
  pending_module = module;
  
  lv_timer_t *switch_timer = lv_timer_create(deferred_module_switch_cb, 10, NULL);
  if (switch_timer) {
    lv_timer_set_repeat_count(switch_timer, 1);
  } else {
    ESP_LOGE(TAG, "Failed to create module switch timer");
    pending_module = NULL;
  }
}

ui_draw_module_t* ui_get_current_module(void) {
  return current_draw_module;
}

static void deferred_canvas_hide_cb(lv_timer_t *timer) {
  if (canvas != NULL) lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  lv_timer_delete(timer);
}

static void deferred_canvas_show_cb(lv_timer_t *timer) {
  if (canvas != NULL) {
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(canvas);
  }
  lv_timer_delete(timer);
}

// Deferred callback to enter Programming mode in LVGL context
static void deferred_programming_mode_enter_cb(lv_timer_t *timer) {
  lv_timer_delete(timer);
  
  if (g_app_mode != APP_MODE_PROGRAMMING) {
    ESP_LOGW(TAG, "Mode changed before Programming mode setup, aborting");
    return;
  }
  
  // Release any active notes to prevent stuck notes in programming mode
  input_manager_release_active_notes();           // CV/Gate mode notes
  midi_cv_scene_handler_release_notes();          // CV mode Notes output
  midi_expression_scene_handler_release_notes();  // Expression Notes output
  midi_als_scene_handler_release_notes();         // ALS Notes output
  midi_proximity_scene_handler_release_notes();   // Proximity Notes output
  
  // Create LVGL encoder touchwheel if not already created (must be in LVGL context)
  if (!s_ui_touchwheel) {
    touchwheel_mode_processor_t* lvgl_mode = touchwheel_mode_create_endless();
    lv_display_t* disp = lv_display_get_default();
    s_ui_touchwheel_output = touchwheel_output_lvgl_create(disp);
    
    if (lvgl_mode && s_ui_touchwheel_output) {
      s_ui_touchwheel = touchwheel_create(lvgl_mode, s_ui_touchwheel_output, 500);
      if (s_ui_touchwheel) {
        touch_register_touchwheel_instance(s_ui_touchwheel);
        ESP_LOGD(TAG, "Created LVGL touchwheel instance for programming mode");
      } else {
        touchwheel_mode_destroy(lvgl_mode);
        touchwheel_output_destroy(s_ui_touchwheel_output);
        s_ui_touchwheel_output = NULL;
      }
    }
  }
  
  // Hide canvas (must be in LVGL context)
  if (canvas != NULL) {
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  }
  
  // Initialize and create menu
  menu_init();
  menu_create();
  
  // Attach encoder to menu group
  if (s_ui_touchwheel_output) {
    lv_indev_t* encoder_indev = touchwheel_output_get_lvgl_indev(s_ui_touchwheel_output);
    if (encoder_indev) {
      lv_group_t* menu_group = menu_get_group();
      if (menu_group) {
        lv_indev_set_group(encoder_indev, menu_group);
        ESP_LOGD(TAG, "Attached encoder to menu group");
      }
    }
  }
}

// Deferred callback to exit Programming mode in LVGL context
static void deferred_programming_mode_exit_cb(lv_timer_t *timer) {
  lv_timer_delete(timer);
  
  if (g_app_mode == APP_MODE_PROGRAMMING) {
    ESP_LOGW(TAG, "Mode changed before Programming mode exit, aborting");
    return;
  }
  
  // Destroy LVGL encoder touchwheel FIRST (before cleanup, while menu is still valid)
  if (s_ui_touchwheel) {
    touch_unregister_touchwheel_instance(s_ui_touchwheel);
    touchwheel_destroy(s_ui_touchwheel);
    s_ui_touchwheel = NULL;
    // Note: output is destroyed by touchwheel_destroy, but we clear our reference
    s_ui_touchwheel_output = NULL;
    ESP_LOGD(TAG, "Destroyed LVGL touchwheel instance (exiting programming mode)");
  }
  
  // CRITICAL: Switch back to default screen BEFORE cleanup
  // This prevents deleting the active screen, which causes crashes
  lv_obj_t *default_screen = NULL;
  if (canvas != NULL) {
    default_screen = lv_obj_get_parent(canvas);
    if (default_screen && lv_obj_is_valid(default_screen)) {
      lv_screen_load(default_screen);
      ESP_LOGD(TAG, "Switched to default screen before menu cleanup");
    }
  }
  
  // NOW it's safe to cleanup menu (not the active screen anymore)
  menu_cleanup();
  
  // Free PSRAM allocations from menu pages (safe now that screens are deleted)
  menu_page_device_config_cleanup();
  menu_page_touchwheel_cleanup();
  menu_page_pads_cleanup();
  menu_page_expression_cleanup();
  menu_page_cv_scene_cleanup();
  menu_page_proximity_scene_cleanup();
  menu_page_als_scene_cleanup();
  
  // Restore Performance mode
  if (saved_draw_module) {
    current_draw_module = saved_draw_module;
    saved_draw_module = NULL;
  }
  
  // Show canvas
  if (canvas != NULL) {
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  }
  
  // Resume Performance mode rendering
  ui_graphics_resume();
  
  // Redraw current module
  if (current_draw_module && current_draw_module->draw_func) {
    current_draw_module->draw_func();
  }
}

void ui_init(void) {
  // Verify shared buffer is available (must be initialized before ui_init)
  if (!shared_canvas_buffer_is_valid()) {
    ESP_LOGE(TAG, "Shared canvas buffer not initialized! Call shared_canvas_buffer_init() first.");
    return;
  }

  void *shared_buf = shared_canvas_buffer_get();
  size_t buf_size = shared_canvas_buffer_get_size();
  uint16_t canvas_width = shared_canvas_buffer_get_width();
  uint16_t canvas_height = shared_canvas_buffer_get_height();

  ESP_LOGI(TAG, "Using shared canvas buffer: %zu KB at %p (%dx%d)", 
           buf_size / 1024, shared_buf, canvas_width, canvas_height);

  // Standard canvas - using display's color format
  canvas = lv_canvas_create(lv_screen_active());
  lv_obj_set_size(canvas, canvas_width, canvas_height);
  lv_obj_center(canvas);

  shared_canvas_buffer_clear();
  lv_canvas_set_buffer(canvas, shared_buf, canvas_width, canvas_height, 
    shared_canvas_buffer_get_format());
  ESP_LOGI(TAG, "Canvas created using shared buffer: %zu bytes", buf_size);

  g_ui_refresh_timer = lv_timer_create(lvgl_timer_cb, 33, NULL);  // ~30fps

  g_app_mode = APP_MODE_PERFORMANCE;
  g_at_programming_top_level_menu = false;
  g_ui_has_buffer = true;  // UI now owns the shared buffer

  ui_event_handler_init();
}

app_mode_t ui_get_app_mode(void) {
  return g_app_mode;
}

void ui_set_app_mode(app_mode_t mode) {
  app_mode_t previous_mode = g_app_mode;
  g_app_mode = mode;
  
  const char* mode_names[] = {"Performance", "Programming", "Screensaver"};
  const char* prev_name = (previous_mode < 3) ? mode_names[previous_mode] : "Unknown";
  const char* new_name = (mode < 3) ? mode_names[mode] : "Unknown";
  
  ESP_LOGI(TAG, "App mode changed: %s -> %s", prev_name, new_name);
  
  // Handle entering Programming mode
  if (mode == APP_MODE_PROGRAMMING && previous_mode != APP_MODE_PROGRAMMING) {
    // Suspend scene input processing (disables scene touchwheel and actions)
    scene_suspend_input();
    
    // Save current Performance mode draw module
    saved_draw_module = current_draw_module;

    // Suspend Performance mode rendering (this creates a deferred timer, safe)
    ui_graphics_suspend();

    // Defer ALL LVGL operations to LVGL context (cannot call LVGL from timer/ISR context)
    // This includes: touchwheel creation, hiding canvas, and creating menu widgets
    // Note: ui_set_app_mode() may be called from FreeRTOS timer callback context
    lv_timer_t *enter_timer = lv_timer_create(deferred_programming_mode_enter_cb, 10, NULL);
    if (enter_timer) {
      lv_timer_set_repeat_count(enter_timer, 1);
    } else {
      ESP_LOGE(TAG, "Failed to create Programming mode enter timer");
    }
  }
  // Handle exiting Programming mode
  else if (mode != APP_MODE_PROGRAMMING && previous_mode == APP_MODE_PROGRAMMING) {
    // Resume scene input processing (re-enables scene touchwheel and actions)
    scene_resume_input();
    
    // Suspend Performance mode rendering (this creates a deferred timer, safe)
    ui_graphics_suspend();

    // Defer ALL LVGL operations to LVGL context (cannot call LVGL from event handler context)
    // This includes: touchwheel destruction, screen switching, and menu cleanup
    lv_timer_t *exit_timer = lv_timer_create(deferred_programming_mode_exit_cb, 10, NULL);
    if (exit_timer) {
      lv_timer_set_repeat_count(exit_timer, 1);
    } else {
      ESP_LOGE(TAG, "Failed to create Programming mode exit timer");
    }
  }
}

bool ui_is_programming_top_level(void) {
  return g_at_programming_top_level_menu;
}

void ui_set_programming_top_level(bool is_top_level) {
  g_at_programming_top_level_menu = is_top_level;
  ESP_LOGD(TAG, "Programming menu level set to: %s", is_top_level ? "Top Level" : "Sub-Level");
}

void ui_graphics_suspend(void) {
  if (g_ui_refresh_timer != NULL) lv_timer_pause(g_ui_refresh_timer);

  // Defer canvas hiding to avoid blocking in timer callback context
  lv_timer_t *hide_timer = lv_timer_create(deferred_canvas_hide_cb, 1, NULL);
  if (hide_timer != NULL) lv_timer_set_repeat_count(hide_timer, 1);
}

void ui_graphics_resume(void) {
  if (g_ui_refresh_timer != NULL) lv_timer_resume(g_ui_refresh_timer);

  // Defer canvas showing to avoid blocking in timer callback context
  lv_timer_t *show_timer = lv_timer_create(deferred_canvas_show_cb, 1, NULL);
  if (show_timer != NULL) lv_timer_set_repeat_count(show_timer, 1);
}

// Deferred callback to release UI's use of the shared buffer (no memory free)
static void deferred_ui_release_cb(lv_timer_t *timer) {
  ESP_LOGD(TAG, "UI buffer release callback executing");

  // Clear the timer reference first
  g_handoff_timer = NULL;

  // Guard against multiple operations
  if (g_handoff_in_progress) {
    ESP_LOGW(TAG, "Handoff already in progress, skipping");
    lv_timer_delete(timer);
    return;
  }

  g_handoff_in_progress = true;

  // Switch to default screen and hide module screen to stop rendering
  // DON'T delete widgets - just hide them
  if (current_draw_module) {
    lv_obj_t *default_screen = lv_obj_get_parent(canvas);
    if (default_screen && lv_obj_is_valid(default_screen)) lv_screen_load(default_screen);
    ESP_LOGD(TAG, "Switched to default screen, module widgets remain in memory");
  }

  // Hide the canvas to stop rendering
  if (canvas != NULL) lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);

  // Mark that UI no longer owns the buffer (screensaver can now use it)
  // NOTE: Buffer is NOT freed - it's the shared persistent buffer
  g_ui_has_buffer = false;

  lv_timer_delete(timer);
  ESP_LOGD(TAG, "UI released shared buffer for screensaver use");

  // Call post-release callback if one was registered
  if (g_post_release_callback) {
    void (*callback)(void) = g_post_release_callback;
    g_post_release_callback = NULL;
    callback();
  }
}

bool ui_release_canvas_buffer(void (*post_release_cb)(void)) {
  ESP_LOGD(TAG, "Releasing UI's use of shared canvas buffer...");

  // Guard against multiple releases
  if (g_handoff_in_progress) {
    ESP_LOGW(TAG, "Buffer handoff already in progress, ignoring release request");
    return false;
  }

  // Check if UI actually has the buffer
  if (!g_ui_has_buffer) {
    ESP_LOGW(TAG, "UI doesn't currently own the buffer, nothing to release");
    // Still call the callback if provided
    if (post_release_cb) post_release_cb();
    return true;
  }

  // Store the callback to run after release completes
  g_post_release_callback = post_release_cb;

  // Cancel any pending handoff timer
  if (g_handoff_timer != NULL) {
    ESP_LOGW(TAG, "Cancelling existing handoff timer");
    lv_timer_delete(g_handoff_timer);
    g_handoff_timer = NULL;
  }

  // Pause the refresh timer to prevent new render cycles
  if (g_ui_refresh_timer != NULL) lv_timer_pause(g_ui_refresh_timer);

  // Defer UI release to LVGL context
  // This ensures any in-flight render completes before we hand off
  g_handoff_timer = lv_timer_create(deferred_ui_release_cb, 10, NULL);
  if (!g_handoff_timer) {
    ESP_LOGE(TAG, "Failed to create handoff timer!");

    // Resume refresh timer to prevent UI freeze
    if (g_ui_refresh_timer != NULL) {
      lv_timer_resume(g_ui_refresh_timer);
      ESP_LOGI(TAG, "Resumed refresh timer to prevent UI freeze");
    }

    g_handoff_in_progress = false;
    return false;
  }

  lv_timer_set_repeat_count(g_handoff_timer, 1);

  ESP_LOGD(TAG, "UI buffer release timer created");
  return true;
}

// Deferred callback to show module screen (no recreation needed)
static void deferred_module_show_cb(lv_timer_t *timer) {
  // Just load the module screen back - widgets are already there
  if (current_draw_module) {
    // The deferred draw callback will load the module's screen
    // Widgets already exist, so this will be instant
    if (current_draw_module->draw_func) {
      current_draw_module->draw_func();
      ESP_LOGD(TAG, "Reloaded module '%s' screen", current_draw_module->name);
    }
  }
  lv_timer_delete(timer);
}

void ui_reclaim_canvas_buffer(void) {
  ESP_LOGD(TAG, "Reclaiming UI's use of shared canvas buffer...");

  // Verify shared buffer is still valid
  if (!shared_canvas_buffer_is_valid()) {
    ESP_LOGE(TAG, "Shared canvas buffer is not valid!");
    g_handoff_in_progress = false;
    return;
  }

  void *shared_buf = shared_canvas_buffer_get();

  // Check current mode to restore appropriate UI
  app_mode_t current_mode = g_app_mode;

  // Restore UI based on current mode
  if (current_mode == APP_MODE_PROGRAMMING) {
    // Restore Programming mode (menu was active)
    ESP_LOGI(TAG, "Restoring Programming mode after screensaver");

    // Mark UI as owning the buffer again
    g_ui_has_buffer = true;
    g_handoff_in_progress = false;

    // The menu screens are still in memory - restore the active one
    lv_obj_t* menu_screen = menu_get_current_screen();
    if (menu_screen && lv_obj_is_valid(menu_screen)) {
      lv_screen_load(menu_screen);
      ESP_LOGI(TAG, "Menu screen restored");
    } else {
      ESP_LOGW(TAG, "Menu screen was deleted, recreating");
      menu_init();
      menu_create();
    }

    // Recreate touchwheel if needed
    if (!s_ui_touchwheel) {
      touchwheel_mode_processor_t* lvgl_mode = touchwheel_mode_create_endless();
      lv_display_t* disp = lv_display_get_default();
      s_ui_touchwheel_output = touchwheel_output_lvgl_create(disp);

      if (lvgl_mode && s_ui_touchwheel_output) {
        s_ui_touchwheel = touchwheel_create(lvgl_mode, s_ui_touchwheel_output, 500);
        if (s_ui_touchwheel) {
          touch_register_touchwheel_instance(s_ui_touchwheel);
          ESP_LOGI(TAG, "Recreated LVGL touchwheel instance for programming mode");
        } else {
          touchwheel_mode_destroy(lvgl_mode);
          touchwheel_output_destroy(s_ui_touchwheel_output);
          s_ui_touchwheel_output = NULL;
        }
      }
    }

    // Attach encoder to menu group
    if (s_ui_touchwheel_output) {
      lv_indev_t* encoder_indev = touchwheel_output_get_lvgl_indev(s_ui_touchwheel_output);
      if (encoder_indev) {
        lv_group_t* menu_group = menu_get_group();
        if (menu_group) {
          lv_indev_set_group(encoder_indev, menu_group);
          ESP_LOGD(TAG, "Attached encoder to menu group");
        }
      }
    }
  } else {
    // Restore Performance mode (normal UI)
    ESP_LOGI(TAG, "Restoring Performance mode after screensaver");

    // Re-attach the shared buffer to the canvas (screensaver was using it)
    if (canvas != NULL && shared_buf != NULL) {
      // Clear the buffer before reattaching (screensaver left its content)
      shared_canvas_buffer_clear();
      lv_canvas_set_buffer(canvas, shared_buf, 
        shared_canvas_buffer_get_width(), shared_canvas_buffer_get_height(),
        shared_canvas_buffer_get_format());
      lv_obj_remove_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    }

    // Mark UI as owning the buffer again
    g_ui_has_buffer = true;
    g_handoff_in_progress = false;

    // Resume the refresh timer
    if (g_ui_refresh_timer != NULL) lv_timer_resume(g_ui_refresh_timer);

    // Defer module screen reload to LVGL context
    lv_timer_t *show_timer = lv_timer_create(deferred_module_show_cb, 50, NULL);
    if (!show_timer) {
      ESP_LOGE(TAG, "Failed to create show timer, calling draw directly");
      if (current_draw_module && current_draw_module->draw_func) {
        current_draw_module->draw_func();
      }
      return;
    }

    lv_timer_set_repeat_count(show_timer, 1);
  }

  ESP_LOGD(TAG, "UI reclaimed shared canvas buffer");
}
