# services/  — the nine active objects (§3.2)

motion(20) audio(18) storage(14) chrono(12) board(11) ui(10) net(6) supervisor(5) cli(3)

All pinned to core 1 except `net`. One owner per peripheral; no AO handler blocks > 2 ms.

| | |
|---|---|
| `motion` ✅ | Trapezoidal profile on a 10 ms control tick, backlash (every move finishes clockwise), absolute targets only — resolved into the hands' own unwrapped frame, the short way for a clock and the way the knob turned for a knob (§6.6e) — and the §6.1 homing FSM: clear → coarse-minute → fine-minute → park-minute → coarse-hour → fine-hour, with a budget that faults rather than sweeping forever. `clear` exists because a hand parked on the index holds the sensor lit and there is then no edge to find at all |
| `chrono` ✅ | Wall clock as an offset from the monotonic base — so `sim warp` warps the clock with it — turned into a `HandTarget` whenever the minute changes. Alarm table, TZ/DST and the SNTP re-home policy still to come |
| `ui` ✅ | README §12's press cycle (bell → alarm → clock → volume → commit), the one 5 s timeout, counts-per-minute plus an acceleration curve, the direction a turn hands to `motion`, and all pixel output. Zero emission when idle is enforced here |
| `audio` `storage` `board` `net` `supervisor` | still to come |

`motion` is the one that carries the interesting bugs, so it is the one with the deepest
tests. Note what is *not* here: commutation. `hal::motor` is a velocity-controlled microstep
axis with a stop target, so the profile and the FSM are identical on the bench and in
`clocksim`.
