#ifndef IO_H
#define IO_H

#define I2C_SCL_SPEED_HZ    400000
#define ADC_ATTEN           ADC_ATTEN_DB_12

#define CV_ADC_CHANNEL      ADC_CHANNEL_0  // GPIO16 (Control voltage/envelope follower)
#define EXP_ADC_CHANNEL     ADC_CHANNEL_1  // GPIO17 (Expression/sustain)
#define REF_ADC_CHANNEL     ADC_CHANNEL_2  // GPIO18 (VCC reference for ratiometric)
#define REV_ADC_CHANNEL     ADC_CHANNEL_3  // GPIO19 (Board revision resistor divider)
#define CV_SW_ADC_CHANNEL   ADC_CHANNEL_4  // GPIO20 (Control voltage switch)

#define PIN_CV_CLOCK       16
#define PIN_EXPRESSION     17
#define PIN_EXP_REFERENCE  18
#define PIN_REVISION       19
#define PIN_CV_SW          20
#define PIN_BUMP_INT       21
#define PIN_BUTTON_R       22
#define PIN_LED            23
#define PIN_DC             28
#define PIN_MOSI           29
#define PIN_SCLK           30
#define PIN_RESET          31
#define PIN_BUTTON_L       35
#define PIN_SDA            40
#define PIN_SCL            41
#define PIN_MIDI_TXD       49
#define PIN_MIDI_RXD       50
#define PIN_EXP_SW         51
#define PIN_MIDI_SW        52
#define PIN_MIDI_TS        53
#define PIN_POLARITY       54

// Available GPIOs
// 0
// 1
// 2
// 32
// 33
// 39
// 42
// 43
// 44
// 45
// 46
// 47
// 48

#endif // IO_H 