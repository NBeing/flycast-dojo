# Divergence from David's fork — temporal and functional map

`[MEASURED 2026-09-07]` unless marked otherwise. Read-only investigation; nothing outside this
file was changed.

**The two trees**

| | path | kind |
|---|---|---|
| his | `/home/nbee/dev/davids_fly` | TAS fork, **no `.git`** (`find -maxdepth 3 -name .git` → nothing) |
| ours | `/home/nbee/dev/flycast-dojo` | branch `dojo7`, full repo |

His docs cite short commit hashes — `WRITE_MODE_RESTRUCTURE.md` names `f09a847`, `22e63d9`,
`71ea209`, and four other docs carry seven-hex refs — so **his real working tree IS a git repo**;
what we hold is a history-less copy of it. A future two-way merge should ask him for the repo, not
for another snapshot.

---

## 0. Evidence quality — read this before trusting any date

| evidence | strength | notes |
|---|---|---|
| dated markers inside his code, e.g. `(David, 2026-09-05)` | **strong** | he stamps features with the day he built them; 60+ such stamps across `core/` |
| dated markers in his docs (`[MEASURED …]`, `Status (2026-09-03)`) | **strong** | |
| file **mtimes** | **weak** | preserved by the copy, but a one-character touch re-dates a file, and a file untouched since August may still be actively depended on. Used here only to *order* work and to bound the snapshot, never to claim a feature was "finished" on a date |
| **directory** mtimes in his tree | **worthless** | uniformly `2026-09-05 23:06` — that is when the copy was made, not when he worked |
| byte-identity between his docs and `docs/tas-fork/` | **strong** | md5 comparison, 15/15 identical |

Everything below labelled *inference* is inference.

---

## 1. TEMPORAL map

### 1.1 The snapshot boundary — what we can and cannot see

- **Newest content in his tree: `2026-09-05 05:34`** — `core/rend/gui.cpp` (227,386 B) and
  `core/dojo/dojo_gui.cpp` (940,380 B), stamped within seconds of each other.
- **The copy was taken `2026-09-05 23:06`** (every directory mtime in his tree).
- **Our port ran `2026-09-06` → `2026-09-07`** (git: `2ca3eccfd` "migrate the Lua/emuapi programme
  onto dojo-7" is the first dojo7 commit dated 2026-09-06; `docs/tas-fork/` files are all
  `2026-09-07 01:18`).

**Therefore: we snapshotted his tree at its head. We are not behind on anything we can see.**
Corroboration — all 15 TAS docs are byte-identical:

```
SAME BIZHAWK_NOTES BRANCHING_RESEARCH CANON_driver_model CANON_macro_mode CANON_onenter
SAME CANON_readwrite_model CANON_ux_batch CLAUDE.md(=CLAUDE.tas-fork.md) CLIP_SCHEMA
SAME david_work_tracker EXPERIMENTS_RESEARCH FCEUX_BRANCHES_RESEARCH IMGUI_UPGRADE
SAME ROADMAP WRITE_MODE_RESTRUCTURE
```

**The blind spot is absolute, though:** anything David did after `2026-09-05 23:06` is invisible to
us — there is no history to diff against and no remote to fetch. Given his cadence below (14 and 17
files touched on 05-04 and 05-05 alone) it is *likely* (inference) that two days of further work
already exist that we have never seen. `docs/tas-fork/david_work_tracker.md` (611 B, his `2026-09-05
01:36`) is his own live scratch queue and names unstarted work — bookmark shortcuts, storing
selections to the clip, shift-selection, **frame-skip branching / "send signals to all 4 branches"**,
generations-as-a-design-pattern, branches inside the States panel, an ImGui-node prototype, an
InputSender backspace bug, and a non-captured overlay pulling from the panels. Those are the most
probable subjects of anything newer than our snapshot.

### 1.2 Cadence — files touched per day (mtimes; `core/deps`, the nested duplicate copy and the
757 MB `.cdi` excluded)

```
2026-08-09  755   <- the base import (whole upstream tree stamped at once)
2026-08-11    3
2026-08-21    3
2026-08-22   14
2026-08-23   22
2026-08-24    5
2026-08-26    2
2026-08-27    1
2026-08-28    3
2026-08-29   12
2026-08-30    1
2026-09-01    3
2026-09-03   10
2026-09-04   14
2026-09-05   17   <- head of the snapshot
```

The `2026-08-09` block is the fork point from flycast-dojo-7, not work. Note the shape: a burst
around 08-22/23, a research burst 08-29, then a **sustained, accelerating build 09-03 → 09-05**.

`/home/nbee/dev/davids_fly/flycast-dojo-7/` is **not** a second, older snapshot — `diff -rq` against
the root reports only missing dotfiles (`.github`, `.gitignore`, `.gitmodules`, `.vscode`); the
content is the same tree. It gives us no extra temporal resolution.

### 1.3 What he was working on, roughly when

Reconstructed from dated code stamps + doc status lines. Dates are *when he stamped it*, which is
strong for "not before" and weak for "not after".

