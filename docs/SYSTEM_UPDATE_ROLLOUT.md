# System Update Rollout

How to push the partition-split firmware (v(N+2)) to a unit running the
pre-split firmware (v(N)).

## What changed and why

Before v(N+2), every customization (scenes, the user `default.json` pedal,
device cache) lived on the same `assets` LittleFS partition that gets
wholesale-replaced on every Assets OTA. So every device-database update
wiped user content.

v(N+2) splits the 10 MB `assets` partition into:

- `/assets` — 8 MB, **read-only**, holds the shared MIDI device DB and UI
  images. Replaced wholesale by the Assets OTA / System Update flow.
- `/userdata` — 2 MB, **read-write**, holds scenes, user-created and
  cloned pedals, and the device cache. Never touched by an OTA.

The split requires a new partition table at flash offset `0x8000`, which
v(N) firmware can't write. v(N+2) firmware contains a CDC command
(`COMMIT_PARTITION_TABLE`) that does write the partition table directly to
flash.

## Migration policy

**No migration of existing user data.** Everyone's existing scenes and
user pedals are gone after the System Update. This is intentional — it's a
small dev userbase, and a sophisticated migration was not worth the
complexity. Re-create scenes from scratch on the new layout.

## Resilience guarantee

The only step that can brick a device is the partition-table commit
itself: a ~50 ms flash write at `0x8000`. Every other interruption point
in the System Update flow lands the device in either:

- **v(N) running normally** — System Update can be retried from the start.
- **v(N+2) booting in resilience mode** — `/userdata` partition not yet
  present, scenes synthesized in-memory, pedal list shows shared devices
  only, but the device is fully usable. The user re-runs System Update to
  recover.

The hard confirmation dialog before the partition-table commit exists to
warn the user not to power off the device during that window.

## End-user rollout (single-jump path)

This is the path each dev user follows on their unit. There is no v(N+1)
intermediate release; v(N) goes straight to v(N+2).

### Pre-flight

1. Make sure the device is plugged in via USB and the cable is reliable.
2. Open the web app, click **Connect**, grant the serial port.
3. The **Info** tab should show the device's current firmware version.

### Step 1 — Standard firmware update to v(N+2)

1. Open the **Updater** tab.
2. Pick `storm-summoner-<v(N+2)>.bin` from the firmware dropdown.
3. Click **Apply**.
4. Wait for the upload + commit + reboot. The device will return as v(N+2).

After this step, the device is in **resilience mode**:
- `/userdata` does not exist yet (the partition table is still v(N)'s).
- Scenes show a single in-memory "Scene 1".
- Pedal list shows shared devices only (the embedded user `default.json` is
  not loaded).
- A boot-time `ESP_LOGE` says `userdata partition missing — re-run system
  update from web app`.

The device is fully usable in this state, but you should not stop here.

### Step 2 — Coordinated System Update

1. Open the new **System Update** tab.
2. Pick the bundle matching v(N+2) from the dropdown. The bundle pins one
   specific firmware + partition_table + shared_assets triple by hash, so
   you cannot accidentally mix versions.
3. The bundle info panel shows the three artifact filenames.
4. Click **Start**.

The orchestrator runs these steps automatically and shows progress for each:

| Step | What it does | If interrupted |
|---|---|---|
| Pre-flight | Sends `DF`, confirms `/userdata` not yet available. | Safe — just reconnect and click Start again. |
| Upload shared assets | Streams the new `shared_assets-<hash>.bin` to the existing 10 MB `assets` partition via `RAW_ASSETS_WRITE` chunks. | Safe — chunk offset is checkpointed in localStorage. Reconnect, click Resume. |
| Upload firmware | Standard `FIRMWARE` OTA + `COMMIT`. Boot partition switches to v(N+2) (already there from Step 1, but this re-flashes it). | Safe — boot partition only switches on `COMMIT`. Up to that point, Step 1's v(N+2) is still the boot target. |
| Upload partition table | `PARTITION_TABLE` upload, candidate staged in PSRAM, device replies `PT_VERIFIED`. | Safe — never writes flash. PSRAM contents lost on reboot. |

5. The **hard confirmation dialog** appears.
6. Read the warning. The text reads "This is the only step that can brick
   the device. The next ~50 ms write to flash 0x8000 cannot be safely
   interrupted. Keep the device powered and the USB cable connected until
   you see 'v(N+2) layout confirmed'."
7. Type `COMMIT` into the input field.
8. Click **Commit Partition Table**.

The device writes the new partition table to flash `0x8000`, replies
`PT_COMMITTED`, and reboots.

### Step 3 — Reconnect and verify

1. The web-serial port drops when the device reboots. The orchestrator
   logs "Reconnect the device when ready, then press Resume."
2. Click **Connect** in the top bar, grant the port again.
3. Click **Resume** in the System Update tab.
4. Verification runs: `DF` returns the new shape, `userdata.available` is
   true, `assets.total` is approximately 8 MB.
5. State changes to **DONE**. Update is complete.

The device now has:
- `/assets` mounted RO at 8 MB with the new shared content.
- `/userdata` mounted RW at 2 MB, freshly auto-formatted, with seeded
  empty directories for `scenes/`, `devices/`, `devices/user/`, and
  `cache/`.
- The default user pedal at `/userdata/devices/user/default.json`.
- A single in-memory default scene (no scenes have been saved yet).

## Recovery from interruption

The System Update tab session is checkpointed in `localStorage`, keyed by
the bundle's three artifact filenames. After any interruption:

1. Reconnect the device.
2. Open the System Update tab.
3. The bundle dropdown auto-selects the in-flight bundle.
4. Click **Resume**.

The orchestrator picks up at the last completed step. Cancellation
clears the localStorage entry and aborts any staged partition table on
the device side.

If the device gets stuck in resilience mode (boot logs say `userdata
partition missing` and scenes don't persist), the user just re-runs the
System Update flow from Step 1 onward. The orchestrator's pre-flight
correctly handles a unit that's already on v(N+2) firmware but has the old
partition table.

## What the dev user can recover after System Update

Nothing automatically. Cut the cord.

- **Scenes**: must be re-created on the device or pushed via the web
  Assets tab to `/userdata/scenes/<scene_001.json>` etc. The firmware
  regenerates `/userdata/scenes/manifest.json` on file create/delete.
- **User pedals**: re-clone or re-create through the (future) clone UI;
  for now, push JSON files to `/userdata/devices/user/<name>.json` via
  the web Assets tab. The firmware regenerates
  `/userdata/devices/manifest.json` on file create/delete.
- **Settings (NVS)**: untouched by the partition split. NVS lives in its
  own partition.

If a user wants to back up their existing scenes and user pedals **before**
running System Update, they can do so via the existing web Assets tab
(legacy single-partition view, paths look like `/scenes/scene_001.json`).
After the update, re-upload them to the new `/userdata/...` paths.

## What the maintainer ships

Each v(N+2) build produces three artifacts in `web/binaries/`:

- `storm-summoner-<v(N+2)>.bin` — the firmware
- `partition_table_v2-<hash>.bin` — the new partition table (hash derived
  from `partitions.csv`)
- `shared_assets-<hash>.bin` — the LittleFS image holding only the shared
  RO content (hash derived from `midi-devices/manifest.json`)

`web/releases.json` gains a `system_update` entry that pins the three by
filename. The web app's System Update tab reads this array and refuses
to push a mismatched combination.

## Failure modes

| Symptom | Cause | Fix |
|---|---|---|
| `DF response not JSON` in Step 1 pre-flight | Device is still on v(N), missing the new commands | Run the standard FIRMWARE OTA first to get to v(N+2) |
| `RAW_ASSETS_WRITE refused at offset N` | Device-side allocator failure (PSRAM exhausted, etc.) | Reboot device, re-run from System Update tab |
| `PT_INVALID: <reason>` | Candidate partition table failed verification (bad MD5, overlapping partitions, partition past flash end) | Bug in the build — do not ship that bundle. Verify `partitions.csv` and rebuild. |
| `PT_COMMIT_FAILED: <reason>` | Flash write failed but device is still alive | Device should still be running v(N+2) with v(N) PT (resilience mode). Re-run System Update. |
| Device unresponsive after Step 4 confirmation | Power loss during the ~50 ms flash write | Bricked. Recoverable only via JTAG/pogo-pin reflash. This is the irreducible risk; surface it in user comms. |
| Web-serial reconnect fails repeatedly after reboot | Browser cached an old port handle | Close the tab, re-open, reconnect from scratch. The localStorage session resumes. |
