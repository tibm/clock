# `ux/tests/` — the page, driven for real

Seventy-six cases that click the actual page in an actual browser, against an actual
`clocksim`, and assert on what the dial shows afterwards. They exist to test the *firmware*
through the surface a person uses, so the rule they are built on is:

> **Nothing here may make the page say something the firmware did not say.**

Concretely:

- **Every gesture is a real DOM event.** Buttons are clicked, the knob is scrolled, hands are
  dragged, sliders are moved, keys are pressed. There is no `evaluate()` that reaches into
  `app.js`, no synthetic call to a handler, no shortcut.
- **Every assertion reads rendered DOM** — pill text, an SVG `rotate()`, a swatch's
  background colour. All of it is written by `onState()`, which runs off a `state` frame.
- **No test opens a socket.** `harness.js` spawns the two processes and never speaks to
  either; the only channel is the browser.

So a red test means the *product* is wrong somewhere between the click and the pixel, and the
job is to work out where — the page, the bridge, or the firmware. Five of the bugs found this
way were in the firmware and one was in the fake HAL; see `FIRMWARE.md` §11.3.

## Run it

```sh
cd ux/tests
npm install                 # once -- @playwright/test only
npx playwright test         # all of it, ~5 min
npx playwright test 03      # one case file
npx playwright test --headed --workers=1     # watch it happen
npx playwright test --ui                     # or step through it
```

It needs `clocksim` built (`cmake --preset host-dev && cmake --build --preset host-dev`) and
uses the **Chrome that is already installed** rather than downloading a private Chromium —
this is a dev tool for one laptop, and `npx playwright install` is a 150 MB answer to a
question nobody asked.

Every test gets its **own** `clocksim` + `uxapp.py` on ports the harness picks, and kills them
afterwards. No test depends on another having run, and none of them touch the pair you have
open in a browser.

## The cases

| file | what it pins |
|---|---|
| `01-attach` | the page attaches, the pixel names come from `hello`, a fresh boot is dark and unhomed |
| `02-knob-press` | press cycles idle → bell → alarm → clock → volume, each lighting its own status pixel and only that one; long press; the 5 s timeout; a turn in idle is ignored |
| `03-homing` | `home` from a scrambled dial, from a hand parked on the index, from both hands on it; twice over; the fault when a sweep outruns the sensor; `stop` mid-run |
| `04-set-time` | every preset button, `now`, and a typed time put the hands where that time is — 12:30 included |
| `05-follow-release` | released, the clock runs on and the hands do not; the buttons report the firmware and not the last click |
| `06-knob-edit` | one detent is one minute, the hands preview what you are setting, a long press commits, the sensitivity slider changes the ratio |
| `07-power-wake` | the plug toggle, two fast clicks being two flips, the wake light refusing on battery, the low-cell pixel |
| `08-radio-tap` | the rear toggle's polarity and label; a tap lit long enough to see |
| `09-warp-steps` | warp really warps; `jumps per minute` quantises the hands without touching the clock; the tuning sliders reach `motion tune` |
| `10-reset-reboot` | `reset fakes` opens a gap the firmware does not know about; `reboot` loses everything and the page reconnects; a reload loses nothing |
| `11-dial` | dragging a hand does **not** round-trip; the opto meter; the plate turning with yaw; PCNT counts |
| `12-modes` | the UX itself: what each mode's pixel *does* (breathe / steady / a burst of three), what the hands show in each (the 6, the alarm, the time being set, the volume gauge — swept, never across the off-scale 10-to-12), where `clock` opens from, the network lock, the ten-second hold into pairing and the one five-second timeout |
| `13-wind` | winding a time: two full turns of the hour hand in each direction, sampling the minute hand every 10 ms — it must never once go backwards, and it must travel all twenty-four of its own revolutions. Then the same for a knob that is **dragged** rather than stepped, which is the case a finger produces and a stepped test never sees |

## Two things that look like cheating and are not

**The CLI box.** Some tests type `sim hand m 41` into the page's own command box to place a
hand at an exact angle. That is a control on the page, the line goes through the same
dispatcher every button uses, and the firmware cannot tell it apart from the *scramble*
button — which does the same thing with a random angle. It is used to arrange a starting
position, never to assert one.

**`sim hand` itself.** Moving a hand is reaching into the case: it changes where the hand
*is*, not what the firmware *thinks*. That gap is the input to homing, which is the whole
point. `11-dial` asserts explicitly that it does not round-trip.

