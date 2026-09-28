# Clock ⇄ App protocol — v1 (BLE)

**The contract between the clock firmware and the iOS app.** Two codebases, developed
separately, that meet only here:

| | Owns | Reads |
|---|---|---|
| Firmware (`firmware/`) | the clock side; implements this file | this file |
| iOS app (`app/ios/` or wherever the Xcode project lives) | the phone side | this file + `protocol.json` |

- **`PROTOCOL.md`** (this file) is normative for *behaviour*.
- **`protocol.json`** is normative for *numbers*: UUIDs, byte offsets, enums, the command list and
  a **golden test vector**. Generate or hand-port from it; unit-test your decoder against
  `snapshot.golden` (the firmware's host tests assert the same bytes against the shipping encoder,
  so a decoder that passes it agrees with the firmware).
- Firmware internals are in `../FIRMWARE.md` §8 — **the app must not need them.** If something
  here is ambiguous, fix this file; don't reverse-engineer the firmware.

**Change process.** Either side may propose a change by editing this file and `protocol.json`
together, adding a line to the changelog at the bottom. Compatible changes (new command, new
snapshot field at the end, new flag bit, new enum value) keep `protocol_version`. Anything that
breaks an existing reader bumps it. A command marked `planned` (in `protocol.json`) is a promise of
grammar, not of availability — the app must handle `bad-arg` ("unknown command") from it
gracefully. (None are planned right now.)

---

## 1. Transport

**Bluetooth Low Energy, GATT.** The clock is a peripheral with one custom service; the phone is
the central. One connection at a time. (BLE rather than Classic: lower power, and custom Classic
profiles are not available to iOS apps.)

| Name | UUID | Properties | Content |
|---|---|---|---|
| service | `7a3e0001-5c1d-4b8e-9f3a-2c6d1e0b9a41` | | |
| `cmd` | `7a3e0002-5c1d-4b8e-9f3a-2c6d1e0b9a41` | write **with response** | UTF-8 command line, §3 |
| `rsp` | `7a3e0003-5c1d-4b8e-9f3a-2c6d1e0b9a41` | notify | UTF-8 response frames, §3 |
| `status` | `7a3e0004-5c1d-4b8e-9f3a-2c6d1e0b9a41` | read, notify | 132-byte binary snapshot, §5 |
| `info` | `7a3e0005-5c1d-4b8e-9f3a-2c6d1e0b9a41` | read | UTF-8 `key=value` pairs separated by spaces, §6 |

The standard GAP/GATT services are also present. **All four characteristics require an
encrypted, bonded link** (§2); before bonding every access fails with an ATT
insufficient-authentication / -encryption error.

### Advertising

- Always on while the clock's rear radio toggle is on. Interval ~1 s normally, ~100 ms while the
  pairing window is open.
- Advertisement carries the **service UUID** → scan with
  `scanForPeripherals(withServices: [serviceUUID])` (also works in the background).
- **Manufacturer data**: `FF FF <state>` — company id `0xFFFF` (little-endian), then one state
  byte. `state & 0x01` = **pairing window open**. Use it to show "ready to pair" in a device list
  without connecting. (iOS: `advertisementData[CBAdvertisementDataManufacturerDataKey]`,
  foreground scans only.)
- Name `clock` (scan response). Several clocks will all be named `clock`; identify a clock by its
  `CBPeripheral.identifier` after bonding.

---

## 2. Pairing and security

LE Secure Connections, **Just Works**, bonded. There is no display or keypad on the clock; the
proof that the user is physically at the clock is the **pairing window**:

- **A phone can bond only while the window is open.** The user opens it by **holding the knob for
  10 s** (the five status LEDs breathe blue). It also opens from the USB console, or when an
  already-bonded app sends `net ble pair`.
- The window closes on: a successful bond (**two green flashes**, LEDs go dark), a knob press,
  `net ble pair off`, the rear radio toggle, or **120 s** passing.
- If the radio toggle is off, the hold is refused (**three red flashes**) and nothing advertises.
- The clock keeps **up to 4 bonds** (oldest evicted). A bonded phone reconnects at any time,
  window or not.

### The iOS flow

1. Scan, connect (whether the window is open or not).
2. `discoverServices([service])` → `discoverCharacteristics`.
3. Touch any characteristic (e.g. `setNotifyValue(true, for: rsp)` or read `info`). The clock
   answers "insufficient encryption", **iOS shows the system pairing prompt** ("Pair / Cancel").
