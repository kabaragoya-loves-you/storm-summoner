# USB CDC SIZE + Binary Transfers

This document describes a full-speed USB bulk transfer quirk that affected ASSETS-mode file downloads (manifests, pedal JSON, ZIP archives), the fix shipped in firmware and the web configurator, and optional future TinyUSB hardening.

**If Pedals/Assets transfers work on your build, you do not need ongoing attention** — keep firmware and web app paired when you change this protocol.

See also: [USB_COMPOSITE.md](USB_COMPOSITE.md) (overall USB layout), `components/usb_cdc_update/usb_cdc_update.c` (ASSETS commands).

## Protocol (unchanged on the wire)

Several ASSETS commands return a text line followed by raw bytes:

```
Host→Device:  MANIFEST shared_devices\n   (or MANIFEST user_devices, GET <path>, ZIP <path>, …)
Device→Host:  SIZE <n>\n
Device→Host:  <n bytes of file content>
```

The host must read exactly `n` bytes from the stream before parsing the next line or sending another command. Implementations: `fetchSizedTransfer()` and `readBinary()` in `web/js/app.js`.

## The bug (full-speed bulk termination)

On **USB full speed**, CDC bulk data uses **64-byte** packets (`TUD_CDC_DESCRIPTOR` … `64` in `main/usb_descriptors.c`; PHY `USB_PHY_SPEED_FULL` in `components/tinyusb_init/tinyusb_init.c`).

The host treats a transfer as **complete** when it receives a **short packet** (fewer than 64 bytes) or a zero-length packet (ZLP).

If the payload length is an **exact multiple of 64** (e.g. the 448-byte user devices manifest = 7 × 64), every packet is full. Without a terminating short packet, the host can hold the final packet indefinitely. From JavaScript, `reader.read()` may see **no data** until a timeout; later bytes can appear glued to the **next** command’s response (`Unexpected response: {…JSON…}SIZE 4237`, Assets `ASSETS_STARTED` timeouts with JSON fragments, etc.).

### TinyUSB ZLP mismatch (root cause)

TinyUSB can emit a ZLP when a transfer length is a multiple of `BULK_PACKET_SIZE` (`cdc_device.c`). That macro is `TUD_OPT_HIGH_SPEED ? 512 : 64`.

On ESP32-P4, `TUP_RHPORT_HIGHSPEED` is 1 and `CFG_TUD_MAX_SPEED` was unset in `main/tusb_config.h`, so ZLP logic used **512** while the link actually runs **full speed with 64-byte** endpoints. Payloads like 448 bytes (`448 % 512 ≠ 0`) did not get a ZLP; the host stalled.

**Do not “fix” this by only adding `#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED` without testing.** That made `BULK_PACKET_SIZE` 64 and caused ZLPs **mid-stream** when 512-byte `send_binary` chunks ended on 64-byte boundaries, truncating large files (~93k/94k). The explicit terminator below avoids relying on that path.

## Shipped fix (protocol extension)

When `payload_size % 64 == 0`, the device sends **one extra byte** after the payload (a short packet — currently `'\n'`). The host reads `size + 1` bytes and **discards** the last byte.

| Layer | Behavior |
|-------|----------|
| Firmware | After a complete SIZE+binary send, if `size % 64 == 0`, `send_binary(&term, 1)` |
| Web app | If `size % 64 === 0`, `termLen = 1`, read `total = size + termLen`, only copy first `size` bytes into the buffer |

### Firmware locations

- `handle_assets_send()` in `usb_cdc_update.c` (GET / MANIFEST via ASSETS streaming)
- ZIP archive send in the same file
- Synchronous `MANIFEST` / `LOAD` paths that call `send_binary` in chunks

### Web locations

- `ConnectionManager.fetchSizedTransfer()` — Pedals manifests/GET, Assets GET/ZIP
- `ConnectionManager.readBinary()` — e.g. MIDI tab device JSON fetch

Payloads that are **not** multiples of 64 (e.g. shared manifest 93965 → last packet 13 bytes) are unchanged; no extra byte is sent or read.

## Symptoms (historical)

| Symptom | Typical cause |
|---------|----------------|
| `Incomplete download: 0/448 bytes` | 64-multiple payload stuck (user manifest) |
| `Unexpected response: {…}SIZE N` | Stuck manifest bytes delivered with next GET |
| Pedals empty until Refresh; then garbled errors | Serial stream out of phase |
| `Incomplete download: 93xxx/93965` after bad TinyUSB experiment | Host buffer / mid-stream ZLP corruption — **USB replug** often required |
| `Incomplete download: 93964/93965` or `4010/4237` (almost complete) | Host abandoned in-flight `reader.read()` when a poll timeout won `Promise.race` — bytes were dropped. Fixed by sticky reads (line phase) and blocking reads (binary phase). |

Device logs may show `File send complete: N bytes` while the browser still reports incomplete — the firmware finished sending; the host reader had dropped tail bytes. The web app uses a **size-scaled stall timeout** (idle gap between chunks, not total transfer time) and **resets the deadline when bytes arrive**.

## Field / release notes

- Ship **firmware and web configurator together** for any change to SIZE+binary handling.
- Old web UI + new firmware (or reverse) can break on files whose size is a multiple of 64.
- Third-party tools that read `SIZE` + binary must implement the same `termLen` rule.
- If users see persistent garbled serial after a bad flash or test build: full **USB disconnect/reconnect** (not just closing the browser port).

## Verification checklist

After firmware or web changes touching transfers:

1. Open Pedals — **User Pedals** and full vendor list load without Refresh.
2. Open a shared pedal JSON (e.g. Meris Ottobit Jr.) — details load, no `Unexpected response`.
3. Assets tab — file list loads.
4. Optional: confirm user manifest size in logs (`Sending manifest … (448 bytes)`) — classic 64-multiple case.

## Optional future work (TinyUSB alignment)

Not required for correct behavior while the terminator is in place. If pursued:

1. Set `CFG_TUD_MAX_SPEED` to `OPT_MODE_FULL_SPEED` (and optionally align `CFG_TUSB_RHPORT0_MODE` with full speed).
2. Run the verification checklist above; confirm large files still complete and 448-byte manifests still work.
3. Only remove the manual terminator if end-of-file ZLP is proven correct and **no** mid-stream ZLP truncation occurs.

Until then, treat the **terminator + `termLen` read** as the canonical contract.

## Constants reference

| Item | Value / location |
|------|------------------|
| FS bulk packet size | 64 (`main/usb_descriptors.c`) |
| PHY speed | Full (`tinyusb_init.c`) |
| `send_binary` chunk size | 512 (`usb_cdc_update.c`) |
| Terminator trigger | `size > 0 && size % 64 == 0` |
