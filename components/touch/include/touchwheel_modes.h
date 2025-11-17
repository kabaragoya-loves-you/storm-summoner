#ifndef TOUCHWHEEL_MODES_H
#define TOUCHWHEEL_MODES_H

#include <stdint.h>

#define NUM_WHEEL_PADS 8

// Mode processor function type
// Returns processed value from raw delta and current position
typedef int (*touchwheel_mode_process_fn_t)(int raw_delta, int current_position, void* mode_data);

// Mode processor structure
typedef struct {
  touchwheel_mode_process_fn_t process_fn;
  void* mode_data;
  const char* name;
} touchwheel_mode_processor_t;

// Mode type enum
typedef enum {
  TOUCHWHEEL_MODE_ENDLESS,
  TOUCHWHEEL_MODE_ODOMETER,
  TOUCHWHEEL_MODE_BIPOLAR
} touchwheel_mode_type_t;

/**
 * Create endless encoder mode (pass-through raw delta)
 * @return Mode processor instance (caller must free with touchwheel_mode_destroy)
 */
touchwheel_mode_processor_t* touchwheel_mode_create_endless(void);

/**
 * Create odometer mode (maps position to 0-100%)
 * Pad 4 (7 o'clock) = 0%
 * Pad 5 (5 o'clock) = 100%
 * @return Mode processor instance (caller must free with touchwheel_mode_destroy)
 */
touchwheel_mode_processor_t* touchwheel_mode_create_odometer(void);

/**
 * Create bipolar mode (maps position to -100 to +100)
 * Pad 4 (7 o'clock) = -100
 * Pads 7+0 (noon) = 0
 * Pad 5 (5 o'clock) = +100
 * @return Mode processor instance (caller must free with touchwheel_mode_destroy)
 */
touchwheel_mode_processor_t* touchwheel_mode_create_bipolar(void);

/**
 * Process delta through mode processor
 * @param mode Mode processor instance
 * @param raw_delta Raw delta from core driver
 * @param position Current pad position (0-7)
 * @return Processed value
 */
int touchwheel_mode_process(touchwheel_mode_processor_t* mode, int raw_delta, int position);

/**
 * Destroy mode processor instance
 * @param mode Mode processor instance to destroy
 */
void touchwheel_mode_destroy(touchwheel_mode_processor_t* mode);

#endif // TOUCHWHEEL_MODES_H