| when | what | evidence |
|---|---|---|
| **≤ 2026-08-22** | the fork's foundations, and **the last date his `CLAUDE.md` fully describes** | `CLAUDE.md:5` "Last full refresh: 2026-08-22" |
| 08-22 → 08-24 | savestate HUD, thumbnails, capture, ImGui-docking swap, hotkey chords, TAS harnesses | `IMGUI_UPGRADE.md` (2 markers, 08-24); `core/sdl/sdl_keyboard.h:67`; `core/input/gamepad_device.cpp:401`; `chordtest.ps1`/`scrubstuck.ps1`/`crashrepro.ps1`/`test.ps1` all `08-23 21:11`; `guardtest.ps1` 08-24 |
| 08-24 → 08-26 | **Piano Roll v2** phase, then the **ASCII Pad V PRO / sequence-library** phase | `ROADMAP.md:290` "Phase PR — Piano Roll v2 … (planned 2026-08-24)"; `ROADMAP.md:812` "THE SEQUENCE LIBRARY - the ASCII Pad V Pro phase (user priority, 2026-08-25)"; 33/20/26 dated lines for 08-24/25/26 |
| 08-28 → 08-31 | the **CANON design docs** — the batch-UX, driver model, read/write model, macro mode, OnEnter | `CANON_ux_batch.md` 08-28, `CANON_driver_model.md` 08-29, `CANON_readwrite_model.md` (12 markers, 08-31), `CANON_macro_mode.md` (2026-08-30, "**Status: DESIGN, not built**") |
| 08-29 → 08-30 | MvC2 reference-data mining (`mvc2_data/`: `SPREADSHEET.json` 302 KB, `QUEUE_SPECS.md` 57 KB, `PALMOD_NOTES.md`, `HITBOX_OVERLAY.md`, a Cheat Engine table, `dojo_studio_planner.html` 142 KB); JetBrains Mono + Roboto fonts vendored | mtimes 08-29/30 |
| **09-01** | macro mode / OnEnter canon finalized | `CANON_macro_mode.md` 09-01 14:03, `CANON_onenter.md` 09-01 00:15 |
| **09-03** | **the WRITE-mode restructure** + notation tooling. Vendored `ImGuiColorTextEdit` **patched** (`TextEditor.cpp` 09-03 19:09, `LanguageDefinitions.cpp` 09-03 17:01); `tasva2.{cpp,h}` (V PRO grammar) written; `pcsx2_reference/` and `notation_reference/va2-notation-grammar.html` added; minidump crash handler after a GPF | `WRITE_MODE_RESTRUCTURE.md:3` "Status (2026-09-03)"; `core/windows/fault_handler.cpp:32` "(David, 2026-09-03)"; `core/dojo/tasva2.cpp:1015`; `core/rend/gui.cpp:3972` "2026-09-03 loss" (a macro-autosave tick added *after a data loss*); `core/rend/gui.cpp:3997` (sidecar autosave after a crash); `core/dojo/dojo.h:310` "the 2026-09-03 loss - two force-killed sessions" |
| **09-04** | **Frame Skip Test** incl. the unattended RUNNER; **States promoted to a dockable studio module**; STAGE launch in the macro browser; `tas_ruler` (relativenumber ruler), `tas_wave` finished 09-04 00:03 | `core/dojo/dojo_gui.cpp:15785` "FRAME SKIP TEST (David, 2026-09-04)"; `core/rend/gui.cpp:5551` "2026-09-04: 'lets call it that and convert it into a fully dockable module like the others'"; `core/dojo/dojo.h:123,127,134,137,143`; `core/dojo/tas_ruler.cpp` 09-04 01:41 |
| **09-05** | **Test Lab** (`replays/<game>/_lab` test folders), MvC2 trainer offsets, final `dojo_gui.cpp`/`gui.cpp` pass | `core/dojo/dojo_gui.cpp:16569` "TEST LAB window (David, 2026-09-05)"; `core/dojo/mvc2.cpp:24` "2026-09-05; trainer 0x2C289642"; `core/dojo/tas_clip.h:66`; `core/rend/gui.cpp:964,4247,4650,5151,5185,5234` |
| **09-05 (research)** | `BRANCHING_RESEARCH.md` (66 KB), `EXPERIMENTS_RESEARCH.md` (51 KB), `FCEUX_BRANCHES_RESEARCH.md` (51 KB) all written/refreshed 09-05 02:36–03:06 | mtimes |

**The research trio is the strongest signal about his NEXT phase.** Three large documents on
branching (FCEUX/BizHawk branch models) written in the last 30 minutes of recorded work, plus
"Frame Skip Branching, Send signals to all 4 branches" at the top of `david_work_tracker.md`
(2026-09-05 01:36). *Inference:* whatever he built on 09-06/07 is most likely **branches** — a
generations/branches model in the States panel.

### 1.4 His `CLAUDE.md` is stale by ~two weeks of features

This matters because we ported it verbatim as `docs/tas-fork/CLAUDE.tas-fork.md` and it reads as
authoritative. Grep counts in his live `CLAUDE.md` (36,154 B):

