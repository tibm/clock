# Clock BLE debug app — plan

## Context
The clock firmware now has a BLE link (commit 0f15b09, protocol v1). The app in `app/clock/` is still the SwiftData template. Goal: a working debug app that tests the BLE setup end to end:
- scan, connect, pair
- a table of commands
- a shell with autocomplete
- a live status view: snapshot + `info`/FW version

The protocol will change over time, so all protocol numbers come from `app/protocol.json`, bundled as-is (UUIDs, command list, snapshot offsets/types/scales, flags, enums, valid_if, sentinels). Swift code holds only the *rules* from PROTOCOL.md, not the *numbers*.

Decisions (confirmed with the user): iOS + macOS (drop visionOS) · autocomplete = bundled JSON + live `help` parse · drop SwiftData · reference the repo's `protocol.json` directly as a bundle resource (no copy).

## Protocol update folded in (2026-09-28, firmware 8a3c2c8)
- `chrono time epoch`, `chrono tz`, `chrono alarm [set|arm]` are now implemented. On every connect the app sends `chrono time epoch <now_ms> <offset_min>`, and sends it again on a timezone/DST change (`NSSystemTimeZoneDidChange`).
- Local time = `epoch_ms + tz_off_min*60000` always. Show the date only when flag `date_valid` is set.
- A command's `args` value is `[min,max]` **or** a text hint → `ArgSpec.range / .hint`.

## Project setup
- Target `clock` (bundle `ch.tallyo.clock`, iOS/macOS 27, synchronized folder group, MainActor default isolation).
- Remove visionOS from `SUPPORTED_PLATFORMS` → `iphoneos iphonesimulator macosx`.
- Delete `Item.swift`. Strip SwiftData from `clockApp.swift`/`ContentView.swift`.
- Add `../protocol.json` (sits next to `clock/`, outside the synced folder) to the target's Copy Bundle Resources as a reference. Do not add PROTOCOL.md.
- Info.plist keys: `NSBluetoothAlwaysUsageDescription` (AddInfoPlist). macOS: App Sandbox → Bluetooth entitlement (AddEntitlement).
- Add a unit-test target `clockTests` (Swift Testing) for the decoder and the framing.

## Files (all in `app/clock/clock/`)

### Protocol layer (`Protocol/`) — pure, UI-free, testable
- **`ProtocolSpec.swift`**: `Decodable` model of protocol.json (`gatt`, `advertising`, `command_channel`, `commands`, `snapshot{fields, flags, enums, golden}`). `ProtocolSpec.bundled` loads it from `Bundle.main`. Unknown JSON keys are ignored, so the file can grow.
- **`SnapshotDecoder.swift`**: generic decoder driven by `spec.snapshot.fields`.
  - Types `u8/i8/u16/i16/u32/i32/i64/f32/u8[N]`, read little-endian at `off`. `scale` gives Double.
  - Returns `Snapshot { fields: [DecodedField], flagsSet: Set<String>, raw: Data }`. A `DecodedField` holds name, raw value, display string, unit, and an `isValid` computed from `valid_if` against the flags. Sentinel labels come from `sentinel`, enum names from `enums` (`unknown(n)` fallback).
  - Rules from §5: reject `schema != spec.snapshot_schema` or `size`/length < spec size; ignore extra bytes; ignore unknown flag bits (show them as `bit N`).
  - Convenience typed accessors (`localTime`, `vbat`, `flags`) look fields up by name, so the dashboard never hard-codes offsets. Missing name → nil → "—".
  - `localTime`: `epoch_ms + (tz_set ? tz_off_min*60000 : 0)`, formatted as UTC. hh:mm:ss only when `tz_set` is clear (v1).
