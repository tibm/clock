# App — open work

Only what is **not built yet**. When a step is done, delete it here and describe the result in
`README.md` (screens, behaviour). Earlier plans (the app itself, History, the alarm schedule,
the Logs tab) are done — they are in `README.md` and in git history (`git log -- app/PLAN.md`).

---

## Logs tab — verify on hardware (built 2026-10-02, simulator/tests only)

`sys debug motion debug` → wait a few seconds → Refresh: new lines. Reset test: `unsafe on`,
`sys reboot`, reconnect, Refresh: the previous boot's file ends with
`--- the last lines before the reset …` and its row shows *rescued lines*. First sync of a full
4 MB part: progress bar, a couple of minutes. Follow on: the current boot's viewer grows every
~5 s and History still syncs on the next connect.
