# `ux/` — the clock, on screen

A visualiser for [`clocksim`](../firmware/apps/clocksim/): the plate, the two hands, the
seven SK6812 pixels, the wake light and the speaker, plus controls for everything a person
touches — the knob, the rear radio toggle, tap-to-snooze, the accelerometer, power.

**It contains no clock logic.** Every number on screen arrived in a `state` frame; every
gesture leaves as one line of CLI text. Trajectories, homing, wrap, backlash, the knob HSM —
all of that is C++ in `firmware/`, the same code that goes on the ESP32-S3. If this app ever
starts computing where a hand ought to be, that is a bug in this app.

## Run it

```sh
cd firmware
cmake --preset host-dev && cmake --build --preset host-dev
./build/host-dev/apps/clocksim/clocksim        # terminal 1 — keeps its own CLI
```

```sh
python3 ux/uxapp.py                            # terminal 2 — opens http://127.0.0.1:8787
```

Python 3 standard library only: no pip, no npm, no lockfile to rot. `uxapp.py` serves
`ux/web/` and bridges the browser's WebSocket to clocksim's TCP line protocol on 4747 —
that is its entire job.

```
ux/web (SVG)  ──WS──  ux/uxapp.py  ──TCP 4747──  clocksim
                                                   ├─ uibridge   CLI in / JSON out
                                                   ├─ cli        the same CmdSpec table
                                                   ├─ services   the real algorithms
                                                   └─ clk_hal/host   fakes + the mechanism
```

Attach and detach freely — reloading the page or restarting either side reconnects. Both
front-ends work at once, so you can drag a hand in the browser and type `sensor homing read`
in the terminal.

| Flag | |
|---|---|
| `--sim-port 4747` | must match `clocksim --ui-port` |
| `--http-port 8787` | where the page is served |
| `--no-browser` | do not open a tab |

## What you can do with it

| | |
|---|---|
| **Drag a hand** | Moves it *physically* — `sim hand h\|m <deg>`. The firmware is not told. The gap you just opened is exactly what `motion home` has to discover. |
| **Drag or scroll the knob** | `sim knob ±n`, raw PCNT counts (256/rev). ←/→ nudge a detent. |
| **press & hold** | `sim press down` / `up` on the real button edges, so a long press is just a long hold. Space works too. |
| **radio off** | The rear J11 toggle. Note the polarity: the pin idles *high* and the switch pulls it low, so a broken harness fails to radios-enabled. |
| **tap** | One BNO085 top-tap — tap-to-snooze. |
| **warp** | Logarithmic, 0.1×–1000×. A 30-minute sunrise in 30 seconds. |
| **the CLI box** | Any command at all. It is the same dispatcher the console uses. |

The homing window at the top of the dial glows with the QRE1113 reading, and the meter under
*mechanism* is the same number — watching both while a hand sweeps past 0° is the fastest way
to see why a sweep that is too fast never finds home.

The app sends `unsafe on` every 30 s. Most of what it does is gated (§9.6) and clicking a
button by hand every minute is not a workflow.

## Geometry

`geometry.json` is traced from [`cad/clock_plate.svg`](../cad/clock_plate.svg) at
1 unit = 0.264583 mm. The SVG's viewBox is millimetres with the origin at the dial centre, so
every coordinate in `clockface.js` is the dimension you would measure on the aluminium.
Angles are the firmware's: **0° = 12 o'clock, clockwise positive**, homing index at 0°.

Three things the CAD and the docs do not agree on, carried here as-drawn:

1. The plate has **12 rectangular batons** (1.70 × 5.71 mm, r 35.14–40.85 mm). README §3 and
   `CLAUDE.md` both say "12 dots".
2. The **40 mm minute hand reaches into the baton band**, which starts at 35.14 mm.
3. `clock_plate.svg` contains **neither the Ø90 mm dial opening nor the five status-LED
   holes** — those are in README §3/§9 only, so their placement here is provisional.

## Protocol

In [`firmware/apps/clocksim/README.md`](../firmware/apps/clocksim/README.md). Short version:
lines of CLI text one way, newline-delimited JSON the other, and `nc 127.0.0.1 4747` is a
perfectly good client.
