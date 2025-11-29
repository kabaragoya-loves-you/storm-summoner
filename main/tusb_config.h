#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// Enable debug logging
#define CFG_TUSB_DEBUG  3  // 0=none, 1=errors, 2=warnings, 3=info

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

// Class support - override sdkconfig defaults
#define CFG_TUD_MIDI            1  // Enable MIDI class

#ifdef CFG_TUD_CDC
#undef CFG_TUD_CDC
#endif
#define CFG_TUD_CDC             1  // Single CDC port for updates and console

#define CFG_TUD_MSC             0  // Disable Mass Storage class

// MIDI FIFO size
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64

// CDC FIFO size
#define CFG_TUD_CDC_RX_BUFSIZE  1024
#define CFG_TUD_CDC_TX_BUFSIZE  1024

// MSC configuration (for firmware upload mode)
#define CFG_TUD_MSC_EP_BUFSIZE  512

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */

