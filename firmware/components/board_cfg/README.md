# board_cfg/  — pin map + device presence, zero IDF

One header per board (`board_rev0_3.hpp`, `board_devkit.hpp`, `board_host.hpp`), selected by
`CONFIG_CLOCK_BOARD_*`. D15: **the pin map is identical across boards** — only the set of
devices actually fitted differs, expressed as a presence bitmask. An absent device yields
`Status::NotPresent` (D16), never a faked success.
