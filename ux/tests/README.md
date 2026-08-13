# `ux/tests/` — the page, driven for real

Fifty-two cases that click the actual page in an actual browser, against an actual
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
npx playwright test         # all of it, ~2.5 min
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
| `02-knob-press` | press cycles idle → alarm → setalarm → setclock → volume, each lighting its own status pixel; long press; the 5 s timeout; a turn in idle is ignored |
| `03-homing` | `home` from a scrambled dial, from a hand parked on the index, from both hands on it; twice over; the fault when a sweep outruns the sensor; `stop` mid-run |
| `04-set-time` | every preset button, `now`, and a typed time put the hands where that time is — 12:30 included |
| `05-follow-release` | released, the clock runs on and the hands do not; the buttons report the firmware and not the last click |
| `06-knob-edit` | one detent is one minute, the hands preview what you are setting, a long press commits, the sensitivity slider changes the ratio |
| `07-power-wake` | the plug toggle, two fast clicks being two flips, the wake light refusing on battery, the low-cell pixel |
| `08-radio-tap` | the rear toggle's polarity and label; a tap lit long enough to see |
| `09-warp-steps` | warp really warps; `jumps per minute` quantises the hands without touching the clock; the tuning sliders reach `motion tune` |
| `10-reset-reboot` | `reset fakes` opens a gap the firmware does not know about; `reboot` loses everything and the page reconnects; a reload loses nothing |
| `11-dial` | dragging a hand does **not** round-trip; the opto meter; the plate turning with yaw; PCNT counts |

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
await ux.pixel(2);                     // { r, g, b, lit } off the swatch strip
await ux.clock();                      // { h, m, s, paused } off the pill
await ux.expectDialShowsTime();        // does the dial agree with the clock?
```

Two habits worth keeping:

- **Assert with `expect(locator)` or `expect.poll`, not a bare snapshot.** Everything on this
  page arrives one state frame late at best; a plain read races it. The one place this bit
  hard was a helper that chose its next knob press from a mode pill that had not caught up
  yet — it overshot, edited the wrong thing, and looked exactly like a firmware fault.
- **Allow for the homing offset.** A homed hand sits ~1.75° short of where the firmware
  believes it is, because the rising edge of the index mark is half a mark before its centre.
  It is systematic, identical on both hands, and is what a `motion zero` trim will remove on
  the bench. `kHomeOffsetDeg` is the tolerance to use.