- **`ResponseFramer.swift`**: parses `rsp` frames (`<id>` + one of `| + = $`) and reassembles `+` fragments per id. Emits `ResponseEvent.line(id, text) / .pair(id, key, value) / .terminal(id, Status)`. `Status` is a string-backed enum built from `spec.command_channel.statuses`, plus `.unknown(String)`.
- **`InfoParser.swift`**: `key=value` splitter → `[String:String]`, plus `proto` compared with `spec.protocol_version` → `.ok / .appTooOld / .firmwareOlder`.
- **`CommandCatalog.swift`**: `CommandEntry { words: [String], argsTemplate, help, status (implemented/planned/device), unsafe, argRanges, source (spec|device) }`.
  - Seeded from `spec.commands`: split each `line` into literal words and `<arg>`/`[<arg>]` placeholders.
  - `mergeHelp(lines:)` is the best-effort parser for `help <group>` rows. Tokens up to the first `<`/`[`/`--` are words; placeholder-looking tokens are args; the rest is help text; a trailing `[unsafe]` sets the flag. Rows that fail to parse are skipped, and the JSON entries always stay.
  - `completions(for input:)` gives prefix matches, one token at a time: next literal words, then an arg hint (e.g. `<pct 0–100>`).

### BLE layer (`BLE/`)
- **`ClockBLE.swift`** — `@Observable @MainActor final class`, owns `CBCentralManager`. UUIDs come from the spec.
  - **Scan**: `scanForPeripherals(withServices:[service])` with duplicates allowed while the scanner is on screen. Each discovered clock shows name, RSSI, `pairingOpen` (manufacturer data: company id from spec, then the state byte and its bit 0), `identifier`.
  - **Connect** → discover service/chars → `setNotifyValue(true, rsp)` first (this triggers iOS pairing) → then subscribe `status`, read `status`, read `info`. Request MTU is automatic on iOS; record `maximumWriteValueLength(.withResponse)`.
  - **Connection state enum**: `idle, scanning, connecting, pairing, ready, disconnected(reason)`. Map the failure modes from PROTOCOL.md §2 to user hints: disconnect right after encryption → "hold the knob 10 s…"; `peerRemovedPairingInformation` → "Forget This Device"; insufficient auth → waiting for pairing.
  - **Command queue**: one request in flight. Ids go up from 1 and wrap at 65535 (skip 0). Write `"<id> <line>"` with response, reject lines longer than min(256, maxWriteLen), 10 s timeout from the spec. `busy` → retry once after 1 s. `send(_ line) async -> CommandResult { lines, pairs, status }` via `CheckedContinuation`. Frames also go to a transcript stream for the shell. On disconnect, pending requests resolve as `.linkLost` ("outcome unknown"), and `sys reboot` counts as expected.
  - Reconnect: remember the last identifier in UserDefaults, `retrievePeripherals(withIdentifiers:)` on launch. Disconnect when the scene goes to background (`scenePhase`), per §7.
  - Delegates are `nonisolated` and hop to MainActor. Uses async/await, no Combine.

### UI (`Views/`) — plain SwiftUI, `TabView` with 3 tabs + a connection sheet
1. **`ConnectView`**: scanner list (name, RSSI, green "ready to pair" badge), connect/disconnect, state banner + the pairing hint text. Shown as a sheet whenever there is no link.
2. **`StatusView`** (dashboard):
   - Header: fw / sha / built / board / proto from `info`, a proto-mismatch warning, seq + gap counter, last-update age.
   - Sections: Time, Power, Room, Light, IMU, Hands, UI, LEDs (7 color swatches from `pixels`), Radio. Each row reads a field by name; invalid values show "—".
   - A flag chip grid over every flag in the spec (set = highlighted).
   - "All fields" disclosure: a generic list of every decoded field, so new fields show up without any UI change.
   - Period picker (1 s / 5 s / 60 s → `net ble period`).
3. **`CommandsView`**: table (`List` grouped by first word) of the catalog entries, showing line, use/help, badges (`planned`, `unsafe`, `device-only`), and arg ranges.
   - Tap a row → an arg form (a stepper/field per placeholder, range-clamped from `args`) → send → inline result (status + lines).
   - Quick-action buttons for the common ones: `sys ver`, `audio tone`, `audio stop`, set time from the phone (`chrono time set HH:MM:SS`), and `unsafe on`.
   - Unsafe commands ask for confirmation and offer to send `unsafe on` first.
   - Planned commands can be sent; `bad-arg` shows as "not in this firmware".