## Writing another one

`harness.js` gives each test a `ux` object:

```js
await ux.home();                       // click home, wait for the movement's own answer
await ux.press(120);                   // press & hold, for this many ms
await ux.toMode('setalarm');           // press until the HSM is there
await ux.turn(5);                      // five detents on the knob
await ux.dragHand('m', 200);           // reach in and move a hand (yaw-aware)
await ux.slide('r-vmax', 12000);       // move a slider
await ux.cli('sim warp 60');           // the page's command box

await ux.hands();                      // { h, m } degrees, off the SVG
await ux.usteps();                     // what the firmware thinks it commanded
await ux.resting();                    // ... once they have actually stopped
await ux.startHandWatch();             // then wind, then:
await ux.stopHandWatch();              // which WAY they went -- min/max step, net, angles
await ux.pixel(2);                     // { r, g, b, lit } off the swatch strip
await ux.watch(2, 1500);               // what that pixel DID over 1.5 s (see below)
await ux.watchMany([2,3,4,5,6], 2400); // ... all of them at once, + `identical`
await ux.expectRowDark();              // the whole row out, once the fade has finished
await ux.clock();                      // { h, m, s, paused } off the pill
await ux.expectDialShowsTime();        // does the dial agree with the clock?
```

**Read a pattern, not a pixel.** Every status LED animates now, so `pixel(i)` is a coin
toss: a breathing pixel is genuinely dark twice a cycle and a blinking one is dark most of
the time. `watch()` samples the same swatch — rendered DOM, written only by a state frame —
every 10 ms and reports what it *did*:

| | |
|---|---|
| `peak` | the brightest sample — is it lit at all, and what colour |
| `levels` | how many distinct non-zero values: `1` = square edges, many = a curve |
| `everDark` / `everLit` | did it reach zero / did it light at all |
| `duty` | fraction of samples lit |

So `levels === 1 && everDark` is a **blink**, `levels > 4 && everDark` is a **breath**, and
`levels === 1 && !everDark` is **steady** — the distinction the spec makes, and the one a
single read cannot see. **Watch a breath for most of its cycle**: a 1.2 s window on a 3.2 s
breath can land entirely inside the dark half, and then `peak` is a number about nothing.
No mode blinks any more (§6.6b), so every window here is at least 2.2 s.

`watchMany` samples several pixels in the same window and adds
`identical`, which is the strong form of "in sync": at every sample, all of them agreed.
Sampling five synchronised breaths one after another proves nothing, because each window
lands somewhere else in the cycle.

**Assert on `#m-pos`, not on an angle, when the question is *which way*.** 350° → 10° is +20
or −340 and nothing on the dial distinguishes them; `#m-pos` is the unwrapped microstep count
the firmware commanded, so a sequence of those settles it. `startHandWatch()`/`stopHandWatch()`
sample it every 10 ms and report the most negative single step, the most positive, the net
travel, and every sample as a dial angle — which is how `13-wind` proves a hand never reversed
and how `12-modes` proves the volume gauge never entered the sector that is off its scale.
`resting()` is the other half of that: it reads the position and `motion idle` in **one**
evaluate, because two reads can straddle a state frame and a start position taken six
microsteps before the hand stopped makes every step measured from it wrong.

Two more things that are true of every mode assertion:

- **Exclusivity arrives, it is not instant.** Leaving a mode *fades* its pixel over ~250 ms,
  so for that quarter second two pixels are legitimately lit. Poll for the row to settle.
- **A press decides on release** (except the ten-second hold, which commits while the knob
  is still down) — so `press(1000)` does not change the mode until a second has passed.

Two habits worth keeping:

- **Assert with `expect(locator)` or `expect.poll`, not a bare snapshot.** Everything on this
  page arrives one state frame late at best; a plain read races it. The one place this bit
  hard was a helper that chose its next knob press from a mode pill that had not caught up
  yet — it overshot, edited the wrong thing, and looked exactly like a firmware fault.
- **Allow for the homing offset.** A homed hand sits ~1.75° short of where the firmware
  believes it is, because the rising edge of the index mark is half a mark before its centre.
  It is systematic, identical on both hands, and is what a `motion zero` trim will remove on
  the bench. `kHomeOffsetDeg` is the tolerance to use.
