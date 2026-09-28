# transport/  — the app link's bytes

`frame`     CLI line in (`"<id> <line>"`), CLI output out (`"<id>|text"` … `"<id>$ok"`)
`snapshot`  the 132-byte status record: encode / decode, schema + size, append-only

Rule: no IDF, no services, no radio. The radio is `hal::ble`; the policy (pairing window,
who may run a command, when to notify) is the `net` AO. The CLI line goes to
`cli::dispatch_line_wait` — the app has exactly the console's command set (§5, §8).
