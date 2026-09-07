# PCSX2-RR vs flycast-dojo-7 — and what we adopted (2026-09-03)

| | PCSX2-RR (v2.0/v3.0, 2017; 0.9.6 by inference) | flycast-dojo-7 |
|---|---|---|
| Movie storage | One `.p2m2`, fixed 12-byte stride, random-access `fseek` by frame, written **in place** | In-memory `session_inputs` (frame → packet map) mirrored to an **append-only** `.flyr`, last-write-wins on load |
| What a savestate carries | `g_FrameCount` only (4 bytes in the state's internals) | Sidecar `{frame, seq, movieLen, prefixHash}`; nothing in the state |
| Load in write/record mode | Counter reset; next frames OVERWRITE blocks; `MaxFrame` never shrinks; stale tail persists | **Same data model** (seek + overwrite in place; tail persists) — ADOPTED as v2.0+ parity (`f09a847`). The un-reached old take is **greyed amber** in the piano roll; PCSX2 has no roll, so there it is invisible |
| Load in read-only | Seek only; log untouched | Same |
| Re-record counter | `UndoCount++` on **every** state load, any mode (even in playback, even on plugin-apply) | Bumps at the first byte-**differing** write after a load/R (a divergence run = 1); a load alone bumps nothing — **open** whether to switch |
| State/movie consistency | None — no hash, no seq, no filename; a stale state silently resumes the old tail | prefix-hash + seq; loading a stale state **warns** ("STALE state") and proceeds — kept as our safety over the reference |
| Truncation on load | v1.1: at the R toggle. v2.0+: **none** | Was added (`60663fa`) then **removed** (`f09a847`) to match v2.0+ |
| Deleting stale states | Never | Auto-purge existed; now **default OFF** (`71ea209`) to match |
| Slot 0 / base | No special slot; 10 identical | No special slot at the timeline level; a UI-protected BASE bookmark (F1-hold, never-deletable, R-at-frontier seek target) — kept |
| Movie start | No anchor; New Record at frame N>0 zero-fills 0..N-1 = "every button pressed" hazard | Explicit State 0 anchor — a real improvement; the zero-fill hazard cannot happen |
| Undo | One `_backup` copy at New Record | Edit undo stack (Ctrl+Z) |
| New movie | `wb+` = file truncated to empty | Record Movie boot clears `session_inputs`, frame 0 |

## What "PCSX2 semantics" means for us now
- **WRITE + F3:** no truncate; re-recording overwrites in place; the amber tail is the old take you have not
  reached yet, and it recedes one frame at a time as you record.
- **READ-WRITE:** the roll drives, overdub keeps cells — a fork extension with no PCSX2 analog.
- **READ:** seek only.
- **The piano roll IS the movie.** There is no separate internal history; the `.flyr` is the on-disk copy.
  The only thing the roll does not show is the edit history (undo stack).

## Retracted
The hypothesis that PCSX2-RR "embeds the movie in each savestate" (making states self-contained branches) was
**wrong**. Later states stay loadable there only because the file never shrinks and nothing validates — an
undetected desync, not a branch. The "per-state movie prefix / branching" restructure is therefore not a parity
goal.

## Still open (see `WRITE_MODE_RESTRUCTURE.md` §5)
- Re-record-count semantics: PCSX2 counts loads; we count divergence runs.
- Behavior when a locked range sits above a load point.
- The Demul comparison (David to drive).
- Two latent leaks in OUR fork (not PCSX2's): `locked_slots`/`base_prelock` are not cleared in `Dojo::Reset()`;
  `rewind_log.clear()` only runs inside `if (j.contains("stats"))` in `BeginClipStats`, so a brand-new clip with no
  `clip.json` inherits the previous clip's rewind log in the same process.
