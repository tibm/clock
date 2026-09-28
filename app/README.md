# Clock — iOS / macOS debug app

A SwiftUI app that talks to the wooden clock over Bluetooth LE. For now it is a **bring-up and
debug tool**: connect, pair, watch the live status snapshot, and drive the clock's CLI from a
command table or a shell with autocomplete. It is the phone side of the contract in
[`PROTOCOL.md`](PROTOCOL.md) + [`protocol.json`](protocol.json).

| | |
|---|---|
| Platforms | iOS 27, macOS 27 (same target; a Mac build is handy at the bench) |
| Language | Swift, SwiftUI, CoreBluetooth, Observation, async/await (no Combine) |
| Tests | Swift Testing (`clockTests`), hosted in the app |
| Protocol | v1 / snapshot schema 1 — read from the bundled `protocol.json` at runtime |

## Build & run

1. Open `clock.xcodeproj` in Xcode 27.
2. Pick **a real iPhone or My Mac**. The Simulator has no Bluetooth: the app runs there, but the
   Clock tab shows "Bluetooth LE not available". The UI previews use a fake link.
3. Run. Allow Bluetooth when asked.
4. **Pair (first time only):** hold the knob on the clock for 10 s until the five LEDs breathe
   blue. The clock then shows **ready to pair** in the Clock tab. Tap it, then accept the iOS
   pairing prompt.
5. Tests: ⌘U (or `xcodebuild test -scheme clock -destination 'platform=macOS'`).

## Screens