4. User taps Pair:
   - **window open** → bond stored on both sides, the request can be retried and succeeds. Done
     for good.
   - **window shut** → the clock **disconnects** immediately after encryption. The app should
     explain: *"Hold the knob on top of the clock for 10 seconds until the lights breathe blue,
     then try again."* Tip: read the manufacturer state bit first and prompt before connecting.

### Failure modes the app must handle

| Symptom | Cause | Tell the user |
|---|---|---|
| Disconnect right after the pairing prompt | window was shut | hold the knob 10 s, retry |
| Pairing fails / "Peer removed pairing information" | the clock forgot this phone (`net ble unbond`, or evicted as the 5th bond) but iOS still has keys | Settings → Bluetooth → clock → **Forget This Device**, then pair again with the window open |
| Clock forgot → phone re-pairs outside the window | refused, disconnected | same as the first row |
| Nothing found in scan | rear radio toggle off, or another phone is connected (one link at a time) | check the toggle; close the app on the other phone |

---

## 3. The command channel (`cmd` → `rsp`)

**The app sends the same text commands as the clock's USB console**, and gets the console's
output back. This is deliberate: the app can do exactly what the console can do, and a debug
screen in the app is simply a terminal.

### Request

Write **with response** to `cmd`, UTF-8:

```
<id> <command line>          e.g.  "17 chrono time set 07:15"
```

- `<id>`: decimal 0–65535, chosen by the app, echoed on every response frame. Optional: a line
  without a leading number is id 0.
- **≤ 256 bytes.** Keep each write ≤ `maximumWriteValueLength(for: .withResponse)` to avoid a
  long write; real commands are far shorter.
- A trailing `\r`/`\n` is ignored. A blank line answers `bad-arg`.
- The write *acknowledgement* only means "received". The *result* arrives on `rsp`.
- **Subscribe to `rsp` before the first write** — frames sent while not subscribed are lost.

### Response

Notifications on `rsp`, UTF-8, each one frame:

| Frame | Meaning |
|---|---|
| `<id>\|<text>` | one output line — **human text, not stable across firmware versions: display it, never parse it** |
| `<id>+<text>` | a **fragment**: this record continues in the next frame(s) with the same id |
| `<id>=<key>=<value>` | a machine-readable pair. Stable when a command in §4 documents it (none do yet) |
| `<id>$<status>` | **terminal**. Exactly one per request, always last |

**Reassembly:** for a given id, append the text of `+` frames until a frame of kind `|` or `=`
arrives; that completes one record. A frame carries up to *negotiated MTU − 3* bytes (the clock
asks for 247 → 244 bytes; iOS typically negotiates ~185 → 182), so short lines usually arrive in
one frame. Example, `help sys` at a tiny MTU:

```
8+sys stat                          one-scre
8|en: what is it doing right now
8+sys ver                           app / bu
8|ild / sdk identity
...
8$ok
```

(20-byte frames; the record is the concatenation, i.e. `sys stat ... one-screen: what is it doing right now`.)

**Status values** (`<id>$...`):

| status | meaning | app action |
|---|---|---|
| `ok` | done | — |
| `bad-arg` | unknown command or bad argument (the `\|` lines say which) | bug, or a `planned` command this firmware lacks |
| `denied` | refused by a gate: needs `unsafe on` (debug commands), or over a firmware limit (e.g. volume ceiling) — the `\|` lines say which | show the text; debug screen may offer `unsafe on` |
| `busy` | the clock's CLI is occupied (USB console streaming), or > 4 requests in flight | retry after ~1 s |
| `not-ready` | refused in the current state (e.g. radio off, movement not homed) | show the `\|` text |
| `failed` | tried and failed | show the `\|` text |
| `not-present` | the hardware for this is not fitted | hide the feature |

### Rules

- **Send one request at a time** and wait for its `$`. The clock queues at most 4; the 5th gets
  `busy`. Commands run strictly in order, so responses never interleave.
