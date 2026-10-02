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
| **Clock → Wi-Fi** | The clock's own scan of networks in range, join (SSID + password sent `hex:`-encoded over the bonded link, never echoed to the shell), forget; state, last failure reason and signal from the snapshot. |
| **History** | The clock's history log, **mirrored on the phone** (`Application Support/History/<clock id>/<yyyymmdd>.bin`, byte for byte; never deleted because the clock deleted it). Syncs on every connect and from *Sync now*: `log days` → diff → `log fetch <day> <from>` → `bulk` packets → CRC-32 → atomic append. Other commands wait while it runs (the toolbar says "syncing history…"). Charts (Swift Charts) per quantity: temperature, humidity, pressure, gas (log), light (mean + peak), battery, charge, Wi-Fi; 24 h · 7 d · 30 d · 1 y · All, bucketed to ≤ ~1000 points with a min…max band, lines broken at gaps > 2 periods, events as rules plus a list. Settings sheet: `log status`, period / keep / cap / record. Export: the raw day files, or a CSV. |
| **Logs** | The clock's debug journal (every log line, one file per boot on its card), **mirrored on the phone** (`Application Support/Journal/<clock id>/<name>`, never deleted because the clock deleted it). Syncs on opening the tab, on *Refresh*, and every 5 s with **Follow** on (one small tail fetch of the current file): `sys journal files` → diff by name → `sys journal fetch <name> <from>` → `bulk` → CRC-32 → atomic append; shares the one download slot with History. Header from `sys journal` (boot, card, lines lost). Boot list, newest first, parts grouped: reset reason (red for panic / watchdog / brown-out), "ended: …" from the next boot, *current* and *rescued lines* badges, size. Viewer: parts concatenated, colour by level, level chips, tag menu, search, `+h:mm:ss.mmm` since boot, the rescued "before the reset" section tinted with **Jump to before the reset**, last 5000 lines with *Load all*; menu for `sys debug <tag> debug` / `sys debug all info`; Share the raw files. |
| **Shell** | A terminal over BLE, the same commands as the USB console. Completion chips for the next word or argument. Tab / ↑ / ↓ on a hardware keyboard, or the chevrons. History is persisted. Output is coloured by record kind (`>` sent, `$` status, `=` pairs, `#` app notes), with an optional raw-frame view. |

On connect the app automatically:
1. subscribes to `rsp` (this is what triggers iOS pairing), then to `status`, and reads `info`
2. sends the zone as a POSIX rule (`chrono tz <posix> <iana>`) and then `chrono time epoch <now_ms> <utc_offset_min>` (PROTOCOL.md "Keeping time"), and sends both again whenever the phone's timezone changes
3. runs `help`, then `help <group>` for each group, and merges the result into the command list
4. syncs the history log (only what is new: normally today's tail)

On iOS it disconnects when backgrounded and reconnects when it comes back (PROTOCOL.md §7).

## Alarm schedule (Clock tab → Alarm)

The clock now keeps a **weekly schedule** (a time per weekday, each on/off — set from the app with
`chrono alarm week`) and at most **one one-off** of the next alarm, set with the clock's knob (or
`chrono alarm next`). The one-off replaces that one day's alarm and never changes the schedule;
the snapshot shows it live (`alarm_next = override`), even while the app is connected. The Alarm
screen: master switch, next alarm + a *Cancel* banner for a one-off, seven day rows (any edit sends
the whole week, debounced), "Just once". A 132-byte snapshot (older firmware) shows the single
daily alarm instead. Status tab: "Alarm" group.
Behaviour: PROTOCOL.md "Alarm schedule".

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
| `history` | the day-file decoder: header checks, record fields (offset, type, scale, `valid_if`, `sentinel`), kinds, `encodings` (parsed `10^(v / k) − c`), flag and event names, golden vectors |
| `journal` | log file names + part order (`name_regex`), line split (`line_regex`), boot header and reset reasons, the rescued marker |

Compatible protocol changes (PROTOCOL.md "Change process") need **no Swift change**:
- **New command:** appears in the table and in autocomplete.
- **New snapshot field at the end:** shows up in *Other fields*.
- **New flag bit:** gets a chip.
- **New history sample field:** gets a chart under *Other*, and a CSV column.
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
│   │   ├── HistoryRecord.swift    day-file decoder: header, CRC-8, samples, events
│   │   ├── HistorySync.swift      `log days` → fetch plan, `FileSync` diff, `bulk` packets + reassembly
│   │   ├── HistorySeries.swift    chart points: buckets (min/mean/max), gap segments
│   │   ├── JournalSync.swift      `sys journal files` → fetch plan, file names, boots
│   │   ├── JournalLine.swift      journal lines: level / ms / tag, header, rescued section, filter
│   │   └── JSONValue.swift
│   ├── BLE/
│   │   ├── ClockLink.swift        CoreBluetooth central, request queue, blob writes, bulk fetches, state
│   │   ├── ToneStore.swift        sound files: list / delete / upload driver, bundled WAVs
│   │   ├── HistoryStore.swift     history: sync driver, settings, chart loading, export
│   │   ├── HistoryArchive.swift   the phone's copy of the day files, CSV
│   │   ├── JournalStore.swift     logs: sync driver, Follow, boot list, parsing cache
│   │   ├── JournalArchive.swift   the phone's copy of the journal files
│   │   └── LinkTypes.swift        phases, problems/hints, results, transcript lines
│   ├── Tones/                   drop alarm WAVs here (bundled as resources)
│   └── Views/                   Connect, Status, History, Logs, Sounds, Commands, Shell, Common
└── clockTests/                  Swift Testing: golden vector, framing, catalog, history, journal, sound files
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| "Pairing refused" right after the iOS prompt | The pairing window was shut. Hold the knob 10 s, then connect again. |
| "Clock forgot this device" / pairing fails at once | Settings → Bluetooth → clock → **Forget This Device**, then pair again with the window open. |
| No clock in the scan | Check the rear radio toggle. Another phone may be connected (one link at a time). |
| `denied` on `motion home` / `sys reboot` | Send `unsafe on` first. The command sheet offers to do it for you. |
| `busy` | The USB console is streaming. The app retries once after 1 s. |
| History / Logs: "no `bulk` characteristic", Sounds tab hidden, while `info` shows a current `sha` | The phone cached the clock's old GATT table (paired before a firmware update). Firmware since 2026-10-01 announces GATT changes (*Service Changed*) so this heals on the next connect; for older firmware, or if it persists: **Forget This Device** (macOS: System Settings → Bluetooth → ⓘ → Forget), `net ble unbond` on the clock, hold the knob 10 s, pair again. If `sha` is old, reflash. |

Reference client to compare against: `firmware/tools/clockctl.py` (`shell`, `status --watch`).