4. **`ShellView`**: monospaced scrolling transcript. `> line` in accent, `|` lines plain, `=` pairs dimmed, `$status` colored by status. Text field with:
   - a completion chip bar above it (tap to insert), and Tab on macOS/hardware keyboards
   - ↑/↓ history (last 100, UserDefaults)
   - clear and copy-all buttons
   - Also a toggle that shows raw frames (debug the framing).

`clockApp.swift`: builds `ProtocolSpec.bundled` and `ClockBLE(spec:)`, puts them in `.environment`. On connect it runs `help`, then `help <group>` for each group from the `groups` line, feeding `CommandCatalog.mergeHelp`.

## Reuse / references
- `firmware/tools/clockctl.py` — reference client (framing in `Session.run`, decode, `pairable()`); mirror its behaviour.
- `firmware/components/cli/src/registry.cpp` `help()` — the exact `help <group>` row format (`group [object ]verb args`, padded to column 34, `   [unsafe]` suffix). Parser handles the 1-space pad case via the placeholder heuristic.

## Verification
1. **Unit tests (`clockTests`, Swift Testing)**:
   - Decode `spec.snapshot.golden.hex` and assert every key in `golden.decoded`. Scaled fields within 1e-3; check `flags_set` and pixels. Note that `opto_raw` in the golden data is the unscaled value.
   - Truncated/wrong-schema input rejects; extra trailing bytes are ignored.
   - Framer: the §3 `help sys` fragment example; interleaved `=` and `$`; unknown status.
   - Catalog: parse the JSON lines plus sample `help sys` rows from registry.cpp; completions for `au` → `audio`, `audio v` → `vol`, `audio vol ` → `<pct 0–100>`.
   - Info parser + proto comparison.
2. `BuildProject` for iOS and for My Mac. `XcodeRefreshCodeIssuesInFile` while editing.
3. `RenderPreview` of Status/Commands/Shell with a mock snapshot (golden vector) and mock transcript.
4. On hardware (by the user; needs a real device, not the simulator): open the pairing window (knob 10 s), pair, see `info` + live status ticking with seq, run `sys ver`, `audio tone`, `help` in the shell, and try `motion home` without `unsafe` → `denied`.

---

## Next: history — capture the clock's log and plot it (added 2026-09-28)

The firmware now records the room (temperature, humidity, pressure, gas), light, battery and
Wi-Fi to the microSD card on its own, every **5 min** by default, and keeps **2 years**. The
contract is `PROTOCOL.md` §4 "History" + `protocol.json` → `history` (record layout, flags,
event codes, encodings, **golden vectors**) and the new `bulk` characteristic. This section is
how the app captures it; nothing here needs a firmware change.

### Model: the phone is the archive
- The clock keeps at most `keep` days and deletes older ones. **The app never deletes a day
  because the clock did** — after the first sync the phone holds the full history.
- Mirror the clock's files **byte for byte**, one file per clock per UTC day:
  `Application Support/History/<clock CBPeripheral.identifier>/<yyyymmdd>.bin`. The raw file is
  the source of truth (re-decodable when the decoder learns new fields); anything derived
  (a SwiftData/SQLite index, hourly aggregates) is a cache that can be rebuilt from it.
- Exclude nothing from backup by default: two years is ~5 MB per clock at the default period.

### Protocol layer (`Protocol/`, pure, `nonisolated`)
- **`HistoryRecord.swift`** — decode a file: header (magic `CLKL`, version 1, record 24), then
  24-byte records; CRC-8/SMBUS on bytes 0–22 (skip failures, keep going); kind 1 = sample,
  2 = event, anything else skipped; trailing partial record ignored. Offsets, flag names,
  event codes and the three log encodings come from `spec.history` (add a `History` block to
  `ProtocolSpec`, optional like `soundFiles`). Output: `[HistorySample]` (Date + optional
  values, nil when the validity flag is clear) and `[HistoryEvent]` (Date, code name, args).