| Tab | What it does |
|---|---|
| **Clock** | Scans for clocks, with a "ready to pair" badge from the advertising data. Connects and disconnects, and reconnects to the last clock. Explains pairing failures (window shut → hold the knob; clock forgot the phone → *Forget This Device*). Shows `info` (fw, sha, board, proto) with a protocol-version check, and has the "send phone time on connect" toggle. |
| **Status** | The live `status` snapshot: sequence number, samples missed, age, clock time vs phone time, every flag as a chip, the 7 LEDs as colour swatches, and grouped values (power, room, light, IMU, hands, UI, radio). Invalid values show "—". Fields the layout doesn't list appear under **Other fields**. There is also a raw hex view and a notify-period menu. |
| **Sounds** | The clock's microSD `/sd/tones` (`storage tones`): size, length, playability state, which one is the alarm; swipe to delete, context menu to play / stop / use it for the alarm. Below it, the WAVs **bundled with the app** (`clock/Tones/`), each checked against `sound_files.format`, with an upload button and a progress bar (`storage put` + `blob` writes, busy retry, resume after a dropped link). Hidden when the clock has no card or no `blob` characteristic. |
| **Commands** | Every known command, grouped, with its help, arg ranges and badges (`unsafe`, `planned`, `device` = only the firmware's `help` knows it). Tap one to fill its arguments (pickers for `on\|off` choices, range warnings) and send it. Quick actions: sync time, `sys ver`, tone, stop, `unsafe on`, close pairing. |
| **Shell** | A terminal over BLE, the same commands as the USB console. Completion chips for the next word or argument. Tab / ↑ / ↓ on a hardware keyboard, or the chevrons. History is persisted. Output is coloured by record kind (`>` sent, `$` status, `=` pairs, `#` app notes), with an optional raw-frame view. |

On connect the app automatically:
1. subscribes to `rsp` (this is what triggers iOS pairing), then to `status`, and reads `info`
2. sends `chrono time epoch <now_ms> <utc_offset_min>` (PROTOCOL.md "Keeping time"), and sends it again whenever the phone's timezone changes
3. runs `help`, then `help <group>` for each group, and merges the result into the command list

On iOS it disconnects when backgrounded and reconnects when it comes back (PROTOCOL.md §7).

## Alarm sounds

Drop `.wav` files into **`app/clock/Tones/`** and rebuild — the folder is part of the synchronized
`clock` group, so every file in it is copied into the app bundle, no project edit needed. They
appear under **Sounds → In the app** and can be uploaded to the clock. The clock plays only
**WAV PCM, 48 kHz, mono, 16-bit**, max 16 MB; other files are listed with the reason and can't be
uploaded. Convert with `ffmpeg -i in -ac 1 -ar 48000 -c:a pcm_s16le -bitexact out.wav`. The file
name is the name on the card (ending `.wav`, 5–63 bytes, no leading `.`, none of `/ \ : * ? " < > |`).

An upload runs at roughly 5–10 KB/s. Keep the app in the foreground (iOS disconnects in the
background); after a dropped link, tap upload again and it resumes where the clock left off.

## Designed to follow the protocol

All protocol numbers come from `protocol.json`, which is bundled **by reference**: the file in
this folder, not a copy. Editing the contract and rebuilding is enough.

| From `protocol.json` | Used for |
|---|---|
| `gatt.*` UUIDs | service / characteristic discovery |
| `advertising.manufacturer_data` | the "ready to pair" bit |
| `command_channel` | id range, max request bytes, timeout, statuses |
| `commands[]` | the command table + autocomplete (with `args` ranges / hints) |
| `snapshot.fields` | the decoder: offset, type, scale, unit, `valid_if`, `enum`, `sentinel` |
| `snapshot.flags` / `enums` | flag chips, enum labels (`unknown(n)` for newer values) |
| `snapshot.golden` | the decoder's unit test |
| `sound_files` | the WAV format check, upload size limit, `blob` ATT error names |

Compatible protocol changes (PROTOCOL.md "Change process") need **no Swift change**:
- **New command:** appears in the table and in autocomplete.
- **New snapshot field at the end:** shows up in *Other fields*.
- **New flag bit:** gets a chip.
- **New enum value:** shows as its label.
- **Longer snapshot:** the extra bytes are ignored until the JSON describes them.

The Swift code holds only the *rules* of `PROTOCOL.md`: framing, one request at a time, busy
retry, pairing hints, and the local-time formula.

Commands the firmware lists in `help` but the JSON doesn't document are parsed on a
best-effort basis. That output is human text, so rows that don't parse are dropped, and the
JSON entries always stay.

## Layout

```
app/
├── PROTOCOL.md, protocol.json   the contract (shared with firmware/)
├── clock.xcodeproj
├── clock/                       app target (synchronized folder)
│   ├── clockApp.swift           loads the spec, creates the link
│   ├── ContentView.swift        tabs, background disconnect
│   ├── Protocol/                pure, UI-free, unit-tested
│   │   ├── ProtocolSpec.swift     Decodable model of protocol.json
│   │   ├── SnapshotDecoder.swift  data-driven little-endian decoder
│   │   ├── Snapshot.swift         decoded record, validity, local time, pixels
│   │   ├── ResponseFramer.swift   rsp frames `| + = $`, per-id reassembly
│   │   ├── DeviceInfo.swift       `info` key=value + proto check
│   │   ├── CommandCatalog.swift   JSON + `help` commands, completion
│   │   ├── SoundFiles.swift       `storage tones` list, WAV header check, CRC-32, blob values
│   │   └── JSONValue.swift
│   ├── BLE/
│   │   ├── ClockLink.swift        CoreBluetooth central, request queue, blob writes, state
│   │   ├── ToneStore.swift        sound files: list / delete / upload driver, bundled WAVs
│   │   └── LinkTypes.swift        phases, problems/hints, results, transcript lines
│   ├── Tones/                   drop alarm WAVs here (bundled as resources)
│   └── Views/                   Connect, Status, Sounds, Commands, Shell, Common
└── clockTests/                  Swift Testing: golden vector, framing, catalog, history, sound files
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| "Pairing refused" right after the iOS prompt | The pairing window was shut. Hold the knob 10 s, then connect again. |
| "Clock forgot this device" / pairing fails at once | Settings → Bluetooth → clock → **Forget This Device**, then pair again with the window open. |
| No clock in the scan | Check the rear radio toggle. Another phone may be connected (one link at a time). |
| `denied` on `motion home` / `sys reboot` | Send `unsafe on` first. The command sheet offers to do it for you. |
| `busy` | The USB console is streaming. The app retries once after 1 s. |

Reference client to compare against: `firmware/tools/clockctl.py` (`shell`, `status --watch`).