```
piano roll 0 | Notepad 0 | Input Sender 0 | Snippet 0 | Test Lab 0 | Frame Skip Test 0
V PRO 0 | va2 0 | Macro mode 0 | F6 0
```

Its section list (`CLAUDE.md:1-455`) stops at the 08-22 feature set: capture pipeline, States window
(F4), generations (F8), TAS options menu, determinism, `test.ps1`. **None of the 09-01…09-05 work is
in it.** The documents that *do* describe that work are `CANON_macro_mode.md`,
`CANON_readwrite_model.md`, `CANON_driver_model.md`, `CANON_onenter.md` and
`WRITE_MODE_RESTRUCTURE.md` — all of which we hold, all of which are current.
`docs/tas-fork/PORTING_NOTES.md` already warns "half of what these files describe is true here";
the sharper statement is that **`CLAUDE.tas-fork.md` also understates his fork**, in the other
direction.

---

## 2. FUNCTIONAL map — what he has that we do not

### 2.0 The shape of the gap, measured

File-set difference over `core/` (`core/deps` excluded):

```
only in HIS core/:   core/dojo/thumbnail.cpp  core/dojo/thumbnail.h
only in OUR core/:   core/deferred.{cpp,h}  core/determinism.{cpp,h}
                     core/lua/lua_console.{cpp,h}  core/pause.{cpp,h}
                     core/rend/video_recorder.{cpp,h}
```

Only **55 shared files differ at all** (CR-insensitive `diff -rq`). Ranked by lines present in his
and absent in ours:

| file | his LOC | ours LOC | his-only lines | ours-only lines |
|---|---:|---:|---:|---:|
| `core/dojo/dojo_gui.cpp` | 21,877 | 2,544 | **19,894** | 561 |
| `core/rend/gui.cpp` | 6,078 | 4,839 | **2,379** | 1,140 |
| `core/input/gamepad_device.cpp` | 1,098 | 862 | 293 | 57 |
| `core/log/ConsoleListenerWin.cpp` | 217 | 19 | 198 | 0 |
| `core/input/keyboard_device.h` | 620 | 507 | 131 | 18 |
| `core/rend/mainui.cpp` | 315 | 258 | 117 | 60 |
| `core/rend/dx9/d3d_renderer.cpp` | 1,503 | 1,428 | 83 | 8 |
| `core/rend/dx11/dx11_renderer.cpp` | 1,506 | 1,433 | 73 | 0 |
| `core/input/mapping.cpp` | 605 | 534 | 73 | 2 |
| `core/windows/fault_handler.cpp` | 239 | 186 | 53 | 0 |
| `core/lua/lua.cpp` | 822 | **2,321** | 51 | 1,550 |
| `core/nullDC.cpp` | 363 | 403 | 48 | 88 |
| `core/rend/gui.h` | 139 | 113 | 39 | 13 |
| `core/input/gamepad_device.h` | 237 | 205 | 35 | 3 |
| `core/log/LogManager.cpp` | 301 | 275 | 29 | 3 |
| `core/dojo/dojo_gui.h` | 122 | 94 | 29 | 1 |
| `core/emulator.cpp` | 1,002 | 1,053 | 28 | 79 |
| `core/sdl/sdl_keyboard.h` | 169 | 142 | 27 | 0 |
| `core/rend/gles/gldraw.cpp` | 983 | 968 | 20 | 5 |
| `core/input/mapping.h` | 152 | 132 | 20 | 0 |
| `core/sdl/sdl.cpp` | 1,203 | 1,189 | 17 | 3 |
| `core/input/gamepad.h` | 136 | 124 | 12 | 0 |
| `core/cfg/option.cpp` | 266 | 266 | 7 | 7 |
| `core/input/mouse.cpp` | 182 | 175 | 7 | 0 |
| `core/dojo/dojo.h` | 389 | 388 | **1** | 0 |
| `core/dojo/dojo.cpp` | 2,896 | 2,901 | 6 | 11 |

The remaining ~29 differing files are ours-ahead-only (dojo-7 base drift: `vulkan_context`, `gles`,
`ggpo`, `spg`, `mem_watch.h`, `rec_x64`, …) and carry nothing of his.

**The single most important number in this report: `core/dojo/dojo.h` differs by ONE LINE**
(`bool manual_pause = false;` at `davids_fly/core/dojo/dojo.h:291`, which we replaced with the
`pausing::` arbiter). And `tas_auto.cpp`, `tas_clip.cpp`, `tas_ruler.cpp`, `tas_wave.cpp`,
`tasmacro.cpp`, `tastext.cpp`, `tasva2.cpp`, `mvc2.cpp`, `avi_dump.cpp` and all of `core/oslib/` are
**byte-identical** (they do not appear in the 55-file diff at all).

**So the divergence is almost purely the UI layer.** The engine his studio calls into — the
re-record pipeline, `session_inputs`/`ApplyEdit`, `.frame` sidecars, clip folders and generations,
`hostfs::scanSavestateInfo`/`savestateFolderOverride`/`MAX_SAVESTATE_SLOTS`,
`dojo.locked_slots`/`base_prelock`, `MacroFlush`, `ReleaseTasHolds`, `tasHotkeysBlocked`,
`savestate_epoch`, `verifyLoadedStateIdempotent`, the V PRO/notation codecs, the MvC2 memory probe —
is already at parity on our side. That is a far better starting position than the raw 19,894-line
number suggests.

