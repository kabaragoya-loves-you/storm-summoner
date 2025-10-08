#ifndef IO_H
#define IO_H

#define I2C_SCL_SPEED_HZ     400000

// ADC1 inputs (GPIO16-23 = ADC1_CH0-7)
#define PIN_CV_CLOCK       16  // GPIO16 = ADC1_CH0 (shared CV/clock sync)
#define PIN_EXPRESSION     17  // GPIO17 = ADC1_CH1 (expression pedal)
#define PIN_EXP_REFERENCE  18  // GPIO18 = ADC1_CH2 (VCC reference for ratiometric)
#define PIN_REVISION       19  // GPIO19 = ADC1_CH3 (hardware revision detection)

#define PIN_SCL        9
#define PIN_SDA        8
#define PIN_BUMP_INT   4
#define PIN_TOUCH_CS   5
#define PIN_TOUCH_MOSI 6
#define PIN_TOUCH_SCLK 7
#define PIN_TOUCH_MISO 8
#define PIN_LED        9
#define PIN_TOUCH_INT 10
#define PIN_MOSI      31 // 11
#define PIN_SCLK      30 // 12
#define PIN_DC        28 // 13
#define PIN_RESET     29 // 14
#define PIN_EXP_SW    20  // Expression cable detect (was 16 - conflicted with ADC)
#define PIN_CV_SW     21  // CV cable detect (was 17 - conflicted with ADC)
#define PIN_MIDI_TS   22  // MIDI cable detect (was 18 - conflicted with ADC)
#define PIN_MIDI_SW   23
#define PIN_POLARITY  26
#define PIN_CALIBRATE 42
#define PIN_MIDI_TXD  43
#define PIN_MIDI_RXD  44

#endif // IO_H 