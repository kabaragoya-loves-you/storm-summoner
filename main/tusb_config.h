#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// Board specific configuration
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE)
#define CFG_TUSB_OS             OPT_OS_FREERTOS

// Device mode
#define CFG_TUD_ENABLED         1

// RHPort number used for device
#define BOARD_TUD_RHPORT        0

// RHPort max operational speed
#define BOARD_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED

// Device configuration
#define CFG_TUD_ENDPOINT0_SIZE  64

// Class support
#define CFG_TUD_MIDI            1  // Enable MIDI class
#define CFG_TUD_MSC             1  // Enable Mass Storage class for firmware updates

// Disable ESP-IDF wrapper's MSC implementation - we provide our own
#ifndef CONFIG_TINYUSB_MSC_ENABLED
#define CONFIG_TINYUSB_MSC_ENABLED 0
#endif

// MIDI FIFO size
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64

// MSC configuration (for firmware upload mode)
#define CFG_TUD_MSC_EP_BUFSIZE  512

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */

