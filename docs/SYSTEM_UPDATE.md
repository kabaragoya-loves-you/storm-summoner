# System Update

Coordinated push of shared assets + firmware + partition table over USB CDC.
Required only when a device's flash layout must change (for example the
partition split that introduced the `/userdata` LittleFS volume).

This is intentionally **hidden from the normal web UI**, not deleted. The last
time the feature was purged after a successful field migration, a single
straggler device needed it again and the restore cost was high. Leave the code
in place; reveal the tab when needed.

## Revealing the tab

The System Update nav entry ships with the `hidden` attribute. To show it:

```
https://<your-pages-host>/?systemupdate=1
```

That sets `localStorage['storm-summoner.system_update.visible'] = '1'` so the
tab survives reloads. Persistence matters: the flow spans a device reboot and
reconnect, and a stray page refresh must not hide the tab mid-run.

Clear with `?systemupdate=0`.

Panel HTML, CSS, `web/js/system_update.js`, and the firmware CDC handlers all
remain in the tree regardless of visibility.

## Operator runbook

1. **Publish** the web app (with this tab available behind the flag) and a
   firmware build that includes the System Update CDC commands
   (`PARTITION_TABLE`, `RAW_ASSETS_WRITE`, etc.).
2. **Updater tab first.** Flash the unit onto that firmware with a normal
   FIRMWARE OTA. The System Update protocol only exists on firmware that was
   built with those handlers; older field builds that had the handlers removed
   cannot run the flow.
3. **Reveal System Update** (`?systemupdate=1`), connect, select the
   `system_update` bundle from `releases.json`, and Start.
4. **Do not power-cycle** during the partition-table commit confirmation.
   That is the only ~50 ms window where a power loss can brick the unit.
5. After commit the device reboots. Reconnect when prompted, then Resume /
   Verify so the host can confirm `/userdata` is mounted.

If the unit already reports `userdata.available === true`, pre-flight exits
with `DONE` and does nothing. That early-exit is intentional for converted
devices.

## Bundle and release plumbing

`cmake/promote_release.cmake` emits, on every successful firmware build:

- `web/binaries/partition_table_v2-<hash>.bin`
- `releases.json` entries under `partition_tables` and `system_update`

A `system_update` entry pins one `{firmware, partition_table, shared_assets}`
triple by filename. The web orchestrator refuses a mismatched combination
before driving the multi-step push. `shared_assets` reuses the existing
`assets-<hash>.bin` (same 8 MB LittleFS image the Updater tab uses).

## CDC protocol

Idle-mode commands (device must not be in CONSOLE / ASSETS / etc.):

| Command | Host → device | Device replies |
|---------|---------------|----------------|
| `PARTITION_TABLE <size>` | then binary of `size` bytes (1..4096) | `READY`, then `PT_VERIFIED` or `PT_INVALID:<reason>` |
| `COMMIT_PARTITION_TABLE` | — | `PT_COMMITTED` or `PT_COMMIT_FAILED:<reason>` |
| `ABORT_PARTITION_TABLE` | — | `PT_ABORTED` |
| `RAW_ASSETS_WRITE <offset> <size>` | then binary of `size` bytes (≤ 1 MB) | `READY`, then `RAW_OK <offset> <size>` or `RAW_ERROR:<reason>` |
| `RAW_ASSETS_FINALIZE [checksum]` | optional 8-hex assets checksum | `RAW_FINALIZED` |

Also used by the same flow (existing Updater protocol):

- `FIRMWARE <size>` → `READY` → binary → `TRANSFER_COMPLETE` → `COMMIT` → `SUCCESS`
- `INFO`, `ASSETS` / `DF` / `EXIT` for pre-flight and post-reboot verify

Protocol constants are mirrored in `web/js/updater.js` as
`SYSTEM_UPDATE_COMMANDS`. Firmware handlers live in
`components/usb_cdc_update/usb_cdc_update.c`; staging / erase / commit logic
in `components/firmware_update/firmware_update.c`.

## Host state machine

`web/js/system_update.js` persists session state under
`localStorage['storm-summoner.system_update.state']`:

