# `apps/clocksim/`

The real services, the real `command/` and the real CLI table, linked against the fake HAL —
the product on your laptop (D14, `FIRMWARE.md` §11.2).

```sh
cmake --preset host-dev && cmake --build --preset host-dev
./build/host-dev/apps/clocksim/clocksim            # console + UI bridge on 4747
./build/host-dev/apps/clocksim/clocksim --no-ui    # console only
./build/host-dev/apps/clocksim/clocksim --ui-port 5000
```

`uibridge.{hpp,cpp}` is the pipe [`ux/`](../../../ux/) attaches to. It lives **in the app and
not under `components/`** on purpose: `apps/clock` puts `components/` on
`EXTRA_COMPONENT_DIRS`, so anything there is visible to the target build. Here it is
structurally impossible to link into the firmware.

## Protocol

Newline-delimited, UTF-8, deliberately asymmetric.

**`ux` → `clocksim`: one CLI line per newline**, optionally prefixed `#<id> `.

```
#7 motion goto 07:30
sim knob -12
```

So there is no JSON parser in the firmware, every input the app can produce is one you can
reproduce by typing it, and `nc 127.0.0.1 4747` is a working client. `#<id>` comes back on
the matching `res` so the app can pair a reply with its request; without it the reply is
still sent, just unlabelled.

**`clocksim` → `ux`: one JSON object per newline.**

| `t` | When | Shape |
|---|---|---|
| `hello` | once, on connect | `proto`, `app`, `board`, `profile`, `usteps_per_rev`, `pixels`, `px_names[]` |
| `state` | 50 Hz | everything in `hal::host::Snapshot` — below |
| `res` | after each command | `id?`, `st` (a `Status` name), `lines[]` |
| `log` | as logged | `lvl`, `mod`, `msg` — the same text stderr got, via `log::set_tap` |

```json
{"t":"state","ms":1552,"warp":1.000,
 "hands":{"h":100.000,"m":0.500,"hp":0,"mp":0,"hv":0,"mv":0,"moving":false,"motor":false},
 "px":[[0,0,0,0], "…7"],"refreshed":true,
 "wake":{"warm":0,"cool":0},"spk":{"on":false,"vol":40},
 "pwr":{"plugged":true,"mv":4021,"soc":96,"chrg":true},
 "knob":{"count":0,"sw":false},"opto":{"n":0.0800,"auto":true},
 "imu":{"yaw":0.00,"taps":0},"radio_off":false}
```

`hands.h`/`hands.m` are degrees of the **true** angle — what you would see through the glass
— with **0° = 12 o'clock, clockwise positive**. `hp`/`mp` are the microsteps the firmware
thinks it commanded. The gap between the two is what homing exists to close.

## Things worth knowing

- **State is read through `hal::host::snapshot()`, never through the `hal::` reads.**
  `knob::read()` consumes its delta; a viewer that ate the `ui` AO's deltas would be changing
  what it is watching.
- **Loopback only.** This surface runs arbitrary CLI commands and the CLI moves hardware.
- **`Busy` is a real answer.** A `sensor … stream` on stdin holds the dispatch mutex for up
  to 120 s; rather than stall its state frames behind it, a client waits 250 ms and then
  answers `Busy`.
- **Log drops are reported, never blocking.** The ring holds 256 lines; a client that falls
  behind gets a `… N log lines dropped` marker (rule 12: a slow reader must not
  back-pressure the producer).
- Four clients max. A reloaded browser page is reaped when its thread raises its done flag.
