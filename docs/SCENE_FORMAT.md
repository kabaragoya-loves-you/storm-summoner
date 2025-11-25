# Storm Summoner Scene JSON Format

## Overview

Scenes are stored as JSON files in the `/assets/scenes/` partition. Each scene defines how inputs (touchpads, buttons, sensors) are mapped to MIDI messages and system functions.

## File Structure

### Scene Manifest (`/assets/scenes/manifest.json`)

The manifest maintains an ordered list of all scenes:

```json
{
  "scenes": [
    {
      "index": 0,
      "name": "Scene 1",
      "filename": "scene_001.json"
    },
    {
      "index": 1,
      "name": "Ambient Pad",
      "filename": "scene_002.json"
    }
  ]
}
```

**Fields:**
- `index` (integer, 0-127): Unique scene identifier
- `name` (string, max 32 chars): Display name
- `filename` (string): JSON filename in `/assets/scenes/`

### Scene File (`/assets/scenes/scene_XXX.json`)

Each scene file contains the complete configuration:

```json
{
  "name": "Ambient Pad",
  "program_number": 5,
  "send_pc_on_change": true,
  "touchwheel_mode": "buttons",
  "touchpads": [
    {
      "enabled": true,
      "actions": [
        {
          "type": 4,
          "cc": 74,
          "value": 127
        }
      ]
    }
    // ... 11 more touchpads (0-11)
  ],
  "button_left": [
    {
      "type": 1
    }
  ],
  "button_right": [
    {
      "type": 0
    }
  ],
  "button_both": [
    {
      "type": 24
    }
  ]
}
```

## Scene Properties

### Top Level

| Field | Type | Description | Default |
|-------|------|-------------|---------|
| `name` | string | Scene name (max 32 chars) | "Scene N" |
| `program_number` | integer | Program change value (0-127) | scene index |
| `send_pc_on_change` | boolean | Send PC when entering scene | true |
| `on_load` | array | Actions to execute when scene loads (max 4) | [] |
| `touchwheel_mode` | string | "buttons" or "encoder" | "buttons" |

### Touchpads Array

Array of 12 objects (indices 0-11), one per touchpad:

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | boolean | Whether touchpad is active |
| `actions` | array | Array of action objects (max 4) |

### Button Actions

Each button field (`button_left`, `button_right`, `button_both`) contains an array of action objects (max 4).

### On-Load Actions

The `on_load` field contains an array of actions that execute when the scene is loaded (on boot or when switching). Perfect for initializing pedal state with multiple CC values.

**Example:**
```json
"on_load": [
  {"type": 4, "cc": 74, "value": 80},
  {"type": 4, "cc": 72, "value": 60},
  {"type": 4, "cc": 1, "value": 0}
]
```

This sends 3 CCs when the scene loads, ensuring the pedal starts in a known state.

## Action Types

Actions are defined by a `type` number and optional parameters:

### MIDI Output Actions

| Type | Name | Parameters | Example |
|------|------|------------|---------|
| 15 | Send CC | `cc`, `value` | `{"type": 15, "cc": 74, "value": 127}` |
| 16 | CC Toggle | `cc`, `value`, `value2` | `{"type": 16, "cc": 1, "value": 0, "value2": 127}` |
| 17 | CC Cycle | `cc`, `num_values`, `values[]`, `current_index` | See below |
| 18 | Send 14-bit CC | `msb_cc`, `lsb_cc`, `value` (0-16383) | `{"type": 18, "msb_cc": 1, "lsb_cc": 33, "value": 8192}` |
| 19 | Send NRPN | `parameter` (0-16383), `value` (0-16383) | `{"type": 19, "parameter": 513, "value": 8192}` |
| 20 | Send RPN | `parameter` (0-16383), `value` (0-16383) | `{"type": 20, "parameter": 0, "value": 200}` |
| 21 | Note On | `note`, `velocity` | `{"type": 21, "note": 60, "velocity": 100}` |
| 22 | Note Off | `note` | `{"type": 22, "note": 60}` |
| 23 | Send PC | `number` | `{"type": 23, "number": 5}` |
| 24 | Pitch Bend | `value` (-8192 to +8191) | `{"type": 24, "value": 2048}` |
| 25 | Aftertouch | `pressure` (0-127) | `{"type": 25, "pressure": 80}` |
| 26 | Poly Aftertouch | `note`, `pressure` | `{"type": 26, "note": 60, "pressure": 80}` |
| 27 | Song Select | `number` (0-127) | `{"type": 27, "number": 3}` |
| 28 | Song Position | `position` (0-16383) | `{"type": 28, "position": 256}` |
| 29 | MMC | `command` (0x01-0x7F) | `{"type": 29, "command": 1}` |
| 30 | Randomize CC | `cc` | `{"type": 30, "cc": 74}` |
| 31 | Randomize Multi | `num_ccs`, `cc_numbers[]`, `min_values[]`, `max_values[]` | See below |

### Control Actions

