#include "menu.h"
#include "menu_pages.h"
#include "action_config.h"
#include "scene.h"
#include "action.h"
#include "ui.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_CC_TRIG"

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

static uint8_t s_editing_slot = 0;
static uint8_t s_remembered_slot = 0;

uint8_t cc_triggers_focus_slot_get(void) {
  return s_remembered_slot;
}

void cc_triggers_focus_slot_set(uint8_t slot) {
  if (slot < NUM_CC_TRIGGERS) s_remembered_slot = slot;
}

static void prepare_cc_triggers_list_focus(void) {
  if (s_editing_slot < NUM_CC_TRIGGERS)
    cc_triggers_focus_slot_set(s_editing_slot);
  menu_set_restore_focus((int)cc_triggers_focus_slot_get());
}

static menu_item_t s_list_items[NUM_CC_TRIGGERS];
static char s_list_labels[LABEL_BUFFER_SETS][NUM_CC_TRIGGERS][48];

static menu_item_t s_slot_items[2];
static char s_action_label[48];
static char s_cc_label[32];

static action_config_context_t s_slot_action_ctx[NUM_CC_TRIGGERS];

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static void persist_scene_changes(void) {
  if (ui_is_in_programming_mode()) {
    uint8_t scene_index = scene_get_current_index();
    scene_save_to_flash(scene_index);
  }
}

static void format_slot_summary(const cc_trigger_slot_t* slot, char* buf, size_t len) {
  if (!slot || slot->action.type == ACTION_NONE) {
    snprintf(buf, len, "<None>");
    return;
  }
  char action_name[24];
  action_get_display_name(&slot->action, action_name, sizeof(action_name));
  snprintf(buf, len, "CC %u -> %s", (unsigned)slot->cc_number, action_name);
}

static void cc_trigger_action_complete(action_config_context_t* ctx, action_t* action) {
  (void)ctx;
  (void)action;
  persist_scene_changes();
}

static void fill_action_ctx(uint8_t slot) {
  scene_t* scene = scene_get_current();
  if (!scene || slot >= NUM_CC_TRIGGERS) return;

  s_slot_action_ctx[slot].target_action = &scene->cc_triggers[slot].action;
  s_slot_action_ctx[slot].return_page = menu_page_cc_trigger_slot_create;
  s_slot_action_ctx[slot].return_depth = 1;
  s_slot_action_ctx[slot].type_picker_pop_depth = 0;
  s_slot_action_ctx[slot].on_complete = cc_trigger_action_complete;
  s_slot_action_ctx[slot].user_data = (void*)(uintptr_t)slot;
  s_slot_action_ctx[slot].trigger_type = ACTION_TRIGGER_CC;

  static char detail_titles[NUM_CC_TRIGGERS][24];
  snprintf(detail_titles[slot], sizeof(detail_titles[slot]), "Trigger %u", (unsigned)(slot + 1));
  s_slot_action_ctx[slot].detail_title = detail_titles[slot];
  s_slot_action_ctx[slot].source_title = detail_titles[slot];
}

static void nav_to_trigger_slot_page(void) {
  static char title[16];
  snprintf(title, sizeof(title), "Trigger %u", (unsigned)(s_editing_slot + 1));
  menu_navigate_to(title, menu_page_cc_trigger_slot_create);
}

static bool slot_page_handle_back(void) {
  menu_set_custom_back_handler(NULL);
  prepare_cc_triggers_list_focus();
  menu_pop_then_replace_deferred(1, "CC Triggers", menu_page_cc_triggers_scene_create);
  return true;
}

static void nav_to_slot(void* user_data) {
  s_editing_slot = (uint8_t)(uintptr_t)user_data;
  if (s_editing_slot >= NUM_CC_TRIGGERS) return;
  cc_triggers_focus_slot_set(s_editing_slot);
  nav_to_trigger_slot_page();
}

