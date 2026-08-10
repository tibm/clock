# hal/  — thin RAII wrappers over peripherals

`api/` **declares**, `esp/` and `host/` **define**. CMake picks one; a driver cannot discover
which it got, and there is no `#ifdef` in driver code. That is what keeps `clocksim` honest.

`api/`  Mcpwm Gptimer I2cBus I2sTx SpiBus Adc Pcnt LedStrip Ledc Gpio Nvs UsbConsole
`esp/`  the IDF implementations — one of only two places `esp_*.h` may be included
`host/` fakes, scriptable from the `sim` command group (§9.5)
