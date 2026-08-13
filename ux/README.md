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

## Stop it

`ctrl-c` in each terminal. If one was left running in the background — by a previous shell,
an editor task, an agent — it still holds its port, and the two halves fail differently:

- **`uxapp.py` dies loudly**, `OSError: [Errno 48] Address already in use`, on the bind.
- **`clocksim` does not.** It logs `ui: port 4747 is taken -- console only (--ui-port to
  move it)` and carries on with just its console. The browser is still attached to the
  *first* clocksim, so the dial answers commands you did not type.

Neither is a stale socket — both listeners set `SO_REUSEADDR`, so a live process has it.
Find it by port, then end it:

```sh
lsof -nP -iTCP:8787 -sTCP:LISTEN               # the page
lsof -nP -iTCP:4747 -sTCP:LISTEN               # the bridge

kill $(lsof -ti tcp:8787)                      # ux
kill $(lsof -ti tcp:4747)                      # clocksim
```

`pkill -f uxapp.py` and `pkill -f clocksim` do the same by name. Reach for `kill -9` only
if a plain `kill` was ignored: both sides close their listener on the way out, and a `-9`
leaves the socket to the kernel to reap.

Or leave the old pair alone and start beside it:

```sh
./build/host-dev/apps/clocksim/clocksim --ui-port 4748
python3 ux/uxapp.py --sim-port 4748 --http-port 8788
```

## What you can do with it

| | |
|---|---|
| **Drag a hand** | Moves it *physically* — `sim hand h\|m <deg>`. The firmware is not told. The gap you just opened is exactly what `motion home` has to discover. |
| **Drag or scroll the knob** | `sim knob ±n`, raw PCNT counts (256/rev). ←/→ nudge a detent. |
| **press & hold** | `sim press down` / `up` on the real button edges, so a long press is just a long hold. Space works too. |
| **radio off** | The rear J11 toggle. Note the polarity: the pin idles *high* and the switch pulls it low, so a broken harness fails to radios-enabled. |
| **tap** | One BNO085 top-tap — tap-to-snooze. |
| **now** | `chrono time set <the browser's clock>`, to the second. There is no RTC and no SNTP yet (§7.4), so a person with a watch is the time source — and reading one is an *input*, which is why it leaves as an ordinary `chrono time set` like every other gesture. |
| **reset fakes vs reboot** | `sim reset` puts the fake *hardware* back to power-on and leaves the services believing exactly what they believed — `motion` still thinks it is homed while the hands have jumped, which is the case worth testing. `sys reboot` restarts the image: `esp_restart()` on the board, a re-exec of the process here. The page reconnects on its own. |
| **yaw** | `sim imu <deg>`. The whole plate turns on screen — index window, status LEDs and all — and shrinks just enough to keep its corners in frame. Dragging a hand still lands where you dropped it: the drag angle is taken in the dial's frame, not the screen's. |
| **warp** | Logarithmic, 0.1×–1000×. A 30-minute sunrise in 30 seconds. |
| **the CLI box** | Any command at all. It is the same dispatcher the console uses. |

The layout is one plate and **two columns of cards**, sized so a laptop shows the whole
instrument without a page scroll; under ~1240 px the cards fall into one column, under
~860 px they go below the plate. Every button and slider carries a tooltip naming the CLI
row behind it, so "what does this do" is a hover rather than a grep.

**Buttons that report as well as act** are coloured from the state frame, never from what
was last clicked: *home* is green when the movement says it is homed, blue while a run is in
progress, red when it is not homed or the last run faulted; *follow*/*release* light
whichever one is currently true. The two hardware switches (*radios*, *plugged*) show what
was just **asked for** until a frame agrees with it — reading the painted state at click
time meant a fast second click was decided from a stale frame and did nothing.

The homing window at the top of the dial glows with the QRE1113 reading, and the meter under
*mechanism* is the same number — watching both while a hand sweeps past 0° is the fastest way
to see why a sweep that is too fast never finds home.

**Homing is sampled, so it can genuinely miss.** The lit window is about 3.6° wide and the
control loop reads the opto once per tick, so a coarse pass that travels further than that
between two reads steps clean over the index and the run ends in `Fault` — the *home* button
turns orange when it has, and *faults* counts them. Two things push it over: `v_coarse`, and
**warp**, because a tick of wall time is `warp` ticks of dial travel. Homing is reliable at
1–10× and starts failing around 60×. That aliasing is modelled on purpose (`hal_host.cpp`,
`opto_from_hands_locked`) — it is the same failure the bench will show.

Two sliders that sound alike and are not:

- **jumps per minute** (*clock*, `chrono steps 1..60`) — how many distinct positions the
  hands take per minute of clock time as it *runs*. `1` is a hand that jumps once a minute
  and is otherwise still, the way quartz ticks; `60` is one move a second. It quantises the
  time, so both hands step together and the hour hand keeps its fraction of the way to the
  next hour. The wall clock itself is untouched — this is only how often it asks the hands
  to move.
- **counts per minute of adjustment** (*knob*, `ui knob counts`) — how far a *turn* moves
  the thing you are setting. PCNT counts, and the encoder is 4 counts per detent: `4` = one
  minute per detent, `1` = one minute per count.

The **knob only edits in a set mode** — press it to cycle idle → alarm → setalarm →
setclock → volume. A turn while idle is ignored by design (§6.6), so the sensitivity slider
appears to do nothing until you are actually setting something.

`unsafe` (§9.6) expires 60 s after the last gated command, so the app arms it **immediately
before** each one rather than trusting the 30 s refresh alone — a background tab has its
timers throttled to a minute or more, and the symptom of getting that wrong is a `motion
home` that works while you watch it and is silently `Denied` when you come back to the tab.

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

## Tests

[`tests/`](tests/) drives this page in a real browser against a real `clocksim` — clicks the
buttons, scrolls the knob, drags the hands — and checks what the dial says afterwards.

```sh
cd ux/tests && npm install && npx playwright test
npx playwright test --headed --workers=1        # watch it
```

They are firmware tests wearing a page: no test opens a socket, nothing reaches into
`app.js`, and every assertion reads pixels that arrived in a `state` frame. That is the only
way to catch the things that live *between* the two halves — a click that never became a
press, a pixel lit for one tick, a `stop` that stopped rather more than it was asked to.
[`tests/README.md`](tests/README.md) has the case list and the rules.

## Protocol

In [`firmware/apps/clocksim/README.md`](../firmware/apps/clocksim/README.md). Short version:
lines of CLI text one way, newline-delimited JSON the other, and `nc 127.0.0.1 4747` is a
perfectly good client.
