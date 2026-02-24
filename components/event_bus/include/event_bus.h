#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// Debug feature flags
#define EVENT_BUS_ENABLE_TRACE_LOG    1
#define EVENT_BUS_ENABLE_STATISTICS   1
#define EVENT_BUS_ENABLE_HISTORY      1
#define EVENT_BUS_ENABLE_PROFILING    1  // Detailed per-type profiling

// Configuration
#define EVENT_BUS_QUEUE_SIZE          128
#define EVENT_BUS_MAX_HANDLERS        64  // Increased for scene/action system
#define EVENT_BUS_HISTORY_SIZE        16

typedef enum {
  EVENT_TOUCH_PRESS,
  EVENT_TOUCH_RELEASE,
  EVENT_LONG_PRESS_DETECTED,
  EVENT_GESTURE_ROTARY,
  EVENT_MODE_CHANGE_REQUEST,
  EVENT_HAPTIC_REQUEST,
  EVENT_LED_FLASH_REQUEST,
  EVENT_LED_FLICKER_START,
  EVENT_LED_FLICKER_STOP,
  
  // Transport events
  EVENT_TRANSPORT_START,
  EVENT_TRANSPORT_STOP,
  EVENT_TRANSPORT_PAUSE,
  EVENT_TRANSPORT_CONTINUE,
  EVENT_TRANSPORT_RECORD,
  EVENT_TRANSPORT_STATE_CHANGED,
  
  // Tempo/timing events
  EVENT_BEAT,
  EVENT_TEMPO_CHANGED,
  EVENT_BUMP_DETECTED,
  EVENT_ENCODER_ROTATE,
  EVENT_TIMER_TICK,
  EVENT_BUTTON_L_PRESS,
  EVENT_BUTTON_R_PRESS,
  EVENT_BUTTON_BOTH_PRESS,
  EVENT_BUTTON_L_LONG_PRESS,
  EVENT_BUTTON_R_LONG_PRESS,
  EVENT_BUTTON_BOTH_LONG_PRESS,
  EVENT_MIDI_ACTION,
  EVENT_UI_ACTION,
  EVENT_SENSOR_ALS,
  EVENT_SENSOR_PROXIMITY,
  EVENT_MIDI_IN,
  EVENT_USB_MIDI_CONNECTED,
  EVENT_USB_MIDI_DISCONNECTED,
  EVENT_EXPRESSION_VALUE,
  EVENT_EXPRESSION_CONNECTED,
  EVENT_EXPRESSION_DISCONNECTED,
  EVENT_EXPRESSION_SUSTAIN,
  EVENT_EXPRESSION_SOSTENUTO,
  EVENT_EXPRESSION_GATE,
  EVENT_EXPRESSION_SWITCH,
  EVENT_CV_VALUE,
  EVENT_CV_DISCONNECTED,
  EVENT_CLOCK_SYNC_PULSE,
  EVENT_SCENE_CHANGED,
  EVENT_SCREENSAVER_TIMEOUT,
  EVENT_NOTE_ON,
  EVENT_NOTE_OFF,
  EVENT_TOUCHWHEEL_VALUE,
  EVENT_LFO1_VALUE,
  EVENT_LFO2_VALUE,
  EVENT_SAMPLE_HOLD_VALUE,
  EVENT_TYPE_MAX
} event_type_t;

typedef enum {
  EVENT_PRIORITY_LOW,
  EVENT_PRIORITY_NORMAL,
  EVENT_PRIORITY_HIGH,
  EVENT_PRIORITY_CRITICAL
} event_priority_t;

// MIDI input sources
typedef enum {
  MIDI_SOURCE_UART,
  MIDI_SOURCE_USB,
  MIDI_SOURCE_NETWORK,
  MIDI_SOURCE_INTERNAL
} midi_source_t;

typedef enum {
  HAPTIC_CLICK,
  HAPTIC_INCREMENT,
  HAPTIC_DECREMENT,
  HAPTIC_LONG_PRESS,
  HAPTIC_ERROR
} haptic_pattern_t;

