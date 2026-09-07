# clip.json — the per-clip metadata contract

Every recording is a **clip folder** under `build/data/replays/<Game>/`, holding its movie, its
savestates, its state backups, and a `clip.json` describing it. The JSON lives *inside* the folder
on purpose: copy, rename or delete the clip and its metadata travels (or dies) with it — there is
no central index to go stale.

This file is the contract between the emulator and external tooling (the VS Code extension).
**It only ever grows.** Fields are added, never renamed or repurposed; `schema` says which
generation you are looking at, so a reader can support old clips forever.

## Example

```json
{
  "schema": 5,
  "game": "NoBGM_VMU",
  "created": "2026-08-22T16_37_41Z",
  "createdUtc": "2026-08-22T16:37:41Z",
  "createdLocal": "08/22/2026 09:37 AM",
  "tags": ["magneto", "rom", "wip"],
  "notes": "ROM into tempest ender; drops at rep 14 if mashed",
  "stats": {
    "frames": 5640,
    "durationSeconds": 94.0,
    "rerecords": 137,
    "editSeconds": 4820,
    "sessions": 3,
    "lastOpenedUtc": "2026-08-22T18:02:11Z",
    "lastOpenedLocal": "08/22/2026 11:02 AM"
  },
  "states": [
    {
      "slot": 0,
      "file": "NoBGM_VMU.state",
      "label": "combo start",
      "movieFrame": 0,
      "bytes": 27793035,
      "savedUtc": "2026-08-22T17:02:26Z",
      "savedLocal": "08/22/2026 10:02 AM",
      "thumb": "NoBGM_VMU.state.png"
    },
    {
      "slot": 4,
      "file": "NoBGM_VMU_4.state",
      "label": "post-DHC",
      "movieFrame": 1446,
      "bytes": 27793035,
      "savedUtc": "2026-08-22T17:14:02Z",
      "savedLocal": "08/22/2026 10:14 AM",
      "thumb": "NoBGM_VMU_4.state.png"
    }
  ]
}
```

## Fields

