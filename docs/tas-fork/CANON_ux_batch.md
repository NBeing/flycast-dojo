# UX batch — canonical request list (2026-08-28)

The contract for this round of changes. Order of work: **canon → delegate (3 agents) → scrutiny
review → align work-vs-requests → integrate serially → build+test → commit by slice.**
Northstar unchanged: `.\test.ps1` stays `1 tested, 0 failed`.

## Decisions locked with David
- **Reload stays allowed.** You want to reload savestates to see where the clip is. Locking a state
  does **not** block loads. It blocks **writes** (input-sending + piano-roll edits) into the locked
  range, greys that content out in the piano roll, and shows a warning when a write is blocked.
- **Header mode = the button is the indicator.** Drop the `[PROTECT]` badge in the piano-roll header;
  the PROTECT/RECORD toggle button itself shows the mode (blue protected / red recording). The Input
  Sender keeps its own always-visible status row instead.

## The 14 requests
| # | Request |
|---|---------|
| R1 | Shorten the PROTECT (and RECORD) button tooltip in the piano-roll header — "outdated and could be shorter". |
| R2 | Remove the "Lock States" button from the piano-roll header — locking moves entirely into the Timeline. |
| R3 | Timeline: the bank-of-ten savestate slot numbers (`[0] 1 2 …`) become clickable to toggle a per-state lock. |
| R4 | Timeline: a spacing row of little lock/unlock icons, one per slot, under the numbers. |
| R5 | Locking a state locks the input range `[frame(state), frame(next state))` (last → movie end). |
| R6 | Piano roll greys out the locked-range content (timeline ↔ piano-roll share the locked set + state frames). |
| R7 | Locked ranges are write-protected from every source (controller, → Live/SEND, LIVE AUTO, paint, verbs): they may drive the guest but don't record into locked cells; a warning surfaces when a write is blocked. |
| R8 | Decouple lock from reload — reloading is always allowed; locked inputs just replay. Removes `states_locked` + its reload gate. |
| R9 | Piano-roll header: drop the redundant `[PROTECT]` badge; the toggle button is the sole indicator. |
| R10 | Input Sender: the mode status row is **static** (always visible), reflecting RECORD vs PROTECT — not only shown in PROTECT. |
| R11 | Input Sender: SEND (→ Live) gets its **own row** (verbs on one row, SEND below). |
| R12 | Input Sender: `3 fr` → `3 FRAMES` (FRAMES in caps). |
| R13 | SEND is cancelable while SENDING (click = `stopLive`). Verify. |
| R14 | SEND auto-cancels on savestate reload (`gui_loadState` → `tas_auto::stopLive()`). |

## Slices (delegation)
- **A — timeline-lock:** R2,R3,R4,R5,R6,R7,R8 (the big one).
- **B — header:** R1,R9.
- **C — input-sender:** R10,R11,R12,R13,R14.

Contested regions the integrator merges: piano-roll header 7446–7557 (A removes Lock button, B edits
badge/button/tooltips); `gui_loadState` (A removes reload gate, C adds `stopLive`).
