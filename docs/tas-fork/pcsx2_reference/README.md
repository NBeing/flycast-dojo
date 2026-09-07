# PCSX2-RR reference — what the source actually does

**We have the source on disk:** `C:\_mvc2\other\pcsx2-1.4.0-rr` — pocokhc's PCSX2 1.4.0-rr
(github.com/pocokhc/pcsx2-1.4.0-rr), a Japanese TAS fork, Jan–Apr 2017. 64 commits, tags `v1.0` (2017-01-07),
`v1.1`, `v1.1a`, `v1.2` (01-11), `v2.0` (01-22), `v3.0` (02-05). The recording code is **`pcsx2/TAS/`**
(`KeyMovie.cpp/.h`, `KeyMovieOnFile.cpp/.h`, `MovieControle`, `PadData`, `KeyEditor`); upstream hooks are marked
`//--TAS--//` (SIO hook `pcsx2/Sio.cpp:179`, savestate hook `pcsx2/SaveState.cpp:239`, VSync hooks
`pcsx2/Counters.cpp:513-521`).

**Version gap:** David's running binary is PCSX2 **0.9.6** (compiled 2013-11-19, savestate version `0x8b400004`) —
that is the OLD code.google `pcsx2-rr`. This repo has **no history back to it** (it starts at a pristine 1.4.0
snapshot, commit `97feab2`). The old `.p2m` format survives only in a converter comment
(`KeyMovieOnFile.cpp:261-264`) and implies the same model. Treat 1.4.0-rr as the reference and flag anything
version-specific.

## Files
- [`savestate_and_movie.md`](savestate_and_movie.md) — what a savestate carries, what a state load does in RECORD
  vs READ-ONLY, slot semantics, the frame counter.
- [`movie_file_and_rerecord.md`](movie_file_and_rerecord.md) — the `.p2m2` layout, how recording writes, the
  truncation history, the re-record counter, modes/controls, the movie start point.
- [`vs_flycast_dojo.md`](vs_flycast_dojo.md) — side-by-side with our fork, what we adopted, what we retracted.

## The one-paragraph answer
A PCSX2-RR savestate embeds **only the frame counter**. The movie is **one random-access frame table** rewritten
**in place**; its end (`MaxFrame`) only ever rises in v2.0+. A state load in RECORD mode **resets the counter and
overwrites blocks as you re-record** — the un-reached old take stays on disk (v1.1 truncated at the R toggle;
v2.0+ dropped that). A READ-ONLY load **seeks and keeps playing**. The re-record counter bumps on **every load**.
There is **no slot 0**, **no anchor**, and **no validation** that a state still matches the movie.

Mapped 2026-09-03 by two read-only agents; every claim carries a `file:line` into the tree above. When a
WRITE-mode / replay / savestate question comes up, **read the source — it is on disk; do not infer from behavior.**