| Field | Since | Meaning |
|-------|-------|---------|
| `schema` | 2 | Metadata generation. Absent = schema 1. |
| `game` | 1 | ROM stem the clip belongs to. |
| `created` | 1 | Original folder timestamp (UTC, `:` replaced by `_`). Kept for compatibility. |
| `createdUtc` | 2 | Canonical ISO-8601 UTC. **Sort on this.** |
| `createdLocal` | 2 | Human string, US format with AM/PM. **Display this** — survives renaming. |
| `tags` | 1 | Freeform strings. Characters, status (`wip`/`final`), combo type — the reader owns the vocabulary. |
| `notes` | 1 | Free text. |
| `stats.frames` | 2 | Movie length in frames (60 fps) — the movie's ACTUAL length. Before schema 4 this was a high-water mark of the frame counter, which over-reported when a state saved past the end was loaded. |
| `stats.slotCycle` | 4 | How many slots F2 was cycling at the time of writing (see `generations[].slotCycle` for per-backup history). |
| `generations[]` | 4 | One APPENDED entry per F8 state backup, never rewritten — generations are immutable. |
| `generations[].gen` / `.name` | 4 | Number and folder name (`<clip>_gen_NN`). |
| `generations[].files` / `.bytes` | 4 | What the backup copied. |
| `generations[].slotCycle` | 4 | The F2 cycle setting **at the time of that backup**. It can change between generations, so this is per-entry rather than global. |
| `generations[].slots` | 4 | Exactly which slots that generation captured. Empty slots write nothing, so this is the real shape, not a range. |
| `generations[].createdUtc` / `.createdLocal` | 4 | When the backup was taken. |
| `rewinds` | 5 | Append-only `[seq, frame]` pairs — one per re-record rewind. **The dead-timeline guard**: a state with sidecar seq `S` at frame `F` is STALE iff some `[seq > S, frame < F]` exists — the movie was re-recorded from below it after it was saved, so its machine belongs to an abandoned branch. The emulator warns on load and badges stale slots in the F4 browser; a reader can compute the same verdict offline. |
| `stats.durationSeconds` | 2 | `frames / 60`, precomputed. |
| `stats.rerecords` | 2 | State loads while recording — PCSX2-rr's re-record count; the effort metric. |
| `stats.editSeconds` | 2 | Wall-clock seconds spent with this clip open, accumulated across sessions. |
| `stats.sessions` | 2 | How many times the clip has been opened. |
| `generations[].kind` | 6 | `gen` (Movie clip backup, folder `<clip>_gen_NN`) or `setup` (macro clip backup, `<clip>_setup_NN`). Absent = `gen`. Setups are recorded from schema 6 on; older ones are synthesized by reconciliation. |
| `generations[].atFrame` / `.movieFrames` / `.rerecords` / `.mode` | 6 | The live facts when the backup was taken: playhead frame, the movie's actual length, the re-record count, `movie` or `macro`. |
| `generations[].slotFrames` | 6 | `[slot, movieFrame]` pairs for every captured state - which frame each state in the copy anchors on (branching groundwork). |
| `generations[].tags` / `.notes` | 6 | Per-backup user fields, edited in the shared GENERATIONS pane (Replays / Play Macro / the States window) - click-to-edit Tags / Notes cells. The only part of an entry meant to change. |
| `generations[].present` | 6 | Reconciliation verdict: the folder exists on disk. An entry whose folder was deleted stays, flagged `false`. |
| `generations[].recovered` | 6 | The entry was synthesized from a folder that had no record (its date is the folder's write time). |
| `generations[].backfilled` | 6 | Some facts of this entry were recovered by reconciliation from the clip.json snapshot inside the backup folder (`stats.frames` -> `movieFrames`, `stats.rerecords` -> `rerecords`, `mode`, `states[]` -> `slotFrames` / `slots`; the .flyr length when the snapshot has no stats). `atFrame` cannot be recovered. Only absent fields are filled. **Maintenance rule:** when `RecordGeneration` gains a new fact, add its derivation to `tas_clip.cpp` `backfillFromFolder` so existing backups catch up on their next reconcile. |
| `generationCount` / `latestGeneration` | 6 | Present backups on disk, and the name of the newest one by `createdUtc`. |
| `contents` | 6 | Manifest of the LIVE set at the top of the folder, refreshed on every write: `files`, `bytes`, `movie`, `macro` (macro clips), `audioEnv`, `skipMap`, `states`, `generationFolders`, `updatedUtc`. |
| `restoredFrom` | 6 | Written by a restore (Replays > Replace live with this backup): `generation`, `utc`, `local`, `filesCopied`, `trashedStateFiles`. A restore copies every backup file except clip.json, moves live-only state files (and sidecars) to `<clip>/.trash/<utc>/`, and MERGES clip.json: `stats.frames` / `durationSeconds`, `rewinds`, `bookmarks`, `states`, `macroBase` / `macroHasState0` / `macroPairState` come from the backup; `stats.rerecords` becomes the max of both (the sequence clock never runs backwards); `generations[]`, tags, notes, `contents` and identity stay live. Live is always backed up first (that backup is tagged `auto, pre-restore`). A clip open in a running session is never restored. `editedSince` (bool, sticky, 2026-09-04): stamped by `WriteClipStats` once the live files changed after the restore - a rewind / edit (`rerecord_count`), a state written to the live folder, or the movie's length changed since the clip opened; a new restore writes a fresh record without it. The UI reads it as `LIVE*` / "edited since". A reconcile writes clip.json only when something on disk changed. |

**One generation system (2026-09-04).** Every F8 generation is a numbered `<clip>_gen_NN` whatever the session mode; `setup` is a *tag* the user puts on a generation (the F8 prompt offers the chip vocabulary from `dojo:GenTags`, default `setup, good, bad, use, versionA, versionB`), not a second kind. Folders named `<clip>_setup_NN` from before this date still list (`kind: setup`) and interleave with gens: `generations[]` is ordered by `createdUtc`, then number. A restore asks one question, *Back up live before proceeding?* - YES backs live up as a new gen tagged `auto, pre-restore` first, NO restores without one, CANCEL does nothing.

**Schema 6 and immutability.** A generation's backup facts (`gen`, `kind`, `name`, `createdUtc`, `slots`, `slotFrames`, `atFrame`, `movieFrames`, `rerecords`, `mode`) never change. Reconciliation (`Dojo::ReconcileGenerations`, run on session open, after every F8 and when the Generations popup opens) only re-counts `files` / `bytes`, sets `present`, adds missing `tags` / `notes`, and synthesizes entries for folders that have none. Entries are ordered gens first, then setups, by number.
| `stats.lastOpenedUtc` / `lastOpenedLocal` | 2 | Last time the clip was opened. |
| `states[]` | 3 | One entry per **occupied** savestate slot, ordered by slot. Derived — see below. |
| `states[].slot` | 3 | 0-99. Slot 0 is BASE, the combo-start bookmark. |
| `states[].file` | 3 | Filename inside the clip folder. |
| `states[].label` | 3 | The user's name for this checkpoint; `""` when unnamed. |
| `states[].movieFrame` | 3 | Which movie frame the state sits at. |
| `states[].rerecordSeq` | 5 | Sidecar-v2 vintage: the re-record count when the state was saved. **Absent** for old 4-byte sidecars (exempt from staleness). With `rewinds`, gives the full stale verdict from JSON alone. |
| `states[].bytes` | 3 | Size of the `.state` file. |
| `states[].savedUtc` / `savedLocal` | 3 | When the state was written. |
| `states[].thumb` | 3 | Thumbnail filename. **Absent when there is no thumbnail** — older states have none and cannot be back-filled. |

Stats accumulate across sessions (the emulator reads the previous values back before adding to
them) and are rewritten on every savestate save/load and on each state backup, so they survive a
crash.

`states[]` is a **cache of the sidecars**, written at the same moments. It is regenerated
wholesale on every write, never merged, so a state deleted from the folder disappears from it too.
Read it for convenience; the sidecar files below stay authoritative if the two ever disagree
(someone can add or remove a `.state` with the emulator closed).

## Derived by scanning, deliberately NOT stored

These change without the emulator running, so a stored copy would lie. `states[]` mirrors the
first four for convenience, but it is only as fresh as the last time the emulator wrote it —
scan the folder when correctness matters:

| What | How |
|------|-----|
| Savestate count | `*.state` files (`<game>.state` = slot 0 / BASE, `<game>_N.state` = slot N) |
| Which movie frame a state is at | its `.state.frame` sidecar: little-endian `u32 frame` — **v2 appends** `u32 rerecordSeq` (re-record count at save) and `u32 movieLen`. Read the first 4 bytes and you have v1 behaviour; 4-byte files are v1 (seq unknown, exempt from staleness) |
| A state's thumbnail | its `.state.png` sidecar: an RGB PNG of the frame it was saved on |
| A state's label | its `.state.label` sidecar: one line of UTF-8, max 64 chars, absent when unnamed |
| State backups | subfolders matching `*gen_NN` (older clips: bare `gen_NN`) |
| Captures | `*.mov` / `*.avi` in the folder; one named after the clip is the primary |
| The movie | the single `.flyr` in the folder |

## Conventions for future readers

- **Screenshots**: `<state>.png` beside each savestate is written automatically (see above) and is
  the per-state preview — the States window (F4) shows it. `thumb.png` is the intended "cover" name for
  the clip as a whole; nothing writes that one yet, drop it in by hand and tooling can use it.
- **Timestamps**: everything machine-facing is UTC ISO-8601; everything user-facing is local time,
  US format with AM/PM. Never show a raw UTC string in UI.
- **Identity**: the folder name is the clip's display name (renaming is a first-class action and
  carries the movie, backups and clip-named captures with it). The date lives in `createdUtc`, so
  a renamed clip never loses when it was made.
