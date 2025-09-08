#include <stdint.h>
#include <stdbool.h>

typedef enum {
  CV_INPUT_MODE_5V,          // AIN1: 0-5V
  CV_INPUT_MODE_10V,         // AIN2: 0-10V
  CV_INPUT_MODE_5V_BIPOLAR,  // AIN3: -5V to +5V
} adc_cv_mode_t;

// void adc_init(void);
void adc_set_cv_mode(adc_cv_mode_t mode);
adc_cv_mode_t adc_get_cv_mode(void);
int16_t adc_get_cv_value(void);

// Expression pedal functions
// void expression_init(void);
// void expression_enable(void);
void expression_disable(void);
float expression_get_value(void);
uint8_t expression_get_midi_value(void);
void expression_set_min_value(int16_t value);
void expression_set_max_value(int16_t value);
uint8_t expression_get_deadzone(void);
void expression_set_deadzone(uint8_t deadzone); 