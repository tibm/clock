# transport/  — Command in, Response out

`cli_console/` argtable3 + the CmdSpec table → `Command`  (lives in the `cli` AO)
`ble_gatt/`    TLV frame → `Command`                        (lives in the `net` AO)

Rule: transport talks to `command::dispatch` and nothing else. Never to a service, never to
`hal`. §5, §8.
