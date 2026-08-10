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
> help
groups  sys  help  unsafe
> sys debug motion verbose
1 module -> verbose
> sys debug drv.* debug
10 modules -> debug
> sys stst
unknown command: sys stst
  did you mean:  sys stat
```

The same commands run over USB-CDC on the target — `dispatch()` and the `CmdSpec` table are
shared, only the console front-end differs.

## Layout

See [`../FIRMWARE.md` §2](../FIRMWARE.md). Short version: `components/{core,command,cli}` are
implemented; `components/{domain,board_cfg,hal,drivers,services,transport}` are placeholders
with a README each explaining what lands there and what it may depend on.

Dependency violations fail the build — the IDF `REQUIRES` graph is the enforcement mechanism,
not a convention.
