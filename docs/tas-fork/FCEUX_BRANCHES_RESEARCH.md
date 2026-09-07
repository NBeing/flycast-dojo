# FCEUX TAS Editor — Bookmarks / Branches / Greenzone / History: condensed file:line reference

Source tree: `C:\_mvc2\other\fceux` (git HEAD `a62b868e` 2026-05-29). All paths below are relative to that root; unqualified
files live in `src/drivers/win/taseditor/` (Win32 TAS Editor, © AnS 2011-2013, MIT). The Qt port
`src/drivers/Qt/TasEditor/` is a straight transliteration of the same classes (e.g. `branches.cpp` Qt:1203
`getFirstDifferenceBetween`, Qt:1225 `findFullTimelineForBranch`, Qt:1341 `recalculateParents` — same algorithm).
Design docs: `web/help/taseditor/{Ideas,Implementation,FM3format,Toolbox,ProgramCustomization,Controls,Glossary,MistakeProofing}.html`
(doc citations are by section heading; they are prose, not code).

Vocabulary trap: FCEUX **Bookmark** = numbered savestate slot that also carries a whole-movie snapshot ("branch").
FCEUX **Marker** = a yellow row label with a text note. Flycast-dojo's "bookmarks" correspond to FCEUX **Markers**,
not FCEUX Bookmarks (see Glossary at the end).

---

## 1. DATA MODEL

### 1.1 `BOOKMARK` (`bookmark.h:17-47`, `bookmark.cpp`)

| Field | Type | What it is | Where filled |
|---|---|---|---|
| `notEmpty` | bool | slot occupied | `bookmark.cpp:85` |
| `snapshot` | `SNAPSHOT` | **the WHOLE input log** (every frame of the movie, not a diff) + lag log + markers + keyframe | `bookmark.cpp:69-72` (`snapshot.init(currMovieData, greenzone.lagLog, hotChanges)`, `keyFrame = currFrameCounter`) |
| `savestate` | `vector<uint8>` | **one** savestate: the Greenzone state of the bookmarked frame (state *before* emulating that frame) | `bookmark.cpp:74` `= greenzone.getSavestateOfFrame(currFrameCounter)`; produced by `FCEUSS_SaveMS(&ms, Z_DEFAULT_COMPRESSION)` `greenzone.cpp:118-120` |
| `savedScreenshot` | `vector<uint8>` | 256×240 8-bit palette-indexed frame, zlib `compress()`ed; `XBuf` (with HUD) or `XBackBuf` (raw) per `HUDInBranchScreenshots` | `bookmark.h:13-15`, `bookmark.cpp:76-83` |
| `flashPhase/flashType/floatingPhase` | int (not saved) | row flash animation: `FLASH_TYPE_SET=0 / JUMP=1 / DEPLOY=2`, 11 phases at 100 ms | `bookmark.h:3-11`, `bookmarks.h:36`, `bookmarks.cpp:312-336` |

Timestamp: the snapshot's `description` starts with `strftime("%H:%M:%S")` at creation (`snapshot.cpp:45-49`); that
string is what the Bookmarks List shows in its "time" column (`bookmarks.cpp:637-641`) and is appended to the
history caption on deploy (`history.cpp:756`).

### 1.2 `SNAPSHOT` (`snapshot.h:7-38`) — the unit of both bookmarks and history items

`inputlog` (INPUTLOG), `laglog` (LAGLOG), `markers` (MARKERS), `keyFrame` (undo jump target), `startFrame/endFrame`
(operation range, for "related items" tinting), `consecutivenessTag`, `recordedJoypadDifferenceBits`,
`modificationType`, `description[100]`. Save layout `snapshot.cpp:91-110`: 6×u32, u8 len + description bytes,
then `inputlog.save`, `laglog.save`, `markers.save`. `skipLoad` (`:135-157`) seeks past an item without decoding.

### 1.3 `INPUTLOG` (`inputlog.h:25-84`, `inputlog.cpp`)

| Item | Detail | Lines |
|---|---|---|
| Layout | `joysticks`: 1 byte/joystick/frame, `[frame*numJoys + joy]`; `commands`: 1 byte/frame; `joysticksPerFrame = {1,2,4}` for 1P/2P/Fourscore | `inputlog.cpp:29, 45-46, 55-56` |
| Hot changes | 4 bits per (frame, button), 2 per byte, 4 bytes per joypad; `hasHotChanges` flag | `inputlog.h:16-23, 80` |
| `init(md)` | copies every record of `MovieData` (full log) | `inputlog.cpp:35-59` |
| `toMovie(md, start, end)` | `md.records.resize(end+1)` then writes frames `[start..end]` — **whole-log swap when called with defaults**; partial write keeps frames `< start` | `inputlog.cpp:98-111` |
| `findFirstChange(INPUTLOG&, start, end)` | frame-by-frame compare of every joystick byte + commands byte; returns first differing frame; if identical and `size < theirs` returns `size`; else −1. NB: reads past the other log's end return 0 (`getJoystickData` `:291-298`), so a longer log with an **all-empty tail** compares equal to a shorter one | `inputlog.cpp:244-264` |
| `findFirstChange(MovieData&)` | same, but size differences in either direction return the shorter length | `inputlog.cpp:266-289` |
| Compression | zlib `compress()` of each vector, once (`alreadyCompressed`); redone only when data changed | `inputlog.cpp:113-142, 306-356` |
| Save layout | u32 size, u8 inputType, u32 len+joysticks(z), u32 len+commands(z), u8 hasHot [+ u32 len+hot(z)] | `inputlog.cpp:144-168` |
| `insertFrames/eraseFrame` | resize the in-memory log (used by the Lua path to keep hot-change maps aligned) | `inputlog.cpp:306-356` |

### 1.4 `LAGLOG` (`laglog.h`, `laglog.cpp`)

`vector<uint8>` of `LAGGED_NO=0 / YES=1 / UNKNOWN=2` (`laglog.h:3-8`); saved zlib'd (`laglog.cpp:32-47, 57-70`);
`invalidateFromFrame(f)` truncates (`:116-123`); `insertFrame/eraseFrame` mirror movie edits (`:137-172`);
`findFirstChange` only reports YES↔NO conflicts — UNKNOWN never counts as a difference (`:187-206`).

### 1.5 `MARKERS` (`markers.h`, `markers.cpp`)

`markersArray` = marker-id per frame (int), `notes` = text per marker id (`markers.h:19-21`); saved as u32 size,
zlib'd int array, u32 count + (u32 len + bytes) notes (`markers.cpp:27-48`, `compressData :106-114`).
Difference test `MARKERS_MANAGER::checkMarkersDiff` (`markers_manager.cpp:329-362`): any marker beyond the shorter
array, any per-frame id mismatch, any note text mismatch, or note[0] mismatch.

### 1.6 Slots and the "set" operation

* `TOTAL_BOOKMARKS 10` (`bookmarks.h:5`); `bookmarksArray` (`bookmarks.h:85`); List rows are ordered 1,2,…,9,0
  (`(row+1) % 10`, `bookmarks.cpp:601, 620-640`); `DEFAULT_SLOT 1` (`bookmarks.h:49`).