static void nav_to_action(void* user_data) {
  (void)user_data;
  scene_t* scene = scene_get_current();
  if (!scene || s_editing_slot >= NUM_CC_TRIGGERS) return;

  fill_action_ctx(s_editing_slot);
  if (scene->cc_triggers[s_editing_slot].action.type == ACTION_NONE) {
    s_slot_action_ctx[s_editing_slot].type_picker_pop_depth = 1;
    action_config_start_type_picker(&s_slot_action_ctx[s_editing_slot]);
  } else {
    action_config_start(&s_slot_action_ctx[s_editing_slot]);
  }
}

static void cc_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  scene_t* scene = scene_get_current();
  if (!scene || s_editing_slot >= NUM_CC_TRIGGERS) return;

  scene->cc_triggers[s_editing_slot].cc_number =
    (uint8_t)(selected_index <= 127 ? selected_index : 127);
  persist_scene_changes();
  prepare_cc_triggers_list_focus();
  menu_pop_then_replace_deferred(2, "CC Triggers", menu_page_cc_triggers_scene_create);
}

static lv_obj_t* cc_roller_create(void) {
  static char options[2048];
  options[0] = '\0';
  for (int i = 0; i < 128; i++) {
    char num[8];
    snprintf(num, sizeof(num), "%d", i);
    if (i > 0) strcat(options, "\n");
    strcat(options, num);
  }

  scene_t* scene = scene_get_current();
  uint32_t cur = 0;
  if (scene && s_editing_slot < NUM_CC_TRIGGERS)
    cur = scene->cc_triggers[s_editing_slot].cc_number;

  return menu_create_roller_page("CC", options, cur, cc_confirm_cb, NULL);
}

static void nav_to_cc(void* user_data) {
  (void)user_data;
  menu_navigate_to("CC", cc_roller_create);
}

lv_obj_t* menu_page_cc_trigger_slot_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene || s_editing_slot >= NUM_CC_TRIGGERS)
    return menu_create_page_2line("Trigger", NULL, 0);

  cc_triggers_focus_slot_set(s_editing_slot);

  cc_trigger_slot_t* slot = &scene->cc_triggers[s_editing_slot];
  bool has_action = slot->action.type != ACTION_NONE;
  int n = 0;

  char action_name[40];
  if (has_action)
    action_get_display_name(&slot->action, action_name, sizeof(action_name));
  else
    snprintf(action_name, sizeof(action_name), "<None>");

  snprintf(s_action_label, sizeof(s_action_label), "Action\n%s", action_name);
  s_slot_items[n++] = (menu_item_t){
    s_action_label, nav_to_action, NULL, true, MENU_ITEM_KIND_ROLLER
  };

  if (has_action) {
    snprintf(s_cc_label, sizeof(s_cc_label), "CC\n%u", (unsigned)slot->cc_number);
    s_slot_items[n++] = (menu_item_t){
      s_cc_label, nav_to_cc, NULL, true, MENU_ITEM_KIND_ROLLER
    };
  } else {
    snprintf(s_cc_label, sizeof(s_cc_label), "CC\nPending");
    s_slot_items[n++] = (menu_item_t){
      s_cc_label, NULL, NULL, false, MENU_ITEM_KIND_DISPLAY
    };
  }

  static char title[16];
  snprintf(title, sizeof(title), "Trigger %u", (unsigned)(s_editing_slot + 1));
  menu_set_custom_back_handler(slot_page_handle_back);
  return menu_create_page_2line(title, s_slot_items, (uint8_t)n);
}

lv_obj_t* menu_page_cc_triggers_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return menu_create_page_2line("CC Triggers", NULL, 0);

  menu_set_custom_back_handler(NULL);

  int buf = get_next_buffer_set();

  for (int i = 0; i < NUM_CC_TRIGGERS; i++) {
    char summary[32];
    format_slot_summary(&scene->cc_triggers[i], summary, sizeof(summary));
    snprintf(s_list_labels[buf][i], sizeof(s_list_labels[buf][i]),
      "Trigger %d\n%s", i + 1, summary);
    s_list_items[i] = (menu_item_t){
      s_list_labels[buf][i], nav_to_slot, (void*)(uintptr_t)i, true,
      MENU_ITEM_KIND_SUBMENU
    };
  }

  return menu_create_page_2line("CC Triggers", s_list_items, NUM_CC_TRIGGERS);
}