typedef struct {
  event_type_t type;
  event_priority_t priority;
  uint32_t timestamp;
  union {
    struct {
      int pad_id;           // 0-12 logical pad number
      int pad_num;          // Actual touch_pad_t value
    } touch;
    
    struct {
      int pad_id;
      uint32_t duration_ms;
    } long_press;
    
    struct {
      int delta;            // Positive = CW, Negative = CCW
      int speed_multiplier; // Speed-based scaling factor
      int position;         // Current logical position (0-7)
    } rotary;
    
    struct {
      haptic_pattern_t pattern;
    } haptic;
    
    struct {
      uint32_t duration_ms;
    } led_flash;
    
    struct {
      int intensity;
      int duration_ms;
    } bump;
    
    struct {
      int delta;            // Encoder steps
      int absolute;         // Absolute position if tracked
    } encoder;
    
    struct {
      uint8_t channel;
      uint8_t type;         // Note on/off, CC, PC, etc.
      uint8_t param1;
      uint8_t param2;
    } midi;
    
    struct {
      uint32_t custom_type;
      uint32_t param1;
      uint32_t param2;
    } custom;
    
    struct {
      uint8_t channel;
      uint8_t controller;    // CC number
      uint8_t value;         // 0-127
    } sensor;
    
    struct {
      uint8_t type;          // MIDI message type (note on/off, CC, etc)
      uint8_t channel;       // 0-15
      uint8_t data1;         // Note/CC number/etc
      uint8_t data2;         // Velocity/value/etc
      uint8_t source;        // MIDI_SOURCE_UART, MIDI_SOURCE_USB, etc
      uint8_t raw_status;    // Original status byte
      uint16_t length;       // For SysEx and other variable length messages
      uint8_t* sysex_data;   // Pointer to SysEx data (if applicable)
    } midi_in;
    
    struct {
      int16_t raw_value;     // Raw ADC value
      uint8_t midi_value;    // Scaled MIDI CC value (0-127)
      uint8_t cc_number;     // MIDI CC number to send
    } expression;
    
    struct {
      bool pressed;          // true = pressed, false = released
    } pedal;
    
    struct {
      bool high;             // true = gate high, false = gate low
      int16_t raw_value;     // Raw ADC value
    } gate;
    
    struct {
      int16_t raw_value;     // Raw ADC value
      uint8_t midi_value;    // Scaled MIDI value (0-127)
      uint8_t mode;          // CV mode (unipolar/bipolar, range)
    } cv;
    
    // Transport event data
    struct {
      uint8_t state;         // transport_state_t
      uint8_t source;        // Source of the event (MIDI, UI, etc)
      uint8_t is_resume;     // 1 = resume from pause, 0 = fresh start
    } transport;
    
    // Beat event data  
    struct {
      uint8_t beat_in_bar;   // 1-based position in bar
      uint8_t bar_length;    // Total beats per bar (from time signature)
    } beat;
    
    // Tempo event data
    struct {
      uint16_t bpm;          // Current tempo in BPM (20-300)
    } tempo;
    
    // Scene event data
    struct {
      uint8_t scene_index;   // Scene number (0-based)
    } scene;
    
    // Generic value for simple events
    uint8_t value_uint8;
    
    // Note event data
    struct {
      uint8_t channel;       // MIDI channel (0-15)
      uint8_t note;          // MIDI note number (0-127)
      uint8_t velocity;      // MIDI velocity (0-127)
    } note;
    
    // Button event data
    struct {
      uint8_t button_id;     // 0=left, 1=right, 2=both
      uint32_t duration_ms;  // Duration for long press
    } button;
    
    // Touchwheel value event data
    struct {
      int value;              // Processed touchwheel value
    } touchwheel_value;
  } data;
} event_t;

// Handler function type
typedef void (*event_handler_t)(const event_t* event, void* context);

// Handler registration info
typedef struct {
  event_type_t type;
  event_handler_t handler;
  void* context;
  event_priority_t min_priority;  // Only receive events >= this priority
  bool active;
} event_subscription_t;

// Core API
esp_err_t event_bus_init(void);
esp_err_t event_bus_deinit(void);

// Subscription management
esp_err_t event_bus_subscribe(event_type_t type, event_handler_t handler, void* context);
esp_err_t event_bus_subscribe_with_priority(event_type_t type, event_handler_t handler, 
                                          void* context, event_priority_t min_priority);
esp_err_t event_bus_unsubscribe(event_type_t type, event_handler_t handler);

// Event posting
esp_err_t event_bus_post(const event_t* event);
esp_err_t event_bus_post_from_isr(const event_t* event, BaseType_t* higher_priority_woken);

// Utility functions
const char* event_type_to_string(event_type_t type);
uint32_t event_bus_get_current_timestamp(void);

// Diagnostics (always available)
void event_bus_print_diagnostics(void);
void event_bus_print_handlers(void);
uint32_t event_bus_get_queue_depth(void);

// Debug/Statistics API (only available when enabled)
#if EVENT_BUS_ENABLE_STATISTICS
typedef struct {
  uint32_t events_posted;
  uint32_t events_processed;
  uint32_t events_dropped;
  uint32_t queue_high_watermark;
  uint32_t processing_time_max_ms;
  uint32_t events_by_type[EVENT_TYPE_MAX];
} event_bus_stats_t;

void event_bus_get_stats(event_bus_stats_t* stats);
void event_bus_reset_stats(void);
#endif

#if EVENT_BUS_ENABLE_HISTORY
void event_bus_dump_history(void);
#endif

#if EVENT_BUS_ENABLE_PROFILING
// Event profiling for identifying noisy publishers
void event_bus_profiling_start(void);
void event_bus_profiling_stop(void);
void event_bus_profiling_reset(void);
void event_bus_profiling_report(void);  // Print sorted table of event frequencies
bool event_bus_profiling_is_active(void);
#endif

#endif // EVENT_BUS_H