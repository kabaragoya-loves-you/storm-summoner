# Settings Protocol (CONFIG Mode)

This document describes the USB CDC CONFIG mode protocol for managing user-facing device settings. Unlike the low-level SETTINGS mode (which operates directly on NVS key-value pairs), CONFIG mode provides a semantic layer that maps user-friendly setting IDs to component-specific APIs.

## Overview

The CONFIG mode is designed to:

1. **Abstract complexity** - Map simple setting IDs to potentially complex component API calls
2. **Schema-driven** - Work in conjunction with `schemas/settings.schema.json` for UI generation
3. **Type-safe** - All values are uint32_t internally, with type conversion handled by the registry
4. **Consistent** - Provide the same interface regardless of how settings are stored internally

## Protocol Flow

```
Host                        Device
  |                            |
  |-- "CONFIG\n" ------------->|
  |<--- "CONFIG_STARTED\n" ----|
  |                            |
  |-- <command>\n ------------>|
  |<--- <response>\n ----------|
  |                            |
  |-- "EXIT\n" --------------->|
  |<--- "CONFIG_STOPPED\n" ----|
```

## Commands

### VALUES
Get all current setting values as a JSON object.

**Request:** `VALUES\n`

**Response:** Single-line JSON object with setting IDs as keys and integer values.

```json
{"config.scene_mode":0,"config.preset_wrap":1,"midi.passthrough":3,...}
```

### GET
Get a single setting value.

**Request:** `GET <setting_id>\n`

**Response:** 
- Success: `<value>\n` (integer as string)
- Error: `ERROR: <message>\n`

**Example:**
```
GET config.scene_mode
0
```

### SET
Set a single setting value.

**Request:** `SET <setting_id> <value>\n`

**Response:**
- Success: `OK\n`
- Error: `ERROR: <message>\n`

**Example:**
```
SET config.scene_mode 1
OK
```

### COUNT
Get the number of registered settings.

**Request:** `COUNT\n`

**Response:** `<count>\n` (integer)

### FACTORY_RESET
Erase all NVS settings and restart the device.

**Request:** `FACTORY_RESET\n`

**Response:** `OK\n` (followed by device restart)

### EXIT
Exit CONFIG mode and return to IDLE.

**Request:** `EXIT\n`

**Response:** `CONFIG_STOPPED\n`

## Setting ID Format

Setting IDs follow a hierarchical naming convention:

```
<component>.<setting_name>
```

Examples:
- `config.scene_mode`
- `tempo.tap_mode`
- `buttons.debounce`
- `sensor.prox_hysteresis`

## Settings Registry Architecture

The settings registry (`components/settings_registry/`) provides a dispatch table that maps setting IDs to component API calls:

```
┌─────────────────────────────────────────────────────────────────┐
│                     settings_registry.c                         │
│                                                                 │
│  ┌───────────────┐    ┌─────────────────────────────────────┐  │
│  │ Setting ID    │ -> │ Getter/Setter Function Pointers     │  │
│  ├───────────────┤    ├─────────────────────────────────────┤  │
│  │ config.scene  │ -> │ get_scene_mode / set_scene_mode     │  │
│  │ midi.passthru │ -> │ get_midi_passthrough / set_passthru │  │
│  │ buttons.deb   │ -> │ buttons_get_debounce / set_debounce │  │
│  └───────────────┘    └─────────────────────────────────────┘  │
│                                                                 │
│  Wrapper functions handle:                                      │
│  - Type conversions (bool <-> uint32_t)                        │
│  - Bitmask packing/unpacking                                   │
│  - Unit conversions (ms <-> seconds)                           │
│  - Multi-value combinations                                    │
└─────────────────────────────────────────────────────────────────┘
```

## Setting Type Mapping

| Schema Type | Internal Type | Notes |
|-------------|---------------|-------|
| `toggle`    | uint32_t (0/1) | Boolean as integer |
| `select`    | uint32_t | Option value from schema |
| `number`    | uint32_t | Range validated against schema |
| `calibration` | N/A | Read-only indicator, not settable |

## Special Cases

### Bitmask Settings

Some settings combine multiple boolean flags into a single value:

**MIDI Passthrough (`midi.passthrough`):**
- Bit 0: USB → UART passthrough
- Bit 1: UART → USB passthrough

**MIDI Loopback (`midi.loopback`):**
- Bit 0: UART loopback
- Bit 1: USB loopback

### Unit Conversions

**Screensaver Delay (`display.screensaver_delay`):**
- Schema: minutes (0-60)
- API: milliseconds
- 0 = disabled

**Touch Stuck Timeout (`touch.stuck_timeout`):**
- Schema: seconds (0, 5-20)
- API: milliseconds
- 0 = disabled

**Touch Idle Calibration (`touch.idle_calibration`):**
- Schema: minutes (0, 10-60)
- API: milliseconds
- 0 = disabled

### Component-Specific Logic

**Bump Sensitivity (`bump.sensitivity`):**
- Single "level" value (0-10) that maps to threshold/intensity pairs
- Handled by `bump_set_sensitivity_level()` / `bump_get_sensitivity_level()`

**Button Glitch Filter (`buttons.glitch_filter`):**
- Mode value that triggers hardware reconfiguration
- When switching to FLEX mode, uses default window if not set

## Conditional Visibility

The schema supports conditional visibility rules:

```json
{
  "id": "tempo.tap_timeout",
  "visible_when": {"id": "tempo.tap_mode", "value": 1}
}
```

```json
{
  "id": "tempo.clock_standard",
  "visible_when_any": [
    {"id": "tempo.clock_output", "value": 1},
    {"id": "tempo.clock_output", "value": 2},
    {"id": "tempo.clock_output", "value": 3}
  ]
}
```

The UI client (web or CLI) is responsible for evaluating these conditions.

## Error Handling

All commands may return errors in the format:

```
ERROR: <message>
```

Common errors:
- `ERROR: Unknown setting` - Setting ID not found in registry
- `ERROR: Invalid value` - Value out of range or wrong type
- `ERROR: Set failed` - Component API returned an error

## Implementation Files

| File | Purpose |
|------|---------|
| `components/settings_registry/settings_registry.c` | Dispatch table and wrapper functions |
| `components/settings_registry/include/settings_registry.h` | Public API |
| `components/usb_cdc_update/usb_cdc_update.c` | CONFIG mode state machine |
| `schemas/settings.schema.json` | Schema definition |
| `web/js/config.js` | Web UI Stimulus controller |
| `tools/device_settings.rb` | CLI tool |
| `tools/ss_serial.rb` | Shared serial communication base |

## Adding New Settings

1. **Add to schema** (`schemas/settings.schema.json`):
   - Add to appropriate category
   - Define type, options/range, default, visibility conditions

2. **Add to registry** (`settings_registry.c`):
   - Add getter/setter wrapper functions if needed
   - Add entry to `s_settings` array

3. **Test**:
   - Verify via CLI: `ruby tools/device_settings.rb get <id>`
   - Verify via web UI

## Version History

- **1.0.0** - Initial implementation with ~60 settings across 13 categories
