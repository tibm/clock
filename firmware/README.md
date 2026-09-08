# firmware/

ESP32-S3 firmware for the wooden smart clock. **Design doc: [`../FIRMWARE.md`](../FIRMWARE.md)** —
it is the source of truth; this file is only how to build.

Status: **early**. Logging, the CLI registry, both build systems, the fake HAL and the first
three active objects (`motion`, `chrono`, `ui`) are real and tested. `audio`, `storage`,
`board`, `net`, `supervisor`, the drivers and the ESP-side HAL are still directories with
READMEs.

## First time

```sh
tools/idf-setup.sh          # clones + installs the ESP-IDF pinned in toolchain.lock
```

Installs into `~/esp/esp-idf-<tag>` (one directory per tag, so versions coexist and a
rollback is a `toolchain.lock` edit, not a re-download). On macOS it also installs
`python@3.13`, because IDF 5.5's `detect_python.sh` takes the first `python3` on PATH and is
not tested above 3.13.

## Formatting

`clang-format`, Google style with four deviations (4-space indent, 100 cols, left-bound
pointers, include blocks preserved) — all of it in [`.clang-format`](.clang-format).

```sh
tools/format.sh                 # rewrite every C++ file under firmware/
tools/format.sh --check         # exit 1 + list what is unformatted
tools/format.sh a.cpp b.hpp     # just these
```

Needs `brew install llvm` (keg-only, so the script looks it up rather than trusting PATH;
`$CLANG_FORMAT` overrides). The major is pinned in `toolchain.lock` because clang-format's
output drifts between releases — a mismatch warns, it does not fail.

Two things run it for you:

- **VS Code** — format-on-save for `.c/.cpp/.h/.hpp` via `.vscode/settings.json`. Needs the
  `ms-vscode.cpptools` extension (it is in `.vscode/extensions.json`, so VS Code offers it).
- **pre-commit hook** — formats and re-stages the staged C++. It ships in `.githooks/`, which
  git does not read on its own; **once per clone**:

  ```sh
  git config core.hooksPath .githooks
  ```

  A *partially* staged file is reported and left alone — reformatting the working tree and
  re-adding it would drag the unstaged hunks into the commit. `--no-verify` skips the hook.

## Target build

```sh
tools/build.sh                             # dev / devkit / build
tools/build.sh dev devkit flash monitor
tools/build.sh release rev0_3 build
tools/build.sh dev devkit-uart flash monitor   # UART console, for chasing a boot crash
```

`PROFILE` ∈ `dev|release`, `BOARD` ∈ `devkit|devkit-uart|rev0_3`. They compose
`sdkconfig.defaults` + fragments; `sdkconfig` itself is git-ignored so a hand-edited
menuconfig can never be committed. Build dirs are `build/<profile>-<board>/`.

### Name the port on macOS — auto-detect picks the wrong one

Every Mac has a built-in `/dev/cu.debug-console`, and `idf.py`'s port scan takes it in
preference to the board. It then fails the way a dead board would:

```
Serial port /dev/cu.debug-console
Failed to get PID of a device on /dev/cu.debug-console, using standard reset sequence.
/dev/cu.debug-console failed to connect: ... No serial data received.
```

Nothing is wrong with the board. Pass the port — anything after `PROFILE BOARD` goes
straight through to `idf.py`, and `-p` covers `flash` and `monitor` in one invocation:

```sh
tools/build.sh dev rev0_3 -p /dev/cu.usbmodem101 flash monitor
```

`export ESPPORT=/dev/cu.usbmodem101` does the same for a whole session. Don't put it in a
shell profile — the number changes with the USB port and with re-enumeration. Find the
current one with `ls /dev/cu.usbmodem*`.

**The board needs its own 5 V while you do this.** USB VBUS reaches the LT3652 and nothing
else, and the charger idles below 11.2 V, so a Mac's 5 V port powers no rail: `J1` is data
only. Inject 5 V at `J12` pins 1/3 — see `../FIRMWARE.md` §12.0.3.

## Host build — tests and `clocksim`

Needs only cmake + ninja. No IDF, no hardware.

```sh
cmake --preset host-dev
cmake --build --preset host-dev
ctest  --preset host-dev
./build/host-dev/apps/clocksim/clocksim            # console + the ux/ bridge on 4747
./build/host-dev/apps/clocksim/clocksim --no-ui    # console only
```

`host-asan` is the same with ASan + UBSan, `host-tsan` with ThreadSanitizer — worth running
whenever you touch `core/ao`, `core/port` or the fake HAL's locking, since the active objects
are the only real concurrency in the host build.

## Seeing it

```sh
python3 ../ux/uxapp.py        # opens http://127.0.0.1:8787 and attaches to clocksim
```

The plate, both hands, the seven pixels, the wake light and the speaker, plus controls for
the knob, the rear radio toggle, tap and power. It is a display and an input device — it
holds no clock logic, and everything it sends is a line of CLI text you could have typed.
See [`../ux/README.md`](../ux/README.md) and
[`apps/clocksim/README.md`](apps/clocksim/README.md) for the protocol.

## What works right now

```
$ ./build/host-dev/apps/clocksim/clocksim
clock-sim 0.1.0  (hal=fake, board=host, profile=dev)  type `help`
> sys debug drv.* debug          # per-module log levels, globs, prefixes
10 modules -> debug
> unsafe on
> ui led bell red                # chain pos 3 = index 2; dial is 0-1
pixel 2  r=255 g=0 b=0 w=0
> sim hand h 137                 # reach in and move the hands: the firmware is not told
> sim hand m 41
> motion home                    # the real homing FSM, against the real fake mechanism
motion: home: sweeping the minute hand for the index
motion: home: minute zero confirmed, coarse was off by -12 usteps
motion: home: minute parked, sweeping the hour hand
motion: home: hour zero confirmed, coarse was off by -8 usteps
motion: homed in 8694 ms of sim time
> chrono time set 07:38          # and now the hands follow the clock
> sensor homing stream 100 5 --csv > opto.csv
> sim unplug
> ui wake 40 10
denied: wake light is plugged-only (12 V boost gated on PD_PG)
```

The same commands run over USB-CDC on the target — `dispatch()` and the `CmdSpec` table are
shared, only the console front-end and the HAL implementation differ. `sim` is host-only.

**On target the HAL is stubbed**: `clk_hal/esp` returns `NotPresent` for every peripheral, which
is also the correct answer on a devkit until you wire something up. Filling it in is the day-one
bring-up task; `FIRMWARE.md` §12.0.2 lists the order.

## Layout

See [`../FIRMWARE.md` §2](../FIRMWARE.md). Short version: `components/{core,command,board_cfg,
clk_hal,cli}` are implemented; `components/{domain,drivers,services,transport}` are placeholders
with a README each explaining what lands there and what it may depend on.

`clk_hal`, not `hal` — ESP-IDF ships its own `hal` component and IDF component names are a
single flat namespace, so a directory called `hal/` here silently shadows `hal/gpio_types.h`
and every SDK header that includes it.

Dependency violations fail the build — the IDF `REQUIRES` graph is the enforcement mechanism,
not a convention.
