#include "menu.h"
#include "menu_pages.h"
#include "note_track_config.h"
#include <stdio.h>
#include <string.h>

lv_obj_t* menu_page_settings_note_track_create(void);

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_NT_ITEMS 4
static menu_item_t s_items[MAX_NT_ITEMS];

static char s_low_label[LABEL_BUFFER_SETS][32];
static char s_high_label[LABEL_BUFFER_SETS][32];
static char s_channel_label[LABEL_BUFFER_SETS][40];
static char s_filter_mode_label[LABEL_BUFFER_SETS][40];

static bool s_callback_in_progress = false;

static const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static void format_note(uint8_t midi_note, char* buf, size_t buf_size) {
  int octave = (midi_note / 12) - 1;
  int idx = midi_note % 12;
  snprintf(buf, buf_size, "%s%d (%u)", NOTE_NAMES[idx], octave, (unsigned)midi_note);
}

static void low_note_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  uint8_t note = (uint8_t)(selected_index <= 127 ? selected_index : 127);
  uint8_t hi = note_track_get_high_note();
  if (note > hi) note = hi;
  note_track_set_low_note(note);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_settings_note_track_create);
}

static lv_obj_t* low_note_roller_create(void) {
  static char options[1536];
  options[0] = '\0';
  for (int i = 0; i < 128; i++) {
    char note[16];
    format_note((uint8_t)i, note, sizeof(note));
    if (i > 0) strcat(options, "\n");
    strcat(options, note);
  }
  uint32_t cur = note_track_get_low_note();
  return menu_create_roller_page("Low Note", options, cur, low_note_confirm_cb, NULL);
}

static void nav_to_low_note(void* user_data) {
  (void)user_data;
  menu_navigate_to("Low Note", low_note_roller_create);
}

static void high_note_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  uint8_t note = (uint8_t)(selected_index <= 127 ? selected_index : 127);
  uint8_t lo = note_track_get_low_note();
  if (note < lo) note = lo;
  note_track_set_high_note(note);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_settings_note_track_create);
}

static lv_obj_t* high_note_roller_create(void) {
  static char options[1536];
  options[0] = '\0';
  for (int i = 0; i < 128; i++) {
    char note[16];
    format_note((uint8_t)i, note, sizeof(note));
    if (i > 0) strcat(options, "\n");
    strcat(options, note);
  }
  uint32_t cur = note_track_get_high_note();
  return menu_create_roller_page("High Note", options, cur, high_note_confirm_cb, NULL);
}

static void nav_to_high_note(void* user_data) {
  (void)user_data;
  menu_navigate_to("High Note", high_note_roller_create);
}

static void channel_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  if (selected_index <= 16) note_track_set_channel((uint8_t)selected_index);
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_settings_note_track_create);
}

static lv_obj_t* channel_roller_create(void) {
  static char options[256];
  strcpy(options, "Omni");
  size_t pos = strlen(options);
  for (int i = 1; i <= 16; i++) {
    int written = snprintf(options + pos, sizeof(options) - pos, "\nCh %d", i);
    if (written < 0) break;
    pos += (size_t)written;
    if (pos >= sizeof(options) - 6) break;
  }
  uint32_t cur = note_track_get_channel();
  return menu_create_roller_page("Channel", options, cur, channel_confirm_cb, NULL);
}

static void nav_to_channel(void* user_data) {
  (void)user_data;
  menu_navigate_to("Channel", channel_roller_create);
}

static void filter_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  note_track_filter_mode_t mode = (selected_index == 1) ? NOTE_TRACK_FILTER_KILL : NOTE_TRACK_FILTER_INTERCEPT;
  note_track_set_filter_mode(mode);
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_settings_note_track_create);
}

static lv_obj_t* filter_mode_roller_create(void) {
  uint32_t cur = (note_track_get_filter_mode() == NOTE_TRACK_FILTER_KILL) ? 1 : 0;
  return menu_create_roller_page("Filter Mode", "Intercept\nKill", cur, filter_mode_confirm_cb, NULL);
}

static void nav_to_filter_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Filter Mode", filter_mode_roller_create);
}

lv_obj_t* menu_page_settings_note_track_create(void) {
  int buf = get_next_buffer_set();
  int n = 0;

  char note_buf[16];
  format_note(note_track_get_low_note(), note_buf, sizeof(note_buf));
  snprintf(s_low_label[buf], sizeof(s_low_label[buf]), "Low Note\n%s", note_buf);
  s_items[n++] = (menu_item_t){ s_low_label[buf], nav_to_low_note, NULL, true, MENU_ITEM_KIND_ROLLER };

  format_note(note_track_get_high_note(), note_buf, sizeof(note_buf));
  snprintf(s_high_label[buf], sizeof(s_high_label[buf]), "High Note\n%s", note_buf);
  s_items[n++] = (menu_item_t){ s_high_label[buf], nav_to_high_note, NULL, true, MENU_ITEM_KIND_ROLLER };

  uint8_t ch = note_track_get_channel();
  if (ch == 0) snprintf(s_channel_label[buf], sizeof(s_channel_label[buf]), "Channel\nOmni");
  else snprintf(s_channel_label[buf], sizeof(s_channel_label[buf]), "Channel\nCh %u", (unsigned)ch);
  s_items[n++] = (menu_item_t){ s_channel_label[buf], nav_to_channel, NULL, true, MENU_ITEM_KIND_ROLLER };

  const char* mode_str = (note_track_get_filter_mode() == NOTE_TRACK_FILTER_KILL) ? "Kill" : "Intercept";
  snprintf(s_filter_mode_label[buf], sizeof(s_filter_mode_label[buf]), "Filter Mode\n%s", mode_str);
  s_items[n++] = (menu_item_t){ s_filter_mode_label[buf], nav_to_filter_mode, NULL, true, MENU_ITEM_KIND_ROLLER };

  return menu_create_page_2line("Note Track", s_items, n);
}
