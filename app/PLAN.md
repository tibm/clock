# App — open work

Only what is **not built yet**. When a step is done, delete it here and describe the result in
`README.md` (screens, behaviour). Earlier plans (the app itself, History, the alarm schedule)
are done — they are in `README.md` and in git history (`git log -- app/PLAN.md`).

---

## Logs tab — read the clock's debug journal (firmware done 2026-10-01)

**Why.** "It got stuck overnight": the USB console is never attached when it matters. The clock
writes every log line to its microSD card, one file per boot, and keeps the lines from just
before a crash / watchdog reset. This tab mirrors those files on the phone and shows them.

**Contract** (read these, don't copy numbers from here into Swift):
- `PROTOCOL.md` → "Debug journal" — files, commands, the download, the line format.
- `protocol.json` → `journal` block (`dir`, `name_regex`, `line_regex`, `header_prefix`,
  `rescued_prefix`, `part_bytes`) and the `sys journal …` commands in `commands`.
- The download is the **same mechanism as History**: `bulk` notifications (4-byte LE offset +
  data), `=from=` `=size=` `=crc=` (CRC-32/zlib of exactly `[from, size)`), one download at a
  time, no other commands while it runs. Reuse `BulkAssembler`, the CRC-32, and `ClockLink`'s
  `beginDownload()/endDownload()` queue hold.

### What the clock does (so the sync rules make sense)
- `sys journal files` → one `=file=<name>/<bytes>` per file, **oldest first**, then
  `=boot=<n>` (this boot) and `=current=<name>` (the file still growing). It writes the RAM ring
  to the card first, so `current` is up to date.
- Names: `<boot>.log` (6 digits, e.g. `000123.log`), and when one boot's file reaches
  `part_bytes` (4 MB) it carries on in `<boot>-<part>.log` (`000123-1.log`, `-2`, …).
  **A file only ever grows, and only `current` grows.** (Legacy `<boot>.old` files from
  firmware before 2026-10-01 may still be listed: treat them as any other file.)
- One exception: after a crash, the next boot appends the rescued lines to the **previous**
  boot's last file — so the file before `current` can grow once more, right after a reset.
  The "longer → fetch the tail" rule covers it.
- Old boots are deleted by the clock (32 MB / 64 boots). The phone keeps its copies.
- `sys journal fetch <name> [<from>]` → `=file=` `=from=` `=size=` `=crc=` `$ok`, bytes on
  `bulk`. `failed` = no such file; `bad-arg` = bad name or offset past the end;
  `not-present` = no card. `sys journal fetch stop` (or `log fetch stop`) abandons it.
- `sys journal` → `=` pairs `boot`, `file`, `bytes`, `ram` (bytes not on the card yet),
  `lost` (lines dropped because the ring was full), `files`, `dir_bytes`, `card` (`ok` /
  `none` / the last error) — for the tab's header.

### Build
1. **`Protocol/` (pure, `nonisolated`)**
   - `ProtocolSpec`: optional `journal` block (absent on an older contract → tab hidden).
   - `JournalSync.swift`: parse `=file=` / `=boot=` / `=current=`; the plan, keyed by name:
     not on the phone → from 0; longer on the clock → from the phone's size; **shorter on the
     clock → from 0 and replace** (card swapped, or the boot counter restarted after a flash
     erase); only on the phone → keep. Order: oldest first, `current` last. Generalise
     `HistorySync`'s diff to a string key rather than copying it.
   - `JournalLine.swift`: split into lines (UTF-8, lossy). `line_regex` → level (`E W I D V`),
     ms since boot, tag, text. Lines that don't match (IDF boot banner, panic backtrace) are
     plain text. A line starting with `header_prefix` = a boot header (boot number, reset
     reason — show the text, don't parse more than the reason word after `reset:`). A line
     starting with `rescued_prefix` starts the "before the reset" section.
2. **`BLE/`**
   - `JournalArchive.swift`: `Application Support/Journal/<clock id>/<name>`, atomic writes,
     like `HistoryArchive`.
   - `JournalStore.swift` (`@Observable`, like `HistoryStore`): `sync()` = `sys journal files`
     → plan → `sys journal fetch` per entry, collecting `bulk` → CRC → append/replace.
     **`ClockLink.onBulk` has one owner today (`HistoryStore`)** — route it to whichever store
     started the current download (e.g. `link.bulkSink = …` set in `beginDownload`).
   - When: on opening the tab, on *Refresh*, and with **Follow** on: re-sync every 5 s while the
     tab is visible (only `current` grows, so this is one small tail fetch). Not on every
     connect — History already syncs then. Don't start while a History sync runs.
3. **`Views/LogsView.swift`** — a new tab "Logs" (`doc.text.magnifyingglass`):
   - Header from `sys journal`: boot, card state, ring `lost`.
   - **Boot list**, newest first, grouped by boot (its parts together): boot #, reset reason
     from the header (highlight `panic`, `task wdt`, `int wdt`, `wdt`, `brown-out`), size,
     a "rescued lines" badge when the file contains `rescued_prefix`, "current" badge.
   - **Viewer** (a boot = its parts concatenated): monospaced, lazy, colour by level
     (E red, W orange, D/V secondary). Level chips, tag filter (menu of the tags seen), search,
     **"Jump to before the reset"**, time as `+h:mm:ss.mmm` since boot. Render the last 5000
     lines by default with "Load all"; parse off the main actor, cache by name + size.
   - Toolbar: Refresh, Follow toggle, Share (`ShareLink` of the boot's raw files), and a menu
     sending `sys debug <tag> debug` / `sys debug all info` (the card only has what is logged).
   - Hidden with a sentence when: no `journal` block, no `bulk` (see README "Troubleshooting"
     — usually a stale GATT cache), or `not-present` (no card).
4. **Tests (`clockTests`)**: sync plans (new / grown / shrunk / previous boot grew after a
   reset / pruned on the clock); line parsing of a real excerpt with a header, the rescued
   marker, IDF lines and a non-matching backtrace line; a boot's parts concatenated in order.
5. `README.md`: add the tab to "Screens"; delete this section.

### Verify on hardware
`sys debug motion debug` → wait a few seconds → Refresh: new lines. Reset test: `unsafe on`,
`sys reboot`, reconnect, Refresh: the previous boot's file ends with
`--- the last lines before the reset …`. Speed is the same as History (~20–40 KB/s): a full
4 MB part takes a couple of minutes the first time — show progress.
