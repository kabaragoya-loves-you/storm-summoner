#ifndef IO_H
#define IO_H

// GPIO and ADC channel definitions for all modules
// Update this file to support multiple PCB revisions

// LED
#define LED_GPIO 15

// Expression Pedal
#define EXPRESSION_ADC_CHANNEL     ADC_CHANNEL_6

// CV
#define CV_ADC_CHANNEL     ADC_CHANNEL_5
#define CV_SYNC_GPIO      16

// Display (SSD1327)
// Note: For DMA support, MOSI must be on GPIO 11 and CLK must be on GPIO 12
// These pins are connected to the ESP32-S3's SPI2 peripheral with DMA capability
#define PIN_CLK   48 // Non-DMA pin (GPIO 12 is DMA-capable)
#define PIN_MOSI  34 // Non-DMA pin (GPIO 11 is DMA-capable)
#define PIN_DC    33
#define PIN_RESET 39

// MIDI
#define MIDI_TXD      26
#define MIDI_RXD      47
#define PIN_POLARITY  38
#define MIDI_GROUND   39

// I2C
#define I2C_MASTER_SDA_IO    21 // move to 8 soon
#define I2C_MASTER_SCL_IO    18 // move to 9 soon
#define I2C_MASTER_NUM       I2C_NUM_0
#define I2C_SCL_SPEED_HZ     100000

// Touch2 (IS31SE5117A)
#define TOUCH2_INTB_GPIO     37

// Bump (LIS3DHTR)
#define BUMP_INT1_GPIO       40

// Expression Cable Detect
#define EXPRESSION_CABLE_DETECT_GPIO 41

// CV Switch
#define CV_CABLE_DETECT_GPIO 36

#endif // IO_H 