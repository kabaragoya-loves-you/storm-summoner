#include "lvgl.h"
#include "../lvgl/src/display/lv_display_private.h"
#include "ui.h"
#include "touchwheel.h"
#include "touchwheel_outputs.h"
#include "touch.h"
#include "menu.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

void ui_event_handler_init(void);

lv_obj_t *canvas = NULL;
static lv_timer_t *g_ui_refresh_timer = NULL;
static ui_draw_module_t* current_draw_module = NULL;

static void *display_buf = NULL;
static bool g_teardown_in_progress = false;
static lv_timer_t *g_teardown_timer = NULL;  // Track pending teardown timer
static void (*g_post_release_callback)(void) = NULL;  // Callback to run after release completes

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
  // Only invalidate if not currently rendering to prevent feedback loops
  if (canvas != NULL) {
    lv_display_t *disp = lv_obj_get_disp(canvas);
    if (disp && !disp->rendering_in_progress) {
      lv_obj_invalidate(canvas);
    }
  }
}

void ui_set_draw_module(ui_draw_module_t* module) {
  if (!module) {
    ESP_LOGW(TAG, "Attempted to set NULL module");
    return;
  }

  if (current_draw_module && current_draw_module->teardown_func) current_draw_module->teardown_func();

  if (canvas) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    if (layer.draw_buf) {
      lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
      lv_canvas_finish_layer(canvas, &layer);
      lv_obj_invalidate(canvas);
    }
  }

  current_draw_module = module;
  ESP_LOGI(TAG, "Switched to module: %s", module->name);

  if (module->init_func) module->init_func();

  if (module->draw_func) module->draw_func();
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
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    // Only invalidate if not currently rendering to prevent feedback loops
    lv_display_t *disp = lv_obj_get_disp(canvas);
    if (disp && !disp->rendering_in_progress) {
      lv_obj_invalidate(canvas);
    }
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
  
  // Create LVGL encoder touchwheel if not already created (must be in LVGL context)
  if (!s_ui_touchwheel) {
    touchwheel_mode_processor_t* lvgl_mode = touchwheel_mode_create_endless();
    lv_display_t* disp = lv_display_get_default();
    s_ui_touchwheel_output = touchwheel_output_lvgl_create(disp);
    
    if (lvgl_mode && s_ui_touchwheel_output) {
      s_ui_touchwheel = touchwheel_create(lvgl_mode, s_ui_touchwheel_output, 500);
      if (s_ui_touchwheel) {
        touch_register_touchwheel_instance(s_ui_touchwheel);
        ESP_LOGI(TAG, "Created LVGL touchwheel instance for programming mode");
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
        ESP_LOGI(TAG, "Attached encoder to menu group");
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
    ESP_LOGI(TAG, "Destroyed LVGL touchwheel instance (exiting programming mode)");
  }
  
  // CRITICAL: Switch back to default screen BEFORE cleanup
  // This prevents deleting the active screen, which causes crashes
  lv_obj_t *default_screen = NULL;
  if (canvas != NULL) {
    default_screen = lv_obj_get_parent(canvas);
    if (default_screen && lv_obj_is_valid(default_screen)) {
      lv_screen_load(default_screen);
      ESP_LOGI(TAG, "Switched to default screen before menu cleanup");
    }
  }
  
  // NOW it's safe to cleanup menu (not the active screen anymore)
  menu_cleanup();
  
  // Restore Performance mode
  if (saved_draw_module) {
    current_draw_module = saved_draw_module;
    saved_draw_module = NULL;
  }
  
  // Show canvas
  if (canvas != NULL) {
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  }
  
  // Resume Performance mode rendering
  ui_graphics_resume();
  
  // Redraw current module
  if (current_draw_module && current_draw_module->draw_func) {
    current_draw_module->draw_func();
  }
}

void ui_init(void) {
  // Allocate display buffer from internal RAM for performance (not LVGL heap)
  // Use 64-byte alignment for PPA hardware acceleration
  size_t bytes_per_pixel = lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
  size_t buf_size = 128 * 128 * bytes_per_pixel;
  display_buf = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!display_buf) {
    ESP_LOGE(TAG, "Failed to allocate display buffer from internal RAM!");
    return;
  }
  ESP_LOGI(TAG, "Allocated %d KB canvas buffer from internal RAM", buf_size / 1024);
  
  // Standard canvas - using native color format
  canvas = lv_canvas_create(lv_screen_active());
  lv_obj_set_size(canvas, 128, 128);
  lv_obj_center(canvas);
  
  memset(display_buf, 0, buf_size);
  lv_canvas_set_buffer(canvas, display_buf, 128, 128, LV_COLOR_FORMAT_NATIVE);
  ESP_LOGI(TAG, "Canvas created: %d bytes", (int)buf_size);

  g_ui_refresh_timer = lv_timer_create(lvgl_timer_cb, 33, NULL);  // ~30fps

  g_app_mode = APP_MODE_PERFORMANCE;
  g_at_programming_top_level_menu = false;
  
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
  ESP_LOGI(TAG, "Programming menu level set to: %s", is_top_level ? "Top Level" : "Sub-Level");
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

// Deferred callback to hide UI and free canvas buffer (no teardown)
static void deferred_ui_hide_and_free_cb(lv_timer_t *timer) {
  ESP_LOGI(TAG, "UI hide and free callback executing");
  
  // Clear the timer reference first
  g_teardown_timer = NULL;
  
  // Guard against multiple operations
  if (g_teardown_in_progress) {
    ESP_LOGW(TAG, "Operation already in progress, skipping");
    lv_timer_delete(timer);
    return;
  }
  
  g_teardown_in_progress = true;
  
  // Switch to default screen and hide module screen to stop rendering
  // DON'T delete widgets - just hide them to avoid fragmentation
  if (current_draw_module) {
    lv_obj_t *default_screen = lv_obj_get_parent(canvas);
    if (default_screen && lv_obj_is_valid(default_screen)) lv_screen_load(default_screen);
    ESP_LOGI(TAG, "Switched to default screen, module widgets remain in memory");
  }
  
  // Hide the canvas to stop rendering
  if (canvas != NULL) lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  
  // Now free only the canvas buffer
  if (display_buf) {
    lv_mem_monitor_t mon_before;
    lv_mem_monitor(&mon_before);
    ESP_LOGI(TAG, "LVGL memory before UI free - used: %d, free: %d, frag: %d%%", 
      mon_before.total_size - mon_before.free_size, mon_before.free_size, mon_before.frag_pct);
    
    heap_caps_free(display_buf);
    display_buf = NULL;
    
    lv_mem_monitor_t mon_after;
    lv_mem_monitor(&mon_after);
    ESP_LOGI(TAG, "UI canvas buffer freed (32KB)");
    ESP_LOGI(TAG, "LVGL memory after UI free - used: %d, free: %d, frag: %d%%", 
      mon_after.total_size - mon_after.free_size, mon_after.free_size, mon_after.frag_pct);
  }
  
  lv_timer_delete(timer);
  ESP_LOGI(TAG, "UI hide and free callback complete");
  
  // Call post-release callback if one was registered
  if (g_post_release_callback) {
    void (*callback)(void) = g_post_release_callback;
    g_post_release_callback = NULL;  // Clear before calling to prevent re-entry
    callback();
  }
}

bool ui_release_canvas_buffer(void (*post_release_cb)(void)) {
  ESP_LOGD(TAG, "Releasing UI canvas buffer (32KB)...");
  
  // Guard against multiple releases
  if (g_teardown_in_progress) {
    ESP_LOGW(TAG, "Teardown already in progress, ignoring release request");
    return false;
  }
  
  // Store the callback to run after release completes
  g_post_release_callback = post_release_cb;
  
  // If there's already a pending teardown timer, cancel it and create a new one
  if (g_teardown_timer != NULL) {
    ESP_LOGW(TAG, "Cancelling existing teardown timer before creating new one");
    lv_timer_del(g_teardown_timer);
    g_teardown_timer = NULL;
  }
  
  // Check current memory state
  lv_mem_monitor_t mon_initial;
  lv_mem_monitor(&mon_initial);
  ESP_LOGI(TAG, "LVGL memory at start of release - used: %d, free: %d, frag: %d%%", 
    mon_initial.total_size - mon_initial.free_size, mon_initial.free_size, mon_initial.frag_pct);
  
  // If memory is critically low or badly fragmented, we may have a leak
  // Force clear the flag in case it got stuck
  if (mon_initial.free_size < 1024 && mon_initial.frag_pct > 40) {
    ESP_LOGW(TAG, "Critically low memory detected - potential leak or stuck teardown");
    ESP_LOGW(TAG, "Force clearing teardown flag and attempting garbage collection");
    g_teardown_in_progress = false;
    lv_obj_clean(lv_screen_active());  // Try to clean up any orphaned objects
  }
  
  // Check if we have enough memory to create a timer
  if (mon_initial.free_size < 512) {
    ESP_LOGE(TAG, "Not enough LVGL memory to create teardown timer (%d bytes free), aborting", 
      mon_initial.free_size);
    g_teardown_in_progress = false;  // Clear flag to allow retry
    return false;
  }
  
  // Pause the refresh timer to prevent new render cycles
  // CRITICAL: We must ensure this gets resumed if anything fails
  if (g_ui_refresh_timer != NULL) lv_timer_pause(g_ui_refresh_timer);
  
  // Defer UI hiding to LVGL context (don't tear down widgets to avoid fragmentation)
  // This ensures any in-flight render completes before we make changes
  g_teardown_timer = lv_timer_create(deferred_ui_hide_and_free_cb, 10, NULL);
  if (!g_teardown_timer) {
    ESP_LOGE(TAG, "CRITICAL: Failed to create hide timer!");
    ESP_LOGE(TAG, "Heap free: %d, frag: %d%% - timer creation failed", 
      mon_initial.free_size, mon_initial.frag_pct);
    
    // MUST resume refresh timer to prevent UI freeze
    if (g_ui_refresh_timer != NULL) {
      lv_timer_resume(g_ui_refresh_timer);
      ESP_LOGI(TAG, "Resumed refresh timer to prevent UI freeze");
    }
    
    g_teardown_in_progress = false;
    return false;
  }
  
  lv_timer_set_repeat_count(g_teardown_timer, 1);
  
  ESP_LOGI(TAG, "UI hide timer created successfully");
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
  ESP_LOGD(TAG, "Reclaiming UI canvas buffer...");
  
  // Check current mode to restore appropriate UI
  app_mode_t current_mode = g_app_mode;
  
  // Reallocate the buffer if needed (from internal RAM, not LVGL heap)
  if (!display_buf) {
    size_t bytes_per_pixel = lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
    size_t buf_size = 128 * 128 * bytes_per_pixel;
    display_buf = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!display_buf) {
      ESP_LOGE(TAG, "Failed to reallocate display buffer from internal RAM!");
      g_teardown_in_progress = false;  // Clear flag on error
      return;
    }
    memset(display_buf, 0, buf_size);
    ESP_LOGI(TAG, "Reallocated %d KB canvas buffer from internal RAM", buf_size / 1024);
  }
  
  // Restore UI based on current mode
  if (current_mode == APP_MODE_PROGRAMMING) {
    // Restore Programming mode (menu was active)
    ESP_LOGI(TAG, "Restoring Programming mode after screensaver");
    
    // Clear teardown flag before recreating widgets
    g_teardown_in_progress = false;
    
    // The menu screens are still in memory - we just need to restore the active one
    // Get the current menu screen from the menu system
    lv_obj_t* menu_screen = menu_get_current_screen();
    if (menu_screen && lv_obj_is_valid(menu_screen)) {
      lv_scr_load(menu_screen);  // Load the menu screen
      ESP_LOGI(TAG, "Menu screen restored");
    } else {
      // If somehow the menu screen was deleted, recreate it
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
          ESP_LOGI(TAG, "Attached encoder to menu group");
        }
      }
    }
  } else {
    // Restore Performance mode (normal UI)
    ESP_LOGI(TAG, "Restoring Performance mode after screensaver");
    
    // Restore the canvas buffer
    if (canvas != NULL && display_buf != NULL) {
      lv_canvas_set_buffer(canvas, display_buf, 128, 128, LV_COLOR_FORMAT_NATIVE);
      lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
      lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
      // Don't invalidate here - let the draw function handle it
    }
    
    // Resume the refresh timer
    if (g_ui_refresh_timer != NULL) lv_timer_resume(g_ui_refresh_timer);
    
    // Clear teardown flag before recreating widgets
    g_teardown_in_progress = false;
    
    // Defer module screen reload to LVGL context with a delay to allow memory to settle
    lv_timer_t *show_timer = lv_timer_create(deferred_module_show_cb, 50, NULL);
    if (!show_timer) {
      ESP_LOGE(TAG, "Failed to create show timer, calling draw directly");
      // Fallback: call draw function directly if timer creation fails
      if (current_draw_module && current_draw_module->draw_func) {
        current_draw_module->draw_func();
      }
      return;
    }
    
    lv_timer_set_repeat_count(show_timer, 1);
  }
  
  ESP_LOGD(TAG, "UI canvas buffer reclaimed");
}