* `BOOKMARKS::set(slot)` (`bookmarks.cpp:363-400`): first flushes an edited marker note (`:368`); **no-op if the slot
  already equals the current movie** — `BOOKMARK::isDifferentFromCurrentMovie()` (`bookmark.cpp:47-64`: same keyframe,
  same log size, `findFirstChange(currMovieData) < 0`, same markers, same hot-changes setting). Otherwise: keep a
  `backup_copy` (`:373`), `bookmarksArray[slot].set()` (`:374`), `branches.handleBookmarkSet(slot)` (`:377`),
  `history.registerBookmarkSet(slot, backup_copy, old_current_branch)` (`:396`) → the **old slot contents ride the
  history ring, so overwriting a slot is undoable** (`history.h:143`, `history.cpp:724-741`, swap on undo/redo
  `history.cpp:281-293, 304-316`). Hot changes are copied from the history's current snapshot (`bookmark.cpp:71-72`).
* All bookmark commands are queued (`bookmarks.cpp:352-361`) and executed in `BOOKMARKS::update()` at end of frame
  (`:284-310`), i.e. never mid-frame (Ideas.html §"Bookmarks and branches").

### 1.7 The `.fm3` project file (`taseditor_project.cpp:74-181`, `taseditor_project.h:28-43`, `FM3format.html`)

```
[FM2 header + Input Log]   currMovieData.dump(ofs, inputInBinary)   project.cpp:128-130  (header has "length" key so the parser stops)
u32 PROJECT_FILE_CURRENT_VERSION = 3                                   project.cpp:133 / .h:37
u32 savedStuffMap  bits: MARKERS 1 | BOOKMARKS 2 | GREENZONE 4 | HISTORY 8 | PIANO_ROLL 16 | SELECTION 32   .h:30-35
u32 numberOfPointers = 6                                               .h:42
6 × u32 absolute offsets (patched in afterwards)                       project.cpp:144-146, 161-167
MARKERS   section  "MARKERS"/"MARKERX"    markers_manager.cpp:70-83     MARKERS::save
BOOKMARKS section  "BOOKMARKS"/"BOOKMARKX" bookmarks.cpp:482-498       10 × BOOKMARK::save (bookmark.cpp:102-118: u8 notEmpty, snapshot, u32 len+savestate, u32 len+screenshot) then BRANCHES::save
GREENZONE section  "GREENZONE"/"GREENZONX" greenzone.cpp:233-346       laglog, u32 greenzoneSize, u32 currFrameCounter, (u32 frame, u32 len, bytes)* , -1
HISTORY   section  "HISTORY"/"HISTORX"     history.cpp:994-1022        u32 cursorPos, u32 totalItems, N × (snapshot, backup BOOKMARK, i8 backupCurrentBranch)
PIANO_ROLL section "PIANO_ROLL"/"PIANO_ROLX" piano_roll.cpp:854-869    u32 top visible row only
SELECTION section  "SELECTION"/"SELECTIOX" selection.cpp:247-268       u32 cursor, u32 total, N × (u32 count, u32 frames...), clipboard selection
```

`BRANCHES::save` (`branches.cpp:410-431`): `cloudTimestamp[9]`, u32 `currentBranch`, u8 `changesSinceCurrentBranch`,
`currentPosTimestamp[9]`, 10 × u32 `parents`, 10 × i8 `cachedTimelines`, 100 × u32 `cachedFirstDifferences`.
Ideas.html §"Bookmarks and branches": only the cached first-difference matrix is essential; parents can be re-derived.

Save modes / "keep original" semantics:

| Rule | Lines |
|---|---|
| Greenzone saving modes: ALL, 16TH (every 16th frame + cursor frame), MARKED (frames with a Marker + cursor), NO (writes `GREENZONX`, the lag log, cursor and **one** savestate at the cursor) | `taseditor_config.h:15-24`, `greenzone.cpp:253-345` |
| "Save Compact" writes to a *different* filename (suggested suffix `-compact`) and does **not** clear the dirty flag: `if (!differentName) reset();` | `taseditor.cpp:555-592`, `taseditor_project.cpp:171-173` |
| Defaults: normal save = everything incl. full greenzone; compact = no history, no selection, no greenzone | `taseditor_config.cpp:72-86` |
| Load honours the *current* `greenzoneCapacity` (thinned frames are skipped on read) and `maxUndoLevels` (redo items dropped first, then oldest undo) | `greenzone.cpp:414-441`, `history.cpp:1060-1082`, MistakeProofing.html |
| On partial greenzone corruption, truncate to last good frame and move the cursor there | `greenzone.cpp:451-475` |
| ROM MD5 mismatch → prompt to rewrite header (save) / load anyway (load); FM3 version mismatch → "try all / movie only / cancel" | `taseditor_project.cpp:80-116, 200-255` |
| Lazy compression: while the emulator is paused, one uncompressed history item is compressed every 500 ms | `history.cpp:177-199`, `history.h:6` |
| Autosave every N minutes (default 15, silent) when dirty | `taseditor_project.cpp:60-72`, `taseditor_config.h:11-13` |

Memory model (Ideas.html §"History Log"): every history item and every bookmark holds a **full** input log + lag log +
markers. NES cost is 1–4 B/frame, so a 100 k-frame movie ≈ 0.4 MB raw per copy, zlib'd when idle. History cap: default
100, max 1000 (`taseditor_config.h:7-9`).

---

## 2. THE BRANCH TREE (`branches.h`, `branches.cpp`)

### 2.1 State (`branches.h:143-150`)

