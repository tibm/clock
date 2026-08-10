# vendor/  — pinned git submodules

Versions live in `../toolchain.lock`, not here.

    git submodule update --init vendor/sh2 vendor/googletest

`sh2/`        CEVA SH-2/SHTP driver for the BNO085 (§6.5.1) — do not hand-roll this
`googletest/` host tests (§11.1); until it is initialised the tests use `test/host/check.hpp`
`bsec/`       Bosch BSEC 2.x — license-gated, fetched by `tools/fetch-bsec.sh`, may be absent