| Type | Name | Parameters | Description |
|------|------|------------|-------------|
| 0 | Program Next | None | Increment program |
| 1 | Program Prev | None | Decrement program |
| 2 | Program Set | `number` (0-127) | Jump to program |
| 3 | Program Bank Set | `number` (0-16383) | Jump to banked preset (auto bank select) |
| 4 | Scene Next | None | Next scene in manifest |
| 5 | Scene Prev | None | Previous scene in manifest |
| 6 | Scene Set | `number` (0-127) | Jump to scene |

### Tempo Actions

| Type | Name | Parameters |
|------|------|------------|
| 7 | Tap Tempo | None |
| 8 | Tempo Nudge Up | `bpm_delta` (optional, default 1) |
| 9 | Tempo Nudge Down | `bpm_delta` (optional, default 1) |

### Transport Actions (not fully implemented)

| Type | Name | Description |
|------|------|-------------|
| 10 | Transport Play | Start playback |
| 11 | Transport Stop | Stop playback |
| 12 | Transport Pause | Pause playback |
| 13 | Transport Record | Start recording |
| 14 | Transport Toggle | Toggle play/stop |

### MIDI System Messages

| Type | Name | Description |
|------|------|-------------|
| 32 | MIDI Start | MIDI Clock Start (0xFA) |
| 33 | MIDI Stop | MIDI Clock Stop (0xFC) |
| 34 | MIDI Continue | MIDI Clock Continue (0xFB) |
| 35 | System Reset | System Reset (0xFF) |
| 36 | Tune Request | Tune Request (0xF6) |

### System Actions

| Type | Name | Description |
|------|------|-------------|
| 37 | Screensaver Toggle | Toggle screensaver on/off |
| 38 | Confirm Pending | Confirm pending scene/program change |
| 39 | Cancel Pending | Cancel pending change |
| 40 | All Notes Off | Send CC123 (All Notes Off) |
| 41 | All Sound Off | Send CC120 (All Sound Off) |

## Complex Action Examples

### CC Cycle

Cycles through multiple CC values on each press:

```json
{
  "type": 6,
  "cc": 1,
  "num_values": 4,
  "values": [0, 64, 99, 127],
  "current_index": 0
}
```

Sends CC1 with values: 0 → 64 → 99 → 127 → 0 → ...

### Multi-CC Randomize

Randomizes multiple CCs simultaneously:

```json
{
  "type": 31,
  "num_ccs": 3,
  "cc_numbers": [74, 72, 76],
  "min_values": [0, 0, 0],
  "max_values": [127, 127, 127]
}
```

### Bank Select for Presets > 127

Jump to preset 300 on a Strymon pedal:

```json
{
  "type": 3,
  "number": 300
}
```

This automatically sends Bank Select (bank 2) + Program Change (44).

### NRPN Example

Many pedals use NRPN for advanced parameters:

```json
{
  "type": 19,
  "parameter": 513,
  "value": 8192
}
```

Sends NRPN parameter 513 with value 8192 (14-bit precision).

### Action Chains

Multiple actions can be assigned to a single input (max 4):

```json
{
  "enabled": true,
  "actions": [
    {
      "type": 11,
      "num_ccs": 3,
      "cc_numbers": [74, 72, 76],
      "min_values": [0, 0, 0],
      "max_values": [127, 127, 127]
    },
    {
      "type": 6,
      "number": 0
    }
  ]
}
```

This randomizes 3 CCs, then starts transport - both on a single button press!

## Scene Modes

Storm Summoner supports three operational modes (configured globally, not per-scene):

### Mode 1: Single Scene
- Only one active scene
- Scene navigation disabled
- Buttons default to program change control

### Mode 2: Preset Sync
- Scenes map 1:1 to pedal presets
- Changing scene sends PC matching scene index
- Buttons default to scene navigation

### Mode 3: Advanced
- Custom PC value per scene
- Full control over PC messages
- Buttons default to scene navigation

## File Location

All scene files must be in:
```
/assets/scenes/
  manifest.json
  scene_001.json
  scene_002.json
  ...
```

Files are accessible when the LittleFS partition is mounted.

## Validation

Use the provided Ruby tools to validate scene files:

```bash
# Validate a scene file
ruby tools/validate_scene.rb scenes/scene_001.json

# Build manifest from scene files
ruby tools/build_scene_manifest.rb scenes/
```

## Best Practices

1. **Keep scene indices sequential** - Gaps make navigation confusing
2. **Limit action chains** - Max 4 actions, most users need 1-2
3. **Use descriptive names** - 32 characters is plenty
4. **Test before deploying** - Invalid JSON will cause scene to fallback to defaults
5. **Version control** - Track scenes in git for collaboration
6. **Backup regularly** - Scenes are your performance configurations

## Notes

- Touchpad 12 is reserved for UI navigation (not configurable)
- All MIDI output uses the global device MIDI channel
- State (like cycle position) is maintained in RAM, resets on reboot
- Dirty scenes auto-save when evicted from cache
- Maximum 128 scenes (limited by uint8_t index)

