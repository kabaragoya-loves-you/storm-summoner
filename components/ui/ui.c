#include "lvgl.h"
#include "ui.h"
#include "usb_cdc_update.h"
#include "shared_canvas_buffer.h"
#include "inspect_config.h"
#include "inspect_overlay.h"
#include "touchwheel.h"
#include "touchwheel_outputs.h"
#include "touch.h"
#include "menu.h"
#include "menu_pages.h"
#include "action.h"
#include "scene.h"
#include "midi_local_output.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
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

// Scene-transition window state. While s_scene_transitioning is true:
//   * ui_set_draw_module() STAGES the requested module into s_staged_module
//     instead of immediately queueing the LVGL teardown/build timer.
//   * touch.c handle_touch_event() drops inbound TOUCH_PRESS/RELEASE so the
//     user does not inadvertently interact with a frozen screen.
// ui_scene_transition_end() flushes the staged module and resets state.
static volatile bool s_scene_transitioning = false;
static ui_draw_module_t* s_staged_module = NULL;
static int64_t s_transition_start_us = 0;

// Forward decl so ui_set_draw_module can call the staged-flush body.
static void ui_set_draw_module_internal(ui_draw_module_t* module);

// Pending-teardown state for the "smooth swap" path. Every module in this
// codebase creates its own screen (lv_obj_create(NULL)) and ends its
// deferred draw callback with lv_screen_load(g_screen). That swap is
// atomic. So instead of switching to a blank default screen and tearing
// down the old module up-front (which left ~110 ms of black canvas
// between switch and the new module finishing its widget build -- very
// visible at boot when going splash -> beat), we keep the old screen
// active until lv_screen_active() actually changes to a different screen,
// then tear down the old module. The user sees a continuous image with a
// single atomic crossfade-like swap.
//
// A safety timeout falls back to the legacy "switch to default + teardown"
// behavior if the new module never loads its screen within the budget,
// so a misbehaving module degrades to today's behavior, not worse.
#define TEARDOWN_POLL_PERIOD_MS  10
#define TEARDOWN_POLL_MAX_ATTEMPTS 25  // 250 ms total; beat needs ~110 ms
static ui_draw_module_t* s_pending_teardown_module = NULL;
static lv_obj_t* s_pending_teardown_screen = NULL;
static int s_teardown_poll_count = 0;

static void clear_canvas_black(void) {
  if (!canvas) return;
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (layer.draw_buf) {
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    lv_canvas_finish_layer(canvas, &layer);
  }
}

static void finalize_pending_teardown(bool fallback_to_default_screen) {
  if (fallback_to_default_screen) {
    // Defensive fallback: the new module never swapped its screen in.
    // Switch to the default (canvas-host) screen so destroying the old
    // module's widgets does not delete the still-active screen.
    if (canvas) {
      lv_obj_t* default_screen = lv_obj_get_parent(canvas);
      if (default_screen && lv_obj_is_valid(default_screen)) {
        lv_screen_load(default_screen);
      }
    }
  }
  if (s_pending_teardown_module && s_pending_teardown_module->teardown_func) {
    s_pending_teardown_module->teardown_func();
  }
  s_pending_teardown_module = NULL;
  s_pending_teardown_screen = NULL;
  s_teardown_poll_count = 0;
}

static void deferred_teardown_old_module_cb(lv_timer_t *timer) {
  if (!s_pending_teardown_module) {
    // Nothing to do (e.g. a second switch already flushed us synchronously).
    lv_timer_delete(timer);
    return;
  }

  lv_obj_t* now_active = lv_screen_active();
  if (now_active == s_pending_teardown_screen) {
    // New module has not loaded its screen yet -- keep waiting.
    if (++s_teardown_poll_count >= TEARDOWN_POLL_MAX_ATTEMPTS) {
      ESP_LOGW(TAG, "Teardown poll timeout after %d ms; falling back to default screen",
        TEARDOWN_POLL_PERIOD_MS * TEARDOWN_POLL_MAX_ATTEMPTS);
      lv_timer_delete(timer);
      finalize_pending_teardown(true);
    }
    return;
  }

  // Active screen changed -- the new module is now on screen and we can
  // safely destroy the old module's widgets without flickering or deleting
  // the live screen.
  lv_timer_delete(timer);
  finalize_pending_teardown(false);
  clear_canvas_black();
}

