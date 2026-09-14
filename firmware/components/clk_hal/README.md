# hal/  — thin RAII wrappers over peripherals

`api/` **declares**; `esp/`, `host/` and `shared/` **define**. CMake picks the backend; a driver
cannot discover which it got, and there is no `#ifdef` in driver code. That is what keeps
`clocksim` honest.

`api/`    the surface — `hal.hpp` plus one header per I2C part
`esp/`    the IDF implementations — one of only two places `esp_*.h` may be included
`host/`   fakes, scriptable from the `sim` command group (§9.5)
`shared/` compiled into BOTH backends: the code that is the same either side of the line, so
          there is one copy to get right and the host tests exercise what the board runs (§11.2)

`shared/` today: the four I2C device drivers (`mcp23017`, `tsl2591`, `bme688`, `bno085`) — on
target they drive silicon, on the host the register models in `host/src/model_*.cpp` behind the
fake `hal::i2c` — and `power.cpp`, which is not a device driver at all. It is there because
`hal::power` turned out to be nothing but arithmetic over `hal::adc` and `hal::expander`: the
SoC endpoints, three open-drain inversions, and R-BOARD-3's decision about which node
`VBAT_SENSE` was looking at. Two copies of that is two chances for the host and the target to
disagree about whether the clock is charging.
