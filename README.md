# Storm Summoner Firmware

Firmware for the **Kabaragoya Storm Summoner** - a touch-based MIDI controller designed for expressive command over effects pedals. For more information about the hardware, visit [kabaragoya.com](https://kabaragoya.com).

## Features

- **8-Zone Capacitive Touch Wheel**: Intuitive control over various parameters for dynamic expression during performances
- **Four Assignable Buttons**: Customize to cycle through values, trigger patterns, or generate random settings
- **Proximity and Ambient Light Sensors**: Assign sensors to control parameters for added responsiveness
- **Expression Pedal Jack**: Traditional foot control integration
- **3.5mm CV/Analog Clock Sync Input**: Seamless sync with modular systems and other gear
- **USB MIDI and ¼" TRS MIDI Output**: Connect to any pedal supporting MIDI, regardless of Type A/B polarity
- **240x240 IPS Display**: Vibrant GC9A01A color display with custom UI
- **Tripod Mounting**: Attach to microphone stands, drum racks, or lighting boards
- **Open-Source Firmware**: Update via drag-and-drop over USB

## Hardware

- **MCU**: ESP32-P4 (custom PCB)
- **Display**: 240x240 GC9A01A RGB888 color IPS
- **Platform**: ESP-IDF v5.5.1
- **UI Framework**: LVGL v9.3 (locally forked)

## Getting Started

### Prerequisites

- ESP-IDF v5.5.1 or compatible
- Python 3.x (for ESP-IDF tools)
- Git

### Building the Firmware

1. **Clone the repository**:
```bash
git clone https://github.com/kabaragoya/storm-summoner.git
cd storm-summoner
```

2. **Set up ESP-IDF environment**:
```bash
. $IDF_PATH/export.sh
```

3. **Build the project**:
```bash
idf.py build
```

The `sdkconfig.defaults` file contains all Storm Summoner-specific configuration, so no manual `menuconfig` setup is required.

4. **Flash firmware and device database**:
```bash
idf.py -p PORT flash
idf.py -p PORT assets-flash monitor
```

Replace `PORT` with your device's serial port (e.g., `COM3` on Windows or `/dev/ttyUSB0` on Linux).

## Project Structure

### Main Components

- **`main/`**: Application entry point and initialization
- **`components/`**: Modular firmware components
  - `touch/`: 8-zone capacitive touch wheel driver
  - `buttons/`: Button input handling
  - `midi/`: MIDI input/output, scene handling, passthrough
  - `display/`: GC9A01A driver and display management
  - `ui/`: LVGL-based user interface
  - `sensor/`: Proximity and ambient light sensors
  - `expression/`: Expression pedal support
  - `cv/`: CV input processing and calibration
  - `clock_sync/`: External clock synchronization
  - `haptic/`: Haptic feedback control
  - `led/`: LED driver and effects
  - `tempo/`: Internal tempo and clock generation
  - `transport/`: MIDI transport controls
  - `device_config/`: Device profile management
  - `assets_manager/`: Device definition loading and caching
  - `firmware_update/`: USB-based firmware update
  - `console_repl/`: Interactive debug console
  - And more...

### Device Profiles

The firmware uses JSON-based device definitions for popular MIDI-enabled effects pedals. These profiles define available MIDI controls, CC mappings, and TRS polarity.

The device database is maintained as a separate project at [kabaragoya-loves-you/midi-devices](https://github.com/kabaragoya-loves-you/midi-devices).

To flash the device database to the Storm Summoner:

```bash
idf.py assets-flash
```

This creates a LittleFS partition image from the `midi-devices/` directory and flashes it to the device.

## Configuration

Project-specific configuration is stored in `sdkconfig.defaults` and applied automatically during the build process. Key settings include:

- **Flash**: 16MB, DIO mode, 80MHz
- **CPU**: 360 MHz with 128KB L2 cache
- **PSRAM**: 80MHz with malloc integration
- **Console**: USB Serial/JTAG
- **TinyUSB**: Kabaragoya Storm Summoner USB descriptors
- **Touch sensor**: IRAM-safe interrupt handling
- **LittleFS**: SPIRAM allocation strategy

To customize settings, run `idf.py menuconfig`. The generated `sdkconfig` file is ignored by git.

## Console Commands

The firmware includes an interactive console (REPL) accessible over USB serial. Connect at 115200 baud to access debugging and configuration commands for all subsystems.

## Contributing

Contributions are welcome! When submitting pull requests:

- Follow the existing code style (2-space indentation, opening braces on same line)
- Verify compatibility with ESP-IDF v5.5.1 and LVGL v9.3
- Test on hardware when possible
- Document any new features or components

## License

This project is licensed under the MIT License. See `LICENSE.md` for details.

## Links

- [Kabaragoya Website](https://kabaragoya.com)
- [MIDI Device Profiles Repository](https://github.com/kabaragoya-loves-you/midi-devices)

---

**Storm Summoner** - Built by musicians, for musicians.
