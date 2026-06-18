/* Storm Summoner - Updating UI Module
 * Displays progress during firmware/assets updates via WebUSB
 */

#include "lvgl.h"
#include "ui.h"
#include "event_bus.h"
#include "usb_cdc_update.h"
#include "display_driver.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>

#define TAG "UPDATING_UI"

// Update state machine
typedef enum {
  UPDATING_STATE_IDLE,
  UPDATING_STATE_RECEIVING,
  UPDATING_STATE_FLASHING,
  UPDATING_STATE_COMPLETE,
  UPDATING_STATE_ERROR
} updating_state_t;

// Time estimates for flash phase (in ms) - based on actual timing measurements
// Firmware: ~25 seconds flash time (measured 95s total including 70s upload)
// Assets: ~90 seconds flash time (measured 8.5min total including download)
#define FIRMWARE_FLASH_ESTIMATE_MS  30000  // ~30 seconds (conservative)
#define ASSETS_FLASH_ESTIMATE_MS   100000  // ~100 seconds (conservative)
#define FLASH_PROGRESS_CAP          95     // Max progress during flash (wait for completion event)

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_title_label = NULL;
static lv_obj_t *g_progress_bar = NULL;
static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_prompt_label = NULL;

// State
static updating_state_t g_state = UPDATING_STATE_IDLE;
static update_type_t g_update_type = UPDATE_TYPE_FIRMWARE;
static uint32_t g_flash_start_time = 0;
static uint32_t g_flash_estimate_ms = 0;
static lv_timer_t *g_progress_timer = NULL;
static ui_draw_module_t *g_previous_module = NULL;

// Cross-task handoff. The event handler runs on the event_bus dispatcher task;
// touching LVGL from there races the renderer (lv_inv_area assert). The handler
// only flips these plain flags, and progress_timer_cb -- which runs on the LVGL
// task -- applies the corresponding widget changes.
static volatile bool g_completion_pending = false;
static volatile bool g_completion_success = false;
static updating_state_t g_applied_ui_state = UPDATING_STATE_IDLE;

// Forward declarations
static void updating_event_handler(const event_t *event, void *context);
static void progress_timer_cb(lv_timer_t *timer);
static void update_progress_display(uint8_t percent);
static void show_completion_ui(bool success);
static void handle_button_press(int pad_id);
static void subscribe_active_events(void);
static void unsubscribe_active_events(void);

// Progress timer callback - polls actual progress during transfer, animates during flash
static void progress_timer_cb(lv_timer_t *timer) {
  (void)timer;

  // Apply completion flagged by the event handler (event_dispatch task) here,
  // on the LVGL task, so every widget mutation stays single-threaded.
  if (g_completion_pending) {
    g_completion_pending = false;
    show_completion_ui(g_completion_success);
  }

  // Apply the status text once per state transition (LVGL task context).
  if (g_state != g_applied_ui_state) {
    g_applied_ui_state = g_state;
    if (g_status_label && g_state == UPDATING_STATE_FLASHING) {
      lv_label_set_text(g_status_label, "Writing to flash...");
    }
  }

  if (g_state == UPDATING_STATE_RECEIVING) {
    uint8_t progress = usb_cdc_update_get_progress();
    update_progress_display(progress);
  } else if (g_state == UPDATING_STATE_FLASHING) {
    uint32_t elapsed = esp_log_timestamp() - g_flash_start_time;
    uint32_t estimated_progress = (elapsed * FLASH_PROGRESS_CAP) / g_flash_estimate_ms;
    if (estimated_progress > FLASH_PROGRESS_CAP) estimated_progress = FLASH_PROGRESS_CAP;
    update_progress_display((uint8_t)estimated_progress);

    if (estimated_progress >= FLASH_PROGRESS_CAP && g_status_label) {
      lv_label_set_text(g_status_label, "Finalizing...");
    }
  }
}

