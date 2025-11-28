#include "tusb.h"
#include "esp_log.h"
#include "version.h"
#include <string.h>

#define TAG "USB_DESC"

// Device Descriptor
const tusb_desc_device_t desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,
  .bDeviceClass       = 0x00,  // Defined in interface descriptor
  .bDeviceSubClass    = 0x00,
  .bDeviceProtocol    = 0x00,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = 0x303A,  // Espressif VID
  .idProduct          = 0x4002,
  .bcdDevice          = 0x0100,
  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,
  .bNumConfigurations = 0x01
};

// ============================================================================
// Composite Configuration (MIDI + CDC)
// ============================================================================
enum {
  ITF_NUM_MIDI = 0,
  ITF_NUM_MIDI_STREAMING,
  ITF_NUM_CDC_COMM,      // CDC Communication interface
  ITF_NUM_CDC_DATA,      // CDC Data interface
  // ITF_NUM_MSC,        // MSC disabled
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN + TUD_CDC_DESC_LEN)

// Endpoint numbers
#define EPNUM_MIDI_OUT   0x01
#define EPNUM_MIDI_IN    0x81

#define EPNUM_CDC_OUT    0x02
#define EPNUM_CDC_IN     0x82
#define EPNUM_CDC_NOTIF  0x83

// #define EPNUM_MSC_OUT    0x04
// #define EPNUM_MSC_IN     0x84

const uint8_t desc_configuration_composite[] = {
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

  // Interface number, string index, EP Out & IN address, EP size
  TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),

  // Interface number, string index, EP notification address and size, EP data address (out, in) and size
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_COMM, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

  // Interface number, string index, EP Out & EP In address, EP size
  // TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

// ============================================================================
// String Descriptors
// ============================================================================
const char* string_desc_arr [] = {
  (const char[]) { 0x09, 0x04 },  // 0: Supported language is English (0x0409)
  "Kabaragoya",                   // 1: Manufacturer
  "Storm Summoner",               // 2: Product
  "STORM001",                     // 3: Serial (should be unique per device)
  "Storm Summoner CDC",           // 4: CDC Interface
  // "Storm Summoner Storage",       // 5: MSC Interface (disabled)
};

const size_t string_desc_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]);

static uint16_t _desc_str[32];

// ============================================================================
// TinyUSB Descriptor Callbacks (override wrapper's implementation)
// ============================================================================

// Invoked when received GET DEVICE DESCRIPTOR
uint8_t const * tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Returns the composite configuration descriptor
uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
  (void) index; // for multiple configurations
  return desc_configuration_composite;
}

// Invoked when received GET STRING DESCRIPTOR request
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;

  uint8_t chr_count;

  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    // Convert ASCII string into UTF-16
    if (!(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0]))) return NULL;

    const char* str;
    if (index == 3) {
      // Serial number: use last 8 chars of MAC-based serial (12 chars total)
      const char* full_serial = version_get_serial();
      size_t len = strlen(full_serial);
      str = (len > 8) ? full_serial + (len - 8) : full_serial;
    } else {
      str = string_desc_arr[index];
    }

    // Cap at max char
    chr_count = strlen(str);
    if (chr_count > 31) chr_count = 31;

    for (uint8_t i = 0; i < chr_count; i++) {
      _desc_str[1+i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);

  return _desc_str;
}

// ============================================================================
// CDC Callbacks
// ============================================================================

// Invoked when received SET_LINE_CODING
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding) {
  (void) itf;
  (void) p_line_coding;
  // We don't need to do anything with line coding for our use case
}

// Invoked when received SET_CONTROL_LINE_STATE
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
  (void) itf;
  (void) dtr;
  (void) rts;
  // We don't need to do anything with line state for our use case
}