// Legacy synchronous switch path: switch to default screen, tear down the
// outgoing module, clear canvas, then build the new one. Used for
// same-module reswitches (where the new build would otherwise clobber the
// outgoing module's static g_screen pointer before our deferred teardown
// could read it) and as a fallback when the smooth-swap path can't run.
static void legacy_switch_to(ui_draw_module_t* module) {
  if (canvas) {
    lv_obj_t *default_screen = lv_obj_get_parent(canvas);
    if (default_screen && lv_obj_is_valid(default_screen)) {
      lv_screen_load(default_screen);
    }
  }
  if (current_draw_module && current_draw_module->teardown_func) {
    current_draw_module->teardown_func();
  }
  clear_canvas_black();

  current_draw_module = module;
  if (module->init_func) module->init_func();
  if (module->draw_func) module->draw_func();
}

// Deferred callback to switch modules in LVGL context
static void deferred_module_switch_cb(lv_timer_t *timer) {
  lv_timer_delete(timer);

  ui_draw_module_t* module = pending_module;
  pending_module = NULL;

  if (!module) {
    ESP_LOGW(TAG, "No pending module to switch to");
    return;
  }

  // If a previous teardown is still pending (rapid back-to-back switches),
  // flush it synchronously now. Its screen is no longer active anyway --
  // the previous switch already replaced it via lv_screen_load -- so
  // destruction is safe and we keep state simple.
  if (s_pending_teardown_module) {
    finalize_pending_teardown(false);
  }

  // Same-module reswitch: the module's static g_screen would be clobbered
  // by the new build before our deferred teardown could read it, which
  // would cause the teardown to delete the freshly-built screen. Fall
  // back to the legacy synchronous path (which is what scene-to-scene
  // changes within the same UI module rely on today). This briefly shows
  // the black default screen, but during scene transitions that's hidden
  // by ui_graphics_suspend(), and outside scene transitions same-module
  // reswitches are rare.
  if (module == current_draw_module) {
    ESP_LOGI(TAG, "Switched to module: %s (same-module reswitch)", module->name);
    legacy_switch_to(module);
    return;
  }

  // Different module: smooth-swap path. Capture the outgoing module + the
  // currently-active screen so we can poll for the new module to swap it
  // out, then tear down. We do NOT switch to the default screen first;
  // the outgoing screen stays visible until the new module's deferred
  // draw calls lv_screen_load(g_screen).
  s_pending_teardown_module = current_draw_module;
  s_pending_teardown_screen = lv_screen_active();
  s_teardown_poll_count = 0;

  current_draw_module = module;
  ESP_LOGI(TAG, "Switched to module: %s", module->name);

  if (module->init_func) module->init_func();
  if (module->draw_func) module->draw_func();

  // If there is no outgoing module to tear down (first ever switch), or
  // it has no teardown_func, skip the polling timer entirely.
  if (!s_pending_teardown_module || !s_pending_teardown_module->teardown_func) {
    s_pending_teardown_module = NULL;
    s_pending_teardown_screen = NULL;
    return;
  }

  // Repeating timer; callback deletes itself once the screen swap is
  // detected or the safety timeout fires.
  lv_timer_t* teardown_timer = lv_timer_create(deferred_teardown_old_module_cb,
                                                TEARDOWN_POLL_PERIOD_MS, NULL);
  if (!teardown_timer) {
    ESP_LOGE(TAG, "Failed to create teardown poll timer; falling back to immediate teardown");
    finalize_pending_teardown(true);
  }
}

static void ui_set_draw_module_internal(ui_draw_module_t* module) {
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

void ui_set_draw_module(ui_draw_module_t* module) {
  if (!module) {
    ESP_LOGW(TAG, "Attempted to set NULL module");
    return;
  }

  if (s_scene_transitioning) {
    // Defer the actual teardown/build until ui_scene_transition_end().
    // The most recent staged module wins -- callers that change their mind
    // mid-transition (e.g. programming-mode toggles) just overwrite.
    s_staged_module = module;
    return;
  }

  ui_set_draw_module_internal(module);
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
  
  // Note release for on-device producers happens synchronously in
  // ui_set_app_mode via midi_local_output_silence(); this deferred callback
  // only handles LVGL widget construction.

  
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
  
  // Let the incoming-CC mirror refresh an open mode-dependent roller.
  action_set_gating_changed_observer(menu_on_gating_cc_changed);
  
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
  
  // If we transitioned to screensaver mode, don't load the default screen -
  // the screensaver will load its own screen. Just do minimal cleanup.
  if (g_app_mode == APP_MODE_SCREENSAVER) {
    ESP_LOGI(TAG, "Exiting Programming to Screensaver - deferring to screensaver");
    
    // Destroy LVGL encoder touchwheel (will be recreated by ui_reclaim_canvas_buffer)
    if (s_ui_touchwheel) {
      touch_unregister_touchwheel_instance(s_ui_touchwheel);
      touchwheel_destroy(s_ui_touchwheel);
      s_ui_touchwheel = NULL;
      s_ui_touchwheel_output = NULL;
    }
    
    // Scene input stays suspended - we'll return to Programming mode later
    // Menu stays in memory - ui_reclaim_canvas_buffer will restore it
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
  menu_page_tilt_axis_scene_cleanup();
  
  // Restore Performance mode - use scene's ui_module (may have changed)
  saved_draw_module = NULL;  // Clear saved reference
  const char* scene_module = scene_get_ui_module(scene_get_current_index());
  ui_draw_module_t* mod = ui_get_module_by_name(scene_module);
  if (mod) {
    current_draw_module = mod;
  } else {
    // Fallback to beat module if scene's module is invalid
    current_draw_module = &beat_module;
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
  inspect_config_init();
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
  if (mode == APP_MODE_PROGRAMMING && previous_mode != APP_MODE_PROGRAMMING) {
    usb_cdc_notify_programming(true);
  } else if (previous_mode == APP_MODE_PROGRAMMING && mode == APP_MODE_PERFORMANCE) {
    usb_cdc_notify_programming(false);
  }

  // Handle entering Programming mode (but NOT when returning from screensaver)
  // When returning from screensaver, ui_reclaim_canvas_buffer will restore menu/touchwheel
  if (mode == APP_MODE_PROGRAMMING && previous_mode != APP_MODE_PROGRAMMING
      && previous_mode != APP_MODE_SCREENSAVER) {
    inspect_overlay_hide();
    // Suspend scene input first: this snapshots the LFO running state and
    // stops the LFO loops so they don't emit fresh values during release.
    scene_suspend_input();

    // Seed the live CC cache from the scene's CC defaults so variant/no-op
    // resolution in the CC choosers reflects the scene's default mode rather
    // than the boot-time placeholder. No MIDI is sent.
    scene_seed_cc_cache();

    // Then synchronously release every on-device producer's held notes,
    // mute the clock, and flip the silence predicate. release_all() catches
    // any LFO mapping note that was held when the loop stopped.
    midi_local_output_silence();

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
    // Resume scene input processing ONLY if going to Performance mode (not Screensaver)
    // When going to Screensaver, we'll return to Programming mode later with scene still suspended
    if (mode == APP_MODE_PERFORMANCE) {
      scene_resume_input();
      scene_apply_deferred_init();
      midi_local_output_enable();
    }
    
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

bool ui_scene_is_transitioning(void) {
  return s_scene_transitioning;
}

void ui_scene_transition_begin(void) {
  if (s_scene_transitioning) {
    ESP_LOGW(TAG, "Scene transition begin called while already transitioning");
    return;
  }
  s_scene_transitioning = true;
  s_staged_module = NULL;
  s_transition_start_us = esp_timer_get_time();
  inspect_overlay_hide();
  ui_graphics_suspend();
  ESP_LOGI(TAG, "Scene transition begin");
}

void ui_scene_transition_end(void) {
  if (!s_scene_transitioning) {
    ESP_LOGW(TAG, "Scene transition end called without a matching begin");
    return;
  }

  // Capture the staged module before clearing the flag so any racy
  // ui_set_draw_module() call that lands during this end-of-transition
  // sequence falls through to the normal (unstaged) path.
  ui_draw_module_t* staged = s_staged_module;
  s_staged_module = NULL;
  const char* staged_name = staged ? staged->name : "<none>";

  // Flip the flag before reopening anything (touch_force_release_all_pads
  // posts directly to the event bus, but any new hardware touch event that
  // arrives at handle_touch_event from this point on should be allowed
  // through -- the window is over).
  s_scene_transitioning = false;

  // Force-release any pad that was physically held across the transition
  // (PRESS/RELEASE were dropped while the window was open).
  touch_force_release_all_pads();

  // Queue the deferred LVGL module switch (teardown old + build new).
  // This happens AFTER scene_set_current's body has finished so we are not
  // racing the heavy scene configuration work for CPU / memory bus.
  if (staged) ui_set_draw_module_internal(staged);

  ui_graphics_resume();

  uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - s_transition_start_us) / 1000);
  ESP_LOGI(TAG, "Scene transition end (elapsed=%u ms, staged_module=%s)",
    (unsigned)elapsed_ms, staged_name);
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
    ESP_LOGD(TAG, "Restoring Programming mode after screensaver");

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
    ESP_LOGD(TAG, "Restoring Performance mode after screensaver");

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

void ui_attach_encoder_to_menu(void) {
  if (!s_ui_touchwheel_output) return;

  lv_indev_t *encoder_indev = touchwheel_output_get_lvgl_indev(s_ui_touchwheel_output);
  if (!encoder_indev) return;

  lv_group_t *menu_group = menu_get_group();
  if (!menu_group) return;

  lv_indev_set_group(encoder_indev, menu_group);
}