// Update the progress bar and status text
static void update_progress_display(uint8_t percent) {
  if (g_progress_bar) {
    lv_bar_set_value(g_progress_bar, percent, LV_ANIM_ON);
  }
}

// Show completion UI with button prompts
static void show_completion_ui(bool success) {
  g_state = success ? UPDATING_STATE_COMPLETE : UPDATING_STATE_ERROR;

  // Note: g_progress_timer is intentionally left running. This function is
  // now invoked from within that timer's own callback, so deleting it here
  // would free the timer mid-dispatch. In the COMPLETE/ERROR state the timer
  // is a no-op, and updating_teardown() frees it.

  if (g_progress_bar) {
    lv_bar_set_value(g_progress_bar, success ? 100 : 0, LV_ANIM_ON);
  }

  if (g_status_label) {
    lv_label_set_text(g_status_label, success ? "Complete!" : "Update Failed");
  }

  if (g_prompt_label) {
    if (success) {
      lv_label_set_text(g_prompt_label, "Tap Omega to restart\nCancel to continue");
    } else {
      lv_label_set_text(g_prompt_label, "Tap Cancel to dismiss");
    }
    lv_obj_remove_flag(g_prompt_label, LV_OBJ_FLAG_HIDDEN);
  }
}

// Handle touch events for button interaction
static void touch_event_handler(const event_t *event, void *context) {
  (void)context;

  if (event->type != EVENT_TOUCH_RELEASE) return;
  if (g_state != UPDATING_STATE_COMPLETE && g_state != UPDATING_STATE_ERROR) return;

  handle_button_press(event->data.touch.pad_id);
}

// Handle button press in completion state
static void handle_button_press(int pad_id) {
  if (pad_id == 8) {
    // Omega button - reset device
    ESP_LOGI(TAG, "User requested device reset");
    esp_restart();
  } else if (pad_id == 12) {
    // Cancel button - dismiss and return to previous module
    ESP_LOGI(TAG, "User dismissed update UI");
    unsubscribe_active_events();
    g_state = UPDATING_STATE_IDLE;
    if (g_previous_module) {
      ui_set_draw_module(g_previous_module);
    }
  }
}

// Event handler for update events
static void updating_event_handler(const event_t *event, void *context) {
  (void)context;

  switch (event->type) {
    case EVENT_UPDATE_STARTED:
      ESP_LOGI(TAG, "Update started: type=%d, size=%lu",
        event->data.update.update_type, (unsigned long)event->data.update.total_size);

      // Subscribe to remaining events now that update is active
      subscribe_active_events();

      g_update_type = (update_type_t)event->data.update.update_type;
      g_state = UPDATING_STATE_RECEIVING;
      g_completion_pending = false;
      g_applied_ui_state = UPDATING_STATE_RECEIVING;
      g_flash_estimate_ms = (g_update_type == UPDATE_TYPE_FIRMWARE)
        ? FIRMWARE_FLASH_ESTIMATE_MS : ASSETS_FLASH_ESTIMATE_MS;

      // Store previous module and switch to updating UI
      g_previous_module = ui_get_current_module();
      ui_set_draw_module(&updating_module);
      break;

    case EVENT_UPDATE_PROGRESS:
      if (event->data.update.phase == UPDATE_PHASE_FLASH) {
        ESP_LOGI(TAG, "Entering flash phase");
        g_state = UPDATING_STATE_FLASHING;
        g_flash_start_time = esp_log_timestamp();
      }
      break;

    case EVENT_UPDATE_COMPLETE:
      ESP_LOGI(TAG, "Update complete: success=%d", event->data.update.success);
      g_completion_success = (event->data.update.success != 0);
      g_completion_pending = true;
      break;

    default:
      break;
  }
}

