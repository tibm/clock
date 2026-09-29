# TODO — the road to the complete clock (device side)

What is left between today and the product in `README.md` §1, feature by feature, **for the
physical clock only** (firmware + the hardware it waits on). The iOS app is a separate track;
its only link to this list is [`app/PROTOCOL.md`](app/PROTOCOL.md).

- The *ordered* firmware work, with bench procedures, is `FIRMWARE.md` §12.2 — IDs like **F6.1**
  point there. This file is the map; §12.2 is the route.
- Hardware fixes for the next board are `kicad/REVIEW.md` (V1–V18).
- ✅ done · 🟡 partly / built but not verified on the board · ⬜ not started. Update this file in
  the same commit that changes a line's state.

*Last updated 2026-09-28.*

## Next up — the open items, in order

1. **The alarm rings** — schedule in `chrono`, `Ringing` / `Snoozed` in `ui`, snooze (tap / press)
   and dismiss (long press). Today the alarm is stored and shown, and never fires.
2. **Audio for the alarm** — the `audio` AO, a WAV from `storage`, 30 s volume ramp, HPF +
   limiter; the 25 % ceiling stays until **F5.4**. *(Next session: audio requirements.)*
3. **Time survives a reboot** — RTC / 32 kHz retention; today only the offset and the alarm do.
4. **Bench the BLE link on the board** — **F6.1** (iOS + Android, pairing window, reconnect).
5. **Sunrise wake light** — `hal::wake` (12 V, plugged-only).
6. **Power modes + the 48 h backup measurement** — `supervisor`, milestone 8.
7. **Bench Wi-Fi + SNTP (built 2026-09-28), then OTA** — **F6.2**.

## Time

| | Feature | Notes |
|---|---|---|
| ✅ | Hands follow the clock (sweep or tick) | `chrono` → `motion`; `chrono steps` |
| 🟡 | Homing on boot, per-unit zero, auto-home trims, dial re-levelled by gravity | all in firmware; homing bench on the printed hands: **F2.4** |
| 🟡 | Movement: steps/rev, direction, silence | **F2.1** (11 520 on the `.NS` part), **F2.3**; direction ✅ F2.2 |
| ✅ | Set time from the phone: date + UTC + offset, `chrono tz` for DST | 2026-09-28, app/PROTOCOL.md "Keeping time" |
| ✅ | Set time of day with the knob (`clock` mode), refused when the network owns the time | |
| ⬜ | **Time survives a reboot / deep sleep** | RTC + 32.768 kHz crystal retention, milestone 2; today `time_valid` is lost on every reset |
| 🟡 | Wi-Fi (credentials from the app over the bonded BLE link) + SNTP (3 free servers, in order) → the clock sets itself; POSIX TZ zones on-device, default San Francisco | **F6.2** — built + host-tested 2026-09-28, **bench next** |

## Alarm — the product

| | Feature | Notes |
|---|---|---|
| ✅ | Alarm time + arm/disarm: knob, CLI, BLE; persisted in NVS | 2026-09-28 |
| ✅ | **The alarm actually rings** | 2026-09-27: `ui` fires it at the set local minute → `Ringing` / `Snoozed` (§6.6). Still `ui`'s single alarm — moving it into `chrono`'s table (§6.4) comes with weekdays below |
| ✅ | Snooze (tap on top via BNO085, or press) and dismiss (long press) | 2026-09-27. Snooze 9 min, auto-dismiss after 15 min unanswered (`Ui::Tuning`, not yet NVS/app-settable). Bench: **F5.7** |
| ⬜ | Repeat by weekday (and several alarms?) | today: one daily alarm. Needs a protocol addition when built |
| ⬜ | Sunrise wake light, 30 min warm→cool before the alarm | `hal::wake` is the last HAL stub (12 V boost, plugged-only; Phase 4). On battery: dim dial-pixel glow instead |
| ✅ | Alarm sound from a file (WAV), volume ramp over 30 s | 2026-09-27: `/sd/tones/*.wav`, 48 kHz mono 16-bit only; `storage` AO + 2 s PSRAM ring; tone chosen by `chrono alarm tone` (NVS); beep fallback. Host-tested; **bench: F5.7** |
| 🟡 | Manage sound files from the app: list, upload, delete, choose the alarm tone | Firmware + protocol done 2026-09-27 (`app/PROTOCOL.md` "Sound files", `blob` characteristic, `storage tones/put/rm`; `clockctl.py put` works as the reference). **The iOS screens are still to build**; bench the transfer rate (**F5.7**) |
| ⬜ | Audio DSP: HPF + limiter (protects the 2″ driver) | `audio dsp`, **F5.6** |
| ⬜ | Lift the 25 % volume ceiling | only after the protector sense loop is measured (**F5.4**, `FIRMWARE.md` R-AUDIO-1) |

