/**
 * MIDI Callback Bridge
 * 
 * This module provides backwards compatibility for the callback-based MIDI IN system
 * by subscribing to MIDI IN events and calling the registered callbacks.
 * 
 * This is a transitional component that allows gradual migration from callbacks to events.
 * Once all components have migrated to the event-based system, this can be removed.
 * 
 * Usage:
 * 1. Call midi_callback_bridge_init() with your callbacks
 * 2. The bridge will subscribe to MIDI IN events and call your callbacks
 * 3. When ready, migrate your component to subscribe directly to events
 * 4. Remove the callback registration
 */

#include "midi_in.h"
#include "midi_in_event_handler.h"
#include "event_bus.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#define TAG "MIDI_BRIDGE"

static midi_in_callbacks_t callbacks_inst = {0};

// Convert event data to legacy midi_message_t format
static void event_to_message(const event_t* event, midi_message_t* msg) {
  // Access the midi_in data directly
  const uint8_t type = event->data.midi_in.type;
  const uint8_t channel = event->data.midi_in.channel;
  const uint8_t data1 = event->data.midi_in.data1;
  const uint8_t data2 = event->data.midi_in.data2;
  const uint8_t raw_status = event->data.midi_in.raw_status;
  const uint16_t length = event->data.midi_in.length;
  const uint8_t* sysex_data = event->data.midi_in.sysex_data;
  
  msg->event = (midi_event_type_t)type;
  msg->channel = channel;
  msg->length = length;
  
  // Build the message data array
  if (sysex_data && length > 0) {
    // SysEx data
    if (length <= sizeof(msg->data)) {
      memcpy(msg->data, sysex_data, length);
    } else {
      ESP_LOGW(TAG, "SysEx too large for legacy buffer");
      msg->length = 0;
    }
  } else {
    // Regular messages
    msg->data[0] = raw_status;
    if (length > 1) msg->data[1] = data1;
    if (length > 2) msg->data[2] = data2;
  }
}

static void midi_bridge_handle_event(const event_t* event, void* context) {
  if (event->type != EVENT_MIDI_IN) return;
  
  midi_message_t msg = {0};
  event_to_message(event, &msg);
  
  // Call appropriate callback based on message type
  switch (msg.event) {
    case MIDI_EVENT_NOTE_OFF:
      if (callbacks_inst.note_off)
        callbacks_inst.note_off(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_NOTE_ON:
      if (callbacks_inst.note_on)
        callbacks_inst.note_on(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_POLY_AFTERTOUCH:
      if (callbacks_inst.poly_aftertouch)
        callbacks_inst.poly_aftertouch(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_CONTROL_CHANGE:
      if (callbacks_inst.control_change)
        callbacks_inst.control_change(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_PROGRAM_CHANGE:
      if (callbacks_inst.program_change)
        callbacks_inst.program_change(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_CHANNEL_AFTERTOUCH:
      if (callbacks_inst.channel_aftertouch)
        callbacks_inst.channel_aftertouch(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_PITCH_BEND:
      if (callbacks_inst.pitch_bend)
        callbacks_inst.pitch_bend(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_TIME_CODE:
      if (callbacks_inst.time_code)
        callbacks_inst.time_code(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_SONG_POSITION:
      if (callbacks_inst.song_position)
        callbacks_inst.song_position(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_SONG_SELECT:
      if (callbacks_inst.song_select)
        callbacks_inst.song_select(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_TUNE_REQUEST:
      if (callbacks_inst.tune_request)
        callbacks_inst.tune_request(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_SYS_EX:
      if (callbacks_inst.sys_ex)
        callbacks_inst.sys_ex(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_REALTIME_CLOCK:
      if (callbacks_inst.realtime_clock)
        callbacks_inst.realtime_clock(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_REALTIME_TICK:
      if (callbacks_inst.realtime_tick)
        callbacks_inst.realtime_tick(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_REALTIME_START:
      if (callbacks_inst.realtime_start)
        callbacks_inst.realtime_start(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_REALTIME_CONTINUE:
      if (callbacks_inst.realtime_continue)
        callbacks_inst.realtime_continue(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_REALTIME_STOP:
      if (callbacks_inst.realtime_stop)
        callbacks_inst.realtime_stop(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_REALTIME_RESET:
      if (callbacks_inst.realtime_reset)
        callbacks_inst.realtime_reset(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    case MIDI_EVENT_ACTIVE_SENSING:
      if (callbacks_inst.active_sensing)
        callbacks_inst.active_sensing(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
      
    default:
      if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      break;
  }
  
  // Free any allocated SysEx data
  if (event->data.midi_in.sysex_data) free(event->data.midi_in.sysex_data);
}

void midi_callback_bridge_init(const midi_in_callbacks_t *callbacks) {
  if (callbacks) callbacks_inst = *callbacks;
  
  // Subscribe to MIDI IN events
  event_bus_subscribe(EVENT_MIDI_IN, midi_bridge_handle_event, NULL);
  
  ESP_LOGI(TAG, "MIDI callback bridge initialized");
}

// Alternative init that uses the event handler directly
void midi_in_init(const midi_in_callbacks_t *callbacks) {
  // Initialize the event handler
  midi_in_event_handler_init();
  
  // Set up the callback bridge if callbacks provided
  if (callbacks) midi_callback_bridge_init(callbacks);
}
