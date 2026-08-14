# domain/  — pure product logic, zero IDF

Everything here is a pure function of its inputs, which is why §11.1 targets it for ≥90 %
host coverage.

| | |
|---|---|
| `hand.hpp` ✅ | Microstep ↔ degrees ↔ time, positive modulo, shortest signed path, and the backlash `approach()` that makes every move finish clockwise (§6.1, §7.2) |
| `anim.hpp` ✅ | Every pattern a light can make — ramp, breathe, blink, flash burst — plus the one `AnimCfg` that times all of them and the one place gamma is applied (§6.6a). `render(anim, cfg, now)` is a pure function, so a 30-minute sunrise ramp is asserted in a microsecond |
| still to come | Alarm scheduler (TZ/DST), the sunrise *colour* curve, DSP biquad + limiter |

Header-only for now — it is all `constexpr`, so there is nothing to link. It still gets a
component so the §2 dependency rule is enforced by CMake rather than by good intentions.

Depends on: `core`, and `hal/api` for `kUstepsPerRev` alone.

**Convention, and it is load-bearing:** 0 microsteps = 12 o'clock, increasing **clockwise**;
the homing index sits at 0. Positions are unwrapped `int32` so "go the long way round" is
expressible — `normalise()` is the one place that knows about the modulo.