`parents[10]` (int, `-1 == ITEM_UNDER_MOUSE_CLOUD` `bookmarks.h:33`), `currentBranch` (int, −1 = none/cloud),
`changesSinceCurrentBranch` (bool → the fireball), `cloudTimestamp` (project start, `branches.cpp:229-231`),
`currentPosTimestamp` (time of last edit, `:874-880`), `cachedFirstDifferences[10][10]` (`FIRST_DIFFERENCE_UNKNOWN -2`,
`branches.h:94`), `cachedTimelines[10]` (tip of each bookmark's timeline).

### 2.2 Parents are DERIVED from input-log comparison, never stored as user intent

`getFirstDifferenceBetween(a, b)` (`branches.cpp:774-792`): symmetric, cached; same slot → `a.inputlog.size`;
computed as `a.snapshot.inputlog.findFirstChange(b.snapshot.inputlog)`; `−1` (identical) is mapped to `a.size`;
either slot empty → 0.

`recalculateParents()` (`branches.cpp:882-939`) — for every non-empty bookmark `i` (loop order 1,2,…,9,0 reversed, `:887-889`):

1. Candidates `t` (`:896-914`): `t != i`, non-empty, `t.keyFrame <= i.keyFrame`, and
   **`firstDiff(t, i) >= t.keyFrame`** — i.e. `i`'s input agrees with `t`'s input at least up to `t`'s own bookmarked
   frame (`:900`). Reject `t` if `i` is already an ancestor of `t` (walk `parents[t]` chain, `:902-913`) — cycle guard.
   Track `maxKeyFrame`.
2. Keep only candidates with `keyFrame == maxKeyFrame`; among them find `maxFirstDifference` (`:917-926`).
3. Keep only those with `firstDiff == maxFirstDifference` (`:927-932`) — longest common prefix wins the tie.
4. `parents[i] = candidates[0]` (`:933-935`); the candidate loop runs `t = (t1+1)%10` for `t1 = 9..0`, so the push order
   is 0,9,8,…,1 → slot 0 then highest slot number wins a full tie (comment `:933`).

So **parent = the bookmark with the latest keyframe ≤ mine whose input is a compatible prefix through its own keyframe**.
Nothing after the parent's keyframe is compared — that is exactly what makes a child a *fork*. If no candidate exists the
parent stays `-1` (cloud), because `invalidateRelationsOfBranchSlot` (`:763-772`) reset every parent to cloud first.

When it runs: `handleBookmarkSet(slot)` (`:738-746`: invalidate slot's cache row/column + all parents + all timelines,
`recalculateParents`, `currentBranch = slot`, `changesSince = false`, relayout) and `handleHistoryJump` (`:753-761`,
after history swapped bookmarks back, `history.cpp:291, 314, 332`). **Not** on deploy: `handleBookmarkDeploy`
(`:747-752`) only sets `currentBranch = slot`, `changesSince = false`, and requests a relayout. Ideas.html
§"Bookmarks and branches": "Markers contained in the Bookmarks do not affect the algorithm … The parent is found by
comparing the Input."

### 2.3 Cloud, fireball, current branch

| Node | Meaning | Code |
|---|---|---|
| **Cloud** (`ITEM_UNDER_MOUSE_CLOUD = -1`) | the ROOT: the movie's power-on frame 0; timestamp = project creation. Parent of every bookmark that has no prefix-compatible earlier bookmark. Always drawn, never re-parented. Click → `playback.jump(0)` | `branches.cpp:196-231, 977-980, 1260-1262`; Toolbox.html "The cloudlet symbolizes the beginning of the movie (the root of the hierarchy)" |
| **Current branch** (blue digit) | the bookmark the working movie was last set to / deployed from (`currentBranch`) | `:743, 749`; digits `:595-600` |
| **Fireball** (`ITEM_UNDER_MOUSE_FIREBALL = 10`) | the WORKING MOVIE once it differs from the current bookmark (`changesSinceCurrentBranch`). It is **always** the child of `currentBranch` (or of the cloud when there is none); no parent search is done for it — "such search … would require comparing the current movie to the Input of each Bookmark, that is too resource-intensive" (Ideas.html). Click → jump to last frame. Hover → time of last edit | `:849-859` (set by every history registration: `history.cpp:637, 679, 721, 881, 913, 986`), layout `:1013-1044`, `:1266-1269, 1284-1287` |
| **Timeline tip** `findFullTimelineForBranch(b)` | the deepest *descendant* of `b` whose input still equals `b`'s (firstDiff ≥ its keyframe, chosen by max first-difference, then max keyframe, then highest id); used to extend the red timeline past the current bookmark to heirs that would not change the movie if deployed | `branches.cpp:794-847`; Ideas.html "red lines go from the cloudlet to the current Bookmark or even further – to the heirs" |

Overwriting a slot: `BOOKMARKS::set` → `invalidateRelationsOfBranchSlot` → full `recalculateParents` → every bookmark that
used the old contents as parent is re-attached (to another bookmark or the cloud); the previous contents are in the
history backup (undoable). Loading a bookmark: the cloud is **not** re-parented; the fireball simply reappears under
the newly current bookmark on the next edit.

History jump (`history.cpp:245-349`): scanning modtypes between old and new cursor positions, a `MODTYPE_BOOKMARK_n /
BRANCH_n / BRANCH_MARKERS_n` item sets `current_branch = n, changes = false`; any input/marker item sets
`changes = true`; undo uses `currentBranchNumberBackups` (`:302-303`); bookmark-set items swap slot ↔ backup
(`:281-293, 304-316`); then `branches.handleHistoryJump(...)` (`:332`).

---

## 3. LOAD / JUMP SEMANTICS (`bookmarks.cpp`)

### 3.1 `jump(slot)` — spatial jump, movie untouched (`bookmarks.cpp:402-411`)

`playback.jump(keyFrame)` + green flash. `PLAYBACK::jump` (`playback.cpp:446-482`): `setPlaybackAboveOrToFrame`
(`:485-512`) walks backwards from `min(greenzoneHead, frame)` to the nearest non-empty Greenzone savestate and loads it
(or `restartPlaybackFromZeroGround` = power-on, `:421-429`, if none); if still short of `frame`, `startSeekingToFrame`
(`:333-343`: `pauseFrame = frame+1`, optional turbo, unpause) and `update()` pauses when
`currFrameCounter+1 >= pauseFrame` (`:178-179`).

### 3.2 `deploy(slot)` — temporal jump = REPLACE the movie + load the savestate (`bookmarks.cpp:413-480`)

| Step | Code |
|---|---|
| 0. `recorder.stateWasLoadedInReadWriteMode = true` (old-scheme recording gate, `taseditor.cpp:938-943`); old scheme + read-only ⇒ degrade to `jump` | `:415-420` |
| 1. Markers: if the bookmark's markers differ (`checkMarkersDiff`), restore them | `:427-431` |
| 2. Input: `branchesRestoreEntireMovie` (default **true**, `taseditor_config.cpp:65`) → `snapshot.inputlog.toMovie(currMovieData)` = **whole-log swap**; else restore `[0..keyframe-1]` and append one blank frame at `keyframe` ("simulating old TASing method") | `:433-445`; ProgramCustomization.html §"Branches restore entire Movie" |
| 3. `first_change = history.registerBranching(slot, markers_changed)` — computes the first differing frame vs the current history snapshot, logs a `MODTYPE_BRANCH_n` item ("Branch n to HH:MM:SS", `keyFrame = startFrame = first_changes`, `endFrame = -1`) or `MODTYPE_BRANCH_MARKERS_n` if only markers changed; **hot changes are replaced by the bookmark's own map** (`:762`); reverts the Greenzone lag log to the bookmarked one (or just truncates it if the bookmarked log is a prefix of the current one, `:782-798`); returns `min(first input change, first lag change)` (`:800-802`) | `:447`, `history.cpp:742-803` |
| 4. If `first_change >= 0`: `pianoRoll.updateLinesCount()`, **`greenzone.invalidate(first_change)`** (the variant that does *not* move playback), red flash | `:448-453` |
| 5. Put the bookmark's savestate back into the Greenzone at `keyframe` if that slot is empty (`writeSavestateForFrame` also advances the head to `keyframe+1`, `greenzone.cpp:648-655`) and `playback.jump(keyframe, forceStateReload=true)` → the cursor lands on the bookmarked frame instantly even though everything between `first_change` and `keyframe` was just invalidated | `:465-467`; Ideas.html "thanks to the savestate stored inside the Bookmark, one savestate returns back to the Greenzone" |
| 6. `branches.handleBookmarkDeploy(slot)`; redraw rows of old/new current branch; "Branch %d loaded." | `:469-479` |
| Nothing differed (no input, no marker change) → treated as a plain jump (green flash) | `:458-462` |
| Selection is not rewritten; only `mustFindCurrentMarker` flags are raised (selection is clamped to the new movie length by `SELECTION::updateSelectionSize`) | `:450, 456`; `selection.cpp:142-157` |

The invalidation rule (the analog of the fork's dead-timeline guard): **`GREENZONE::invalidate(after)` keeps savestates
`[0..after]` inclusive and clears everything `> after`** (`greenzone.cpp:572-590`, loop `for i > after`), sets
`greenzoneSize = after+1`, and increments the rerecord counter once (`:584`; `src/movie.cpp:1646-1664`). The state
*at* the first changed frame is still valid because Greenzone item N is the state *before* emulating frame N
(Ideas.html §"Greenzone"). `first_change` is the first frame whose bytes actually differ — it can be later than the
first *edited* frame (Ideas.html §"History Log": "if you set buttonpresses in all selected frames … the Greenzone will
be truncated only after the frame where the button wasn't pressed before").

Undo of a deploy = `HISTORY::undo → jumpInTime` (`history.cpp:245-422`): restores the previous snapshot's markers
(`:371-378`), writes input back **from the first differing frame** (`toMovie(currMovieData, first_changes)`, `:381-388`),
truncates-or-replaces the lag log (`:396-412`), then `greenzone.invalidateAndUpdatePlayback(result)` (`:424-430`).

---

## 4. THE GREENZONE (`greenzone.h`, `greenzone.cpp`)

| Aspect | Detail | Lines |
|---|---|---|
| Storage | `vector<vector<uint8>> savestates` indexed by frame; `greenzoneSize` = head+1; item N = state before frame N; savestates **exclude movie data** and are always zlib'd | `greenzone.h:55-57`; Implementation.html §"SaveStates"; MistakeProofing.html |
| Collection | `update()` every frame → `collectCurrentState()`: if `savestates[cur]` empty, `FCEUSS_SaveMS(Z_DEFAULT_COMPRESSION)`; head advances. `enableGreenzoning` off → only the head advances | `greenzone.cpp:60-71, 111-124`; config default on `taseditor_config.cpp:55` |
| Capacity | `greenzoneCapacity` default 10000, min 1, max 50000 ("32-bit OS, 2 GB limit") | `taseditor_config.h:3-5` |
| Thinning ("gradual rarefication") | every 10 s (`TIME_BETWEEN_CLEANINGS`): frames older than `cursor − capacity` are cleared geometrically — next 2×capacity keep every 2nd (`i & 1` cleared), next 4×cap keep every 4th, next 8×cap every 8th, next 16×cap every 16th, everything older cleared; frame 0 never cleared; memory actually freed (`swap`) | `greenzone.h:7-12`, `greenzone.cpp:134-188, 202-212`; ProgramCustomization.html worked example: capacity 100 ⇒ 3100 reachable frames for 500 stored |
| Invalidate vs cleaning | `invalidate` uses `resize(0)` (keeps the allocation because "the place of the old savestates will soon be taken by new data of about the same size"), cleaning uses `swap` to free | `greenzone.cpp:191-212`; Ideas.html §"Greenzone" |
| `invalidate(after)` | no playback fix-up; used by Branching, Recording, AdjustLag | `greenzone.cpp:571-590` |
| `invalidateAndUpdatePlayback(after)` | additionally, if the cursor is now past the head: if it was seeking & running, keep seeking to `pauseFrame`; else `setLastPosition(cursor)` (the **green arrow**) and either auto-seek back to it (`autoRestoreLastPlaybackPosition`, default off) or `ensurePlaybackIsInsideGreenzone` (load head state) | `greenzone.cpp:592-627`, `playback.cpp:431-443, 514-527` |
| Seeking | see §3.1; seeking from the nearest earlier stored state, turbo optional, progress bar; Esc / click progress bar cancels | `playback.cpp:485-512, 333-350`, `mapinput.cpp:97` |
| Manual "Ungreenzone" of selected frames | | `greenzone.cpp:214-231`, `taseditor_window.cpp:965` |
| Piano-roll colouring | bright green/red = stored state; **pale** if the frame sits in a thinned gap (any of `frame & EVERY16TH/8TH/4TH/2ND` stored); **very pale** above the tail or below the head when only lag info is known | `piano_roll.cpp:1395-1431`, colours `piano_roll.h:108-130` |
| Save/load | see §1.7; load skips frames the current capacity would have thinned | `greenzone.cpp:414-441` |
| Rerecord counter | increments **only** on Greenzone truncation, not on savestate loads | `greenzone.cpp:584, 604`; Ideas.html §"Rerecords counter" |

### 4.1 Lag log and "auto-adjust input according to lag"

* FCEUX lag definition: `lagFlag = 1` at the start of every frame (`src/fceu.cpp:838`), cleared by the joypad read
  paths (`src/input.cpp:123, 478, 495`), counted after the frame (`src/fceu.cpp:903-904`) — a lag frame is a frame in
  which the game **did not poll input** (Glossary.html "Lag").
* Greenzone records the lag of the *previous* frame at the start of the current one (`greenzone.cpp:77-107`), also
  into the history's current snapshot (`:100, 105`).
* `autoAdjustInputAccordingToLag` (default **on**, `taseditor_config.cpp:57`): if a frame that used to lag no longer
  does, `adjustUp` deletes the now-superfluous frames of input (`greenzone.cpp:485-533`); if new lag appears,
  `adjustDown` clones the frame (`:534-569`). Both go through `history.registerAdjustLag` which **merges into the
  current history item instead of adding one** (`history.cpp:644-683`). Autofire patterns skip lag frames
  (`selection.cpp:517-536`, `autofirePatternSkipsLag`).
* Every snapshot/bookmark carries its own lag log, and deploy/undo restore it (`history.cpp:396-412, 780-798`), so the
  red rows are correct immediately after switching branches, without re-emulation.
* Relevance to MvC2: FCEUX's lag is *measured* per frame and can move; MvC2's frameskip is a fixed cadence
  (fork `core/dojo/tas_ruler.h:6`). The per-branch lag log idea transfers; the auto-adjust feature is the opposite of what
  a fixed-cadence skip map wants (it would rewrite input when the cadence appears to shift).

---

## 5. HISTORY (`history.h`, `history.cpp`)

| Aspect | Detail | Lines |
|---|---|---|
| Item = **full SNAPSHOT** (whole input log + lag log + markers), plus `bookmarkBackups[i]` (a whole BOOKMARK when the item is a bookmark-set) and `currentBranchNumberBackups[i]` | | `history.h:142-146`; Ideas.html "Each item of the History Log stores a full copy of current Input, Lag and Markers" |
| Ring buffer sized `maxUndoLevels+1` (default 100, max 1000); oldest dropped; resizing re-packs preferring undo over redo | | `history.cpp:135-158, 202-242, 439-482`, `taseditor_config.h:7-9` |
| `registerChanges(mod_type, start, end, size, comment, consecutivenessTag, frameset)` — snapshot the movie, `findFirstChange` vs current item in `[start,end]`; **no item if nothing differs** (returns −1, or the first lag-log change when `end == -1`) | | `history.cpp:487-643` |
| `keyFrame` (undo jump target): first actual change for SET/UNSET/TRUNCATE/CLEAR/CUT; the *attempted* frame (`start`) for INSERT/INSERTNUM/PASTEINSERT/PASTE/CLONE/DELETE/PATTERN | | `history.cpp:509-532` |
| Description = `"HH:MM:SS"` + caption + frame range + comment; `Insert#` appends the count | | `history.cpp:49-105, 536-544, 585-600` |
| Consecutive draws/recordings combined into one item when `combineConsecutiveRecordingsAndDraws` (default off) | | `history.cpp:546-581, 804-845`, `taseditor_config.cpp:59` |
| `MOD_TYPES` | INIT, UNDEFINED, SET, UNSET, PATTERN, INSERT, INSERTNUM, DELETE, TRUNCATE, CLEAR, CUT, PASTE, PASTEINSERT, CLONE, RECORD, IMPORT, BOOKMARK_0..9, BRANCH_0..9, BRANCH_MARKERS_0..9, MARKER_SET/REMOVE/PATTERN/RENAME/DRAG/SWAP/SHIFT, LUA_MARKER_SET/REMOVE/RENAME, LUA_CHANGE | `history.h:10-71`; categories `history.cpp:1198-1273` |
| Bookmark set → `MODTYPE_BOOKMARK_n` item whose `keyFrame` = bookmarked frame and which **stores the overwritten slot** | | `history.cpp:724-741` |
| Branch deploy → `MODTYPE_BRANCH_n` ("Branch n to <bookmark time>"), `keyFrame = first_changes`, `endFrame = -1`; markers-only → `MODTYPE_BRANCH_MARKERS_n` | | `history.cpp:742-779` |
| Recording → `MODTYPE_RECORD`, merged frame-by-frame with `SNAPSHOT::reinit` when consecutive; recorder then `greenzone.invalidate(currFrameCounter)` | | `history.cpp:804-883`, `snapshot.cpp:52-65`, `recorder.cpp:310-314` |
| `jumpInTime(pos)` (undo/redo/click): swap bookmarks back, recompute branch state, restore markers, `toMovie` from first change, lag log, purple **undo hint** row for 200 ms, returns frame to invalidate | | `history.cpp:245-422`, `history.h:3`, `piano_roll.cpp:1371-1373` |
| History List: click a row = jump there; rows whose frame range intersects the current item's are tinted `HISTORY_RELATED_BG_COLOR` | | `history.cpp:1119-1181`, `history.h:85` |
| Selection has its **own** undo ring (Ctrl+Q / Ctrl+W), independent of input history | | `selection.cpp:415-477` |
| Future idea logged by AnS: a separate history for bookmark operations (Alt+Z/Y) and a per-frame "number of Greenzone truncations" heat array | | Ideas.html §"Future" |

### 5.1 Hot changes (the "recently edited" heat map)

* Storage: 4-bit value per (frame, button) inside each snapshot's INPUTLOG (`inputlog.h:16-23`); `0xF` = just changed.
* New item: inherit the previous map faded by 1 (`inheritHotChanges` → `fadeHotChanges`, `inputlog.cpp:379-392, 621-637`)
  then set `0xF` on every bit that differs (`fillHotChanges`/`setMaxHotChangeBits`, `:583-619`). Structural ops use
  aligned variants: `inheritHotChanges_DeleteSelection / InsertSelection / InsertNum / DeleteNum / PasteInsert`
  (`inputlog.cpp:393-582`, chosen in `history.cpp:605-633`). TRUNCATE copies without fading (`:629-632`); marker and
  bookmark ops copy unchanged (`:719, 738`); **deploy replaces the map with the bookmark's** (`:762`); import does
  not inherit (`:904-908`).
* Rendering: button glyph colour = `hotChangesColors[heat]` (`piano_roll.cpp:58, 1357-1358`) — black → warm reds
  (heat 5-8) → blues (11-15); `-` glyph for a hot but released button (`:1329-1332`). Toggle View → Enable Hot Changes
  (default on, `taseditor_config.cpp:49`, `taseditor_window.cpp:603, 1029`).

---

## 6. THE UI

### 6.1 Two views of the same 10 slots

Clicking the "Bookmarks / Branches" group caption toggles `displayBranchesTree` (`taseditor_window.cpp:1496-1507`);
`redrawBookmarksSectionCaption` picks `EDIT_MODE_BRANCHES / BOTH / BOOKMARKS` (`bookmarks.cpp:539-563`,
`bookmarks.h:7-12`; default list view, `taseditor_config.cpp:46`).

**Bookmarks List** (`bookmarks.cpp:69-250, 611-730`): 3 columns — digit icon (green 0-9 = `IDB_BITMAP0..9`, **blue**
10-19 = current branch, +20 = "selected slot" variants for the old scheme; `:83-204, 618-630`), bookmarked frame,
timestamp. Cell backgrounds mirror the piano-roll state of the bookmarked frame (cursor / greenzone / lag / pale gap;
`:647-730`). Left-click on frame column → JUMP, on time column → DEPLOY, right-click → SET (`:732-743`); press and
release must hit the same row and a wheel in between cancels (`:787-872`; MistakeProofing.html). Flash colour ramps
per command: set = blue, jump = green, deploy = red (`:49-56`).

**Branches Tree** — a 170×145 px GDI bitmap (`branches.h:13-14`), canvas 140×130 (`:23-24`), cloud anchored at
(14, 72) (`:25-26`), redrawn at 25 fps (`BRANCHES_ANIMATION_TICK 40`, `:3`) with a 12-frame lerp when the layout
changes (`BRANCHES_TRANSITION_MAX`, `:4`; `branches.cpp:287-298, 942-949`). Hover is disabled during the transition
(`isSafeToShowBranchesData`, `:731-736`).

### 6.2 Layout algorithm `recalculateBranchesTree` (`branches.cpp:940-1192`)

1. **Levels = depth**: BFS from the cloud; `gridX[b] = depth`; `children[parent+1]` lists built (`:963-1012`).
   `gridHeight[b]` = number of leaves in its subtree via `recursiveAddHeight` (`:1003-1010, 1193-1201`).
2. **Fireball** placed as a child of `currentBranch` at `gridX+1` (or as the cloud's only child if none); if the current
   branch already has ≥ `MAX_NUM_CHILDREN_ON_CANVAS_HEIGHT` (9) children it is drawn *above* the branch instead
   (`:1013-1044`).
3. Column pitch `grid_width = 140 / levels` clamped to [14, 30] px; `cloud_prefix` pads the first edge to ≥ 19 px
   (`:1045-1056`).
4. **Rows**: `recursiveSetYPos` stacks each parent's children vertically, centred on the parent, each child block
   `2*gridHeight` tall (`:1058-1059, 1202-1219`); row pitch `grid_halfheight = 130 / (2*totalHeight)` clamped [8, 12]
   (`:1060-1072`).
5. Overflow fixes: chain longer than `MAX_CHAIN_LEN` 10 → fireball pulled back and up (`:1073-1081`); rows beyond
   ±`MAX_GRID_Y_POS` 8 re-spread (`:1082-1115`); cloud with all 10 children → one child moved 2 columns left
   (`:1116-1146`).
6. Pixels: `x = cloud_prefix + gridX*grid_width`, `y = CLOUD_Y + gridY*halfheight`; **empty slots are parked in a column
   at the left edge** (`x = 4`, `y = 9 + 14*row`, `:1156-1160`); the whole tree is centred: `cloudX = (170+10-max_x)/2`,
   min 12 (`:1180-1187`).

Result: root on the LEFT, time flows LEFT→RIGHT **by tree depth, not by frame number**, siblings stacked vertically.
Frame numbers appear only as hover text.

### 6.3 What each visual element encodes

| Element | Encodes | Draw routine / lines |
|---|---|---|
| Gradient background (light cyan → white) | — | `branches.cpp:80-92, 464` |
| Thin black lines parent→child | derived parent relation | `redrawBranchesBitmap` `:465-488` |
| **Red** polyline cloud→…→timeline tip (`timelinePen` 0x0020E0 BGR) | the current movie's timeline: bookmarks whose deploy would not change input up to their frame; stops at `currentBranch` when the fireball exists | `:489-514` |
| Thick **blue** polyline (`selectPen` 0xFF9080 BGR, width 2) | the timeline of the hovered bookmark / fireball | `:515-543` |
| Line from current branch (or cloud) to the fireball | working movie diverged | `:544-563` |
| Cloud sprite | root / frame 0 / project start time on hover | `:565`, hover text `:630-636` |
| Digit card 0-9 with black frame; **blue digit** = current branch, green = others; mouse-over row of the sprite sheet; frame colour = flash ramp while flashing; empty slots "float" 4 px right on hover | slot id, currency, feedback | `:566-610`, `bookmark.h:6-11`, `MAX_FLOATING_PHASE 4` `branches.h:10` |
| Fireball sprite (10×10, 12 anim frames, grows 0→5 while `changesSinceCurrentBranch`) | working movie ≠ current bookmark | `paintBranchesBitmap` `:679-690`, size `:276-285` |
| Blinking mini-arrow | **playback cursor projected onto the timeline**: find the segment `[lowerFrame, upperFrame]` of the current timeline that contains `currFrameCounter`, lerp between the two node positions | `:304-379, 691-693` |
| Four "corner" brackets that ease towards the cursor (`speed = sqrt(distance)`, teleport if < 1 or > 256 px) with a breathing offset | attention cue for the cursor | `:380-397, 694-712`, `corners_cursor_shift` `:48` |
| Blinking red square around the "selected slot" | only in old control scheme | `:668-677` |
| Top-left text | frame number of hovered bookmark / current frame for fireball | `:611-626` |
| Bottom-left text | timestamp of hovered item (cloud = project start, fireball = last edit, bookmark = creation time) | `:627-651` |
| Mouse cursor "arrow with question mark" | hovering a bookmark that is *off* the current timeline (a jump there lands on another branch) | MistakeProofing.html §"Branches Tree"; `taseditorWindow.mustUpdateMouseCursor` `:1227-1231` |

Hit-testing `findItemUnderMouse` (`:861-872`): digit rect ±1 px, cloud rect, fireball rect (only while it exists).

### 6.4 Mouse on the tree (`BranchesBitmapWndProc`, `branches.cpp:1222-1326`)

L-click: cloud → frame 0, digit → `COMMAND_JUMP`, fireball → last frame (`:1256-1273`). Double-click digit →
`COMMAND_DEPLOY` (`:1274-1291`). Right press+release on the same digit → `COMMAND_SET` (`:1292-1310`). Middle → pause /
seek (`playback.handleMiddleButtonClick`, `playback.cpp:287-331`: Shift = seek to next Marker, Ctrl = seek to / replay
from Selection cursor, else restore last position or unpause). Wheel forwarded to the piano roll (`:1319-1323`).

### 6.5 Screenshot / description popup (`popup_display.cpp`)

When hovering a non-empty bookmark (list time-column or tree digit) and the tree is not mid-transition (`:137`), two
`WS_EX_LAYERED|WS_EX_TRANSPARENT` popup windows are created to the **left** of the Bookmarks box (`:255-266`): the
decompressed 256×240 8-bit screenshot rendered with the *current* palette (`:90-105, 225-244`), fading in over 8 ticks at
25 fps (`:164-180`, `popup_display.h:3-7`), and a one-line description = the **Marker note above the bookmarked frame,
taken from the bookmark's own markers snapshot** (`:245-253`) — so a label set before bookmarking travels with the
branch even if the marker was later deleted (ProgramCustomization.html §"Display Branch Descriptions"). Fade-out and
destroy on leave (`:181-221`). Toggles `displayBranchScreenshots / displayBranchDescriptions` (default on,
`taseditor_config.cpp:47-48`).

### 6.6 Piano roll icons

`findBookmarkAtFrame(frame)` (`bookmarks.cpp:745-757`): blue digit (`slot+10`) if the current branch sits there, else the
first green digit found in 1..9,0 order, else −1. Piano roll adds +20 (`BOOKMARKS_WITH_BLUE_ARROW`) when the playback
cursor is on that row and +40 (`…GREEN_ARROW`) for the "last position" green arrow (`piano_roll.cpp:1279-1303`,
`piano_roll.h:90-91`).

### 6.7 Keyboard

FCEUX emulator hotkeys are rerouted while the editor is engaged (`taseditor.cpp:950-1043`):

| Default key (`src/drivers/win/mapinput.cpp:51-98`) | EMUCMD | TAS Editor meaning (new scheme) | Old scheme (`oldControlSchemeForBranching`, default off `taseditor_config.cpp:64`) |
|---|---|---|---|
| Shift+F1…F9, Shift+F10 | `SAVE_STATE_SLOT_1..9, _0` | `COMMAND_SET` slot | same |
| F1…F9, F10 | `LOAD_STATE_SLOT_1..9, _0` | `COMMAND_DEPLOY` slot | deploy if recording, jump if read-only (`bookmarks.cpp:416-420`) |
| 1…9, 0 | `SAVE_SLOT_1..9, _0` | `COMMAND_JUMP` slot | `COMMAND_SELECT` slot (red square) |
| I / P | `SAVE_STATE` / `LOAD_STATE` | set / deploy the *selected* slot | same |
| Space | `TASEDITOR_RESTORE_PLAYBACK` | seek to the green arrow | |
| Esc / Ctrl+Space / Backspace / W / Shift+R / Ctrl+F1 | cancel seeking / toggle auto-restore / rewind / multitrack / play from start / reload project | | |

Editor accelerators (`src/drivers/win/res.rc:3040-3079`, Controls.html §"Hotkeys"): Ctrl+Z/Y undo/redo, Ctrl+Q/W
selection undo/redo, Ctrl+A select between markers, Ctrl+B reselect clipboard, Ctrl+C/X/V, **Ctrl+Shift+V
paste-insert, Delete clear, Ctrl+Delete delete frames, Insert "insert N blank frames", Ctrl+Insert clone,
Ctrl+Shift+Insert insert blank frames before the selection**, Ctrl/Shift+PgUp/PgDn/Home/End/Up/Down for selection /
playback cursor navigation, Ctrl+F find note. In old scheme recording is refused until a state is loaded
(`isTaseditorRecording`, `taseditor.cpp:938-943`; `recorder.cpp:73, 126`).

---

## 7. PIANO-ROLL SELECTION + SPLICER

### 7.1 Selection (`selection.h`, `selection.cpp`)

`RowsSelection = std::set<int>` (`selection.h:4`); the "Selection cursor" is `*begin()` (`:669-675`). Kept in its own
ring history (`:415-456`), undo/redo (`:470-477`), mirrored from ListView state notifications (`:369-413`), clamped to
movie length every frame (`:142-157`). Verbs: clear/set single/region/all (`:489-515`), pattern select skipping lag
frames (`:517-536`), select-between-markers cycling 4 inclusion modes (`:537-609`), reselect clipboard (`:610-620`),
transpose ±N (`:622-651`), jump to marker / frame (`:205-245`). `getCopyOfCurrentRowsSelection()` (`:683-688`) is what
every splicer verb consumes.

### 7.2 Splicer verbs (`splicer.cpp`) and their invalidation

All verbs follow one shape: mutate `currMovieData` **and** `greenzone.lagLog` **and** (if `bindMarkersToInput`,
default on `taseditor_config.cpp:62`) the markers array → `history.registerChanges(MODTYPE_x, …)` → if a real
difference was found, `greenzone.invalidateAndUpdatePlayback(first_changes)`; otherwise, if only markers moved, log a
`MODTYPE_MARKER_SHIFT`.

| Verb (key) | Movie primitive (`src/movie.cpp`) | Lag / markers | History | Lines |
|---|---|---|---|---|
| **Clone** (Ctrl+Insert) `cloneSelectedFrames` | per contiguous region (walk selection in reverse, `:120-139`) `cloneRegion(at, n)` (`movie.cpp:161-169`: insert n blanks, copy the following n records) | `lagLog.insertFrame(at,false,n)`, `markersManager.insertEmpty` | `MODTYPE_CLONE`, start = first selected, frameset = selection (hot-change alignment) | `splicer.cpp:106-153` |
| **Insert** (Ctrl+Shift+Insert) `insertSelectedFrames` | per region `insertEmpty(at, n)` (`movie.cpp:150-159`) — a blank frame before each selected run | same | `MODTYPE_INSERT` | `:155-201` |
| **Insert # of frames** (Insert) `insertNumberOfFrames` | dialog (default = selection size); `index` = selection cursor or playback cursor if nothing selected; `insertEmpty(index, k)`; selection shifted down by k | `lagLog.insertFrame(index,false,k)`, markers | `MODTYPE_INSERTNUM`, `keyFrame = index`, caption "Insert#k" | `:203-252` |
| **Delete** (Ctrl+Delete) `deleteSelectedFrames` | `eraseRecords(frame)` per selected frame in reverse (`movie.cpp:132-148`); power-on restart if the movie becomes empty | `lagLog.eraseFrame`, `eraseMarker` | `MODTYPE_DELETE`; if nothing differs (deleted an empty tail) still invalidate at the new last frame | `:254-293` |
| **Clear** (Delete) / **Cut** (Ctrl+X) `clearSelectedFrames` | `records[f].clear()` (`movie.cpp:178-183`) | — | `MODTYPE_CLEAR / CUT` over `[begin, rbegin]` → invalidation at the first frame that *actually* changed | `:295-315, 423-432` |
| **Truncate** `truncateMovie` | `truncateAt(frame+1)` (`movie.cpp:420-423`) | markers array resized | `MODTYPE_TRUNCATE` at frame+1 | `:317-348` |
| **Copy / Paste / Paste-insert** | clipboard text `"TAS <range>\n"` + one line per selected frame, `+skip|` for gaps, button letters `A B S T U D L R` per joypad separated by `|` (`:37, 366-394`); paste overwrites from the selection cursor honouring the superimpose tri-state (`:433-555`); paste-insert inserts a fresh frame per line and records the inserted set for hot changes (`:556-676`) | paste-insert: `lagLog.insertFrame`, markers | `MODTYPE_PASTE` / `MODTYPE_PASTEINSERT` | |

Why this matters for a "Frame Skip Test" sweep: `insertNumberOfFrames` is the exact primitive (insert k blank frames at a
point, keyframe = that point); each sweep step is one undoable history item and one greenzone truncation whose frame is
the first byte that actually changed (`history.cpp:494`), and the lag log is shifted with the input so the skip pattern
stays aligned (`splicer.cpp:223`). NonlinearTASing.html describes precisely this loop for luck manipulation:
Ctrl+Shift+Insert one frame → Restore Playback (Space) → observe → repeat. Caveat: with FCEUX's auto-adjust-lag on, the
emulator may itself delete/clone frames during the replay (`greenzone.cpp:83-93`), which a cadence sweep must not allow.

---

## 8. OPINIONS — what Flycast-dojo should copy or avoid

**(a) Tree derived from input comparison vs explicit parent pointer.** FCEUX's rule (§2.2) is the *right definition*
of "parent" — latest-keyframe bookmark whose input is a compatible prefix through its own keyframe — but FCEUX can only
afford it because there are 10 slots (45 pairs, cached in a 10×10 matrix, `branches.cpp:779-791`) and it still refuses
to compute a parent for the live movie ("too resource-intensive", Ideas.html). With an unbounded set of branch folders:
store an **explicit parent id + divergence frame + the parent's prefix hash at that frame** when a branch is created,
draw from that, and keep the FCEUX comparison only as an offline repair/import tool. The fork's `MoviePrefixHash(frame)`
(`C:\_mvc2\other\flycast-dojo-7\core\dojo\dojo.cpp:621`) is the O(1) stand-in for `getFirstDifferenceBetween`. Note the
subtlety in `findFirstChange` (`inputlog.cpp:244-264`): trailing *empty* frames compare equal to absent frames — decide
explicitly whether "same inputs, different length" is the same branch. Copy FCEUX's cycle guard (`:902-913`) if parents
are ever recomputed. Copy: parent recomputation after *every* history jump (`branches.cpp:755`) is fine at 10 nodes,
wrong at 200 — recompute lazily per dirty node.

**(b) Bookmark = full input-log snapshot.** Cheap for NES; for the fork's 12-byte frames a 36 k-frame (10 min) movie
is ~430 KB per copy, still cheap in RAM if zlib'd lazily the way FCEUX does it (compress-when-idle,
`history.cpp:177-199`; compress-once flag `inputlog.cpp:137`). The fork's folder-per-branch is the on-disk equivalent
of one BOOKMARK (movie file ≈ snapshot, `states[]` ≈ the one savestate, thumbnail ≈ screenshot). Keep **full**
snapshots for branches (they must survive undo-history truncation), keep **diffs** (the fork's `EditPatch undo_stack`,
`dojo.h:247`) for undo — FCEUX pays a full snapshot per undo step and caps at 100 for that reason. Copy the two
guard rails: *set is a no-op when identical* (`bookmark.cpp:47-64`) and *overwriting a slot stashes the old contents
in history* (`history.h:143`, `history.cpp:724-741`) — with folders that means a `.trash`-style move, which the fork's
`tas_clip.cpp:192` already does. Also copy: bookmarks outlive truncation; deploying one re-extends the movie
(Ideas.html §"Bookmarks and branches").

**(c) Screenshot per bookmark.** Copy the interaction (hover popup, fade, description = the branch's *own* label
snapshot, `popup_display.cpp:245-253`), not the storage: NES is 61 KB raw; Dreamcast 640×480 is ~900 KB — store a
downscaled thumbnail (fork already has `thumbnail.cpp`). Snapshotting the HUD layer optionally (`HUDInBranchScreenshots`)
is a good idea for a combo-research tool: bake the input display into the thumbnail.

**(d) Greenzone thinning.** Copy the geometric rarefaction keyed off distance from the cursor (`greenzone.cpp:134-188`)
and the "pale gap" colouring (`piano_roll.cpp:1404-1413`) — with multi-MB Dreamcast states the capacity will be
hundreds, not thousands, so the 1/2, 1/4, 1/8, 1/16 tail is what keeps rewind cheap. Copy the two-tier free
(`resize(0)` on invalidate to reuse allocations, `swap` on cleaning, `:191-212`). Copy the invariant
"invalidate keeps state[first_change]" (`:579`) and "rerecord = number of truncations" (`:584`). The fork today has no
per-frame state array — its dead-timeline guard validates a *stored* state lazily at load (`dojo.cpp:716-721`), whereas
FCEUX truncates eagerly at edit time and never lets a stale state be loaded. Eager truncation is the simpler mental
model for a branch UI (a state's validity is a property of the timeline, not of the file).

**(e) The cloud.** Copy it as a real root node = power-on/frame 0 (clickable: jump to 0), with a creation timestamp;
it is the parent of every branch with no compatible ancestor. Do **not** map the fork's "live roll" onto the cloud — it
is FCEUX's **fireball** (the working movie once it diverges from the current bookmark, always drawn as a child of the
current bookmark, click = end of movie). Improve on FCEUX: make the live timeline a first-class node with its own
prefix hash so it can be placed correctly instead of "always under the current bookmark"; FCEUX's fireball is not
stored and not comparable.

**Other keepers.** One comparator (`INPUTLOG::findFirstChange`) serves history diffing, parent derivation and
greenzone truncation — keep the fork's single prefix-hash primitive in that same triple role. Deploy = "movie swap +
history item `Branch n to <time>` + truncate at min(first input change, first lag change) + put the branch's one state
back + jump" (`bookmarks.cpp:413-480`) is the whole contract; the "nothing changed ⇒ it's just a jump" degrade
(`:458-462`) avoids phantom history entries. Per-branch lag log restored on deploy (`history.cpp:780-798`) maps
directly onto restoring the frameskip ruler per branch. Keep bookmark ops queued to frame boundaries
(`bookmarks.cpp:284-310`).

**Avoid.** The dual "old control scheme" key semantics (F-keys meaning jump *or* deploy depending on read-only,
`bookmarks.cpp:416-420`) — pick one meaning per key. Automatic lag-driven input shifting (`greenzone.cpp:83-93`) for a
fixed-cadence skip. Recomputing every parent on every undo. A 10-slot cap chosen for the number keys (Ideas.html) —
irrelevant once branches are nodes; keep 10 *hotkeyed favourites* at most. Windows-GDI-style whole-bitmap
re-rasterisation on every change (`redrawBranchesBitmap`) — imgui-node-editor already handles layout/pan.

---

## Glossary — FCEUX term → nearest Flycast-dojo concept

Fork references are from a grep of `C:\_mvc2\other\flycast-dojo-7\core\dojo` (branch `tas-tools`), not a full read.

| FCEUX | Definition (source) | Flycast-dojo nearest | Fit |
|---|---|---|---|
| **Bookmark** (slot 0-9) | full-movie snapshot + one savestate + screenshot + keyframe (`bookmark.h:35-38`) | a savestate slot / clip `states[]` entry with sidecar (`dojo.cpp:665-721` seq + prefix hash) | partial — a fork state has no whole-movie copy attached; the branch folder is the copy |
| **Branch** | the movie stored inside a bookmark (Glossary.html) | branch folder (planned; today: clip folder with movie + states + `clip.json`, `tas_clip.cpp`) | good |
| **Current branch** (blue digit) | bookmark the working movie was last set to / deployed from (`branches.cpp:743, 749`) | the loaded branch folder | good |
| **Cloud** | root node = power-on frame 0 + project start time (`branches.cpp:229-231`, Toolbox.html) | no equivalent (needs a root node) — **not** the live roll | — |
| **Fireball** | working movie diverged from current bookmark; child of current bookmark; click = end of movie (`branches.cpp:849-859, 1013-1044`) | live roll (in-memory movie being written) | partial — FCEUX's is not a stored node |
| **Timeline (red line)** | cloud→…→timeline tip; bookmarks whose deploy would not change input (`:489-514, 794-847`) | the ancestor chain of the loaded branch | good if parent pointers exist |
| **Greenzone** | per-frame savestate array + head, truncated at first differing frame (`greenzone.cpp:572-590`) | dead-timeline guard + prefix hash (`IsStateStale`/`MoviePrefixHash`, `dojo.cpp:621-721`) | partial — fork validates stored states lazily; no per-frame array, rewind is re-emulation |
| **Greenzone head / green arrow ("last position")** | end of verified frames / frame where you stopped watching before editing (`playback.cpp:514-527`) | no equivalent | — |
| **Seeking / pause frame** | emulate forward from nearest stored state to a target, then pause (`playback.cpp:333-350, 485-512`) | fork's re-emulate-to-frame (pause + step) | partial |
| **Lag log** | per-frame "input not polled" flag, part of every snapshot/bookmark, restored on deploy (`laglog.cpp`, `history.cpp:780-798`) | frameskip ruler / `tas_ruler` skip map (`tas_ruler.h:6`) | analogous (measured vs fixed cadence) |
| **Hot changes** | 4-bit per-cell recency heat carried and faded through history (`inputlog.h:16-23`, `inputlog.cpp:583-637`) | no equivalent (the fork's `undo_stack` is the History Log, not a heat map) | — |
| **History Log** | ring of full snapshots + bookmark backups, `MODTYPE_*` (`history.h:142-146`) | `undo_stack` of `EditPatch` diffs with GUI meta (`dojo.h:245-251`, cap 128 `dojo.cpp:1452`) | partial — diffs vs full copies; fork's carries piano-roll bookmarks along like FCEUX carries bookmark backups |
| **Undo hint** | purple row at the undo keyframe for 200 ms (`history.h:3`, `history.cpp:361-367`) | no equivalent | — |
| **Marker + Note** | yellow row label with text, own history, saved in every snapshot (`markers.h`) | piano-roll bookmarks (`gui_meta` "bookmarks", `clip.json` "bookmarks", `tas_clip.cpp:236, 267`) | good — naming collision with FCEUX "Bookmark" |
| **Selection / Selection cursor** | `std::set<int>` rows, own undo ring (`selection.h:4`, `selection.cpp:415-477`) | Piano Roll selection | good |
| **Splicer** verbs | clone / insert / insert-# / delete / clear / cut / truncate / paste / paste-insert (`splicer.cpp`) | Piano Roll selection verbs | good; `insertNumberOfFrames` = the frame-skip-test primitive |
| **Snapshot** | full input log + lag log + markers (`snapshot.h`) | no single equivalent (movie file + ruler + bookmarks) | — |
| **Piano Roll** | the editable frame grid (`piano_roll.cpp`) | Piano Roll (`dojo_gui.h:48` F6) | good |
| **Playback cursor** | currently displayed frame (`currFrameCounter`) | paused frame | good |
| **.fm3** | FM2 + six offset-indexed module streams (`taseditor_project.cpp:74-181`) | clip folder + sidecars (`tas_clip.cpp`) | analogous |
| **Rerecord count** | number of Greenzone truncations (`greenzone.cpp:584`) | re-record seq / `rewinds` in `clip.json` (`tas_clip.cpp:236, 267`) | analogous |
| **Old control scheme** | F-key semantics depend on read-only; recording locked until a load (`taseditor.cpp:938-943`) | no equivalent (and none wanted) | — |