### 2.1 What he has, ranked by (value to us ÷ effort to port)

Effort is my estimate; "engine support" is measured against our tree.

---

**Tier 1 — high value, low effort, engine already present**

**1. TAS colour language + markdown/mono font layer**
`dojo_gui.cpp:1-240` (~240 L). Pure leaves: `TAS_ACCENT/READ/WRITE/P1/P2/ACTIVE/STAGED`,
`tasCol/tasLit/tasDrk`, `tasSecHdr`, `tasMarkdown*`. Needs the font block at `gui.cpp:401-434` and
the vendored `core/deps/imgui_markdown/imgui_markdown.h` (1,176 L, header-only, **we lack it**).
Everything else in his file reads from these, so this is the mandatory first move.

**2. `tas_thumb` savestate thumbnails**
`core/dojo/thumbnail.{cpp,h}` (3,451 + 867 B ≈ 120 L) — the only two files that exist in his `core/`
and not ours. **Blocked on a readback**: it calls `renderer->GetLastFrameRGB()`
(`thumbnail.cpp:84`), declared at `core/hw/pvr/Renderer_if.h:71` and implemented **only for DX11
(`dx11_renderer.cpp:1465`) and DX9 (`d3d_renderer.cpp:1211`)** — i.e. Windows only. On our Linux/GL
build we would have to write the GL path ourselves. Small, but not free.

**3. GL viewport letterbox for docking** — `core/rend/gles/gldraw.cpp` (+20 lines, his-only). Makes
the game shrink into the dockspace central node instead of being covered. **This is the one
renderer-side docking piece he already wrote for GL**, and we are a GL build; pairs with
`gui_set/get_game_viewport` (`gui.cpp:719-733`, ~15 L). Total ~35 lines, immediately useful now that
we have the docking ImGui.

**4. LogManager channel-mute semantics** — `core/log/LogManager.cpp` (+29 L, his-only) +
`LogManager.h` (+5). Rewrites `IsEnabled` so channel toggles are a real mute of NOTICE, and bakes
quiet-by-default channel lists into `Init`. Every TAS trace is `NOTICE_LOG`, so without this the
toggles do nothing. Zero dependencies.

**5. `MouseAsController=no`** — `core/input/mouse.cpp` (+7 L). A stray click on the game window
currently injects a face button into a recording on our build. One `cfgLoadBool` guard.

**6. TAS defaults in `core/cfg/option.cpp`** (7 lines changed). We still default `NetBeacon`,
`Transmitting`, `AutoLoadNetState`, `AutoLoadTrainingNetState`, `PlayerNameOverlay` to **true**; he
flipped all five to false, each with a comment naming the bug it caused (shadow recordings, blocked
headless boots, dirty captures). Trivial, and each one is a live hazard for headless replay runs.

**7. `gui.cpp` autosave ticks** — macro autosave (`gui.cpp:3976-3995`) and TAS sidecar autosave
(wave envelope + skip map, `gui.cpp:4001-4025`), ~45 L. Both were written **after real data loss**
(`dojo.h:310` "the 2026-09-03 loss - two force-killed sessions"). `Dojo::MacroFlush`,
`tas_wave::saveClip`, `tas_ruler::saveClip` all already exist on our side.

---

**Tier 2 — high value, medium effort**

**8. The TAS hotkey system (registry + engine + editor)**
`dojo_gui.cpp:1075-1494` (registry/rebind engine, ~420 L) + `dojo_gui.cpp:1957-2192` (editor pane,
~236 L) + `gui.cpp:1389` `gui_dc_control_for_input` + `gui.cpp:2127-2401` `gui_settings_controls_body`
(~275 L).
**Engine support we LACK and must port with it:** `core/input/gamepad.h` (+12 L: the eleven
`EMU_BTN_*` TAS actions — AVI_TOGGLE, TOGGLE_READONLY, GEN_ARCHIVE, SLOT_PICKER, HOTKEY_HELP,
SAVESTATE_SLOT_NEXT/PREV, INPUT_VIZ, PIANO_ROLL, TAS_UI, FST_NEXT); `core/input/mapping.h` (+20 L:
`KEY_MOD_SHIFT/CTRL/ALT`, `add_button`/`add_axis`/`clear_*_code`/`get_*_codes`) and
`mapping.cpp` (+73 L); `core/input/keyboard_device.h` (+131 L: the default layout and the whole
chord machinery, `isModifierKey`/`modifierFlags`/`chordCode`); `core/sdl/sdl_keyboard.h` (+27 L: the
`migrate`/`unmigrate` shims without which saved cfgs never pick up new defaults);
`core/input/gamepad_device.{h,cpp}` (+35/+293 L: TEST mode, `last_input_*`, `sharesMapping`,
`detachMapping`, and the hotkey dispatch itself). **Our input layer is stock dojo-7** — our
`gamepad.h` still lists only the training record/play slots (`flycast-dojo/core/input/gamepad.h:43-75`).
This is ~1,150 lines and it is the prerequisite for every keyboard-driven TAS harness he wrote.

