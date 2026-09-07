# BizHawk TAS-suite research notes

Distilled from three research passes over the local checkout at `C:\_mvc2\other\BizHawk`
(TAStudio UI, movie format, state management). The *decisions* extracted from this research
live in [ROADMAP.md](ROADMAP.md) — Appendix B (text grammar), Appendix C (deferred ledger).
This file is the fuller reference: where each mechanism lives in the BizHawk tree, how it
works, and what we adopted vs. rejected for flycast-dojo-7. Paths verified against the
checkout on 2026-08-23.

---

## 1. TAStudio / the piano-roll editor

Sources: `src/BizHawk.Client.EmuHawk/tools/TAStudio/` (tool logic),
`src/BizHawk.Client.EmuHawk/CustomControls/InputRoll/` (the grid widget itself).

### InputRoll — the grid

- **Virtualized**: `InputRoll.cs` never materializes rows. It computes the visible range from
  scroll position + row height and asks for cell text/color via callbacks
  (`QueryItemText`, `QueryItemBkColor`). Millions of frames scroll flat.
  → Ours: same idea via `ImGuiListClipper` (already the plan in ROADMAP Appendix C).
- **Cell model** (`Cell.cs`, `RollColumn.cs`): a cell is (row = frame, column = button/axis).
  Columns carry type (boolean vs. axis/float), width, and emulator-agnostic names taken from
  the movie's LogKey — the grid knows nothing about any console.
- **Blank-when-default**: boolean cells draw their mnemonic char only when pressed; axis cells
  draw only when off neutral. The eye scans for ink, not for values.
- **Column-locked drag paint**: mouse-down on a boolean cell latches (column, new value);
  dragging paints *that column only* with *that value* — crossing other columns does nothing.
  Prevents the classic smear-across-columns misedit. Right-drag is a separate "splicer" path.
- **Edit ≠ navigate**: left-click edits cells; clicking the frame-number gutter seeks. Keys:
  Clear (neutralize frame, keep it) vs Delete (remove frame, shift movie) are distinct
  operations with distinct undo entries.
- **Rotation** (`RollColumns.cs` supports horizontal layout): skipped for us — vertical only.

### TAStudio behaviors worth copying

