# Branches / timelines for flycast-dojo-7 — research + design

Repo: `C:\_mvc2\other\flycast-dojo-7`, branch `tas-tools`. Read on 2026-09-04. Every claim about the repo carries a
`file:line` (absolute paths shortened to the repo-relative form below; all live under that root). Nothing was edited.

## 0. Verdict in ten lines

1. Everything slot- and clip-related resolves through ONE string, `hostfs::savestateFolderOverride`
   (`core/oslib/oslib.h:92`, resolver `core/oslib/oslib.cpp:133-142` and `:280-298`). Swapping that string
   in-session is already done by the Macros window (`core/dojo/dojo_gui.cpp:9379`) — a **checkout is that swap
   plus a movie reload plus a state load**, all of which exist as separate primitives.
2. There is no per-slot indirection and one movie append target (`Replay::filename`, `core/dojo/replay.cpp:172,260,284,313`),
   so a branch that shares a parent's files cannot be expressed without new plumbing. **Recommend: a branch is a
   full clip folder nested under its root clip (`<clip>/branches/<id>/`), created by copy** — the F8 `archive`
   copy loop (`core/dojo/tas_clip.cpp:156-175`) already does the copy; `main` = the live set at the top of the folder,
   the vocabulary `WriteClipStats` already uses (`core/dojo/dojo.cpp:978`).
3. **Commit = an F8 generation** taken inside the branch folder (unchanged mechanics, `core/dojo/dojo.cpp:1144-1178`);
   working tree = the live set; "dirty" = `restoredFrom.editedSince` (`core/dojo/dojo.cpp:904-915`). The movie's
   append-only `.flyr` (`core/dojo/replay.cpp:153-176`) and the `rewinds` log (`core/dojo/dojo.h:211`) are the
   fine-grained history underneath.
4. **Merge** has no textual 3-way; the safe semantics are a *prefix fast-forward* guarded by `MoviePrefixHash`
   (`core/dojo/dojo.cpp:621-642`, the same hash the v3 sidecar uses) and a *tail splice* through `ApplyEdit` /
   `ApplyEditResize` (`core/dojo/dojo.cpp:1371-1480`, `:1482-1573`) so the dead-timeline guard stales exactly the
   states a splice invalidates. A changed prefix below the branch frame is a real conflict = desync; refuse or flag.
5. **Jumping** between branches costs one `dc_loadstate` (~500 ms per `core/nullDC.cpp:295`) + a ~160 KB `.flyr`
   parse; no file copies on checkout, copies only on create.
6. **imgui-node-editor**: MIT, v0.9.3 (2023-10-14), master touched 2026-02-20; requires "Vanilla ImGui 1.72+",
   its version guards top out at `IMGUI_VERSION_NUM < 19002`; the repo's vendored ImGui is **1.90.4-docking
   (19040)** (`core/deps/imgui/imgui.h:26-30`). It reads `imgui_internal.h` (ImDrawList splitter/CmdBuffer/VtxBuffer,
   ImGuiWindow members) but nothing docking-specific; viewports are off here (`core/rend/gui.cpp:159-161`). **Feasible.**
   Vendor it exactly like `ImGuiColorTextEdit` (`CMakeLists.txt:704-715`). Risk lives in the fork's ButtonBehavior
   patch (`IMGUI_UPGRADE.md:21-36`) and in assert-free release builds.
7. Fallback: a hand-drawn git-lane graph with `ImDrawList` (1-2 days, no dependency). The graph is tiny (tens of nodes);
   node-editor is heavier than the problem but cheap to try — keep the renderer behind the same data model.
8. The N-way fan-out feature is cheapest as **one shared state + N in-memory variants** producing N outcome states in
   one folder; only variants you keep become branches (`create()` from the outcome slot). The dead-timeline guard
   will (correctly) grey the other outcomes — they *are* different timelines.
9. The one primitive that does not exist yet: an in-session "attach this `.flyr`" (`Replay::Init` is boot-only,
   `core/rend/gui.cpp:928-929`, and arms boot pauses `core/dojo/replay.cpp:128-134`). It is ~30 lines around
   `LoadReplayFileV1` (`core/dojo/replay.cpp:522-570`).
10. Phase 1 (create + checkout, no graph, no merge) is 3-5 days including a headless harness; the graph is 2-3 more;
    merge 2-3; fan-out 2-3.

---

## 1. Existing infrastructure

### 1.1 Primitive table