**9. The States window (F4)** — `gui.cpp:4692-5742`, ~771 L (`slotScan`, `getSlotThumbnail`,
`slotFillColor`, `drawSlotLabelEditor/PreviewImage/Facts`, `slotTooltip`, `slotRowMenu`,
`drawSlotBrowser` 279 L, `gui_draw_slot_picker` 194 L, `gui_open/show_slot_picker`,
`gui_states_is_open/set_open`, `gui_cycle_savestate_slot`).
Engine: `hostfs::scanSavestateInfo`, `MAX_SAVESTATE_SLOTS`, `savestate_epoch`,
`dojo.locked_slots` — **all present on our side**. External hooks it needs: `tasSlotLocked/Toggle`
(`dojo_gui.cpp:17884-17885`), `tasStudioWindowZoom` (`dojo_gui.cpp:6771`),
`DojoGui::show_states_snapshots` (`dojo_gui.cpp:17561-17668`), and `tas_thumb` (item 2).

**10. The dead-timeline guard's UI surface** — `gui.cpp:4766-4970` (~205 L): `gui_slot_stale`,
`gui_purge_stale_now/tick` (`dojo:PurgeStale`, BASE exempt), `gui_stale_blink`,
`gui_stale_blink_deleted`, plus `gui_state_frames`, `gui_slot_frame`, **the real
`gui_locked_ranges`** and `gui_frame_locked` (`gui.cpp:4848-4922`).
**We ship `gui_locked_ranges` as a documented stub** (`flycast-dojo/core/rend/gui.h:33-40`,
`gui.cpp:4836`) that always returns empty, and `dojo.cpp:1413-1519` already calls it. So our
re-record pipeline consults a lock list nothing can populate. Porting this makes an existing engine
path real.

**11. Hold-Space slow-motion scrub + slot-repeat** — `core/rend/mainui.cpp` (+117 L, his-only), with
its pacing half in `gui_display_osd`'s stepping hook. `dojo:HoldStepFPS/HoldStepDelay/HoldStepRampMs`
are **absent from our tree** (grep: 0 hits). Depends on `dojo.step_held`, `step_held_since`,
`next_step_time`, `gui_open_step` — all present. Self-contained, host-side only, cannot perturb the
guest.

**12. Startup prompt / TAS Combo Studio + the two pre-boot browsers** —
`gui_display_startup_prompt` (`dojo_gui.cpp:19764-20101`, ~338 L),
`gui_display_macro_browser` (`dojo_gui.cpp:20255-20459`, ~205 L),
the rewritten `gui_display_replays` (`dojo_gui.cpp:20461-21464`, ~1,004 L), on the shared
`TasBrowser` class family (`dojo_gui.cpp:8573-9040`, ~468 L) and the browser leaf primitives
(`dojo_gui.cpp:8377-8572`, ~196 L).
Needs two new `GuiState` members (`StartupPrompt`, `MacroBrowser` — `davids_fly/core/rend/gui.h`
enum tail) which we do not have.

**13. Generations / clip popups + SNAPSHOTS pane** — `dojo_gui.cpp:11075-11912` (~838 L):
`tasGenPanelDraw` (262 L sortable table), delete-to-`.trash`, "Replace live with this backup".
Engine (`tas_clip::deleteGeneration/restore`, `clip.json`) is **byte-identical on our side**;
this is pure UI over machinery we already have.

**14. Input Visualizer** — `dojo_gui.cpp:17188-17559` (~372 L) plus the run-length history at
`dojo_gui.cpp:17128-17187`. Movie-SENT vs game-READ pads, mismatch in red, scene/skip clocks, combo
meters. Depends on `tas_mvc2::read()` (`mvc2.cpp`, **identical on our side**) and the sent/received
fields in `dojo.h` (identical). The agent flags it self-contained. This is the instrument that makes
every other TAS claim checkable, and it is one of the cheapest big wins here.

**15. Timeline / savestate HUD** — `show_savestate_overlay` (`dojo_gui.cpp:18833-19207`, ~375 L):
frame counter, READ/WRITE driver banner, BASE-hold gauge and blink, the 10-slot bank strip with
clickable lock toggles. Needs items 9 and 10.

**16. Settings → TAS tab + hotkey cheat sheet** — `settings_tas_tab` (`dojo_gui.cpp:1495-1951`,
~457 L) and `show_hotkey_overlay` (`dojo_gui.cpp:16933-17089`, ~157 L, generated from
`TAS_HOTKEYS[]`). Depends on item 8 and on `avi_dump.h`, which **we removed from the build** — the
capture half of this panel would need rewiring onto `videorec::`.

---

**Tier 3 — high value, high effort (the studio proper)**