| State | Meaning |
|-------|---------|
| `IDLE` | No in-flight work |
| `CHECKING_VERSION` | Pre-flight `INFO` + `DF` |
| `UPLOADING_ASSETS` | `RAW_ASSETS_WRITE` chunks (checkpointed per chunk) |
| `UPLOADING_FIRMWARE` | Standard FIRMWARE OTA + `COMMIT` |
| `UPLOADING_PT` | Stage + verify candidate partition table |
| `AWAITING_COMMIT_CONFIRMATION` | Hard confirm dialog open |
| `COMMITTING_PT` | `COMMIT_PARTITION_TABLE` in flight |
| `WAITING_REBOOT` | Waiting for USB drop |
| `VERIFYING` | Post-reboot `DF` check for `/userdata` |
| `DONE` | Finished (or early-exit: already converted) |
| `FAILED` | Error; `failedAt` records which step died |

Steps before `COMMITTING_PT` are idempotent on the device (re-upload
overwrites). `WAITING_REBOOT` and later depend on physical device state.

**Resume:** a `FAILED` session is resumable when `failedAt` is set (or, for
older sessions, when upload progress can be inferred). Start and Resume both
rewind to that step. A failure during PT commit rewinds to `UPLOADING_PT`
because the PSRAM-staged table may be gone.

## Operational notes (hard-won)

### Raw assets writes are destructive and in-place

`RAW_ASSETS_WRITE` erases and writes sectors of the live assets partition
directly. An interrupted upload leaves a half-written LittleFS image. On the
next boot you will see `Corrupted dir pair` / mount failure, missing UI
images, and `userdata unavailable` warnings — the latter because
`assets_manager_init` returns early before attempting the userdata mount.
That is a degraded but recoverable state, not a brick: resume (or re-run)
the assets upload, finalize, and reboot.

### Device quiesce during a session

While FIRMWARE / ASSETS / `PARTITION_TABLE` / `RAW_ASSETS_WRITE` is active,
firmware:

- Suspends the tempo and LFO tasks (`tempo_set_suspended` /
  `lfo_set_suspended`) so flash writes are not fighting the 24 PPQN clock
  grid.
- Suppresses all `EVT:` CDC notify lines (`cdc_may_push_notify` returns
  false) and drops the notify queue.
- Posts `EVENT_UI_ACTION` periodically so the screensaver inactivity timer
  does not fire mid-upload.

Without this, TX-FIFO pressure can truncate a notify mid-line (observed as
`VT:clock:...` on the host). The mangled line no longer matches the `EVT:`
filter, slips through as a protocol reply, and aborts the run.

Quiesce releases on `RAW_ASSETS_FINALIZE`, FIRMWARE/`COMMIT` completion,
`COMMIT_PARTITION_TABLE` / `ABORT_PARTITION_TABLE`, `CANCEL`, and CDC
disconnect.

### Host reads are prefix-filtered

`system_update.js` uses pump-based I/O (`mode=null`, `runSerialTask`,
`sendRaw`, `_readPumpLineBody`) — never `requestMode('UPDATE')` + dedicated
reader, which stalls under Chromium Web Serial.

Waits for `READY` / `RAW_OK` / PT verdicts use `readPumpUntil` with an
explicit wanted-prefix list. Stray log or notify lines are logged and
skipped instead of being treated as refusals. Include failure verdicts
(`RAW_ERROR`, `PT_INVALID`, `PT_COMMIT_FAILED`) in the wanted list so real
errors still surface with a step-specific message.

### Dangerous flash writes stay enabled

`sdkconfig.defaults` sets `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y`.
Partition-table commit writes flash at `0x8000`, which IDF otherwise
refuses. This is a deliberate shipping tradeoff: the validated
PSRAM-buffered commit path in `firmware_update.c` is the only consumer, and
turning the option off would make a future layout migration impossible
without a Tag-Connect reflash. Do not remove it without a replacement plan.

## File index

| Path | Role |
|------|------|
| `web/js/system_update.js` | Host orchestrator |
| `web/index.html` | Tab panel + `?systemupdate=` reveal |
| `web/css/app.css` | Tab / step-list styles; `wa-tab[hidden]` rule |
| `web/js/updater.js` | `SYSTEM_UPDATE_COMMANDS` protocol constants |
| `web/releases.json` | `system_update` / `partition_tables` arrays |
| `cmake/promote_release.cmake` | Emits PT binaries and bundle entries |
| `components/usb_cdc_update/usb_cdc_update.c` | CDC command + binary receive + quiesce |
| `components/firmware_update/firmware_update.c` | PT stage/verify/commit; raw assets erase/write |
| `sdkconfig.defaults` | `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED` |
| `partitions.csv` | Current layout (assets 8 MB + userdata 2 MB) |