- **`HistorySync.swift`** — the pure diff from PROTOCOL.md "Downloading": given
  `log days` pairs (`=day=<yyyymmdd>/<bytes>`) and the local sizes, return the fetch plan
  `[(day, from)]`: new day → 0, longer on the clock → local size, shorter on the clock → 0 and
  replace; days only on the phone are kept. Oldest first, today last.
- **CRC-32** already exists for uploads (`zlib.crc32`); reuse it to check `=crc=`.

### BLE layer (`BLE/`)
- `ClockLink`: subscribe to `bulk` together with `rsp` when the characteristic exists
  (`hasBulk`, like `hasBlob`). Route `bulk` notifications to a handler instead of the framer:
  `offset = UInt32(le: bytes 0..<4)`, payload after it.
- **`HistoryStore`** (`@Observable`, MainActor), like `ToneStore`:
  1. `send("log days")` → sync plan.
  2. For each entry: `send("log fetch <day> <from>")` → read `=size=`, `=from=`, `=crc=`.
     Collect `bulk` packets into a buffer at `offset - from` until `size - from` bytes arrived
     (timeout: nothing new for 5 s → give up this day, keep what is on disk, retry next sync).
  3. Check CRC-32 of the received bytes, then append to (or, from 0, replace) the local file
     atomically (write to a temp file + `replaceItemAt`). A bad CRC → discard and retry once.
  4. Progress: bytes done / total for the whole plan; cancellable (`log fetch stop`).
  - Don't send other commands during a fetch (the shell/Commands tabs should show "syncing").
  - Run a sync on every connect (after time + zone), and from a "Sync now" button. On iOS a
    backgrounded app loses the link (§7): a sync interrupted there simply resumes next time,
    because every step is idempotent and resumable by offset.

### UI (`Views/`) — a "History" tab (Swift Charts)
- One chart per quantity: temperature (°C), humidity (%), pressure (hPa), gas (Ω, log scale),
  light (lx, log scale, mean line + peak points), battery (mV / %). Range picker: 24 h · 7 d ·
  30 d · 1 y · all. Break the line where two points are more than 2 × period apart (PROTOCOL.md:
  plot by `t`, not by position).
- **Downsample for long ranges** before handing points to Charts (≤ ~1000 points per series):
  bucket by hour (≥ 7 d) or by day (≥ 90 d) with min/mean/max bands. Compute from the decoded
  samples; cache per day.
- Events as vertical rule marks with an icon (alarm, boot, Wi-Fi), tappable for details.
- Local time for display (`TimeZone.current`); the data are UTC.
- Settings sheet: `log status` pairs → period / keep / cap pickers sending `log period|keep|cap`;
  show the `|` line when the clock answers `denied` (the budget numbers). Show `used`, `days`,
  `projected`, and the last sync time.
- Export: share the raw `.bin` files and a CSV (`time_utc,temp_c,rh_pct,…`) generated on the
  phone.

### Tests (`clockTests`)
- Decode `spec.history.golden.sample_hex` / `event_hex` / `header_hex` and assert every key in
  the matching `*_decoded` (scaled/log fields within 0.1 %); a flipped bit fails the CRC and is
  skipped; a torn tail is ignored.
- `HistorySync` plans: new / grown / shrunk / clock-deleted days.
- The fetch loop against a fake link feeding out-of-order-free packets with offsets, including a
  drop mid-day and a resume from the stored length.

### On hardware
`log period 10` on the bench makes records every 10 s; `log tail` on the console shows what is
being recorded, `log days` / `log status` what is on the card. `firmware/tools/clockctl.py` can
be extended the same way as the app for a desktop check.