| Primitive | Where (file:line) | What it does / constraint it imposes |
|---|---|---|
| Clip folder creation | `core/dojo/replay.cpp:374-434` | `replays/<game>/<ISO-ts>/` (`:376-386`), `.flyr` named `<rom>__<ts>__<p1>__<p2>__.flyr` (`:388-396`), `savestateFolderOverride = clip_dir` (`:408`), slot forced to 0 via Option + virtual cfg (`:412-417`), `BeginClipStats` (`:419`), `tas_clip::seed` (`:424`), scratch states wiped (`:428`) |
| Savestate → clip binding | `core/oslib/oslib.h:92`; `core/oslib/oslib.cpp:109`, `:133-142` (`savestateDir()`), `:280-298` (`getSavestatePath`: `<game>.state` = slot 0, `<game>_N.state` = N, `.net` for index -1) | The override wins over the data path for read AND write. `scanSavestateInfo` (`:182-248`) reads the whole folder once and parses sidecars v1/v2/v3 (`:225-244`). `deleteSavestate` (`:329-341`) owns the sidecar set |
| State write / read | `core/nullDC.cpp:130` (path), `:145-161` (RZipFile, in-place write), `:165` (`SaveStateFrame`); load `:242-299` (`LoadStateFrame` at `:294`, idempotency verify `:298-299`, "~500 ms state load" `:295`) | States are ~10-28 MB (`core/dojo/tas_clip.cpp:153-155`; example 27,793,035 B `CLIP_SCHEMA.md:40`). Because RZipFile rewrites in place, **hard links between branches are unsafe** |
| GUI load / save | `core/rend/gui.cpp:4553-4590` (`gui_loadState`: empty-slot warn `:4560-4568`, `emu.stop()` `:4576`, `dc_loadstate` `:4577`, stays Paused `:4581`, `tas_auto::stopLive` `:4585`); `:4592-4628` (`gui_saveState`: thumbnail `:4607`, wave snapshot `:4610`, wave/ruler flush `:4611-4612`, `savestate_epoch++` `:4615`, `WriteClipStats` `:4618`) | Both require Closed/Paused/ReplayEnd and `savestateAllowed()` |
| Replay playback init | `core/dojo/replay.cpp:7-135` | Boot-only: live cfg read (`:14`), `LoadReplayFile` (`:15`), override = parent of the `.flyr` (`:22`), slot 0 (`:29-34`), `BeginClipStats` (`:36`), forces `RecordMatches/Transmitting=no` (`:107-108`), `play_match=true` (`:113`), arms the boot pause + State-0 seek (`:128-134`) |
| `.flyr` writer | `core/dojo/replay.cpp:233-296` (`AppendToReplay`: 120-frame batches `:249`, gated on `filename` not the mode flag `:258-264`); `FlushReplay` `:303-325`; `AppendEditedFrames` `:153-176` (append, last-write-wins, "edits never rewrite the movie in place" `:158-159`); `RewriteReplayFile` `:178-231` (the only shrink path; header reproduced verbatim `:185-186`, `file_header` `replay.h:25-28`) | **One append target per process** (`Replay::filename`, `replay.h:10`; `HasAppendTarget` `:18-20`; `DetachFile` `:24`). On disk the movie lags the roll by ≤119 frames until a flush (`:298-302`) |
| `.flyr` parser | `core/dojo/replay.cpp:522-570` → `Dojo::ProcessBody` `core/dojo/dojo.cpp:2564-2585` | `session_inputs[frame_num] = inputs` (`:2585`) — appends INTO whatever the map already holds (last-write-wins). Captures `file_header` (`replay.cpp:553-558`). 28-byte records: u32 frame + 24 B (`dojo.h:41`) |
| The roll | `core/dojo/dojo.h:104-106` (`frame_number` atomic, `session_inputs`, `rec_inputs`); `MovieEnd()` `:151`; `stale_tail_from` `:115-119`; `load_seq` `:122`; `macro_armed` `:299-303` (READ-WRITE vs WRITE axis); `rerecord_count/rerecord_base` `:361-362`; `rewind_log` `:211`; undo/redo stacks `:241-255`; `locked_slots`/`base_prelock` `:345-350`, emu-side snapshot `:354-356`; `savestate_epoch` `:381-383` | `session_inputs` has no mutex: the emu thread writes it in `PollRecordAction` while running (`dojo.cpp:436`); GUI writes need the emulator STOPPED (`dojo_gui.cpp:9368-9371`) |
| Record path | `core/dojo/dojo.cpp:390-451` | Timeline event fires on the first byte-CHANGING overwrite (`:409-429`: `rerecord_count++`, `rewind_log` append, `WriteClipStats`), cell write `:436`, stale-tail marker advance `:437-438`, live macro autosave `:440-451` |
| Sidecar `.frame` | write `core/dojo/dojo.cpp:656-679` (v1 `frame`, v2 `+seq +movieLen`, v3 `+prefixHash`); read `:681-796` | Load = time travel in both modes: warn if stale (`:721-729`), `frame_number = fn` (`:740`), wave `onStateLoad` (`:741`), `load_seq++` (`:742`), re-arm divergence (`:743-751`), **WRITE load does NOT truncate — marks `stale_tail_from = fn`** (`:752-766`), `WriteClipStats` (`:767`), READ dead-end warning when the state is at/past the end (`:768-782`) |
| Dead-timeline guard | `core/dojo/dojo.cpp:611-617` (seq rule), `:621-642` (`MoviePrefixHash`: FNV-1a over every packet below `frame`), `:644-654` (hash exoneration); GUI memo `core/rend/gui.cpp:4726-4749`; purge `:4767-4800` | Exact rule: stale iff some rewind `(seq > stateSeq, frame < stateFrame)`; identical prefix bytes exonerate. **This is the mechanism that makes branch/merge validity automatic** |
| Edit funnel | `core/dojo/dojo.cpp:1371-1480` (`ApplyEdit`: refuses truncation `:1376-1384`, diff `:1386-1398`, no-op guard `:1399-1405`, locked ranges filter `:1407-1434`, undo capture `:1436-1455`, apply `:1457-1458`, stale-tail `:1463-1464`, timeline event `:1466-1473`, `.flyr` append `:1474`); `ApplyEditResize` `:1482-1573` (shrink; refuses to shift a locked range `:1511-1527`; rewrites the file `:1567`) | The only sanctioned way a non-recording write reaches the roll. Undo/redo replay through it (`:1575-1579`) |
| Clip stats | `BeginClipStats` `core/dojo/dojo.cpp:818-858` (zeroes counters `:820-823`, clears `rewind_log` `:824`, live-from `:825-827`, wave/ruler `loadClip` `:833-834`, reconcile `:835`, reads `stats.rerecords` → `rerecord_base` `:841`, `rewinds` `:842-846`, `restoredFrom` `:850-855`); `WriteClipStats` `:860-1052` (frames `:887-892`, rerecords `:896`, editSeconds `:897-898`, editedSince `:904-915`, `states[]` `:926-957`, `rewinds` `:959-964`, macro block `:967-977`, `contents` manifest `:978-1025`, `schema=6` `:1026`, write-on-difference `:1032-1051`) | Keyed on `savestateFolderOverride` (`:831`, `:862`) — **after an override swap these functions describe the NEW folder** |
| Generations (F8) | `ArchiveGeneration` `core/dojo/dojo.cpp:1144-1178`; `ArchiveClipDir` → `tas_clip::archive` `core/dojo/tas_clip.cpp:107-183`; `RecordGeneration` `dojo.cpp:1093-1135`; `reconcile` `tas_clip.cpp:405-552`; `restore` `:185-284` + `RestoreClipDir` `dojo.cpp:1071-1086`; `deleteGenerations` `tas_clip.cpp:554-630`; `loadGenerations` `:883-923`; folder predicate `genFolderKind` `:71-92` | See §1.2 for exactly what a gen carries |
| clip.json IO | `tas_clip::read` `core/dojo/tas_clip.cpp:639-654`; `write` `:656-681` (tmp+rename, bumps `libraryVersion` `:679`); `libraryVersion/bump` `:21-29`; `appendGeneration` `:811-822`; `renameMeta` `:824-866`; `seed` `:868-881`; one `clipMutex` `:403` | Rules: functions take a DIRECTORY, are pre-boot safe, and read-modify-write (`tas_clip.h:18-19`) |
| In-session clip swap (precedent) | `core/dojo/dojo_gui.cpp:9372-9401` (`macroLoadFull`) gated at `:9772-9778` (Paused, `!play_match`, has State 0) | Sets `macro_armed`, **re-points the override** (`:9379`), `BeginClipStats` (`:9380`), slot 0 + `gui_loadState` (`:9381-9382`), replaces the roll relative to the loaded frame (`:9386-9390`), resets `stale_tail_from` (`:9388`), re-points `loaded_macro_path` (`:9391-9392`). Pre-boot twin: `LoadMacroFull` `core/dojo/dojo.cpp:1793-1842`, `LoadClipState0Boot` `:1849-1867`, injection `:1873-1884`, handoff `core/rend/gui.cpp:4392-4444` |
| Boot paths | `gui_start_game` `core/rend/gui.cpp:883-887` (`dojo.Reset()`, override cleared), `Replay::Init` call `:928-929`, roll cleared for write sessions `:936-938`; Replays-browser staging `core/dojo/dojo_gui.cpp:19590-19596` (`dojo:Replay=yes`, `ReplayFilename=<flyr>`); "Restart replay" `:931-935` | A branch's `.flyr` can be booted TODAY by staging its path — the folder is derived from it (`replay.cpp:22`) |
| Stepping | `gui_open_step` `core/rend/gui.cpp:5893-5919`; `gui_step_frames(n)` `:5923-5945` (used by the Input Sender's Auto-Send loop `:5921-5922`); the OSD stop `:4341` → `emu.stop()` + Paused `:4390-4391`; macro READ auto-pause `core/dojo/dojo.cpp:1933-1942` | The "run exactly N frames then hold" primitive a stagger loop needs; it is asynchronous (the stop happens on the render thread) |
| Session teardown | `Dojo::Reset` `core/dojo/dojo.cpp:2808-2890` (`FlushReplay` `:2834-2835`, `DetachFile` `:2836`, `MacroFlush` `:2843`, per-session flags `:2847-2871`, wave/ruler `saveClip`+`reset` `:2872-2875`, auto-fire cleared `:2884-2885`) | The list of session-scoped state a checkout must ALSO handle (it is the union of "what a boot resets") |
| Paused autosave tick | `core/rend/gui.cpp:3972-3996` | Every 2 s while Paused: wave/ruler `saveClip` on the CURRENT override (`:3985`, `:3991`) + `FlushReplay` (`:3994-3995`) |
| Caches keyed on the clip / epochs | slot scan `core/rend/gui.cpp:4707-4722` (`savestate_epoch` or 0.5 s); thumbnails `metaDir` `:4671-4676`; stale memo `:4734-4747`; `gen_count` `core/dojo/dojo_gui.cpp:15814-15836`; bookmarks `:2679-2684`; Macros rescan on `libraryVersion` `:9018-9027`; snapshots pane on `libraryVersion` `:11719-11727`; States totals check `clipUiStates.gens.dir == override` `:16537` | A checkout must bump `dojo.savestate_epoch` (`dojo.h:383`) and `tas_clip::bump()`; the override-keyed caches re-key themselves |
| Studio-module plumbing | `tasStudioMode` `core/dojo/dojo_gui.cpp:15842-15845`; `tasWindowUiZoom` `:6704-6722` (+ export `:6724`), `tasZoomKeyName` `:6660-6674`, `tasResetAllZoom` `:6690-6699`, `tasSelectedKey` `:6654`; open-flag persistence `:15741-15804` (`dojo:TimelineOpen` etc.), States' own flag `core/rend/gui.cpp:5456-5468` (`dojo:StatesOpen`); Windows menu `:17703-17729`; View `:17731-17741`; per-window owner menus keyed on `tasSelectedKey` `:17326-17359`; dockspace host `core/rend/gui.cpp:4188-4235` (default layout `:4209-4220`); draw sites Paused `:4131-4142` and OSD `:4300-4302`; window skeleton `:11880-11887` (Macros); Timeline `Begin` `:17808-17813`; `ClipUiHost` request dispatcher `:9081-9107`, `tasClipPopups` `:11619`; exports `core/rend/gui.h:57-84` | See §1.6 |
| Hotkey chain | `core/input/gamepad.h:51-52,102-108` (enum), `core/input/gamepad_device.cpp:169-176,234-276` (handlers), `core/input/keyboard_device.h:68-70` (defaults), `core/input/mapping.cpp:61-75` (persistence), `TAS_HOTKEYS[]` `core/dojo/dojo_gui.cpp:1090-1093`, `migrate()` rule `CLAUDE.md:447` | For "next/prev branch" and "new branch" keys |
| Outcome probes | `tas_mvc2::read()` `core/dojo/mvc2.h:26-42` (latched inputs, skip rate/count, scene/total frames); `fidelity.jsonl` per-frame dump `core/dojo/dojo.cpp:1312-1339` | What a fan-out comparison can key on besides thumbnails |
| Vendored ImGui | `core/deps/imgui/imgui.h:26-30` (`"1.90.4"`, `19040`, `IMGUI_HAS_DOCK`); `CMakeLists.txt:704-715` (sources + include dirs, `ImGuiColorTextEdit` precedent); `imconfig.h:93-103` (`IM_VEC2_CLASS_EXTRA` and `IMGUI_DEFINE_MATH_OPERATORS` both commented out); `gui_init` `core/rend/gui.cpp:148-161` (`imgui.ini` next to emu.cfg unless `dojo:UiIni=no`; `DockingEnable`, viewports off); patch inventory `IMGUI_UPGRADE.md:9-35`; frame-stream rules `CLAUDE.md:425-448` | See §2 |

### 1.2 What an F8 generation captures — and what it does not

`tas_clip::archive` (`core/dojo/tas_clip.cpp:107-183`) creates `<clip>/<clip>_gen_NN` (`:142-150`; number = one past the
highest ever used on disk OR in clip.json `:113-141`) and copies **every regular file** whose extension is in
`.flyr .flyreplay .state .frame .json .png .label .txt .env .wave .map` (`:156-175`, list at `:161-162`).

Captured:
- the movie **as it is on disk** — which lags the roll by up to 119 frames unless flushed (`core/dojo/replay.cpp:298-302`).
  `ArchiveGeneration` calls `WriteClipStats()` first (`core/dojo/dojo.cpp:1155`) but **not** `FlushReplay()`; only the
  Paused 2-s tick (`core/rend/gui.cpp:3994-3995`) and teardown (`dojo.cpp:2834-2835`) flush. The comment at
  `dojo.cpp:798-804` acknowledges this ("may lag the movie by up to a batch");
- every `.state` present (no slot ceiling — `SlotCycleCount` is a cycle limit only, `CLAUDE.md:352-354`) with its
  `.frame` / `.png` / `.label` / `.wave` sidecars;
- `clip.json` **as a snapshot of that moment** (the backfill reads it back: `tas_clip.cpp:325-399`);
- `<clip>_macro.txt` and any other clip-scoped `.txt` (snippets saved into the clip, `dojo_gui.cpp:11825-11839`);
- `audio.env`, `skip.map`.

NOT captured:
- sub-folders (`:158-159`): other gens, `.trash/`, and a future `branches/`;
- the in-memory roll beyond the flushed file, the undo/redo stacks, `locked_slots` (session-only — `WriteClipStats`
  never writes them), the Notepad buffer, `tas_auto` arms;
- captures (`.mov/.avi`) and `fidelity.jsonl` (extensions not in the list).

The record (`RecordGeneration`, `core/dojo/dojo.cpp:1093-1135`): `gen kind name files bytes slotCycle createdUtc/Local
atFrame movieFrames rerecords mode slots slotFrames tags notes present` (`:1101-1131`). Immutable except tags/notes
(`CLIP_SCHEMA.md:84,94`). Reconcile (`tas_clip.cpp:405-552`) synthesizes records for unrecorded folders (`:471-494`),
flags missing ones `present:false` (`:512-520`), sorts by `createdUtc` then number (`:531-536`), and writes only on a
difference (`:545-550`). `slotFrames` was explicitly added as "branching groundwork" (`CLIP_SCHEMA.md:83`).

### 1.3 Restore — and why it is pre-boot only

`tas_clip::restore` (`core/dojo/tas_clip.cpp:185-284`): live-only state files → `<clip>/.trash/<utc>/` (`:191-224`),
every backup file except clip.json copied over (`:225-234`), clip.json MERGED (`:235-270`: `stats.frames/durationSeconds`,
`rewinds`, `bookmarks`, `states`, macro pairing from the backup; `rerecords = max`; generations/tags/notes/identity kept),
`restoredFrom` written (`:271-278`). `Dojo::RestoreClipDir` **refuses the clip open in the session**
(`core/dojo/dojo.cpp:1073-1081`): "the loaded movie, the replay writer, the rewind log, undo, bookmarks, the wave / skip
stores and the loaded macro all stay stale in memory and their next write undoes the restore". The popup flow with the
`auto, pre-restore` backup: `core/dojo/dojo_gui.cpp:11546-11614`.

That list of in-memory owners is exactly the checklist a checkout has to honour (§4C, §5) — the difference is that a
checkout never overwrites the open folder; it switches folders and re-seeds memory from the new one, which is what a
boot does.

### 1.4 The in-session swap precedent

`macroLoadFull` (`core/dojo/dojo_gui.cpp:9372-9401`) already performs, while Paused: override swap → `BeginClipStats`
→ slot 0 → `gui_loadState` → roll replaced → `stale_tail_from` reset → macro path re-pointed. Its gate
(`:9772-9778`): `gui_state == Paused && !play_match && hasState0`. What it does NOT do (because a macro session has no
`.flyr` writer): re-point `Replay::filename`, flush the previous file, reload a movie from disk, save the previous
clip's wave/ruler stores, clear undo. Those are the additions a real checkout needs.

### 1.5 Caches and epochs a checkout must invalidate

| Cache | Key | Action on checkout |
|---|---|---|
| `slotScan()` `core/rend/gui.cpp:4707-4722` | `dojo.savestate_epoch` / 0.5 s | `savestate_epoch++` |
| thumbnails `metaDir` `:4671-4676` | `savestateFolderOverride` | automatic |
| stale memo `:4734-4747` | `slotScanGen`, `rerecord_count`, `rewind_log.size()` | automatic once the scan rebumps |
| `gen_count` `core/dojo/dojo_gui.cpp:15814-15836` | dir + epoch | automatic |
| bookmarks `:2679-2684` | override | automatic |
| Macros list `:9018-9027`, snapshots pane `:11719-11727` | `tas_clip::libraryVersion()` | `tas_clip::bump()` (every `write` bumps already) |
| States totals `:16537` | `clipUiStates.gens.dir == override` | automatic |
| `locked_ranges_cache` `core/dojo/dojo.h:354` | published by `gui_locked_ranges` each frame (`dojo_gui.cpp:17752`) | automatic next frame |

### 1.6 Registering a new dockable studio module (the checklist, from the States module)

1. Open flag: a `static bool` loaded once from cfg and saved on toggle — `core/dojo/dojo_gui.cpp:15741-15804`
   (`dojo:TimelineOpen` etc.) or the gui.cpp variant `core/rend/gui.cpp:5456-5468` (`dojo:StatesOpen`).
2. Window: `ImGui::Begin("<Name>", &open)` + `ImGui::SetWindowFontScale(tasWindowUiZoom("<Name>UiScale"))`
   (`:11880-11887`); studio vs pinned decided by `tasStudioMode()` (`:15842-15845`, Timeline example `:17801-17821`).
3. Draw it in BOTH frame streams, once per frame: from `show_tas_tool_windows` which is called on the Paused path
   (`core/rend/gui.cpp:4141`) and the OSD path (`:4301`) — never before the stream's `NewFrame` (`CLAUDE.md:429-437`).
4. Windows menu entry (`:17715-17727`), zoom name (`:6660-6674`) and reset table (`:6692-6697`), an owner menu keyed on
   `tasSelectedKey` (`:17326-17359`), a `DockBuilderDockWindow` line in the default layout (`core/rend/gui.cpp:4212-4220`).
5. Hotkeys must respect `gui_keyboard_captured()` semantics (`CLAUDE.md:136-138`, `IMGUI_UPGRADE.md:39-43,55-59`).

### 1.7 Vendored ImGui / CMake

`core/deps/imgui/` is in-tree (not a submodule), 1.90.4-docking (`imgui.h:26-30`), compiled from `CMakeLists.txt:708-715`
with include dirs at `:706-707`; `ImGuiColorTextEdit` is vendored the same way (`:707`, `:714-715`) — the template for
imgui-node-editor. Local ImGui patches: `ImGuiWindowFlags_DragScrolling`, `ImGuiWindow::DragScrolling/ScrollSpeed`, and a
30-line `ButtonBehavior` drag detector (`IMGUI_UPGRADE.md:19-27`; corrected so `ClearActiveID` fires only when a
DragScrolling window is the target `:31-36`). `imgui.ini` persists next to emu.cfg unless `dojo:UiIni=no`
(`core/rend/gui.cpp:148-155`); every headless harness passes that (`IMGUI_UPGRADE.md:60-62`).

---

## 2. imgui-node-editor verdict

### 2.1 Facts (fetched 2026-09-04)

- Repo: https://github.com/thedmd/imgui-node-editor. **License: MIT** (LICENSE: "MIT License", 2019, Michał Cichoń);
  the source headers say "VERSION 0.9.1" with a public-domain/perpetual-licence preamble.
- Releases (GitHub API): **v0.9.3 2023-10-14** ("bugfix release … problems with ImCanvas after internals of ImGui changed"),
  v0.9.2 2023-09-01 ("Fix for broken clipping … with Dear ImGui 1.89+", "Support ImGui r18836 after
  SetItemUsingMouseWheel removal", "Define IMGUI_DEFINE_MATH_OPERATORS before <imgui.h>", "Don't use deprecated
  SetItemAllowOverlap", "Use ImGuiKey directly with ImGui r18822"), v0.9 2019-07-16, v0.1-prototype 2019-06-21.
  Latest commit on `master`: `021aa0e`, **2026-02-20**, "minor styling change". `develop`: `b302971`, 2024-07-21,
  "Examples: Minimum supported version is now Dear ImGui 1.89".
- README requirement: **"Vanilla ImGui 1.72+"**, C++14, "copy&paste sources into your project". No CMake target for the
  library itself (examples only).
- Files to vendor (all at the repo root): `imgui_node_editor.h`, `imgui_node_editor.cpp`, `imgui_node_editor_api.cpp`,
  `imgui_node_editor_internal.h`, `imgui_node_editor_internal.inl`, `imgui_canvas.h/.cpp`, `imgui_extra_math.h/.inl`,
  `imgui_bezier_math.h/.inl`, `crude_json.h/.cpp`.
- Internals: the public header includes only `<imgui.h>`. `imgui_node_editor_internal.h` does
  `#define IMGUI_DEFINE_MATH_OPERATORS` then includes `imgui.h`, **`imgui_internal.h`**, the math headers, the canvas
  and `crude_json`. `imgui_extra_math.h` also includes `imgui_internal.h`. Internal symbols used: `ImGui::GetCurrentWindow`,
  `ImGuiWindow::SkipItems/DC.CursorPos/DC.CursorMaxPos/Pos`, `ItemSize/ItemAdd/CalcItemSize`, `ButtonBehavior`,
  `GetActiveID/SetActiveID`, `PushClipRect/PopClipRect`, `ImDrawListSplitter`, `ImDrawList::_Splitter/_Channels/_Current/
  CmdBuffer/VtxBuffer/_VtxCurrentIdx/_ClipRectStack`, `ImGui::GetWindowViewport()` + `viewport->WorkPos/WorkSize`,
  `SetNextItemAllowOverlap`, `SetItemKeyOwner(ImGuiKey_MouseWheelY)`.
- Version guards found: `IMGUI_VERSION_NUM < 18822` (keys), `> 18101` (round-corner flags), `>= 18836` /
  `>= 17909` (wheel ownership), `> 18415` (`IsClippedEx` signature), `>= 18967` (`SetNextItemAllowOverlap`),
  `> 18002` (viewport `WorkPos`), `< 18955` (unary `operator-`), `< 19002` (`==`/`!=` on ImVec2). **No guard above 19002.**
- Docking: no docking or viewport-specific code. Issue #118 (2021, "add docking feature support") is open with no
  maintainer reply; issue #137 ("Node won't render", ImGui docking 1.83) is closed without a documented cause; a `docking`
  branch exists in the repo (297 commits) but its contents could not be verified from the fetch. Strongest evidence of
  docking-branch operation: pthom's fork branch `imgui_bundle` (https://github.com/pthom/imgui-node-editor, branches
  include `imgui_bundle`, `fix_imgui_v1.92.8`) ships inside ImGui Bundle, whose UI is docking-based; discussion #66 there
  states the fork carries "a patch for an issue inside `ed::EditorContext::Begin`" and a `std::string SettingsFile`
  variant; discussion #428 (pthom, 2026-01-09): "I maintain an active fork … Regular updates to stay compatible with
  latest ImGui".
- Settings: `Config.SettingsFile` (default `"NodeEditor.json"`), callbacks `SaveSettings/LoadSettings/SaveNodeSettings/
  LoadNodeSettings` with `UserPointer`, `BeginSaveSession/EndSaveSession`; the JSON stores node `location`, `group_size`,
  the `view` (scroll/zoom/visible_rect) and the `selection`. Save is triggered from `End()` when dirty and no action is in
  flight. A node's position must be `SetNodePosition`'d before its first `BeginNode` or it comes from the saved settings.
- API needed here: `CreateEditor/DestroyEditor/SetCurrentEditor`, `Begin/End`, `BeginNode/EndNode`, `BeginPin/EndPin`
  (`PinKind::Input/Output`), `Link(id, a, b, color, thickness)`, `Flow`, `SetNodePosition/GetNodePosition/CenterNodeOnScreen`,
  `NavigateToContent/NavigateToSelection`, `SelectNode/GetSelectedNodes`, `GetHoveredNode`, `IsBackgroundClicked`,
  `ShowNodeContextMenu/ShowBackgroundContextMenu`, `Suspend/Resume` (required around any ImGui popup/tooltip drawn inside
  the canvas), `PushStyleColor(StyleColor_NodeBg/NodeBorder/HovNodeBorder/SelNodeBorder/…)`, `EnableShortcuts`,
  `GetCurrentZoom`.

### 2.2 Compatibility judgment against 1.90.4-docking + the fork's patches

- **Version**: 19040 sits inside the range the code was last adapted to (guards up to 19002; v0.9.3 fixed the 1.89+
  canvas changes; develop pins the examples at ≥1.89). No known 1.90-specific break was found. `imgui_extra_math.h`'s
  operator overloads are all compiled OUT at 19040 (the `< 18955` / `< 19002` guards), so nothing collides with
  `imgui.h`'s own operators. `imconfig.h` defines no `IM_VEC2_CLASS_EXTRA` (`:93-101` commented) and no global
  `IMGUI_DEFINE_MATH_OPERATORS` (`:103` commented) — node-editor's TUs define it themselves before their first `imgui.h`.
- **Docking branch**: the `ImGuiViewport`/`ImGuiWindow`/`ImDrawList` members it touches exist unchanged in the docking
  branch; viewports are off (`core/rend/gui.cpp:159-161`), so `GetWindowViewport()` is the main viewport. The canvas
  transforms `VtxBuffer` and clip rects in place — the only interaction with docking is that a docked window's
  `ImGuiWindow::Pos`/clip rect are whatever the dock node gives it, which is the same contract as a floating window.
  Expect it to work; keep pthom's `Begin` patch in the back pocket if a docked canvas misclips.
- **Fork patches**: node dragging is `ButtonBehavior` + `SetActiveID`; the fork's `ButtonBehavior` drag detector now
  clears the active id only when a `DragScrolling` ancestor exists (`IMGUI_UPGRADE.md:31-36`). **Never give the Branches
  window `ImGuiWindowFlags_DragScrolling`** and node drags are untouched.
- **Two frame streams** (`CLAUDE.md:429-437`): one `ed::EditorContext`, `ed::Begin/End` once per frame in whichever
  stream draws the window — the same "exactly one stream per frame" rule every module already obeys.
- **Wheel zoom**: node-editor owns the wheel (`SetItemKeyOwner(ImGuiKey_MouseWheelY)`); `tasWindowUiZoom`'s Ctrl+wheel
  (`core/dojo/dojo_gui.cpp:6711-6720`) would fight it over the canvas — scope the zoom hook to the header row.
- **DPI/zoom**: the canvas zooms geometry (vertex transform), not fonts; text blurs at high zoom. Acceptable for a graph
  with ~50 nodes; the Ctrl+wheel font-scale idiom does not apply inside the canvas.
- **Release builds compile asserts out** (`IMGUI_UPGRADE.md:50-52`): a Suspend/Resume imbalance or `ed::Begin` outside
  a window corrupts silently. Prototype once with `IM_ASSERT` enabled.
- **Settings file**: default `NodeEditor.json` in the CWD. Use the callbacks and store the layout string in
  `<clip>/branches/graph.json` (or in the root clip.json `branchGraph`) so it travels with the clip; obey `dojo:UiIni=no`
  by installing no-op callbacks for harness runs.

**Verdict: feasible, moderate risk, ~2-3 days including vendoring.** The node-editor gives pan/zoom, selection,
context menus and links for free; it gives no auto-layout (positions are ours: x = frame or time, y = lane).

### 2.3 Vendoring steps (exact)

1. `core/deps/imgui-node-editor/` ← the 13 files from `thedmd/imgui-node-editor@master` (v0.9.3+; record the sha in a
   `VERSION.txt` like the ImGui swap did in `IMGUI_UPGRADE.md`).
2. `CMakeLists.txt` after line 715 (inside the `if(NOT LIBRETRO)` block):
   ```cmake
   target_include_directories(${PROJECT_NAME} PRIVATE core/deps/imgui-node-editor)
   target_sources(${PROJECT_NAME} PRIVATE
       core/deps/imgui-node-editor/imgui_node_editor.cpp
       core/deps/imgui-node-editor/imgui_node_editor_api.cpp
       core/deps/imgui-node-editor/imgui_canvas.cpp
       core/deps/imgui-node-editor/crude_json.cpp)
   ```
   (`core/deps/imgui` is already on the include path at `:706`, so its `#include <imgui_internal.h>` resolves.)
3. Do NOT add `IMGUI_DEFINE_MATH_OPERATORS` globally; the library defines it per-TU. If a warning about
   "IMGUI_DEFINE_MATH_OPERATORS defined after imgui.h" appears in a flycast TU that includes `imgui_node_editor.h`
   after `imgui_internal.h`, include the node-editor header first in that TU.
4. Build once with asserts (`-DIMGUI_DISABLE_DEMO_WINDOWS` stays; add `-UNDEBUG` for the prototype) and run the demo
   "Simple" example body inside a dockable window to validate docking + the ButtonBehavior patch before writing the graph.
5. Add the folder to the `.github` packaging only if it ships headers — not needed (static).

### 2.4 Drawing the branch graph (sketch)

```cpp
// core/dojo/tas_branch_gui.cpp  (drawn from show_tas_tool_windows, both streams)
namespace ed = ax::NodeEditor;
static ed::EditorContext *g_ed = nullptr;
static std::string g_layoutJson;                    // persisted via tas_branch::saveGraphLayout()
static bool BranchLoad(char *data, void *) {...}    // copy g_layoutJson (size when data==nullptr)
static bool BranchSave(const char *d, size_t n, ed::SaveReasonFlags, void *) { g_layoutJson.assign(d, n); tas_branch::saveGraphLayout(g_layoutJson); return true; }

void tasBranchesWindow(float scaling) {
    if (!branchesOpen) return;
    if (!ImGui::Begin("Branches", &branchesOpen)) { ImGui::End(); return; }
    ImGui::SetWindowFontScale(tasStudioWindowZoom("BranchesUiScale"));      // header only; the canvas owns the wheel
    if (!g_ed) { ed::Config c; c.SettingsFile = nullptr; c.LoadSettings = BranchLoad; c.SaveSettings = BranchSave; g_ed = ed::CreateEditor(&c); }
    const tas_branch::Graph& g = tas_branch::graph();   // nodes: heads + generations; edges: parent links (cached on libraryVersion)
    ed::SetCurrentEditor(g_ed);
    ed::Begin("##branchcanvas", ImVec2(0, 0));
    for (const auto& n : g.nodes) {
        if (n.fresh) ed::SetNodePosition(n.id, ImVec2(n.frame * 0.25f, n.lane * 90.f));   // git lanes: x = movie frame, y = branch
        const bool head = (n.dir == hostfs::savestateFolderOverride && n.isTip);
        if (head) ed::PushStyleColor(ed::StyleColor_NodeBorder, TAS_ACCENT);
        ed::BeginNode(n.id);
            ed::BeginPin(n.inPin, ed::PinKind::Input);  ImGui::Dummy(ImVec2(6, 6)); ed::EndPin(); ImGui::SameLine();
            ImGui::TextUnformatted(n.title.c_str());        // "main" / "tempest-alt" / "gen 03"
            ImGui::TextDisabled("f %u  %d states  rr %u", n.frame, n.states, n.rerecords);
            ImGui::SameLine(); ed::BeginPin(n.outPin, ed::PinKind::Output); ImGui::Dummy(ImVec2(6, 6)); ed::EndPin();
        ed::EndNode();
        if (head) ed::PopStyleColor();
    }
    for (const auto& e : g.edges) ed::Link(e.id, e.fromOut, e.toIn, e.isBranchFork ? TAS_SELECT_COL : TAS_DIM, 2.f);
    ed::NodeId ctx;
    ed::Suspend();                                      // REQUIRED around ImGui popups inside the canvas
    if (ed::ShowNodeContextMenu(&ctx)) ImGui::OpenPopup("branchnode");
    if (ImGui::BeginPopup("branchnode")) { /* Checkout / New branch here / Merge into… / Delete */ ImGui::EndPopup(); }
    ed::Resume();
    ed::End();
    ed::SetCurrentEditor(nullptr);
    ImGui::End();
}
```
Node ids: a small registry mapping `(dir, genName)` → stable `uintptr_t` (hash) so ids survive rescans. Double-click a
node → `tas_branch::checkout(dir, slot)`; drag from a node's out pin onto the background → "new branch from here"
(`ed::BeginCreate()/QueryNewNode()` gives this for free).

### 2.5 Pitfalls (node-editor specific)

1. Popups/tooltips/menus inside the canvas without `Suspend/Resume` are drawn in the transformed space.
2. `SettingsFile` writes `NodeEditor.json` in the CWD unless nulled — must follow the `dojo:UiIni` rule.
3. Zoom scales geometry; use small text and lean on tooltips.
4. It consumes the mouse wheel and drag on the background; hotkeys are unaffected but the Ctrl+wheel window-zoom idiom is.
5. No auto-layout; positions come from our lane algorithm on first sight and from settings afterwards — a node added later
   gets `SetNodePosition` once (`fresh`), never every frame (that would make it undraggable).
6. Assert-free release builds hide misuse.

### 2.6 Fallback: hand-drawn lane graph with ImDrawList

A git graph is a lane diagram: one row per branch, x = frame (or created time), circles = gens/heads, a Bézier from the
parent's fork point into the child lane. Everything needed is in `ImDrawList` (`AddLine/AddBezierCubic/AddCircleFilled/
AddCircle/AddText`) plus one `ImGui::InvisibleButton` per node for hover/click/double-click, inside a scrollable child.
The fork already has drag-on-void scrolling (`gui_util.cpp windowDragScroll()/scrollWhenDraggingOnVoid()`,
`IMGUI_UPGRADE.md:66-68`) and the Timeline's strip draws stale/locked verdicts the same way (`core/rend/gui.cpp:4993,5311`).
Zoom = a scale factor on x plus `SetWindowFontScale`. No dependency, no internals, no settings file. ~300-400 lines.

### 2.7 Effort

| Item | Estimate |
|---|---|
| Vendor + compile + assert-build smoke test | 0.5 d |
| Node-editor graph (nodes, links, HEAD, context menu, layout persistence, lane layout) | 1.5-2 d |
| Risk buffer (docked-canvas clipping, ButtonBehavior patch, settings plumbing) | 0.5-1 d |
| **Fallback lane graph** (same data model, no dependency) | **1-2 d** |

Opinion: build the data model (`tas_branch::graph()`) first; try node-editor because David wants to; keep the lane
renderer as the shipping default if the docked canvas misbehaves. The graph is tiny; the *backend* is where the days go.

---

## 3. Proposed branch model

### 3.1 Decision: a branch is a full clip folder, nested under its root clip

Options weighed:

| Model | Pros | Cons (cited) |
|---|---|---|
| **Copy-on-branch: `<clip>/branches/<id>/` is a complete clip folder** (movie + states + sidecars + clip.json + macro) | Every existing per-folder machinery works unchanged per branch: slots (`oslib.cpp:280-298`), F8 gens (`dojo.cpp:1144`), restore (`tas_clip.cpp:185`), reconcile, `.flyr` writer (one `filename`), stats, bookmarks, wave/ruler, thumbnails (`gui.cpp:4671-4676`), the VS Code extension (a `clip.json` per folder). Checkout = swap the override. No new resolver. | Disk: N states × 10-28 MB per branch (`tas_clip.cpp:153-155`). Hard links unsafe (`nullDC.cpp:145-161` rewrites in place). The Replays browser ignores non-gen subfolders today (`dojo_gui.cpp:19417-19421`) → Phase 2 lists them |
| Sibling folder `<clip>@<name>/` beside the clip | Zero browser work: shows up as a clip; `test.ps1` sees it | Family scattered across `replays/<game>/`; rename flow (`tas_clip.cpp:824-866`) is name-based, so kinship must be by id anyway; "delete clip" leaves orphans |
| Shared states + per-branch movie diff | Cheap on disk | Needs a per-slot resolver (`getSavestatePath` has one dir), a per-branch state namespace, and the guard's seq clock split per branch; restore/gens/reconcile all assume one folder = one timeline. Reinvents everything |

Recommendation: **nested full folders**. `main` is the root clip's live set (the word `WriteClipStats` already uses,
`core/dojo/dojo.cpp:978`). A branch's folder name must never match `genFolderKind` (`tas_clip.cpp:71-92`), otherwise
reconcile records it as a generation (`:471-494`); `branches/` as the parent dir guarantees that. `archive` skips
directories (`:158-159`), so gens never nest branches; `contents` counts only gen dirs (`dojo.cpp:991-997`).

What the copy takes ("take everything with it"): the whole movie (not just the prefix — the tail beyond the branch frame
is greyed as the un-reached old take exactly like an F3-in-WRITE, `dojo.cpp:752-766`, and re-recorded in place), **all**
states + sidecars (validity is decided by divergence, not by the branch frame: the event fires at the first byte-changing
write, `dojo.cpp:409-429`, so states between the branch frame and the first edit stay clean — copying them is right),
`clip.json` (with the parent's `stats.rerecords` and `rewinds` verbatim, or every copied sidecar's seq mis-verdicts: seq =
`rerecord_base + rerecord_count`, `dojo.cpp:668`, `rerecord_base` ← `stats.rerecords`, `:841`), the macro `.txt` renamed
to the branch folder's basename (`WriteMacroFile` names it after the folder, `dojo.cpp:2778-2779`; `renameMeta` shows
the rename rule, `tas_clip.cpp:839-840`), `audio.env`, `skip.map`, bookmarks (inside clip.json). Economy switch
`statesUpTo = atFrame` copies only states with `movieFrame <= atFrame` (from `scanSavestateInfo`, `oslib.cpp:182-248`).

### 3.2 On-disk layout

```
data/replays/NoBGM_VMU/2026-09-04T18_11_02Z/            <- root clip = branch "main" (unchanged)
  NoBGM_VMU__2026-09-04T18_11_02Z__..__.flyr
  NoBGM_VMU.state(.frame/.png/.label/.wave)  NoBGM_VMU_5.state ...
  clip.json                                              <- schema 7: clipId, branches[], head
  2026-09-04T18_11_02Z_gen_01/ ...                       <- main's generations (as today)
  .trash/...
  branches/
    b01_tempest-alt/                                     <- a COMPLETE clip folder
      NoBGM_VMU__...__.flyr                              (copied; keeps its name - Replay::Init derives the dir from the path)
      NoBGM_VMU.state ... NoBGM_VMU_5.state ...          (copied, sidecars included)
      b01_tempest-alt_macro.txt                          (renamed copy, if the parent had one)
      audio.env  skip.map  clip.json                     (clip.json: schema 7 with a `branch` block)
      b01_tempest-alt_gen_01/                            <- the branch's own generations (= its commits)
      NoBGM_VMU.head.state(+.frame)                      <- where you left it (see 4E)
    b02_dhc-first/ ...
  branches/graph.json                                    <- node-editor layout (optional)
```

Branch folder = `<NN>_<safe-name>`; `NN` is stable (ids), the display name lives in JSON. Rename of the root clip moves
the whole tree; `branches[].dir` is stored relative.

### 3.3 Schema 7 sketch

Root (`main`) clip.json — additive on top of schema 6 (`CLIP_SCHEMA.md`: "It only ever grows", `:8-10`):

```json
{
  "schema": 7,
  "clipId": "c_2026-09-04T18_11_02Z_7f3a",
  "branches": [
    {
      "id": "b01", "name": "tempest-alt", "dir": "branches/b01_tempest-alt",
      "parent": "main", "atFrame": 1446, "fromSlot": 5, "fromSeq": 137,
      "prefixHash": "9d1c3a0b7e55f0c2",
      "createdUtc": "2026-09-04T18:40:01Z", "createdLocal": "09/04/2026 11:40 AM",
      "present": true,
      "lastCheckedOutUtc": "2026-09-04T19:02:11Z",
      "merges": [ { "from": "main", "utc": "...", "kind": "prefix-ff", "frames": 0, "statesCopied": 2 } ]
    }
  ],
  "head": { "branch": "main", "slot": 5, "frame": 1446, "utc": "..." }
}
```

Branch clip.json — a full clip.json (so every reader keeps working) plus:

```json
{
  "schema": 7,
  "clipId": "c_2026-09-04T18_11_02Z_7f3a",
  "branch": {
    "id": "b01", "name": "tempest-alt", "parent": "main", "parentDir": "../..",
    "atFrame": 1446, "fromSlot": 5, "fromSeq": 137, "prefixHash": "9d1c3a0b7e55f0c2",
    "createdUtc": "...", "createdLocal": "...",
    "statesCopied": "all",
    "merges": []
  },
  "generations": [],
  "stats": { "rerecords": 137, "frames": 5640, "...": "copied from the parent at branch time" },
  "rewinds": [ "...copied verbatim, then appended to..." ],
  "head": { "slot": 5, "frame": 1446 }
}
```
`restoredFrom` is dropped from the copy (a branch is not a restore). `fromSeq` = `rerecord_base + rerecord_count` at
creation (the guard clock); `prefixHash` = `MoviePrefixHash(atFrame)` (`dojo.cpp:621-642`) — the merge precondition.
`generations[].branch` is unnecessary: gens live inside the branch folder and `reconcile` is per folder.

Also record in `CLIP_SCHEMA.md` that `branches/` is a reserved subfolder and that `clipId` is the identity to key on
(the folder name is the display name, `CLIP_SCHEMA.md:138-140`).

### 3.4 What a commit is

| git | here | mechanism |
|---|---|---|
| commit | an F8 generation inside the branch folder | `ArchiveGeneration` `dojo.cpp:1144-1178` + `RecordGeneration` `:1093-1135` (facts: `atFrame`, `movieFrames`, `rerecords`, `slotFrames`) |
| working tree | the live set at the top of the branch folder | `contents` manifest `dojo.cpp:978-1025` |
| dirty | `restoredFrom.editedSince` (only after a restore today) → generalize to `head.dirty` = `rerecord_count > 0 \|\| live_state_writes > 0 \|\| movie length changed` (the exact predicate at `dojo.cpp:910-912`) | `WriteClipStats` |
| reflog / fine history | the append-only `.flyr` (`replay.cpp:153-176`) — kept but not addressable; the `rewinds` `[seq, frame]` log (`dojo.cpp:959-964`) — the timeline events | already persisted |
| checkout of a commit | `restore` (pre-boot only, `dojo.cpp:1073-1081`) | unchanged in Phase 1; in-session "restore" = create a branch from that gen (copy the gen folder as the new branch's live set) — the no-overwrite way to look at an old commit |

"A history of the changes in the branch" = the branch's `generations[]` (ordered by `createdUtc`, `tas_clip.cpp:531-536`)
+ its `rewinds` since the branch point (`fromSeq` splits them) + `stats.rerecords`. That is enough for the graph and for
"what changed since gen 02" (diff two `.flyr`s = the `ApplyEdit` diff loop, `dojo.cpp:1386-1398`, run offline).

---

## 4. Operations

### A. What a branch is physically → §3.1-3.2.  B. What a commit is → §3.4.

### C. "Checkout state5 into a new branch" — step by step

Preconditions (all from the macro Full-load gate, `core/dojo/dojo_gui.cpp:9772-9778`, plus the loads' own gate
`core/rend/gui.cpp:4571-4573`): `gui_state == Paused`, a clip bound (`!savestateFolderOverride.empty()`), slot 5 exists
with a sidecar (`scanSavestateInfo`), and — for WRITE/READ-WRITE sessions — the roll owns the file (`HasAppendTarget`).

`tas_branch::create(fromDir = live, atFrame = frame(slot 5), fromSlot = 5, name)` — pure folder work, pre-boot safe:
1. **Flush the live set so the copy is complete**: `replay.FlushReplay()` (`replay.cpp:303-325`) — this is the step F8
   skips (§1.2); `MacroFlush()` (`dojo.cpp:2799-2806`); `tas_wave::saveClip` + `tas_ruler::saveClip` (`:2872-2874`);
   `WriteClipStats()` (`:860`).
2. Copy the live set into `<live>/branches/<NN>_<name>/` with the `archive` copy loop generalized to take a destination
   (`tas_clip.cpp:156-175`; new `copyLiveSet(src, dst, filter)`), optionally filtered to states `<= atFrame`.
3. Rename `<oldbase>_macro.txt` → `<branchbase>_macro.txt` in the copy (rule at `tas_clip.cpp:839-840`).
4. Write the branch's clip.json: start from the copy, set `schema 7`, `branch{}`, `generations = []`, erase
   `restoredFrom`, keep `stats/rewinds/states/bookmarks` (through `tas_clip::write`, `:656-681`).
5. Append `branches[]` to the root clip.json (`tas_clip::read/write`), with `prefixHash = MoviePrefixHash(atFrame)` and
   `fromSeq`. Both writes bump `libraryVersion` (`:679`) so every pane rescans.

`tas_branch::checkout(dir, slot)` — in-session, Paused only (the emulator is stopped, so the roll can be written:
`dojo_gui.cpp:9368-9371`):
6. Guard: `gui_state == Paused`; refuse if `avi_dump.isRecording()`; refuse if a live SEND is in flight (mirror
   `tas_auto::stopLive()` at `gui.cpp:4585`).
7. **Leave the current branch cleanly**: step 1's flushes (this is also where `WriteClipStats` stamps `editSeconds`,
   `dojo.cpp:897-899`); optionally `dc_savestate` into `<game>.head.state` (§4E); write `head{}` into the current clip.json.
8. `replay.DetachFile()` (`replay.h:24`) **after** the flush — the pending `replay_msg` batch would otherwise be flushed
   later into the NEW file with OLD frame numbers (last-write-wins → corrupts the new branch's prefix).
9. Save + reset the clip-scoped stores: `tas_wave::saveClip(old)`, `tas_wave::reset()`, `tas_ruler::saveClip(old)`,
   `tas_ruler::reset()` (the teardown order, `dojo.cpp:2872-2875`), else the Paused tick writes the old `audio.env`
   into the new folder (`gui.cpp:3982-3992` keys on the override at call time).
10. `hostfs::savestateFolderOverride = dir` (precedent `dojo_gui.cpp:9379`).
11. Reload the movie: `session_inputs.clear()` (mandatory — `ProcessBody` appends, `dojo.cpp:2585`; precedents
    `dojo_gui.cpp:9387`, `dojo.cpp:1818`), new `Replay::AttachFile(flyr)` = `filename = flyr; LoadReplayFileV1(flyr)`
    (`replay.cpp:522-570`; captures `file_header` `:553-558`), reset `replay_msg`/`replay_frame_count` (idiom at
    `:226-227`). Do NOT call `Replay::Init` (it re-arms boot pauses and forces cfg, `:107-134`).
12. `BeginClipStats()` (`dojo.cpp:818-858`) — reads the branch's `stats.rerecords`/`rewinds`, reloads wave/ruler,
    reconciles gens, resets `movie_len_at_begin`. Note it zeroes `rerecord_count` (`:820`) — every later sidecar seq is
    `rerecord_base(new) + count`, consistent with the copied sidecars because `stats.rerecords` was copied verbatim.
13. Mode: set `play_match` / `macro_armed` from the branch's `mode` and the caller's choice (READ vs READ-WRITE/WRITE);
    `macro_armed` is otherwise a leak (`dojo.cpp:2868-2870`); re-point `loaded_macro_path/loaded_macro_rr`
    (`:1837-1838`) or clear (`:2854-2855`).
14. Per-roll session state: `undo_stack/redo_stack` clear (`dojo.h:247-248` — an undo after the swap would push OLD
    bytes through `ApplyEdit`), `macro_pending.clear()`, `divergence_open = false`, `stale_tail_from` = `atFrame` for a
    just-created branch (the tail beyond it is the old take, the F3-in-WRITE rule `dojo.cpp:752-766`) or `~0u` for a plain
    jump (`:1878`, `dojo_gui.cpp:9388`), `tas_auto::clearAll()/stopLive()` (`dojo.cpp:2884-2885`), the piano-roll
    selection (`selSet`) and the Notepad target (`tasSendTarget`, `dojo_gui.cpp:4451`) are frame-relative → clear the
    selection.
15. Load the state: `config::SavestateSlot.set(slot)` + `cfgSetVirtual("config","Dreamcast.SavestateSlot",…)`
    (`replay.cpp:29-34`) then `gui_loadState()` (`gui.cpp:4553`): `dc_loadstate` → `LoadStateFrame` sets `frame_number`
    (`dojo.cpp:740`), `load_seq++`, `WriteClipStats` (now on the new folder), verify (`nullDC.cpp:298-299`). In READ the
    state must be `< MovieEnd()` or playback dead-ends (`dojo.cpp:768-782`).
16. Invalidate: `dojo.savestate_epoch++` (`dojo.h:383`), `tas_clip::bump()`, republish locks (`gui_locked_ranges`),
    `gui_show_slot_picker()` (`gui.cpp:4970-4974`) with a READY banner like the boot handoff (`:4419-4444`).
17. Log the whole thing (`NOTICE_LOG(NETWORK, "TAS BRANCH: checkout %s @ slot %d frame %u ...")`) — the house method
    (`CLAUDE.md:100-103`).

Phase-1 alternative with zero new invariants — **checkout by reboot**: stage `dojo:Replay=yes` +
`dojo:ReplayFilename=<branch .flyr>` exactly like the Replays browser (`dojo_gui.cpp:19590-19596`) and `gui_start_game
(LastRomPath)`; `Replay::Init` derives the folder (`replay.cpp:22`), the handoff seeks State 0 (`gui.cpp:4406-4418`),
and **R** flips to write with the branch's `.flyr` attached (`HasAppendTarget`, `replay.h:18-20`; writes are gated on the
filename, `replay.cpp:254-264`). Cost: a full boot (seconds). Good enough to validate the on-disk model before the
in-session swap exists.

### D. What "merge main into a branch" means for input movies

There is no textual 3-way merge of frames; a movie is a function of its entire prefix. The useful, honest semantics:

| Case | Test | Action |
|---|---|---|
| **Prefix fast-forward** (main's inputs below `atFrame` unchanged since the fork) | `MoviePrefixHash(atFrame)` computed on main's roll == `branches[].prefixHash` (`dojo.cpp:621-642`) — or, offline, hash the `.flyr`'s decoded prefix | Inputs: nothing to do. Bring over: main's states with `movieFrame < atFrame` into EMPTY slots of the branch (same prefix ⇒ valid there; the copied sidecar's v3 hash exonerates it, `:644-654`), bookmarks/tags/notes below `atFrame`. Record `merges[] {kind: "prefix-ff"}` |
| **Main's prefix changed below `atFrame`** | hash differs; first differing frame via the `ApplyEdit` diff loop (`:1386-1398`) | **A real conflict = a desync of the branch's tail.** Default: refuse, report the first differing frame and how many frames differ. Opt-in "splice anyway": `ApplyEdit(branchRoll with main's prefix substituted)` — the funnel logs the timeline event at that frame (`:1466-1473`), so every branch state above it goes STALE by the existing guard, and the `.flyr` keeps history (`:1474`). The user then re-verifies from BASE |
| **Land the branch into main** (the reverse) | main's prefix below `atFrame` unchanged (same hash test) | `ApplyEdit(mainRoll with the branch's frames ≥ atFrame)`; if the branch is shorter, `ApplyEditResize` (`:1482-1573`, refuses if a locked range would shift `:1511-1527`; the pre-BASE auto-lock `base_prelock` filters writes below BASE `:1407-1434`). Then copy the branch's states with `movieFrame ≥ atFrame` into empty slots of main; record `merges[]` on both sides |
| **Extend from main's later work** ("bring main's later savestates/tags over") | only meaningful when the branch has not diverged yet below those frames — i.e. the branch's own `rewinds` since `fromSeq` all sit above the state's frame | copy, else refuse per state (the guard's rule `:611-617` applied across folders) |

Merges run Paused, on the CURRENT branch only (the roll is the target), through the funnel — never by editing another
folder's `.flyr` behind its back. Record every merge in both clip.jsons (`branch.merges[]`, `branches[].merges[]`).
Never claim a merge "worked": the truthful post-condition is "no state above frame X is trusted until replayed", which
the guard already displays.

### E. "Jump around like git" between 3 branches

What must swap (all cited in §4C): the override (10), the movie file + `file_header` + pending batch (8, 11), the roll (11),
`frame_number` (15), the clip stats/rewind clock (12), the mode flags (13), per-roll GUI state (14), the wave/ruler
stores (9), the caches (16). Nothing is copied on a jump.

Cost: `dc_loadstate` of a ~28 MB zipped state ≈ 500 ms (`nullDC.cpp:295`) + idempotency verify (~10 ms, same line) +
parsing a `.flyr` of 28 B/frame (5640 frames ≈ 158 KB, sub-ms) + `BeginClipStats`'s reconcile (one directory walk) +
lazy thumbnail re-uploads (`gui.cpp:4678-4700`). **Under a second per jump.**

HEAD: write `<game>.head.state` (+ `.frame` via `SaveStateFrame`, which takes a filename, `dojo.cpp:656`) when leaving a
branch, so a jump back lands where you left it — needs a 1-line extension in `getSavestatePath` (index -1 already maps to
`.net`, `oslib.cpp:289-290`; add -2 → `.head`) and the same in `dc_savestate/dc_loadstate` index handling
(`nullDC.cpp:130`, `:250`). Cheaper first cut: `head = {slot, frame}` in clip.json and land on that slot.

API (`core/dojo/tas_branch.h`, no ImGui, folder-first like `tas_clip`):
```cpp
namespace tas_branch {
  struct Ref { std::string id, name, dir, parent; u32 atFrame = 0; int fromSlot = -1; u32 fromSeq = 0;
               u64 prefixHash = 0; std::string createdUtc, createdLocal; bool present = true; };
  std::string rootDir(const std::string& anyDir);        // walk up out of /branches/<id>; "" if not a clip
  bool isBranchDir(const std::string& dir);
  bool list(const std::string& rootDir, std::vector<Ref>& out);   // branches[] reconciled with the folders
  bool create(const std::string& fromDir, u32 atFrame, int fromSlot, const std::string& name,
              u32 statesUpTo /*~0u = all*/, Ref& out, std::string& err);       // pure folder work
  bool checkout(const std::string& dir, int slot, bool readOnly, std::string& err);  // in-session, Paused
  enum class MergeKind { PrefixFastForward, SpliceTail, Land };
  struct MergeReport { bool ok; u32 firstDiff; u32 diffFrames; int statesCopied; std::string why; };
  bool merge(const std::string& fromDir, MergeKind kind, bool force, MergeReport& r);   // target = the current branch
  bool remove(const std::string& dir);                   // -> <root>/.trash/<utc>/ (restore's rule, tas_clip.cpp:191-224)
  struct Graph { struct Node {...}; struct Edge {...}; std::vector<Node> nodes; std::vector<Edge> edges; };
  const Graph& graph();                                   // cached on tas_clip::libraryVersion() + savestate_epoch
}
```

UI: (1) the **Branches** module (§2.4 / §2.6); (2) a **compact branch bar in the Timeline** next to the folder icon
(`dojo_gui.cpp:17808-17813`): `[main ▾]` combo listing branches (checkout on pick, disabled unless Paused with the reason
in a tooltip — the `menuWhy` idiom `:14949`), a `+` "branch from state N" button; (3) hotkeys `EMU_BTN_BRANCH_NEXT/PREV/NEW`
through the six-file chain (§1.1 "Hotkey chain").

### F. The parallel feature: N sibling branches from one frame, staggered by k frames

Constraint check on the branch model: N `create()` calls from the same state copy the state N times (N × ~28 MB) plus
the movie N times (~160 KB each) — acceptable for N ≤ 10, but the outcome *comparison* does not need branches at all.

**Cheap path = one shared state, N in-memory variants, N outcome states in ONE folder** (a GUI-thread state machine in
the style of the Input Sender's Auto-Send loop, `gui.cpp:5921-5922`):
```
edited0 = session_inputs                                  // to restore at the end
for k in 0..N-1:
  SavestateSlot = S; gui_loadState()                       // frame_number = F            (gui.cpp:4553; dojo.cpp:740)
  roll = edited0 with [F, ∞) := k neutral rows + inputs    // neutral row = zeroed 2×FrameInputs (dojo.cpp:1813)
  ApplyEditResize(roll, "fanout k")                        // through the funnel: .flyr rewritten, event logged (dojo.cpp:1482-1573)
  gui_step_frames(k + len + settle)                        // run, then the OSD stop pauses (gui.cpp:5923-5945, :4341)
  on Paused: probe = tas_mvc2::read() (mvc2.h:42); PNG via tas_thumb::captureForState(path) (gui.cpp:4607 takes any path)
             dc_savestate(10 + k)                          // the outcome, with its sidecar (seq, prefixHash of variant k)
             append fanout.json {k, F, probe, slot}
  if keep[k]: tas_branch::create(live, F, /*fromSlot*/ 10 + k, "k" + k)   // copies the roll AS variant k → the branch is exact
ApplyEditResize(edited0, "fanout restore")
```
Why the outcome slots go grey in the parent: each iteration's edit is a timeline event at frame F
(`dojo.cpp:1562-1563`), and outcome k sits at frame > F with an older seq → stale by `IsStateStale` (`:611-617`). That is
the truth — they are different timelines. The kept ones are clean inside their own branch because the branch's movie IS
variant k and the v3 hash exonerates (`:644-654`). `ApplyEdit` (non-resize) would refuse a roll that ends before the
original's last frame (`:1376-1384`), hence `ApplyEditResize`. The lock filter (`:1407-1434`) is a feature: a locked
range above F blocks the injection loudly.

Fan-out is an *experiment* object (`fanout.json` in the parent: `{sourceSlot, atFrame, N, kStep, inputs, results[]}`)
that the graph can render as a fan of dashed provisional nodes; only kept variants become real `branches[]` entries.
"Cycle through branches to compare" = F2 across slots 10..10+N (existing) for the cheap path, `checkout()` (~0.5 s) for
the real ones.

---

## 5. Hazards / pitfalls (each with the code that makes it real)

1. **Un-flushed `.flyr` tail** (≤119 frames, `replay.cpp:249,298-302`); F8 does not flush (`dojo.cpp:1144-1178`) — a
   branch created from an unflushed clip loses up to 2 s of tail. Always `FlushReplay()` before any copy.
2. **Pending batch + filename swap**: `replay_msg` (`replay.cpp:239-247`) flushed after re-pointing `filename` writes
   old-branch frames into the new file; last-write-wins makes them silently win. Flush, then `DetachFile`, then attach.
3. **`LoadReplayFileV1` appends into the roll** (`dojo.cpp:2585`) — clear `session_inputs` first or stale cells from
   the previous branch survive wherever the new movie has no record.
4. **`file_header`** must come from the new file (`replay.cpp:553-558`), or a later `RewriteReplayFile` regenerates a
   header from current config and "silently changes how the frames decode" (`replay.h:25-27`).
5. **Guard clock continuity**: sidecar seq = `rerecord_base + rerecord_count` (`dojo.cpp:668`); `BeginClipStats` zeroes
   the count and re-reads the base (`:820-821, 841`). A branch's clip.json must carry the parent's `stats.rerecords` and
   `rewinds`, or copied sidecars are judged against the wrong clock.
6. **Wave/ruler singletons**: loaded per clip (`:833-834`), saved on the CURRENT override by the Paused tick
   (`gui.cpp:3982-3992`) — save+reset before the swap (`dojo.cpp:2872-2875` order).
7. **`stale_tail_from`** is per roll (`:1878`, `dojo_gui.cpp:9388`, `dojo.cpp:2871`) — set explicitly on every swap.
8. **`macro_armed` / `play_match` / `loaded_macro_*`** leak across sessions unless reset (`:2868-2870`, `:2854-2855`);
   a checkout must pick the mode explicitly.
9. **Undo/redo stacks** hold the old roll's bytes (`dojo.h:247-248`) and replay through `ApplyEdit` — clear on swap.
10. **Frame-relative GUI state**: piano-roll selection (`selSet`, `dojo_gui.cpp:17277`), the shared TARGET
    (`tasSendTarget`, `:4451`), Notepad pulls — clear the selection on swap.
11. **`WriteClipStats` timers** (`clip_start_time`, `edit_base`, `clip_sessions`, `:822-830, 897-899`): stamp the old
    branch before the swap or its `editSeconds`/`lastOpened` are lost; `movie_len_at_begin`/`live_state_writes`
    restart (editedSince semantics restart per checkout — document it).
12. **The emu thread owns the roll while running** (`dojo.cpp:436`) — every write in §4C happens Paused
    (`gui.cpp:4576` stops the emulator; `dojo_gui.cpp:9774` gate). clip.json is mutex-protected (`tas_clip.cpp:403`).
13. **Restore stays pre-boot** (`dojo.cpp:1073-1081`); a checkout must never target the folder currently open — it
    switches, it does not overwrite.
14. **READ dead-end**: checking out a state at/after the branch movie's end ends playback immediately
    (`dojo.cpp:768-782`) — pick `slot` with `movieFrame < MovieEnd()`.
15. **Folder names**: a branch dir must not match `genFolderKind` (`tas_clip.cpp:71-92`) or reconcile synthesizes a gen
    (`:471-494`); the macro `.txt` is named after the folder basename (`dojo.cpp:2778-2779`) — keep folder names stable
    (ids), display names in JSON; `renameMeta` (`tas_clip.cpp:824-866`) must learn `branches[].dir` if it ever becomes
    absolute (keep it relative).
16. **Disk**: ~10-28 MB per state (`tas_clip.cpp:153-155`); hard links unsafe (`nullDC.cpp:145-161` rewrites in place);
    no reflink on NTFS. Offer `statesUpTo`, show the size in the create dialog (the F8 prompt shows it, `dojo.cpp:1177`).
17. **Browsers**: the Replays list ignores non-gen subfolders (`dojo_gui.cpp:19417-19421`) — branches are invisible
    pre-boot until listed (Phase 2); `test.ps1` scans one level (script) — same.
18. **Two ImGui streams**: draw the Branches window from `show_tas_tool_windows` only (`gui.cpp:4141`, `:4301`);
    never before a `NewFrame` (`CLAUDE.md:429-437`).
19. **Hotkeys vs the canvas**: node-editor takes the wheel and background drag, not keys; `gui_keyboard_captured()` rules
    stay (`IMGUI_UPGRADE.md:39-43,55-59`). Never flag the window `DragScrolling` (`IMGUI_UPGRADE.md:31-36`).
20. **Settings files**: node-editor's `NodeEditor.json` default and the `dojo:UiIni=no` harness rule
    (`gui.cpp:148-155`, `IMGUI_UPGRADE.md:60-62`).
21. **Assert-free release builds** hide ImGui/node-editor misuse (`IMGUI_UPGRADE.md:50-52`).
22. **Boot paths clear the override** (`gui.cpp:887`) and `Reset()` clears branch-relevant flags — a Restart-replay
    (`dojo_gui.cpp:931-935`) re-stages the ROOT movie unless `ReplayFilename` points at the branch; make "current branch"
    the thing Restart re-stages.

---

## 6. Phased implementation plan

### Phase 0 — refactors that de-risk everything (½-1 day)
- `core/dojo/tas_clip.cpp`: extract the copy loop of `archive` (`:156-175`) into `copyLiveSet(src, dst, filter)`;
  `archive` calls it. Add `writeBranchMeta`/`readBranches` helpers next to `appendGeneration` (`:811-822`).
- `core/dojo/replay.cpp`/`.h`: `Replay::AttachFile(path)` (= detach + `LoadReplayFileV1` + reset `replay_msg`/`replay_frame_count`,
  reusing `:522-570` and the idiom at `:226-227`); keep `Init` boot-only.
- `core/dojo/dojo.cpp`/`.h`: `Dojo::FlushLiveClip()` (FlushReplay + MacroFlush + wave/ruler save + WriteClipStats — the
  teardown subset `:2834-2845, 2872-2874`) and `Dojo::SwitchClipFolder(dir)` (steps 8-12, 14, 16 of §4C).
- `CLIP_SCHEMA.md`: schema 7 section (§3.3), the `branches/` reservation, the maintenance rule for `backfillFromFolder`
  (`CLIP_SCHEMA.md:87`) extended to branch records.

### Phase 1 — create + checkout, no graph, no merge (3-5 days)
- New `core/dojo/tas_branch.h/.cpp` (§4E API: `rootDir/isBranchDir/list/create/checkout/remove`), listed in
  `CMakeLists.txt` next to `core/dojo/tas_clip.cpp` (`:1045`).
- UI, minimal: Timeline branch bar (`dojo_gui.cpp:17808-17813`) with a combo + "New branch from slot N…" popup (name,
  states all/≤frame, size estimate) and a Windows-menu-less flow; the States card row menu (`gui.cpp` slot picker, row
  menu near `:5311`) gets "Branch from here…".
- Hotkeys `EMU_BTN_BRANCH_NEW/NEXT/PREV` (six-file chain, §1.1) + `migrate()` line (`CLAUDE.md:447`).
- Logging: `TAS BRANCH:` traces for create/checkout with the numbers (frame, slot, files, MB, ms).
- Harness `branchtest.ps1` (pattern of `textguard.ps1`/`resizeguard.ps1`; a `-config dojo:BranchProbe=…` one-shot like
  `ResizeProbe`, `replay.cpp:57-89`): create a branch at slot N, checkout, record 30 frames, checkout back, assert the
  root `.flyr` is byte-identical, the branch `.flyr` has the new tail, both clip.jsons parse with schema 7, and
  `STATE VERIFY: idempotent OK` after each load. Pass `dojo:UiIni=no`.
- Also ship the reboot-based checkout (§4C alternative) as `Play a Movie` on a branch: teach the Replays browser to list
  `branches/*` indented under their root (`dojo_gui.cpp:19402-19424`).

### Phase 2 — the graph UI (2-3 days)
- Vendor imgui-node-editor (§2.3); `core/dojo/tas_branch_gui.cpp` with `tasBranchesWindow` (§2.4) drawn from
  `show_tas_tool_windows` (`dojo_gui.cpp:15789-15792`), open flag `dojo:BranchesOpen` (`:15746-15749`, `:15793-15796`),
  Windows menu (`:17715-17727`), zoom name/reset (`:6660-6674`, `:6692-6697`), owner menu (`:17326-17359` pattern:
  New branch / Checkout / Delete / Layout: fit), default dock slot (`gui.cpp:4212-4220`, the right column).
- `tas_branch::graph()` cached on `tas_clip::libraryVersion()` + `dojo.savestate_epoch`; nodes = each branch's tip
  (live) + its `generations[]` (`loadGenerations`, `tas_clip.cpp:883-923`); edges = gen→next gen, parent fork at
  `atFrame` → child. HEAD = the node whose `dir == savestateFolderOverride` and is the tip; dirty from `WriteClipStats`'s
  predicate (`dojo.cpp:910-912`).
- The lane-graph fallback behind `dojo:BranchGraph=lanes|nodes` (§2.6) — same `Graph`, two renderers.
- Layout persistence via the callbacks into `<root>/branches/graph.json`; no-op callbacks when `dojo:UiIni=no`.

### Phase 3 — merge (2-3 days)
- `tas_branch::merge` (§4D) on top of `MoviePrefixHash` (`dojo.cpp:621-642`), `ApplyEdit` (`:1371`) and
  `ApplyEditResize` (`:1482`); state copy into empty slots via `hostfs::getSavestatePath` + the sidecar set
  (`oslib.cpp:329-341` for the file list); records in both clip.jsons.
- UI: node context menu "Merge main into this branch…" with the report (hash verdict, first differing frame, frames,
  states) and the "splice anyway" checkbox off by default. Never resolve; the guard shows the consequences.
- Harness: `mergetest.ps1` — fast-forward path byte-identical; changed-prefix path refused; `--force` path stales exactly
  the states above the first diff (read `states[].rerecordSeq` + `rewinds` from clip.json, `CLIP_SCHEMA.md:76,101`).

### Phase 4 — fan-out (2-3 days, after Phase 1)
- `tas_fanout` state machine on the GUI thread (§4F), driven like the Input Sender's Auto-Send (`gui.cpp:5921-5945`),
  writing `fanout.json`; "keep" turns an outcome into `tas_branch::create(...)` from the outcome slot; the graph renders
  provisional nodes from `fanout.json`.
- Compare view: the States wall already shows the PNGs + labels per slot (`gui.cpp:5450+`); add the probe numbers from
  `fanout.json` to the card details.

Sources consulted outside the repo: https://github.com/thedmd/imgui-node-editor (README, LICENSE, `imgui_node_editor.h`,
`imgui_node_editor.cpp`, `imgui_node_editor_internal.h`, `imgui_canvas.cpp`, `imgui_extra_math.h`, releases, issues #118
and #137, GitHub API for releases/commits), https://github.com/pthom/imgui-node-editor (branches via API),
https://github.com/pthom/imgui_bundle/discussions/428 and /discussions/66.
