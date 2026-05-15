# Storm Summoner Scene Files

This directory holds the developer-side scene tooling and the **factory
preset library** that ships in the firmware's read-only assets image.

## Layout

```
scenes/
  factory/                 # Factory presets baked into the assets blob
    default_scene.json     # (sample preset)
    tap_randomize.json     # (sample preset)
    <your_preset>.json
  tools/
    validate_scene.rb
    build_scene_manifest.rb
  README.md
```

## Factory presets (`scenes/factory/`)

Every `*.json` file in this folder is built into the read-only `assets`
LittleFS image at `/assets/scenes/factory/<filename>.json`. On a device's
first boot (i.e. no `/userdata/scenes/manifest.json` present yet), the
firmware copies each one into `/userdata/scenes/` and adds a manifest
entry with `active: false`. The user can then enable/duplicate/edit them
from the on-device Scenes menu or the web app's file browser.

The truly-default scene (Scene 1) is still synthesized in-memory by
`scene_init_defaults()` — factory presets are *examples*, not the default.

### Merging new presets into shipped units

Factory presets are reconciled again whenever the assets blob changes.
On each boot the firmware compares the active assets checksum (NVS
`assets_csum`, set when an assets OTA — classic or system-update RAW —
commits) against the last-merged checksum (NVS `fac_seed_csum`). When
they differ, it runs the same seeding pass and appends any new factory
filenames as inactive entries. Existing entries are left strictly alone:

- A preset already present in `/userdata/scenes/` (whether the user
  activated, edited, or just left it inactive) is **never overwritten**.
- A preset the user has **deleted** is tombstoned in
  `/userdata/scenes/.factory_tombstones.json` and will not be resurrected
  by any future merge, no matter how many times the same filename is
  shipped. To get a tombstoned preset back, restore it manually via the
  web app's file browser.
- A newly-added preset filename is merged in as `active: false` so it
  cannot disrupt navigation; the user decides whether to enable it.

Identity is by **filename**: `scenes/factory/tap_randomize.json` ↔
`/userdata/scenes/tap_randomize.json`. User-created and duplicated
scenes use slug filenames (`scene_<NNN>.json` or `<slug>.json`), so they
can never collide with future factory drops.

### Adding a factory preset

1. Drop a JSON file in `scenes/factory/`. The top-level `name` field is
   what the on-device manifest will display.
2. Validate (optional): `ruby tools/validate_scene.rb factory/my_preset.json`
3. Rebuild the firmware. The new factory preset will be in the next
   `assets` blob: a fresh device sees it on first boot, and existing
   devices pick it up on the first boot after they accept the new
   assets blob.

## User scenes (`/userdata/scenes/` on-device)

Once on the device, scenes are user data — they live on the writable
`userdata` LittleFS partition and survive firmware/asset updates. Use the
web app's file browser or `tools/assets_manager.rb` to upload/download
arbitrary scene files outside the factory mechanism.

## Tools

- `tools/validate_scene.rb <file>` — schema/value sanity check
- `tools/build_scene_manifest.rb <dir>` — generate a manifest.json
  alongside loose scene files (mostly for offline batch testing; the
  on-device manifest is regenerated automatically by the firmware)

## Format

See `../docs/SCENE_FORMAT.md` (or `SCENE_FORMAT.md` at project root) for
the full schema.
