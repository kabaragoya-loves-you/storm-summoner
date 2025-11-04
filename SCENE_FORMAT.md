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
| `touchwheel_mode` | string | "buttons" or "encoder" | "buttons" |

### Touchpads Array

Array of 12 objects (indices 0-11), one per touchpad:

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | boolean | Whether touchpad is active |
| `actions` | array | Array of action objects (max 4) |

### Button Actions

Each button field (`button_left`, `button_right`, `button_both`) contains an array of action objects (max 4).

## Action Types

Actions are defined by a `type` number and optional parameters:

### MIDI Output Actions

| Type | Name | Parameters | Example |
|------|------|------------|---------|
| 4 | Send CC | `cc`, `value` | `{"type": 4, "cc": 74, "value": 127}` |
| 5 | CC Toggle | `cc`, `value`, `value2` | `{"type": 5, "cc": 1, "value": 0, "value2": 127}` |
| 6 | CC Cycle | `cc`, `num_values`, `values[]`, `current_index` | See below |
| 7 | Note On | `note`, `velocity` | `{"type": 7, "note": 60, "velocity": 100}` |
| 8 | Note Off | `note` | `{"type": 8, "note": 60}` |
| 9 | Send PC | `number` | `{"type": 9, "number": 5}` |
| 10 | Randomize CC | `cc` | `{"type": 10, "cc": 74}` |
| 11 | Randomize Multi | `num_ccs`, `cc_numbers[]`, `min_values[]`, `max_values[]` | See below |

### Control Actions

| Type | Name | Description |
|------|------|-------------|
| 0 | Program Next | Increment program |
| 1 | Program Prev | Decrement program |
| 2 | Program Set | `number`: Jump to program |
| 3 | Scene Next | Next scene in manifest |
| 4 | Scene Prev | Previous scene in manifest |
| 5 | Scene Set | `number`: Jump to scene |

### Tempo Actions

| Type | Name | Parameters |
|------|------|------------|
| 12 | Tap Tempo | None |
| 13 | Tempo Nudge Up | `bpm_delta` (optional, default 1) |
| 14 | Tempo Nudge Down | `bpm_delta` (optional, default 1) |

### Transport Actions

| Type | Name | Description |
|------|------|-------------|
| 6 | Transport Play | Start playback |
| 7 | Transport Stop | Stop playback |
| 8 | Transport Pause | Pause playback |
| 9 | Transport Record | Start recording |
| 10 | Transport Toggle | Toggle play/stop |

### System Actions

| Type | Name | Description |
|------|------|-------------|
| 23 | Screensaver Toggle | Toggle screensaver on/off |
| 24 | Confirm Pending | Confirm pending scene/program change |
| 25 | Cancel Pending | Cancel pending change |

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
  "type": 11,
  "num_ccs": 3,
  "cc_numbers": [74, 72, 76],
  "min_values": [0, 0, 0],
  "max_values": [127, 127, 127]
}
```

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