- **Named undo batches** (`TasMovie.History.cs`): every edit funnels through an undo history
  where a drag-paint or multi-frame paste is ONE batch with a display name ("Paint Down
  frames 300–350"), not N single-cell entries.
- **Green-arrow restore anchor** (`TAStudio.ListView.cs` + main form): when you edit in the
  past, TAStudio remembers where you *were* (the green arrow), rewinds you to the edit for
  re-emulation, and offers auto-restore back to the anchor. The anchor is frozen across an
  edit burst — ten rapid edits keep one anchor. This is the single biggest QoL feature for
  combo iteration.
- **Follow-cursor / auto-scroll policies**: the roll can pin the playback cursor to view
  center during playback but stop following during hand-scroll; resumes on seek.
- **Markers** (`TasMovieMarker.cs`): frame + free-text label, shown in a side list, used as
  seek targets. Cheap to add and huge for combo phase labeling ("launcher", "OTG", "DHC").
- **Broken there, do better**: their `AutoAdjustInput` (shifting input when frames are
  inserted/deleted mid-movie) is documented-buggy and half-disabled. We refuse truncation in
  `ApplyEdit` and treat insert/delete as explicit, guarded operations instead.

---

## 2. Movie format — bk2 / tasproj

Sources: `src/BizHawk.Client.Common/movie/bk2/` (`Bk2Movie.*`, `Bk2LogEntryGenerator.cs`,
`StringLogs.cs`), `src/BizHawk.Client.Common/movie/tasproj/` (`TasMovie.*`).

- A `.bk2` is a **zip** holding `Header.txt`, `Comments.txt`, `Subtitles.txt`, and
  `Input Log.txt`. `.tasproj` extends it with TAStudio state (markers, branches, greenzone
  settings, `TasSession`).
- **Input Log**: first line is the LogKey (`LogKey = #P1 Up|P1 Down|...`), then one `|`-piped
  line per frame. `Bk2LogEntryGenerator` renders a controller state to a line; a `.` is the
  only unpressed char, pressed chars are cosmetic on read (position is what matters). Axes
  print as padded ints, comma-terminated.
- **LogKey is the contract**: import rebuilds column order from the FILE header, never from
  the live controller definition. Old files stay replayable after button-map changes. This is
  the single most important thing we copied (our `BOOL_COLS[]`/`AXIS_COLS[]` table +
  self-describing header in `core/dojo/tastext.cpp`).
- **In-memory model**: BizHawk stores the movie AS the text lines (`StringLogs.cs`) and
  re-parses on demand; comparisons are string-equality. We deliberately did NOT copy this —
  our binary `session_inputs` map stays authoritative; text is an authoring/interchange layer
  (ROADMAP Appendix B). We did copy "normalize on ingest → canonical form → cheap equality".
- **Parser laxity — do not copy**: bk2's line parser skips malformed pipe groups silently,
  which mis-assigns every column after the bad group. Our importer errors with a line number
  (strict group validation).
- **Repeat/compression**: bk2 has none (one line per frame, always). Our ` *N` repeat suffix
  is our own addition; in memory log-index == frame-number always holds.
- **MovieZone / macros** (`src/BizHawk.Client.Common/tools/TAStudio/MovieZone.cs`): a saved
  snippet = its own LogKey + a run of input lines, placeable at any frame with two merge
  modes — **Replace** (stomp the range) vs **Overlay** (OR the booleans, take non-neutral
  axes). Overlay is exactly the combo-injection semantic we want later (deferred ledger).

---

## 3. Greenzone / state management / invalidation

Sources: `src/BizHawk.Client.Common/movie/tasproj/ZwinderStateManager.cs` (+ `Settings`,
`IStateManager.cs`, `StateDictionary.cs`; tests in
`BizHawk.Tests.Client.Common/Movie/ZwinderStateManagerTests.cs`).

- **The greenzone** = the set of stored emulator states along the movie; "green" frames are
  reachable instantly. Modern BizHawk uses `ZwinderStateManager`: several ring buffers
  ("zwinders") with different capture cadences —
  - `current`: dense, recent (short rewind),
  - `recent`: sparser, older,
  - `ancient`/gap fillers: sparser still,
  - plus a **never-evicted reserved list** (frame 0, branch points, markers).
  Eviction is by ring overwrite, so memory is strictly bounded; density decays with age.
- **Invalidation** (`IStateManager.InvalidateAfter(frame)` called from
  `TasMovie.Editing.cs`): any input edit at frame F drops ALL stored states with
  frame > F. The editor then re-emulates from the nearest state ≤ F. States are keyed by
  frame and assumed valid *only* because inputs before them are unchanged — the same
  contract our sidecar-v2 + `rewinds` guard expresses, except we mark-stale (grey the slot,
  warn) instead of hard-deleting, because our states are user-owned slots, not a cache.
- **Capture discipline**: capture-on-frame-advance gated by the cadence rules; plus a forced
  capture at F-1 right before an edit at F (so re-emulation has a launch pad), deleting the
  previous forced capture. That trick is in the deferred ledger for if we ever go dense.
- **Scale context**: BizHawk states for 8/16-bit cores are ~100 KB–2 MB, so thousands fit in
  RAM. Our flycast savestates are ~28 MB — dense greenzone is off the table; we are in the
  sparse-skeleton + fast-replay regime by necessity, not preference. If density ever matters:
  interval `K = targetLen × stateSize / budget`, zstd states, coarse never-evict skeleton.
- **Lag log** (`TasLagLog.cs`): BizHawk tracks per-frame "input not polled" (lag) to keep the
  grid honest on consoles with variable polling. MvC2 on DC is fixed-cadence with the skip
  system we measured (ROADMAP Appendix A); we track read-vs-sent lag empirically in the
  fidelity harness instead. Skipped as machinery, kept as a concern.
- **Branches** (`TasBranch.cs`): full snapshots of movie + state + screenshot, a manual
  "save my whole timeline" list. Our generations[] in clip.json covers the movie half;
  branch-style state capture is not planned.

---

## Adopt / reject summary

| BizHawk mechanism | Our verdict | Where |
|---|---|---|
| LogKey self-describing header | **adopted** | `tastext.cpp`, ROADMAP App. B |
| `.` unpressed / positional chars | **adopted** | same |
| Axis comma-termination | **adopted** | same |
| Text as in-memory storage | rejected (binary authoritative) | ROADMAP App. B |
| Silent pipe-group skipping | rejected (strict errors) | `tastext.cpp` |
| One line per frame, no compression | rejected (` *N` repeat) | ROADMAP App. B |
| InvalidateAfter on edit | **adopted as mark-stale** | sidecar v2 + `rewinds`, `IsStateStale` |
| Edit-before-savestate call order | **adopted** | `ApplyEdit` funnel (ROADMAP T6) |
| Virtualized input grid | **adopted (planned)** | piano roll, ROADMAP App. C |
| Column-locked drag paint, Clear≠Delete | **adopted (planned)** | same |
| Named undo batches | **adopted (planned)** | same |
| Green-arrow restore anchor | **adopted (planned)** | same |
| Markers | worth adding cheap | unscheduled |
| MovieZone Replace/Overlay | **adopted (planned)** | snippets, ROADMAP App. C |
| Zwinder dense greenzone | rejected (28 MB states) | ROADMAP App. C |
| Forced capture at F-1 on edit | deferred | ROADMAP App. C |
| Lag-frame machinery | rejected (fixed-cadence fighter) | fidelity harness covers it |
| Branches | rejected (generations[] suffices) | — |
| AutoAdjustInput | rejected (buggy there) | `ApplyEdit` refuses truncation |
| Grid rotation, autofire painting | rejected | — |