## Lights and knob

| | Feature | Notes |
|---|---|---|
| ✅ | Knob HSM: modes, long press, 10 s hold = pairing, 5 s timeout | §6.6 |
| ✅ | Status-pixel animations (five patterns), dial wash, tap acknowledgement | |
| 🟡 | The five status pixels on the `J12` harness | **F3.1** — the on-PCB dial pixels already light |
| ⬜ | Brightness follows the room (TSL2591), dark at night | reading exists; policy doesn't |
| ⬜ | Fault codes on the status row | `supervisor` |

## Power

| | Feature | Notes |
|---|---|---|
| ✅ | Charging + running off the cell (build #1 reworked) | §12.0.17; cell-insertion SOP still needed until v0.4 **V17** |
| 🟡 | Battery / charger status in firmware | `hal::power::read()`; plugged half of the bench check **F1.6** |
| ⬜ | Power modes: battery = quiet, deep-sleep cadence for the hands, low-battery shutdown | `supervisor`, §7.3–7.4, milestone 8 — **the 48 h backup claim is unmeasured** |
| ⬜ | Radio policy on battery (slower advertising / off) | BLE advertises at 1 s whenever the toggle is on |

## Connectivity (device side)

| | Feature | Notes |
|---|---|---|
| 🟡 | BLE link: pairing window, commands, status snapshot | built + host-tested 2026-09-27; **bench it: F6.1** |
| ✅ | Set time / alarm over BLE | 2026-09-28 |
| 🟡 | Wi-Fi + SNTP (app: Clock tab → Wi-Fi; zone rule sent on connect) | **F6.2** — built, not benched |
| ⬜ | Firmware update over the air (OTA) | partition table is ready; `net ota` |
| ⬜ | Status history on the device (plot survives the phone being away) | **F6.3** |
| ⬜ | Upload alarm sounds from the phone | `Bulk` characteristic → `storage`; protocol addition |

## Sensors

| | Feature | Notes |
|---|---|---|
| ✅ | BME688, TSL2591, BNO085 drivers; values in the status snapshot | |
| ⬜ | `board` AO: sole I²C owner, sensors on their own cadence, BNO085 tap by interrupt | §6.5; also removes the ~1 s command stall (**F6.4**) |
| ⬜ | Air-quality index (BSEC) | optional, licence-gated |

## Robustness

| | Feature | Notes |
|---|---|---|
| ⬜ | Task watchdog on every AO, reset-reason + coredump report at boot | `supervisor`, §6.8 |
| ⬜ | `sys top`, `sys heap`, `sys ev dump` | registered, still `not implemented` |
| ⬜ | One `storage`-owned config (§7.5) instead of ad-hoc NVS keys | keys today: `ui.input`, `ui.alarm`, `ui.armed`, `chr.tz`, motion zero/inhibit |
| ⬜ | `BOARD=devkit-uart` compiles | **F5.5** |
| ⬜ | Flaky host tests (motion AO cases, ~20–30 % of runs) + `test_sim` opto cases broken by the 5 mV span | the opto cases fail on every run since `ed6214f` |
