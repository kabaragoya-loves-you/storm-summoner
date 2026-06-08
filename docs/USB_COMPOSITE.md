# USB Composite Device - MIDI + CDC + MSC

## Overview

The Storm Summoner now operates as a USB composite device, simultaneously exposing three classes:

1. **MIDI** - For musical instrument control and real-time communication
2. **CDC (Serial)** - For firmware/assets updates and web-based configuration
3. **MSC (Mass Storage)** - For drag-and-drop access to device profiles and scenes

This eliminates the need for mode switching and provides a seamless user experience.

## Architecture

### USB Descriptors

Location: `main/usb_descriptors.c`

Composite descriptor with endpoint allocation:
```
MIDI:  OUT=0x01, IN=0x81
CDC:   OUT=0x02, IN=0x82, NOTIF=0x83  
MSC:   OUT=0x04, IN=0x84
```

### Components

#### usb_manager

Manages the MSC volume and provides LittleFS sync capabilities.

- **Location**: `components/usb_manager/`
- **12MB PSRAM-backed FAT12 volume** for device profiles and scenes
- Placeholder for LittleFS→RAM sync (requires FAT write library)
- File change detection framework

#### usb_cdc_update

Handles CDC-based firmware and assets updates via text protocol.

- **Location**: `components/usb_cdc_update/`
- Text-based protocol over CDC serial
- Supports firmware and assets updates
- Progress reporting
- **SIZE + binary downloads** (ASSETS `MANIFEST` / `GET` / `ZIP`): see [USB_CDC_BINARY_TRANSFER.md](USB_CDC_BINARY_TRANSFER.md) for the full-speed bulk terminator and host read rules

#### assets_manager enhancements

New functions for reload and sync:

- `assets_manager_reload_manifest()` - Re-scan manifest.json
- `assets_manager_reload_device(slug)` - Invalidate cache and reload device
- `assets_manager_sync_to_msc()` - Copy to MSC volume (placeholder)

## CDC Update Protocol

### Commands

**Firmware Update:**
```
Host→Device: FIRMWARE <size_bytes>\n
Device→Host: READY\n
Host→Device: <binary data>
Device→Host: PROGRESS <percent>\n
Device→Host: TRANSFER_COMPLETE\n
Host→Device: COMMIT\n
Device→Host: SUCCESS\n
```

**Assets Update:**
```
Host→Device: ASSETS <size_bytes>\n
Device→Host: READY\n
Host→Device: <binary data>
Device→Host: PROGRESS <percent>\n
Device→Host: TRANSFER_COMPLETE\n
Host→Device: COMMIT\n
Device→Host: SUCCESS\n
```

**Status Query:**
```
Host→Device: STATUS\n
Device→Host: PROGRESS <percent>\n
```

**Cancel:**
```
Host→Device: CANCEL\n
Device→Host: CANCELLED\n
```

## Testing

### Ruby CDC Updater

A Ruby script is provided for testing CDC-based firmware updates:

```bash
ruby tools/cdc_updater.rb /dev/ttyACM0 build/storm-summoner.bin
```

Or on Windows:
```bash
ruby tools/cdc_updater.rb COM3 build/storm-summoner.bin
```

Requires the `serialport` gem:
```bash
gem install serialport
```

### Web-Based Updater (Future)

The CDC serial port can be accessed via the Web Serial API in Chrome/Edge browsers, enabling:
- Web-based firmware updater interface
- Configuration editor (talking to device via CDC)
- No driver installation required

## MSC Volume Structure

```
/
├── devices/
│   ├── manifest.json
│   └── <manufacturer>/
│       └── <device>.json
├── scenes/
│   └── scene_<001-128>.json
└── firmware.bin (optional, for MSC-based update fallback)
```

## File Change Detection

The MSC write callbacks track writes and will (when fully implemented):

1. Detect `.json` files in `/devices/` → sync to LittleFS → reload device
2. Detect `.json` files in `/scenes/` → sync to LittleFS → reload scene
3. Detect `firmware.bin` → trigger OTA update
4. Detect `assets.bin` → trigger assets partition update

**Note:** Full file detection requires FAT filesystem parsing library.

## Update Methods

### 1. CDC Serial (Primary)

Web-based or command-line tool connects to CDC serial port and sends update via protocol.

**Advantages:**
- Fast transfer speeds
- Real-time progress feedback
- Works from web browsers (Web Serial API)
- No driver installation

### 2. MIDI SysEx (Existing)

Firmware/assets transferred via MIDI System Exclusive messages.

**Advantages:**
- Works over standard MIDI connections
- No special drivers needed
- Can update via MIDI cable

**Location**: `components/midi/midi_sysex_update.c`

### 3. MSC Drag-and-Drop (Fallback)

Copy `firmware.bin` to MSC volume to trigger update.

**Advantages:**
- Intuitive user experience
- No special software required
- Works on any OS with USB support

**Note:** Currently requires FAT filesystem implementation for file detection.

## Configuration

### Enable CDC in tusb_config.h

```c
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
```

### Initialize in main.c

```c
usb_manager_init();
tinyusb_init_and_start();
usb_cdc_update_init(false);
```

### Call CDC Task Periodically

The CDC update handler should be polled regularly. This can be added to the main loop or a dedicated task:

```c
while (1) {
  usb_cdc_task();
  vTaskDelay(pdMS_TO_TICKS(10));
}
```

## Known Limitations

1. **FAT Filesystem**: The current MSC implementation uses a minimal FAT12 boot sector. Full FAT write support requires a proper library (e.g., FatFs, littlefs-fuse) to:
   - Create directories and files
   - Parse directory entries for change detection
   - Sync files between LittleFS and RAM volume

2. **MSC Sync**: `usb_manager_sync_from_littlefs()` is currently a placeholder. Implementing this requires:
   - FAT directory creation
   - File-by-file copy from LittleFS to FAT volume
   - Maintaining directory structure

3. **File Change Detection**: MSC write callbacks track LBA writes but need FAT parsing to determine actual file paths.

## Future Enhancements

1. **Web Configurator**: Browser-based tool using Web Serial API to:
   - Update firmware/assets
   - Edit device profiles and scenes
   - Real-time device configuration

2. **Full FAT Implementation**: Integrate a FAT library for complete MSC functionality:
   - User can drag-and-drop device profiles
   - Automatic sync back to LittleFS
   - Real-time file change detection

3. **OTA via HTTP**: Add Wi-Fi support for OTA updates over HTTP/HTTPS.

## Migration Notes

### From Old Architecture

The previous dual-mode system (MIDI-only vs MSC-only) has been completely removed:

- ❌ `usb_mode_manager` → ✅ `usb_manager`
- ❌ `usb_switch_to_midi()` / `usb_switch_to_msc()` → ✅ Always composite
- ❌ Mode switching menu → ✅ USB status info
- ❌ `usb_descriptors_set_midi_mode()` → ✅ Single composite descriptor

### Component Dependencies

Update any component that previously depended on `usb_mode`:

```cmake
# Old
REQUIRES usb_mode

# New
REQUIRES usb_manager
```

## References

- [TinyUSB Composite Device](https://github.com/hathach/tinyusb/tree/master/examples/device/cdc_msc)
- [Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Serial)
- [USB MIDI 1.0 Specification](https://www.usb.org/sites/default/files/midi10.pdf)