- **Timeout:** 10 s is safe for everything in §4. (Debug `... stream` commands can run 120 s —
  don't use them from the app.)
- A response may be cut short if the link drops; treat a disconnect as "outcome unknown".
- `sys reboot` drops the link before its `$`.

---

## 4. Commands the app may rely on

Full list with argument ranges in `protocol.json` → `commands`. The app should depend on the
**terminal status** of these; their `|` text is for display only. **State is read from the
status snapshot (§5), not from command output.**

| Command | Status | Purpose |
|---|---|---|
| `chrono time epoch <unix_ms> [<utc_offset_min>]` | implemented | **set date + time + UTC offset from the phone** — see "Keeping time" below |
| `chrono tz [<utc_offset_min>]` | implemented | the UTC offset alone (−720…840). Same instant, the hands move. Persisted |
| `chrono time set <hh:mm[:ss]>` | implemented | set the *local* time of day only; keeps the date if the clock has one |
| `chrono alarm set <hh:mm>` | implemented | alarm time, local 24 h. Persisted. `$ok` only after the clock has taken it |
| `chrono alarm arm <on\|off>` | implemented | arm / disarm. Persisted. Armed, it rings at the set local minute: `ui_mode` goes to `ringing` (6), then `snoozed` (7) or back to `idle` |
| `chrono alarm tone [<name>\|none]` | implemented | which `/sd/tones` WAV rings; `none` = the built-in beep. Checked on the card first — `bad-arg` if it is not 48 kHz mono 16-bit PCM, `not-present` with no card. Persisted. (No list the app can parse yet — `storage ls` is display text) |
| `chrono alarm fire` · `chrono alarm snooze` · `chrono alarm dismiss` | implemented | ring now (test the sound) · snooze a ring · stop it (stays armed). The last two answer `not-ready` when nothing rings |
| `audio play <name> [loop]` | implemented | preview a `/sd/tones` WAV; `audio stop` ends it |
| `chrono alarm` | implemented | show it (display only — read the state from the snapshot) |
| `chrono steps <1-60>` | implemented | hands tick (1) or sweep (60) |
| `audio vol <0-100>` | implemented | volume. Above the firmware's current ceiling (25) → `denied` |
| `audio tone [<hz>] [<ms>]` · `audio stop` | implemented | test sound / "find my clock"; `audio stop` also ends `audio play` |
| `ui knob bright <0-100>` | implemented | status-LED brightness |
| `net ble period <100-3600000>` | implemented | status notify interval, ms. Global, **not persisted** (1000 after every boot) |
| `net ble pair off` | implemented | close the pairing window |
| `net ble unbond` | implemented | forget **all** phones; drops this link (then Forget This Device in iOS) |
| `sys ver` · `sys snap` | implemented | identity / whole snapshot as text, for an "about" or debug screen |
| `unsafe on` · `motion home` · `sys reboot` | implemented | debug screen only (`motion home`, `sys reboot` need `unsafe on` within the last 60 s) |

Anything else the console accepts also works (`help` lists it) — treat it as debug.

### Keeping time

The phone is the clock's time source until Wi-Fi/SNTP exists, and it owns the timezone rules:
the clock stores only *the offset in force now*, never a zone or a DST table.

- **On every connect**, send `chrono time epoch <now_unix_ms> <utc_offset_min>` — iOS:
  `Int64(Date().timeIntervalSince1970 * 1000)` and `TimeZone.current.secondsFromGMT() / 60`.
  Milliseconds: a value under 10¹¹ is taken as a seconds mistake and answered `bad-arg`.
- **When the offset changes** (DST transition, travel — iOS `NSSystemTimeZoneDidChange` /
  `significantTimeChangeNotification`) send it again, or just `chrono tz <utc_offset_min>`.
- The knob can also set the time of day; it keeps the date and the offset.
- The offset and the alarm survive a reboot. **The time itself does not yet** (no RTC retention
  in firmware) — after a reboot `time_valid` is clear until the phone reconnects.

Not on this channel: **Wi-Fi credentials** will use Espressif's standard BLE provisioning
(separate service, SRP6a security), and sound uploads will get their own characteristic. Both
will be specified here when built.

---

## 5. The status snapshot (`status`)

One **132-byte, little-endian, fixed-layout** record of everything the clock knows. Read it, or
subscribe: the clock re-takes it every `net ble period` ms (1000 by default) and notifies each
new one. Designed to be **logged and plotted** (132 B/sample ≈ 190 KB/day at 1/min).

Field-by-field layout, types, units, scales and enums: **`protocol.json` → `snapshot`**. Summary:

| Bytes | Content |
|---|---|
| 0–1 | `schema` (=1), `size` (=132) |
| 2–35 | `seq`, `uptime_s`, `epoch_ms`, `tz_off_min`, reset reason, clock source, **`flags`** (u32 @20), `fw_id`, heap |
| 36–39 | battery mV, SoC %, source |
| 40–57 | room: temperature, humidity, pressure, gas, age · light: lux, age |
| 58–71 | gravity xyz, yaw/pitch/roll, tap counter |
| 72–87 | hands: motion state, dial tick, shown time, target time, homing sensor, faults, trims |
| 88–95 | UI mode, volume, alarm h:m, brightness, wake-light %, BLE state |
| 96–123 | the 7 LEDs, RGBW each |
| 124–131 | knob count, bonds, Wi-Fi state, RSSI, reserved |

### Decoding rules

1. Reject `schema != 1` or `size < 132` (or fewer bytes). **Ignore bytes beyond the 132 you know**
   — newer firmware appends fields.
2. **Validity lives in `flags`**, not in the values: e.g. if `env_ok` is clear, temperature/humidity
   /pressure are garbage whatever they hold. Each field's `valid_if` in the JSON names its flag.
   Show "—" for invalid values; do not plot them.
3. Ignore unknown flag bits; show unknown enum values as `unknown(n)`.
4. `seq` increments per record; a gap means you missed notifications (fine — log what you get).
5. Room sensor is sampled every 60 s and light every 5 s; `env_age_s` / `als_age_s` say how old
   the value is. Don't plot them faster than that.
6. `soc_pct == 255` means unknown — while plugged in, the clock cannot measure the cell.

### Time

- `epoch_ms` is valid only when `time_valid` is set (the clock has been told the time since boot).
- **Local time = `epoch_ms` + `tz_off_min` × 60 000**, formatted as UTC. Always — `tz_off_min` is 0
  until an offset has been given, and then `epoch_ms` is true UTC.
- **`date_valid`** (flag bit 30): the date part is real (it came from `chrono time epoch`). Clear
  → only the time of day means anything (the clock was set by the knob or `chrono time set` since
  boot and never got a date); show hh:mm:ss only.
- `tz_set` (bit 2): an offset has been given at some point (it is persisted).
- `hand_h:hand_m` is what the hands physically show right now (differs from the time while
  moving, homing, or when a UI mode uses the hands as a gauge).

---

## 6. `info`

Read-only UTF-8, space-separated `key=value`, e.g.

```
fw=0.1.0 sha=ed6214f built=2026-09-27T12:00:00Z board=rev0_3 profile=dev sdk=v5.5.5 proto=1 schema=1
```

Check `proto`: if it's higher than the app knows, warn "update the app"; if lower, hide features
marked newer. Unknown keys: ignore.

---

## 7. Power and etiquette

- The clock usually runs plugged in, but also runs on a battery. Don't hold a connection open in
  the background for no reason; connect, do the job, and disconnect when the app is backgrounded
  (unless it's deliberately logging).
- For a live dashboard keep the default 1 s period; for long logging, `net ble period 60000`.
  It's global: set it back when you're done (or leave it — it resets at reboot).
- Only one phone can be connected. Disconnect promptly so another phone (or the same user's
  iPad) can connect.

---

## 8. Testing without the app

- `firmware/tools/clockctl.py` (Python, `pip install bleak`) is the reference client: `scan`,
  `shell` (interactive console over BLE), `run "<cmd>"`, `status --watch --csv`. If the app and
  `clockctl.py` disagree, compare against this file.
- Any generic BLE app (nRF Connect, LightBlue) works: bond with the window open, subscribe to
  `rsp`, write `1 sys ver` to `cmd`.
- The golden vector (`protocol.json` → `snapshot.golden`) is the unit test for the snapshot decoder:
  `hex` must decode to exactly `decoded` (scaled fields within float tolerance).

---

## Changelog

| Date | Version | Change |
|---|---|---|
| 2026-09-27 | proto 1 / schema 1 | First version: 4 characteristics, CLI-over-GATT framing, 132-byte snapshot, pairing window |
| 2026-09-28 | proto 1 / schema 1 | Implemented `chrono time epoch` (offset now optional), `chrono alarm set/arm`; added `chrono tz`, `chrono alarm`; `tz_off_min` populated; new flag bit 30 `date_valid`; "Keeping time" guidance. All compatible |
| 2026-09-27 | proto 1 / schema 1 | The alarm rings. New `ui_mode` values `ringing` (6) and `snoozed` (7); new commands `chrono alarm tone`, `chrono alarm fire/snooze/dismiss`, `audio play`. All compatible (new enum values, new commands) |
