# firmware/

ESP32-S3 firmware for the wooden smart clock. **Design doc: [`../FIRMWARE.md`](../FIRMWARE.md)** —
it is the source of truth; this file is only how to build.

Status: **scaffold**. Logging, the CLI registry and both build systems are real and tested.
The nine active objects, the HAL and the drivers are directories with READMEs.

## First time

```sh
tools/idf-setup.sh          # clones + installs the ESP-IDF pinned in toolchain.lock
```

Installs into `~/esp/esp-idf-<tag>` (one directory per tag, so versions coexist and a
rollback is a `toolchain.lock` edit, not a re-download). On macOS it also installs
`python@3.13`, because IDF 5.5's `detect_python.sh` takes the first `python3` on PATH and is
not tested above 3.13.

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

## Host build — tests and `clocksim`

Needs only cmake + ninja. No IDF, no hardware.

```sh
cmake --preset host-dev
cmake --build --preset host-dev
ctest  --preset host-dev
./build/host-dev/apps/clocksim/clocksim
```

`host-asan` is the same with ASan + UBSan.

## What works right now

```
$ ./build/host-dev/apps/clocksim/clocksim
clock-sim 0.1.0  (hal=fake, board=host, profile=dev)  type `help`
> sys debug drv.* debug          # per-module log levels, globs, prefixes
10 modules -> debug
> unsafe on
> ui led bell red                # chain pos 3 = index 2; dial is 0-1
pixel 2  r=255 g=0 b=0 w=0
> sim opto 0.42                  # drive the fake homing sensor
> sensor homing read
homing  mv=1376 norm=0.420
> sim noise 30
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
