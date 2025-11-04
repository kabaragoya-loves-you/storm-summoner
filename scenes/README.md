# Storm Summoner Scene Files

This directory contains scene configuration files that are packaged into the device's LittleFS partition.

## Quick Start

### Validate Scenes
```bash
cd scenes
ruby tools/validate_scene.rb scene_001.json
ruby tools/validate_scene.rb *.json  # Validate all
```

### Build Manifest
```bash
ruby tools/build_scene_manifest.rb .
```

This generates `manifest.json` with an ordered list of all scenes.

### Flash to Device
```bash
cd ..
idf.py assets-flash
```

## File Structure

```
scenes/
  ├── manifest.json          # Auto-generated scene index
  ├── scene_001.json         # Default scene
  ├── scene_002.json         # Example advanced scene
  ├── tools/
  │   ├── validate_scene.rb  # Validation script
  │   └── build_scene_manifest.rb  # Manifest generator
  └── README.md              # This file
```

## Creating New Scenes

1. Copy an existing scene file
2. Rename with next number (`scene_003.json`)
3. Edit the JSON (see SCENE_FORMAT.md in project root)
4. Validate: `ruby tools/validate_scene.rb scene_003.json`
5. Rebuild manifest: `ruby tools/build_scene_manifest.rb .`
6. Flash: `idf.py assets-flash`

## Scene Numbering

- Files must be named `scene_XXX.json` (001-128)
- The number in the filename determines the scene index (001 = index 0)
- Gaps in numbering are allowed but discouraged
- The manifest defines the display order

## Action Types Quick Reference

| Type | Action | Example |
|------|--------|---------|
| 0 | Program Next | `{"type": 0}` |
| 1 | Program Prev | `{"type": 1}` |
| 4 | Send CC | `{"type": 4, "cc": 74, "value": 127}` |
| 5 | CC Toggle | `{"type": 5, "cc": 1, "value": 0, "value2": 127}` |
| 6 | CC Cycle | `{"type": 6, "cc": 1, "num_values": 3, "values": [0, 64, 127], "current_index": 0}` |
| 7 | Note On | `{"type": 7, "note": 60, "velocity": 100}` |
| 10 | Randomize CC | `{"type": 10, "cc": 74}` |
| 11 | Randomize Multi | `{"type": 11, "num_ccs": 3, "cc_numbers": [74,72,76], "min_values": [0,0,0], "max_values": [127,127,127]}` |
| 12 | Tap Tempo | `{"type": 12}` |

See `../../SCENE_FORMAT.md` for complete documentation.

## Tips

- Use the REPL console (`cd assign`) to prototype scenes on the device
- Export working configurations by reading from RAM
- Version control your scenes - they're your performance setlists!
- Keep action chains short (1-2 actions is typical)
- Comments in JSON are ignored by the parser (but helpful for you!)

## Requirements

- Ruby 2.7+ (standard library only, no gems required)

