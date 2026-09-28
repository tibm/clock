# Clock BLE debug app — plan

## Context
The clock firmware now has a BLE link (commit 0f15b09, protocol v1). The app in `app/clock/` is still the SwiftData template. Goal: a working debug app that tests the BLE setup end to end:
- scan, connect, pair
- a table of commands
- a shell with autocomplete
- a live status view: snapshot + `info`/FW version

The protocol will change over time, so all protocol numbers come from `app/protocol.json`, bundled as-is (UUIDs, command list, snapshot offsets/types/scales, flags, enums, valid_if, sentinels). Swift code holds only the *rules* from PROTOCOL.md, not the *numbers*.

Decisions (confirmed with the user): iOS + macOS (drop visionOS) · autocomplete = bundled JSON + live `help` parse · drop SwiftData · reference the repo's `protocol.json` directly as a bundle resource (no copy).

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