// Deferred drawing callback
static void updating_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen == NULL) {
    uint16_t disp_w = display_get_width();
    uint16_t disp_h = display_get_height();

    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, disp_w, disp_h);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_screen, 0, 0);

    // Title label
    g_title_label = lv_label_create(g_screen);
    const char *title = (g_update_type == UPDATE_TYPE_FIRMWARE)
      ? "Updating Firmware" : "Updating Assets";
    lv_label_set_text(g_title_label, title);
    lv_obj_set_style_text_color(g_title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_title_label, LV_ALIGN_TOP_MID, 0, 50);

    // Progress bar
    g_progress_bar = lv_bar_create(g_screen);
    lv_obj_set_size(g_progress_bar, 160, 16);
    lv_obj_align(g_progress_bar, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_range(g_progress_bar, 0, 100);
    lv_bar_set_value(g_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_progress_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_progress_bar, lv_color_hex(0x00AA00), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_progress_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(g_progress_bar, 4, LV_PART_INDICATOR);

    // Status label
    g_status_label = lv_label_create(g_screen);
    lv_label_set_text(g_status_label, "Receiving...");
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(g_status_label, LV_ALIGN_CENTER, 0, 30);

    // Prompt label (hidden until completion)
    g_prompt_label = lv_label_create(g_screen);
    lv_label_set_text(g_prompt_label, "");
    lv_obj_set_style_text_color(g_prompt_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(g_prompt_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(g_prompt_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_prompt_label, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_add_flag(g_prompt_label, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Updating screen created: %s", title);
  }

  // Update title if type changed
  if (g_title_label) {
    const char *title = (g_update_type == UPDATE_TYPE_FIRMWARE)
      ? "Updating Firmware" : "Updating Assets";
    lv_label_set_text(g_title_label, title);
  }

  lv_screen_load(g_screen);

  // Start progress polling timer
  if (g_progress_timer == NULL) {
    g_progress_timer = lv_timer_create(progress_timer_cb, 100, NULL);
  }

  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(updating, updating_draw_deferred_cb)

static void updating_teardown(void) {
  if (g_progress_timer) {
    lv_timer_delete(g_progress_timer);
    g_progress_timer = NULL;
  }

  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_title_label = NULL;
    g_progress_bar = NULL;
    g_status_label = NULL;
    g_prompt_label = NULL;
  }

  g_state = UPDATING_STATE_IDLE;
  g_completion_pending = false;
  g_applied_ui_state = UPDATING_STATE_IDLE;
  ESP_LOGD(TAG, "Updating module teardown");
}

static bool s_init_subscribed = false;
static bool s_active_subscribed = false;

// Subscribe to events only needed during active update
static void subscribe_active_events(void) {
  if (s_active_subscribed) return;
  s_active_subscribed = true;
  event_bus_subscribe(EVENT_UPDATE_PROGRESS, updating_event_handler, NULL);
  event_bus_subscribe(EVENT_UPDATE_COMPLETE, updating_event_handler, NULL);
  event_bus_subscribe(EVENT_TOUCH_RELEASE, touch_event_handler, NULL);
}

// Unsubscribe from active update events
static void unsubscribe_active_events(void) {
  if (!s_active_subscribed) return;
  s_active_subscribed = false;
  event_bus_unsubscribe(EVENT_UPDATE_PROGRESS, updating_event_handler);
  event_bus_unsubscribe(EVENT_UPDATE_COMPLETE, updating_event_handler);
  event_bus_unsubscribe(EVENT_TOUCH_RELEASE, touch_event_handler);
}

static void updating_init(void) {
  if (s_init_subscribed) return;
  s_init_subscribed = true;

  // Only subscribe to the trigger event at boot - other events subscribed on demand
  event_bus_subscribe(EVENT_UPDATE_STARTED, updating_event_handler, NULL);

  ESP_LOGI(TAG, "Updating module initialized (1 subscription)");
}

ui_draw_module_t updating_module = {
  .draw_func = updating_draw,
  .teardown_func = updating_teardown,
  .init_func = updating_init,
  .name = "updating",
  .title = "Updating"
};
