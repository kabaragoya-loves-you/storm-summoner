#include "event_bus.h"
#include "midi_messages.h"
#include "esp_log.h"

#define TAG "MIDI_EXPR"

#define EXPRESSION_CHANNEL 0  // MIDI channel for expression

static void expression_event_handler(const event_t* event, void* context) {
  switch (event->type) {
    case EVENT_EXPRESSION_VALUE:
      // Send MIDI CC message using the CC number from the event
      send_control_change(EXPRESSION_CHANNEL, event->data.expression.cc_number, event->data.expression.midi_value);
      break;
      
    case EVENT_EXPRESSION_DISCONNECTED:
      // Optionally send CC value 0 when disconnected
      // send_control_change(EXPRESSION_CHANNEL, EXPRESSION_CC_NUMBER, 0);
      ESP_LOGI(TAG, "Expression pedal disconnected");
      break;
      
    default:
      break;
  }
}

void midi_expression_handler_init(void) {
  // Subscribe to expression events
  event_bus_subscribe(EVENT_EXPRESSION_VALUE, expression_event_handler, NULL);
  event_bus_subscribe(EVENT_EXPRESSION_DISCONNECTED, expression_event_handler, NULL);
  
  ESP_LOGI(TAG, "MIDI expression handler initialized");
}
