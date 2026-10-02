# CLAUDE.md — app/

SwiftUI debug app (iOS 27 + macOS 27, one target) for the clock's BLE link. `README.md` = what it
does and the layout; `PLAN.md` = the open work only (start
there; delete a step from it once it is built and in `README.md`).

## Rules
- **The contract is `PROTOCOL.md` (behaviour) + `protocol.json` (numbers).** Never hard-code a UUID,
  offset, enum, flag, status or command in Swift — read it from `ProtocolSpec`. If the app needs a
  new number, it goes in `protocol.json` first (both files + changelog line — the firmware side
  shares them; `firmware/test/host/test_net.cpp` asserts the golden vector).
- `protocol.json` is bundled **by reference** (`../protocol.json` is in Copy Bundle Resources), not
  copied. The firmware side edits it too — re-read it before changing decoder/catalog code.
- Compatible protocol changes must keep working with **zero Swift changes** (new field → "Other
  fields", new flag → chip, new command → table/autocomplete). Keep it that way.
- `|` response text is display-only — never parse it for state. State comes from the snapshot. The
  one exception is `help` parsing for autocomplete, which is best-effort and must fail soft.
- `Protocol/` is pure and `nonisolated`; UI + `BLE/` are MainActor (project default isolation).
  CoreBluetooth callbacks are on the main queue.
- Async/await + Observation, no Combine. Swift Testing for tests (`clockTests`, hosted in the app;
  the test target has no MainActor default).

## Workflow
- Build: `BuildProject` (iOS sim) and also **My Mac** before committing UI/BLE changes.
- Test: `RunAllTests`. The golden-vector test is the decoder's gate.
- Previews use `ClockLink.preview()` (golden snapshot, fake transcript, no radio).
- BLE itself can only be verified on hardware (iPhone or Mac + a clock) — say so, don't claim it.
- Commit **only files under `app/`**; one commit per feature step.