**17. ★ The Piano Roll** — `show_piano_roll`, `dojo_gui.cpp:12379-15778`, **~3,400 L**, the largest
single function in his tree. Sub-features: virtualized rows (`ImGuiListClipper`), the FRAME gutter
with Alt/Ctrl/Shift-drag row surgery, the two-button tool model (L = select, R = paint), SET BRUSH
stamping at 60/30/20 Hz, the **MOVIE MAP heat map**, the **WAVE audio-envelope column**
(`tas_wave::`), the **REL relativenumber ruler** (`tas_ruler::`), INPUT TOOLS fold, bookmarks,
selection grammar. Every edit funnels through `Dojo::ApplyEdit`/`ApplyEditResize` — **which we
have**. It shares ~40 file-static globals with items 18 and 19.

**18. ★ The Notepad** — `tasNotepadWindow` (`dojo_gui.cpp:6773-8328`, **~1,556 L**) plus its parse
(`4923-5263`), diagnostics/squiggles (`5264-5458`), lint/reflow (`5459-5880`), file ops
(`5885-6100`), appearance profiles (`4671-4922`) and V PRO import (`6210-6520`) — ~3,100 L total.
**Needs the vendored `core/deps/ImGuiColorTextEdit` (TextEditor.cpp 3,285 L + TextEditor.h 535 L +
LanguageDefinitions.cpp 52 KB), which we do NOT have**, and which he **patched on 2026-09-03**
(mtimes `TextEditor.cpp` 09-03 19:09) — so take his copy, not upstream's. The notation engine it
drives (`tasva2.cpp` 1,115 L, `tasmacro.cpp`, `tastext.cpp`) is already identical on our side.

**19. ★ The F5 studio command bar** — `show_main_menu_bar` (`dojo_gui.cpp:17893-18831`, ~939 L). A
main menu bar whose menus change with the focused window. Reaches into every other window through
file-statics; the agent's verdict is "port last", and items 17/18/19 "must move together or not at
all".

**20. Input Sender** — `dojo_gui.cpp:9914-11074` (~1,161 L), the Blender-style radial pad, the
READ / READ-WRITE / WRITE `tasDriverBanner` (shared with the roll and the timeline), live send via
`tas_auto::` (identical on our side). Moderately liftable but shares the driver banner.

**21. Snippet library + notation switcher + MASH bar** — `dojo_gui.cpp:3915-4319` (~405 L),
`4320-4495` (~176 L, five notation styles incl. the PPAD/V PRO dialect), `3501-3788` (~288 L).
The notation switcher is nearly a leaf and is worth taking early even without the roll.

---

**Tier 4 — newest work; least likely to be ported, most separable**

**22. ★ Frame Skip Test + THE RUNNER** *(2026-09-04)* — `dojo_gui.cpp:15780-16567` (~788 L),
`gui_frame_skip_test_step` at `dojo_gui.cpp:16041` (F10/Shift+F10). Bakes a combo at each of MvC2's
four skip phases and runs every variant unattended (BAKE → RUN → SNAP, reading the combo meter and
writing outcome PNGs). Engine: `tas_mvc2::` combo meter (identical on our side),
`Dojo::ApplyEditResize` (present), `gui_saveState/loadState`, `avi_dump` (**we removed avi_dump from
the build** — would need `videorec::`). Self-contained window, but reads the roll's selection
statics.

**23. ★ Test Lab** *(2026-09-05, the newest thing he built)* — `dojo_gui.cpp:16569-16793` (~225 L)
plus `gui_lab_add_test` (`gui.cpp:5155-5201`), BASE re-anchor (`gui.cpp:4650-4667`), lab scratch
seeding (`gui.cpp:964-995`), startup-prompt row 4 (`dojo_gui.cpp:19969+`), and
`gui_lab_write_roll_macro` (`dojo_gui.cpp:2447`). Test folders under `replays/<game>/_lab`, where a
test is a folder whose slot-0 BASE is a permanent fixture and 1–99 are outcomes.
**The engine side (`tas_clip::labDir/labNewTestDir/labIsActive/seed/bump`) is byte-identical on our
side already.** Cleanest of his recent features, and conceptually the closest to our own
`scripts/testrun.sh` + ctest direction — a plausible convergence point.

**24. Branching** — **not built.** `BRANCHING_RESEARCH.md` (66,687 B), `FCEUX_BRANCHES_RESEARCH.md`
(51,283 B), `EXPERIMENTS_RESEARCH.md` (51,558 B), all `2026-09-05 02:36–03:06`, plus the tracker's
"Frame Skip Branching, Send signals to all 4 branches" and "Each Generation can support Branches".
Listed here so it is on the radar: *(inference)* this is what he is building now, and it is the item
most likely to have landed since our snapshot.

---

**Tier 5 — Windows-only; low value to us**

