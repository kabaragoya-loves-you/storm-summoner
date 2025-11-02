#ifndef _USB_DESCRIPTORS_H_
#define _USB_DESCRIPTORS_H_

#ifdef __cplusplus
extern "C" {
#endif

// Switch to MIDI-only mode (default)
void usb_descriptors_set_midi_mode(void);

// Switch to MSC-only mode (firmware update)
void usb_descriptors_set_msc_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* _USB_DESCRIPTORS_H_ */