| what | files | LOC | why low |
|---|---|---:|---|
| Native docked console (`dojo:NativeConsole/ConsoleDock/ConsoleCols/...`) | `core/log/ConsoleListenerWin.cpp` (+198), `nullDC.cpp` `os_OpenNativeConsole`/`os_DockNativeConsole` (+11), `dojo_gui.cpp:1795-1817` | ~210 | Win32 console API; we have `core/lua/lua_console.{cpp,h}` and a working log file |
| Minidump crash handler | `core/windows/fault_handler.cpp` (+53) | 53 | `dbghelp.h`; we have breakpad wired |
| DX9/DX11 capture readback + `GetLastFrameRGB` | `d3d_renderer.cpp` (+83), `dx11_renderer.cpp` (+73), `Renderer_if.h` (+7) | ~163 | DX-only. The `Renderer_if.h` virtual is worth taking as the *interface*, so a GL implementation can slot in |
| `sdl.cpp` LAlt+LCtrl guard + `.txt` drag-and-drop → sequence library | `core/sdl/sdl.cpp` (+17) | 17 | The drop half is portable and cheap; take it with item 21 |
| VfW/comdlg32 file dialogs inside `dojo_gui.cpp` (`<windows.h>`, `<commdlg.h>`, `<shellapi.h>` at its head) | — | — | **A porting tax on every UI item above**: his `dojo_gui.cpp` includes Windows headers directly. Any port needs a small host-dialog shim |

---

### 2.2 Vendored dependencies we would have to add

| dep | his path | size | needed by |
|---|---|---|---|
| `imgui_markdown` (enkisoftware, zlib) | `core/deps/imgui_markdown/imgui_markdown.h` | 1,176 L | item 1, and every tooltip/help surface |
| `ImGuiColorTextEdit` (santaclose fork, **his std::regex patch**) | `core/deps/ImGuiColorTextEdit/{TextEditor.cpp,TextEditor.h,LanguageDefinitions.cpp,LICENSE}` | 3,285 + 535 L + 52 KB | item 18 (the Notepad) |

CMake wiring is 33 his-only lines in `CMakeLists.txt`; only ~5 of them are the deps
(`target_include_directories … ImGuiColorTextEdit`, `TextEditor.cpp`, `LanguageDefinitions.cpp`),
the rest are `core/dojo/*` sources we already list at `flycast-dojo/CMakeLists.txt:1012-1025`.

**ImGui itself is at parity as of today** — `imgui.cpp`, `imgui.h`, `imgui_widgets.cpp`,
`imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_internal.h` **and** `imconfig.h` all compare SAME, both
at `IMGUI_VERSION "1.90.4"` docking. So the docking prerequisite for his whole studio is met.

### 2.3 One correction to our own notes

`docs/tas-fork/PORTING_NOTES.md` states `dojo:AutoSeekState` is **ABSENT**. That is now stale:
commit `24a334e98` "feat(tas): port AutoSeekState, and wire the .frame sidecar hooks" (2026-09-07)
landed it — `AutoSeekState` appears in `core/rend/mainui.cpp` and `core/dojo/replay.cpp`.
`dojo:HoldStepFPS` and `dojo:StatesOpen` remain genuinely absent (0 grep hits in `core/`).

---

## 3. REVERSE — what WE have that HE does not

A merge goes both ways. Everything here is absent from his tree (verified by the `only in OUR core/`
set difference and by grep against his `core/`).

| ours | files / LOC | what it is | in his tree? |
|---|---|---|---|
| **The Lua surface** | `core/lua/lua.cpp` **2,321 L** vs his **822 L** (+1,550 / −51) | 23 namespaces (`flycast.emulator/video/movie/replay/config/memory/input/state/display/savestate/session/frame/ui`), incl. `movie.getButtons/setButtons`, `replay.*` unguarded, `savestate.tostring/fromstring/hash`, `snapshotLater/takeSnapshot/restoreLater`, `ui.*` primitives, `video.startRecording/...` | his is stock flycast Lua |
| **`core/lua/lua_console.{cpp,h}`** | 349 L | in-emulator Lua console | no |
| **`core/pause.{cpp,h}`** | 114 L | pause arbiter — no owner can cancel another's pause (commit `8aa99727d`). Replaces his single `bool manual_pause` (`davids_fly/core/dojo/dojo.h:291`) | no |
| **`core/deferred.{cpp,h}`** | 93 L | deferred-action hook — run work between frames at a safe point (commit `716a52174`) | no |
| **`core/determinism.{cpp,h}`** | 511 L + `DETERMINISM.md` (14,802 B) | a *predicate* (`determinism::isDeterministicRun()`) plus SERMAP and three savestate round-trip bug fixes (`070be4f6b`, `29662373c`). His equivalent is ~20 inline `if (config::GGPOEnable \|\| config::RecordMatches \|\| …)` sites, e.g. `davids_fly/core/emulator.cpp` Sh4Clock pin | conceptually yes, structurally no |
| **`core/rend/video_recorder.{cpp,h}`** | 631 L | the single capture stack actually wired into our renderers/audio/gui/Lua. **`avi_dump` is removed from our build** (0 hits in `CMakeLists.txt`); his is the live one | his is `avi_dump` |
| **The machine pool** | `docs/SPIKE-machine-pool.md` (27,459 B), `scripts/lua/pool-*.lua`, `scripts/tests/pool_determinism.lua` | in-memory + process-per-machine savestate pooling, verified (`f0ddc8ac3`, `b51062403`) | no |
| **emuapi** | submodule at `emuapi/` (adapters for flycast/fbneo/agnes/mock, `conformance.lua`, `spec.lua`, `FEATURE_MAP.md`) — incl. the `clock` group (`emuapi/conformance.lua:1215,1786`) | a cross-emulator Lua API + conformance suite | no |
| **Test tooling** | `scripts/testrun.sh` (12,166 B), `scripts/isotest.sh`, `scripts/replay-bindings-test.sh`, `scripts/tests/{tour,pool_determinism}.lua`, `scripts/tests/negative/*` (4 deliberately-failing fixtures), `docs/TEST-TOOLING.md` | Linux, offscreen, three-valued verdicts, `--watch`/`--hold`, and **ctest registration** (`CMakeLists.txt:1876-1905`, registered unconditionally, not under `ENABLE_CTEST`) | he has 15 PowerShell harnesses that all need his UI; **his gtest suite is bit-rotted and he says so** (`CLAUDE.tas-fork.md`) |
| **A working Linux/GL build** | — | he is DX9-on-Windows only (`pvr.rend = 1`, "AVI/MOV capture is wired for DX9 and DX11 only") | no |
| **Docs** | `docs/CROSS-PROJECT-LESSONS.md` (59,472 B), `UNIFICATION.md`, `UNIFIED.md`, `SYNC_SETTINGS.md`, `LUA_TODO.md` (29,978 B), `TODOS.md`, `docs/adapters/` | — | no |
| **Base drift (ours ahead)** | `mem_watch.h` (+127 ours), `vulkan_context.cpp` (+206), `gles.cpp` (+132), `dx11context.cpp` (+137), `dxcontext.cpp` (+114), `ggpo.cpp` (+88), `sh4_mmr.cpp` (+23), `spg.cpp` (+17), `pvr.cpp` (+18), `rec_x64.cpp` (+17) | his fork froze the dojo-7 base at `2026-08-09`; ours tracks a newer point | — |
| **A `.git` history** | 57 commits on `dojo7` | provenance for every change | no |

**Merge hazards this creates**, worth writing down before anyone attempts a two-way merge:

1. **Capture** — one stack each, different names. Ours (`videorec::requestStart/requestStop`) is
   wired; his (`avi_dump.isRecording()/avi_toggle_recording`) is what his UI calls, at
   `davids_fly/core/dojo/dojo.cpp:1952,1982` and `mainui.cpp:209-212` and `gui.cpp:4219`. Any UI port
   must translate those call sites. What is genuinely worth salvaging from `avi_dump` is its
   **clean pre-OSD readback** and the ProRes/CineForm recipe — as a mode of `video_recorder`, not as
   a second stack.
2. **Pause** — his `manual_pause` bool vs our `pausing::` arbiter (`dojo.cpp:2851` calls
   `pausing::resetAll()` where his sets `manual_pause = false`). His UI reads `manual_pause`
   directly in places.
3. **`replay.h`** — his has `extern Replay replay;` at `davids_fly/core/dojo/replay.h:55` with no
   definition anywhere (compiles, fails at link for any TU outside `dojo.cpp`); we deleted it and
   documented why at `flycast-dojo/core/dojo/replay.h:55-60`. Do not let it come back.
4. **Determinism** — his inline `config::GGPOEnable || …` conditions vs our predicate. A naive
   file-level merge would reintroduce ~20 of them.
5. **Base version** — his `core/` is frozen at the 2026-08-09 dojo-7 import; ours moved. File-level
   merges of `rend/`, `hw/` and `network/` will regress us.

---

## 4. Summary in one paragraph

We forked from a snapshot taken at **his head, `2026-09-05 05:34`** (copied `23:06`); all 15 of his
TAS docs are byte-identical to our `docs/tas-fork/` copies, so nothing he had written by then is
missing — but we have **no visibility at all** past `2026-09-05 23:06`, and his own tracker plus
three large branching-research documents written in the final recorded hour say the next thing he
built is **branches**. The divergence is **almost entirely the UI layer**: `core/dojo/dojo.h`
differs by one line, all eight `tas_*` modules and `mvc2.cpp`, `avi_dump.cpp` and `oslib/` are
byte-identical, and only 55 shared files differ at all — while `dojo_gui.cpp` is 19,894 lines of
his that we do not have and `gui.cpp` another 2,379. The cheap wins (colour/markdown layer, GL
viewport letterbox, log mute semantics, TAS option defaults, autosave ticks, thumbnails) total well
under 600 lines against engine support we already ship; the mid tier (hotkey system ~1,150 L, States
window ~771 L, stale/lock guard ~205 L — which would replace our documented `gui_locked_ranges`
stub, hold-Space scrub ~117 L, Input Viz ~372 L, generations UI ~838 L) is where the leverage is;
the studio proper (piano roll 3,400 L + notepad 3,100 L + menu bar 939 L, and two vendored deps
including a text editor he patched himself) has to move as one piece or not at all. Going the other
way, we hold the Lua surface (2,321 vs 822 lines), emuapi, the machine pool, the pause arbiter, the
deferred hook, a real determinism predicate, ctest wiring, a Linux/GL build and a git history —
none of which he has.
