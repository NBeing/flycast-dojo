# Panel inventory — every window in both trees, classified, and what a descriptor must hold

`[WRITTEN 2026-09-08]` The classification pass behind the panel registry
(`core/rend/panel.{h,cpp}`). `docs/MODULARIZATION.md` §S1 is the survey that
motivated it and counted the cost; this file is the enumeration §S1 does not
have — every `ImGui::Begin` in **both** trees put in exactly one class, the
eight facts each panel actually carries today, and the requirements those facts
put on the descriptor.

Marks follow `CLAUDE.md`: **`[MEASURED]`** a number this pass produced,
**`[SOURCE]`** read out of code (the quote is the citation, the line is a hint),
**`[REASONED]`** derived, **`[OPEN]`** unsettled.

**Scope note.** The descriptor has to fit both trees at once. Ours has **one**
window of the panel kind; his has **eleven**. A descriptor derived only from
ours would have no reason to exist; one derived only from his would encode his
four mechanisms as fields. Everything below is stated for both.

The four classes, as used here:

| class | test | who owns visibility |
|---|---|---|
| **PANEL** | user-openable, closable, independent, coexists with other panels | its own bool, per panel |
| **SCREEN** | full-screen modal state, mutually exclusive with every other screen | one arm of the `GuiState` switch |
| **OVERLAY** | transient non-interactive HUD drawn over gameplay | a cfg/config predicate at the call site; not user-openable |
| **TRANSIENT** | popup, notification, prompt, confirm | ImGui's popup stack, or a timer |

---

## Deliverable 1 — the classification

### 1.1 Ours — `/home/nbee/dev/flycast-dojo` @ `dojo7`

`[MEASURED 2026-09-08]` 24 top-level `ImGui::Begin` sites across the two files
named in the brief (`grep -n "ImGui::Begin(" core/rend/gui.cpp core/dojo/dojo_gui.cpp`).

| window | file:line | enclosing fn | class | why |
|---|---|---|---|---|
| `Game` | `core/rend/gui.cpp:559` | `submitGamePanel():517` | **PANEL** | the only dockable window in the tree; docked into the central node `FirstUseEver`, gated by `dojo:GamePanel`. Not *closable* — see §3.4 |
| `##commands` | `core/rend/gui.cpp:901` | `gui_display_commands():894` | SCREEN | `GuiState::Commands` arm, `:4384` |
| `Settings` | `core/rend/gui.cpp:2388` | `gui_display_settings():2381` | SCREEN | `GuiState::Settings` arm, `:4381` |
| `##main` | `core/rend/gui.cpp:3724` | `gui_display_content():3718` | SCREEN | content browser; `GuiState::Main` **and** `SelectDisk` arms, `:4387`/`:4404` |
| `##network` | `core/rend/gui.cpp:4093` | `gui_network_start():4085` | SCREEN | `GuiState::NetworkStart` arm, `:4410` |
| `##loading` | `core/rend/gui.cpp:4199` | `gui_display_loadscreen():4194` | SCREEN | `GuiState::Loading` arm, `:4407` |
| `##osd` | `core/rend/gui.cpp:4528` | `gui_display_osd():4507` | OVERLAY | `NoDecoration\|NoNav\|NoInputs\|NoBackground`; drawn only when a notification or FPS string is non-empty |
| `Profiler` | `core/rend/gui.cpp:4620` | `gui_display_profiler():4614` | OVERLAY | `config::ProfilerEnabled && config::ProfilerDrawToGUI` (`mainui.cpp:218`). **The one arguable row — see §4.3** |
| `Quick Map` | `core/rend/gui.cpp:4781` | `quick_map():4776` | SCREEN | `GuiState::QuickMap` arm, `:4431`; `fullScreenWindow(false)` |
| `Quick Player Select` | `core/rend/gui.cpp:4925` | `quick_player_select():4920` | SCREEN | `GuiState::QuickPlayerSelect` arm, `:4434` |
| `##disconnected` | `core/dojo/dojo_gui.cpp:562` | `gui_display_disconnected():557` | SCREEN | `GuiState::Disconnected` arm, `gui.cpp:4419` |
| `##replay_end` | `core/dojo/dojo_gui.cpp:643` | `gui_display_replay_end():638` | SCREEN | `GuiState::ReplayEnd` arm, `gui.cpp:4422`; a deliberate dead-end |
| `#one` | `core/dojo/dojo_gui.cpp:691` | `show_player_name_overlay():666` | OVERLAY | `NoDecoration\|NoInputs`; `dojo.play_match && config::PlayerNameOverlay` |
| `#two` | `core/dojo/dojo_gui.cpp:718` | `show_player_name_overlay():666` | OVERLAY | same call, second port |
| `#pos` | `core/dojo/dojo_gui.cpp:1051` | `show_replay_position_overlay():1017` | OVERLAY | `NoDecoration\|NoInputs`; `config::ReplayPositionOverlay` |
| `#one_input` | `core/dojo/dojo_gui.cpp:1178` | `show_last_inputs_overlay():1150` | OVERLAY | `NoDecoration\|NoInputs`; `ShowTrainingInputDisplay`/`ShowReplayInputDisplay` |
| `#two_input` | `core/dojo/dojo_gui.cpp:1188` | `show_last_inputs_overlay():1150` | OVERLAY | same call, second port |
| `#button_check_title` | `core/dojo/dojo_gui.cpp:1294` | `show_button_check():1256` | OVERLAY | `NoDecoration\|NoInputs` title banner *inside* the ButtonCheck screen |
| `##button_check<i>` (var `bc_title`) | `core/dojo/dojo_gui.cpp:1331` | `show_button_check():1256` | SCREEN | the interactive body of the `GuiState::ButtonCheck` arm (`gui.cpp:4425`), one per port in a loop |
| `#exit_description` | `core/dojo/dojo_gui.cpp:1537` | `show_button_check():1256` | OVERLAY | `NoDecoration\|NoInputs` hint strip in the same screen |
| `##test_game` | `core/dojo/dojo_gui.cpp:1558` | `gui_display_test_game():1551` | SCREEN | `GuiState::TestGame` arm, `gui.cpp:4428` |
| `Choose Platform` | `core/dojo/dojo_gui.cpp:1706` | `gui_display_select_platform():1699` | SCREEN | `GuiState::QuickSelectPlatform` arm, `gui.cpp:4437`; `NoTitleBar` despite the name |
| `#pause` | `core/dojo/dojo_gui.cpp:2491` | `show_pause():2439` | OVERLAY | `NoDecoration\|NoInputs`; the "Paused / Stepping / Buffering" tag only |
| `##stream_wait` | `core/dojo/dojo_gui.cpp:2521` | `gui_display_stream_wait():2511` | SCREEN | `GuiState::StreamWait` arm, `gui.cpp:4469` |

**Ours: PANEL 1, SCREEN 13, OVERLAY 10, TRANSIENT 0.**

Two screens in `dojo_gui.cpp` are `BeginPopupModal`, not `Begin`, so they do not
appear above and are still SCREEN by this classification — they are `GuiState`
arms: `gui_display_replays()` (`:1739`, `GuiState::Replays`) and
`gui_display_savestate_dl()` (`:2193`, `GuiState::DownloadState`). They are
listed because the port turns the first into a studio window.

There is **no TRANSIENT top-level `Begin` in this tree.** The class is real and
served entirely by ImGui's popup stack — `error_popup()` (`gui.cpp:2220`, called
once at the tail of `gui_display_ui`, `:4477`), `gui_display_notification()`
(`:3638`), and the mapping/confirm modals at `:1825`, `:1914`, `:1991`, `:2140`.
None of them is registry business, now or after the port.

Four more `Begin` sites exist outside the brief's two files and none changes the
count of panels: `gui_cheats.cpp:36,89` (`##main`, the Cheats SCREEN),
`gui_util.cpp:626-647` (four `##inset*` safe-area OVERLAYs),
`gui_android.cpp:36` (`Virtual Joystick`, the `VJoyEdit` SCREEN), and
`lua.cpp:796` / `:1478` — the second of which is a **script-authored** window
(`ui.begin(name)`), i.e. a panel the registry can never know about. §3.5.

### 1.2 His — `/home/nbee/dev/davids_fly` @ `tas-tools`

`[MEASURED 2026-09-08]` 38 top-level `ImGui::Begin` sites (27 in
`core/dojo/dojo_gui.cpp`, 11 in `core/rend/gui.cpp`).

**The eleven PANELs.** All eleven, and only these eleven, appear in three
independent hand-maintained lists — the Windows menu (`dojo_gui.cpp:18798-18812`),
the DockBuilder default layout (`gui.cpp:4236-4252`), and `tasZoomKeyName`'s
id→label table (`dojo_gui.cpp:6705-6720`). That the three agree is luck, not
structure; §3.1.

| window (ImGui title) | file:line | draw fn | class | why |
|---|---|---|---|---|
| `Piano Roll` | `dojo_gui.cpp:12769` | `show_piano_roll():12379` | **PANEL** | `Begin(…, &open, ghost\|MenuBar)`; dockable, own menu bar |
| `Notepad — …###seqnotepad` | `dojo_gui.cpp:6786` | `tasNotepadWindow():6773` | **PANEL** | `Begin(title, &notepadOpen)`; dynamic title over a stable `###seqnotepad` id |
| `Input Sender` | `dojo_gui.cpp:10328` | `tasInputSenderWindow():10321` | **PANEL** | `&inputSenderOpen`, dockable |
| `Snippets` | `dojo_gui.cpp:12142` | `tasSequencesWindow():12134` | **PANEL** | `&seqLibOpen`, dockable |
| `Macros` | `dojo_gui.cpp:11931` | `tasMacrosWindow():11913` | **PANEL** | `&macrosBrowserOpen`, dockable |
| `Frame Skip Test` | `dojo_gui.cpp:16256` | `tasFrameSkipTestWindow():16229` | **PANEL** | `&fstOpen`, dockable |
| `Test Lab` | `dojo_gui.cpp:16670` | `tasTestLabWindow():16645` | **PANEL** | `&labOpen`, dockable; auto-opens on a lab clip |
| `Timeline` | `dojo_gui.cpp:18893` | `show_savestate_overlay():18833` | **PANEL** | `&timelineOpen`; **dual-mode**, see §1.3 |
| `Input Viz` | `dojo_gui.cpp:17349` | `show_input_visualizer():17188` | **PANEL** | plain resizable, cfg-backed; **dual-mode** |
| `Hotkeys` | `dojo_gui.cpp:16970` | `show_hotkey_overlay():16933` | **PANEL** | `Begin("Hotkeys", NULL, 0)`; **dual-mode**, and has no close X at all |
| `States` | `gui.cpp:5586` | `gui_draw_slot_picker():5549` | **PANEL** | `&slot_picker_open`; **dual-mode**; owned by a *different* TU |

**The four dual-mode partners** (§1.3) — same draw function, second `Begin`:
`#savestate_slot` (`dojo_gui.cpp:18904`, OVERLAY), `#input_viz` (`:17356`,
OVERLAY), `#hotkey_help` (`:16979`, OVERLAY), `##slotpicker` (`gui.cpp:5597`,
**TRANSIENT** — a forced-centre `ImGuiCond_Always` no-titlebar dialog that is
nonetheless interactive, so it is neither a pinned HUD nor a dockable panel).

**The SCREENs (14).** `##commands` `gui.cpp:1037`, `Settings` `:2430`, `##main`
`:3414`, `##network` `:3752`, `##loading` `:3858`, `Quick Map` `:5794`,
`Quick Player Select` `:5938`; `##disconnected` `dojo_gui.cpp:802`,
`##replay_end` `:887`, `##button_check<i>` `:19396`, `##test_game` `:19623`,
`##startup_prompt` `:19823`, `Choose Platform` `:20110`, `##stream_wait`
`:21852`. Each is one arm of the `GuiState` switch, exactly as ours.
`##startup_prompt` is the one screen we do not have.

**The OVERLAYs (12).** `##osd` `gui.cpp:4297`, `Profiler` `:4493`; `#one`/`#two`
`dojo_gui.cpp:987`/`1014`, `#one_input`/`#two_input` `:19241`/`:19251`,
`#button_check_title` `:19359`, `#exit_description` `:19602`, `#pause` `:21828`,
plus the three dual-mode partners above.

**His: PANEL 11, SCREEN 14, OVERLAY 12, TRANSIENT 1.**

Two further windows are not `Begin` sites and are not registry business:
`ImGui::BeginMainMenuBar()` in `show_main_menu_bar()` (`dojo_gui.cpp:17927`) —
the F5 studio command bar, chrome — and `tasDockspaceHost()` (`gui.cpp:4217`),
the host itself. There is **no** `tasBegin`/window-factory wrapper: every panel
calls `ImGui::Begin` directly and then decorates with `tasPreviewGhostFlags()`
(`dojo_gui.cpp:34`) and `tasWindowUiZoom()` (`:6747`).

### 1.3 The finding the class list does not survive intact: four panels are dual-mode

`[SOURCE]` Four of his eleven are **one draw function with two `Begin` sites**,
chosen by `tasStudioMode()` — `cfgLoadBool("dojo","TasUi",true) && !avi_dump.isRecording()`
(`dojo_gui.cpp:16920-16923`). With the studio up they are dockable PANELs; with
F5 down or a capture running they are pinned `NoDecoration|NoInputs` OVERLAYs:

| feature | PANEL arm | OVERLAY / TRANSIENT arm |
|---|---|---|
| savestate HUD | `Timeline` `:18893` | `#savestate_slot` `:18904` |
| input visualizer | `Input Viz` `:17349` | `#input_viz` `:17356` |
| cheat sheet | `Hotkeys` `:16970` | `#hotkey_help` `:16979` |
| slot wall | `States` `gui.cpp:5586` | `##slotpicker` `gui.cpp:5597` |

`[SOURCE]` `dojo_gui.cpp:16915-16918` states the design: *"while the TAS UI (F5)
is up, the pinned HUD decals become real dockable windows … Off (F5 hidden /
capture) they stay pinned overlays."*

**This is a requirement, not a curiosity.** A registry whose draw loop is
"if open, call draw()" is fine here *only because* the mode switch lives inside
the draw function. It stops being fine the moment anything outside asks "is this
a panel?" — the docking layout, a `--panel=x` test hook, the focus ring. §3.6.

---

## Deliverable 2 — the eight facts, as they exist today

### 2.1 Ours — one panel

| fact | `Game` |
|---|---|
| 1 · stable id | none. The ImGui title `"Game"` is the only key, and `centralNodeId` docking is by that literal (`gui.cpp:559`) |
| 2 · label | `"Game"` — same string as the id, which is the collision the registry separates |
| 3 · open bool lives | **nowhere.** `cfgLoadBool("dojo","GamePanel",true)` read live each frame (`:533`); there is no `bool` and no close X |
| 4 · persisted | yes, as `dojo:GamePanel` — but as a *setting*, not as window state. `imgui.ini` holds its position/dock, never its visibility |
| 5 · draw fn | `submitGamePanel()` `gui.cpp:517` |
| 6 · **stream** | **BOTH.** `gui_display_ui` `:4374` (for six enumerated `GuiState`s) and `gui_display_osd` `:4517`. Told by: it is called immediately after `submitDockspaceHost()` in each, and the comment at `:437-446` names the rule |
| 7 · gating | `dojo:GamePanel` **and** `renderer != nullptr` **and** `GetFrameTexture().handle != 0` — three separate reasons, each logged once via `why()` (`:524-527`) |
| 8 · shortcut | none |

`[MEASURED]` `grep -rn "panels::" core/` outside `panel.{h,cpp}` returns nothing:
the registry compiles but is **not wired yet**, so `Game` is still hand-called.

### 2.2 His — eleven panels, and the four mechanisms

**Fact 3 — where the "is it open" bool lives.** `[SOURCE]` MODULARIZATION.md §S1
says four mechanisms; the enumeration is:

| mechanism | panels | where |
|---|---|---|
| **cfg key, no bool** | Piano Roll (`dojo:PianoRoll`), Input Viz (`dojo:InputViz`) | read live at the top of the draw fn (`:12382`, `:17193`) |
| **file-static `bool`** | Snippets `seqLibOpen:3934`, Notepad `notepadOpen:4609`, Macros `macrosBrowserOpen:9078`, Input Sender `inputSenderOpen:10028`, Timeline `timelineOpen:10029`, Frame Skip Test `fstOpen:15849`, Test Lab `labOpen:16573` | seven statics declared ~12,600 lines apart |
| **`Dojo` god-object member** | Hotkeys — `dojo.hotkey_overlay` | `core/dojo/dojo.h:376` |
| **cross-TU accessor pair** | States — `gui_states_is_open()` / `gui_states_set_open()` | `gui.cpp:5017-5023`, over a static `slot_picker_open` `:4675` |

The cfg-key panels are the awkward ones: the Piano Roll's close X writes into a
function-local `bool open = true` at `:12768` and the *write-back* is 3,000 lines
later at `:15773-15777` (`cfgSetVirtual` + `cfgSaveBool`). There is no `bool` for
`open` to point at, so the registry has to create one and drive the cfg from it.

**Fact 4 — persistence.** Three separate places, and one panel is in none:

| panel | persisted as | written by |
|---|---|---|
| Snippets | `dojo:SnippetsOpen` | `show_tas_tool_windows()` load-once `:16814` + dirty-check `:16862` |
| Input Sender | `dojo:InputSenderOpen` | same block |
| Timeline | `dojo:TimelineOpen` | same block |
| Macros | `dojo:MacrosOpen` | same block |
| Frame Skip Test | `dojo:FrameSkipTestOpen` | same block |
| Test Lab | `dojo:TestLabOpen` | same block |
| Notepad | `dojo:NotepadOpen` (+ `NotepadPath`/`Lib`/`LibFile`) | same block, its own branch `:16820-16846` |
| States | `dojo:StatesOpen` | **a second, duplicate load-once/dirty-check block**, `gui.cpp:5554-5568` |
| Piano Roll | `dojo:PianoRoll` | the cfg key *is* the flag; written by the menu `:18798`, the close X `:15775`, and the hotkey handler `gamepad_device.cpp:307` |
| Input Viz | `dojo:InputViz` | same shape; menu `:18801`, hotkey `gamepad_device.cpp:298` |
| Hotkeys | `dojo:ShowHotkeyOverlay` | **only from the input layer** — `gamepad_device.cpp:329`. The Windows menu toggles `dojo.hotkey_overlay` (`:18808`) and **does not save it**, so a menu toggle is lost on exit while the F9 toggle survives |

`[SOURCE]` The load-once block names the gap it exists to fill —
`dojo_gui.cpp:16805-16808`: *"imgui.ini saves positions/docking but NOT whether
a window is shown, so load the open flags once … and save whenever one toggles."*

**Fact 1 — stable id.** No panel has an explicit id, but one exists *de facto*:
the **UI-zoom cfg key**. `tasSelectedKey` (`:6702`) holds it, `tasZoomKeyName()`
(`:6705-6720`) maps it back to the label, and the focus menu bar dispatches on it.

| panel | de-facto id | label | note |
|---|---|---|---|
| Piano Roll | `RollGridScale` | Piano Roll | owns a *second* key `RollUiScale` (chrome vs grid) — id ≠ one key |
| Notepad | `NotepadUiScale` | Notepad | ImGui id is `###seqnotepad`; a third spelling |
| Input Sender | `InputSenderUiScale` | Input Sender | |
| Input Viz | `InputVizUiScale` | Input Viz | |
| Timeline | `TimelineUiScale` | Timeline | |
| Snippets | `SnippetsUiScale` | Snippets | |
| Macros | `MacrosUiScale` | Macros | |
| Hotkeys | `HotkeysUiScale` | Hotkeys | |
| States | `StatesUiScale` | States | |
| Frame Skip Test | `FrameSkipTestUiScale` | Frame Skip Test | **no** menu-bar branch |
| Test Lab | `TestLabUiScale` | Test Lab | **no** menu-bar branch |
| — | `ReplaysUiScale` | Replays | a zoom key for a **SCREEN**; the table already leaks |

So three id-ish strings per panel (zoom key, ImGui title, cfg open key) and no
rule saying which is canonical. `tasResetAllZoom` (`:6738-6747`) is a **fourth**
hand-maintained list of the same set, with defaults — and the four disagree:
`RollUiScale` is in `tasResetAllZoom` but has no `tasZoomKeyName` entry;
`FrameSkipTestUiScale`, `TestLabUiScale` and `ReplaysUiScale` are in both name
tables but have **no branch in the focus menu bar**, so those windows can take
the bar (steel-blue chip, `View` menu) and contribute no menus. `[SOURCE]`
`tasSelectedKey` is set from exactly one place — `tasWindowUiZoom(cfgKey)`
(`:6751-6768`), called by each panel right after its `Begin` — which is the
single registration point the id field replaces.

**Fact 6 — which frame stream.** `[SOURCE]` The draw list is written **four
times and the four are not the same list**:

| # | site | what it calls |
|---|---|---|
| 1 | `gui.cpp:4082-4085` — Settings, while a size slider is held | `show_savestate_overlay`, `show_input_visualizer`, `show_piano_roll` (3 of 11) |
| 2 | `gui.cpp:4164-4170` — `GuiState::Paused` | menu bar, host, `gui_purge_stale_tick`, `show_pause`, `gui_draw_slot_picker`, `show_piano_roll`, `show_tas_tool_windows` |
| 3 | `gui.cpp:4328-4333` — the OSD stream | `show_savestate_overlay`, `show_hotkey_overlay`, `show_input_visualizer`, `show_piano_roll`, `show_tas_tool_windows`, `gui_draw_slot_picker` (all 11) |
| 4 | `dojo_gui.cpp:21791-21795` — inside `show_pause()` | `show_savestate_overlay`, `show_hotkey_overlay`, `show_input_visualizer` |

List 2 reaches all eleven **only because** list 4 is nested inside it. The
comment above list 2 (`gui.cpp:4161-4163`) is the scar: *"show_pause() renders
the whole overlay suite itself … calling them again here was double-rendering
everything (user saw the Timeline content twice)."* The second scar is at
`dojo_gui.cpp:15779-15783`: the four tool windows *"used to be drawn at the tail
of show_piano_roll, so toggling the roll off made them vanish even though their
own on/off flags were still set."*

`[SOURCE]` Two facts make the divergence concrete rather than aesthetic.
**Hotkeys is the only one of the eleven missing from list A** — nothing decided
that; it is where it landed. And **`timelineOpen` is loaded by
`show_tas_tool_windows()` but the Timeline is drawn by `show_savestate_overlay()`**,
which list A calls *without* ever calling `show_tas_tool_windows` — so in the
Settings-preview stream the flag holds whatever it last had, i.e. its compile
default `true` if the load-once block has not run this session. A latent bug
that the registry's single load makes unrepresentable.

**Every panel needs both streams.** Told two ways: (a) all eleven appear in
list 3 (gameplay) and list 2 (Paused); (b) the Piano Roll's own note —
*"paused is exactly when the roll is edited"* (`gui.cpp:4169`) — and the
States note *"must work while frame-advance-paused too"* (`:4168`). List 1 is
the odd one and is a deliberate 3-of-11 preview, not a stream. `[REASONED]`
So `stream = Both` for all eleven; the field earns its keep by making list 1's
divergence and list 4's nesting representable instead of accidental.

**Fact 7 — gating predicates.** Every one is a conjunction, and they are not the
same conjunction:

| panel | gate |
|---|---|
| all 11 (blanket) | `dojo:TasUi` (F5) **and** `!avi_dump.isRecording()` — applied at `show_tas_tool_windows():16797-16800` for six, and re-implemented inside each of the other five |
| Piano Roll | + `dojo:PianoRoll` (`:12381-12384`) |
| Input Viz | + `dojo:InputViz` **and** `!settings.content.fileName.empty()` — a game must be loaded (`:17193-17196`) |
| Hotkeys | + (`dojo.hotkey_overlay` **or** a >0.4 s Shift-peek) (`:16957`) |
| Timeline | + `timelineOpen` (`:18848`) |
| States | + `slot_picker_open` (`gui.cpp:5567`) |
| Notepad…Test Lab (6) | + their own static, checked at the top of each draw fn |
| Test Lab | + auto-opens itself when the clip is a lab clip |

**Fact 8 — shortcut.** Four of eleven have one, and **none of them is a string**:
they are rebindable `DreamcastKey` actions in `TAS_HOTKEYS[]` (`dojo_gui.cpp:1085`),
handled in `core/input/gamepad_device.cpp`:

| panel | action | default key | handler | semantics |
|---|---|---|---|---|
| States | `EMU_BTN_SLOT_PICKER` | **F4** (`keyboard_device.h:77`) | `:269` | **opens** (`gui_open_slot_picker()`) — does *not* toggle |
| Hotkeys | `EMU_BTN_HOTKEY_HELP` | **F9** (`keyboard_device.h:78`) | `:325` | toggles `dojo.hotkey_overlay`, saves `ShowHotkeyOverlay` |
| Input Viz | `EMU_BTN_INPUT_VIZ` | **none** — F5 was reassigned to `TAS_UI` (`keyboard_device.h:81`) | `:296` | toggles the cfg key inline |
| Piano Roll | `EMU_BTN_PIANO_ROLL` | **none** — *"no default key by user decision"* (`keyboard_device.h:82-83`), F6 explicitly un-migrated (`sdl_keyboard.h:80`) | `:305` | toggles the cfg key inline |
| — (blanket) | `EMU_BTN_TAS_UI` | F5 | `:314` | toggles `dojo:TasUi`; not a panel |
| Frame Skip Test | `EMU_BTN_FST_NEXT` | F10 / Shift+F10 | — | an **action inside** the panel (`fstStepReq`), not a visibility toggle |

So of eleven panels: **two** have a default key, **two** more have an unbound
action, and **one** has a hotkey that is not about visibility at all. Four
different relationships between "a panel" and "a key".

`[SOURCE]` The `TAS_HOTKEYS` comment states the property the registry must not
break: *"The KEY shown in the panel is looked up LIVE from the keyboard device's
input mapping (single source of truth), so this display can never drift."*
**So `const char *shortcut` — nbneo's field — is the wrong type here.** §3.3.

`gamepad_device.cpp:296-332` is a **fifth** place that duplicates the toggle+
persist dance, once per panel, and the four copies do not agree (one opens
rather than toggles; one writes a cfg key with a different name than the bool).

---

## Deliverable 3 — requirements on the descriptor

The descriptor being built is `panels::Panel` (`core/rend/panel.h`), today:
`id`, `label`, `bool *open`, `void (*draw)()`, `u8 stream`, `bool persist`.
Six fields. Assessed field by field against the evidence above.

### 3.1 The four fields that are load-bearing, with the panel that forces each

| field | forced by | evidence |
|---|---|---|
| **`id` separate from `label`** | Notepad | its ImGui title is dynamic (`"Notepad — %s%s###seqnotepad"`, `:6782`), its dock key is `###seqnotepad`, its cfg key is `NotepadOpen`, its focus key is `NotepadUiScale`. **Four spellings of one panel.** An id collapses them |
| **`open` as the one owner** | all 11 | four mechanisms (§2.2 fact 3) and five write sites (menu, close X, hotkey handler, load-once block, dirty-check block) for one bit |
| **`stream`** | Timeline, and the four tool windows | the two shipped defects (`gui.cpp:4161`, `dojo_gui.cpp:15779`) are both "wrong stream set", and today that fact is stored only in the *shape of four call lists* that disagree (§2.2 fact 6) |
| **`persist`** | Hotkeys, and the Piano Roll | Hotkeys is persisted from the input layer but **not** from its menu item — it is half-persisted today, which no reader can tell. And `persist` must stay mandatory: nbneo's post-mortem records `showRemap` as deliberately *not* persisted (*"a remap window left open across a restart is a surprise, not a preference"*), and a default would reverse that silently |

`label` is not independently load-bearing but costs nothing and deletes
`tasZoomKeyName`'s 12-branch strcmp map (`:6705-6720`) outright.

### 3.2 The field the descriptor is missing, and the one panel that decides it: `enabled()`

MODULARIZATION.md §S1's sketch had `bool (*enabled)()`; `panels::Panel` does
not. The evidence says **add it**, on the strength of one panel, not eleven:

- Ten of the eleven gates are either the blanket `tasStudioMode()` (which belongs
  to the *loop*, not a panel) or a restatement of `*open` (which the loop already
  checks). Neither needs a field.
- **Input Viz is the exception**: `!settings.content.fileName.empty()`
  (`:17196`) — "a game must be loaded". That is a genuine third predicate, it is
  not derivable from `open`, and without it the menu item offers a panel that
  draws an empty frame. `[REASONED]` Our tree will grow more of these fast, since
  most TAS panels are meaningless with no movie loaded (MODULARIZATION.md §S5
  makes the same prediction: *"S1's `enabled()` predicates land here or become 14
  more conjunctions"*).

**Recommendation: add `bool (*enabled)()` with `nullptr` = always.** One panel
forces it today; the alternative is that the predicate goes back into the top of
each draw function, which is precisely where the four mechanisms came from. It
is also what greys the menu row, which nothing else can do.

### 3.3 The field to leave out: `shortcut`

nbneo has `const char *shortcut`. **Do not copy it.** Ours are rebindable
`DreamcastKey` actions whose display string must be resolved live from the input
mapping (§2.2 fact 8) — a literal string in the descriptor would be the exact
drift `TAS_HOTKEYS` was built to prevent, and it would be wrong the first time a
user rebinds F4.

The honest options are (a) nothing, and let `gamepad_device.cpp` keep its cases;
or (b) `DreamcastKey toggleKey` plus one generic handler that does
`panels::find`-by-key → flip `*open` → `saveOpenState()`, replacing five
divergent copies with one. `[OPEN]` (b) is the better end state and would delete
real duplication, but it is a second registry keyed the other way round
(action→panel) and it couples `core/input` to `core/rend`. **Ship without it.**
Revisit when the second panel-toggling hotkey lands in our tree; today we have
zero.

### 3.4 What a plain struct cannot express — six things, and only one needs a new field

**(a) Ordering — a panel that owns an offscreen pass.** `Game` must be submitted
before every other dockable window in its frame, because `submitDockspaceHost()`
publishes the central node and `submitGamePanel()` consumes it and republishes
`rend::setContentArea` (`gui.cpp:559-596`). A `for` loop over a vector gives
draw order = registration order, which is *implicit*. nbneo hit this exactly and
solved it by keeping the Game panel **out** of the loop and asserting the
constraint at two arms (`main_gui.cpp:1143` `order_fail`, `:1161` the
`ImGui::Render()` check). `panel.h`'s `find()` doc already anticipates this
(*"the one ordering exception that has to name a panel"*). **Keep `Game` called
by name, not from the loop.** Registration order is not a contract worth having.

**(b) Work that must happen when the panel is CLOSED.** Six real cases, all his,
all currently sitting *above* the early-return. `drawStream()` skips a closed
panel entirely (`panel.cpp:57-58`), so every one of these is dropped by a naive
conversion:

| panel | work done while closed | site |
|---|---|---|
| **Frame Skip Test** | `fstRunTick()` — **the variant sweep itself** — plus the F10 de-bounce, the Paused bake, and the combo-peak fold whose own comment says *"runs whether the window shows or not"* | `:16229-16252` (the `if (!fstOpen) return;` is at `:16252`, after **five** blocks) |
| **Test Lab** | the `labIsActive` edge detector that **sets `labOpen = true` itself** — a lab session opens its own window once | `:16646-16655`, before `:16656` |
| Timeline | `gui_locked_ranges(_lr)` — publishes the locked-range snapshot the **emu thread** reads, *"so it stays current even when every TAS window is hidden"* | `:18836-18837` |
| Hotkeys | maintains `dojo.shift_held_since` for the Shift-peek before deciding to draw | `:16942-16955` |
| Piano Roll | clears `gutterOpMode`/`gutterOpAnchor` — *"an interrupted drag must NOT fire a stale delete/insert when the roll re-appears"* | `:12385-12388` |
| Snippets | *inverse*: `gui_sequence_dropped()` refuses an OS file drop unless the window rendered within the last 0.5 s (`seqWinAliveAt`) — the panel's **liveness is the gate** for an external event | `:8335-8342`, set at `:12138` |

Two of these are structurally different from the rest and they decide the field.

**Test Lab is the one the loop cannot express at all.** It writes its own `open`
flag from inside the code the loop only runs when `open` is already true — so
converted as-is, a lab session would never open its window. This is not a
side effect to relocate; it is a panel asking to be opened by a condition. It is
served by `enabled()` returning true plus an explicit `gui_open_panel("testlab")`
call from wherever `labIsActive` becomes true — **not** by a new field.

**Frame Skip Test is the one that most tempts a `tick()` field, and should not
get one.** `fstRunTick()` is a state-machine step that drives savestate loads and
frame advances; it is emulator work that happens to live in a GUI file. Give it
a `tick` slot in the descriptor and the registry becomes a scheduler, and the
next question is what thread it runs on.

**Recommendation: no `tick()` field.** All six move out to their own per-frame
call as they are ported, and the registry stays a *draw* registry. Recorded here
so the port cannot silently drop them — the locked-range publish in particular
is a correctness bug if lost (S4's territory, not S1's), and the FST sweep is a
feature that would simply stop.

**(b′) A panel that must draw a sub-pane it does not own.** `States` hosts the
shared GENERATIONS pane across a TU boundary — `gui.cpp:5640-5651` calls
`dojo_gui.show_states_snapshots()` (`dojo_gui.cpp:17641-17668`), which draws
`tasClipSnapshotsPane` **and** `tasClipPopups`, three modals submitted
unconditionally. `[REASONED]` Fine for a descriptor: it is all inside one
`draw()`. Named only because it is why `States`' `open` bool lives in `gui.cpp`
while its menu row lives in `dojo_gui.cpp` — the exact split `bool *open` exists
to bridge.

**(b″) Panels that own GPU texture lifetime.** `States` caches thumbnails as
`imguiDriver->updateTexture("tas_slot_thumb_<n>", …)` (`gui.cpp:4692-4740`),
invalidated by mtime and cleared wholesale when `savestateFolderOverride` changes;
`Macros` and the Replays browser do the same for PNG strips
(`dojo_gui.cpp:8456-8500`). Each carries the *"NEVER cache an `ImTextureID`
across frames"* contract (`gui.cpp:4678-4685`). `[REASONED]` No field: the driver
owns the map and the panel re-asks each frame. But it is the reason a panel is
not freely reorderable in the draw loop if its texture is uploaded lazily during
its own draw — one more argument for not treating registration order as a
contract (see (a)).

**(c) Dual-mode panels** (§1.3) — four of eleven are a PANEL or an OVERLAY
depending on `tasStudioMode()`. `[REASONED]` This is expressible *today* because
the switch is inside the draw function and both arms are the same `draw()`. It
stops being expressible the moment anything outside asks the question. **No field
now**; the trap is registering the OVERLAY arm as a second panel, which would
give one feature two ids, two menu rows and two persistence keys.

**(d) Script-authored panels.** `lua.cpp:1478` (`ui.begin(name)`) creates windows
the registry cannot know about. Out of scope, permanently.

### 3.5 One correctness note on `panel.cpp` as written

`add()` calls `find(p.id)` **before** the null-field check (`panel.cpp:26` vs
`:31`). It is safe — `find(nullptr)` returns `nullptr` (`:43-44`) — but a null
`id` gets past the duplicate check and is rejected one branch later, with a log
line that prints `"(null id)"`. Reordering the two checks would say what
happened more directly. Not a bug.

### 3.6 The verdict on the descriptor

`{ id, label, open, draw, stream, persist }` + **`enabled`** = seven fields.
Six of the seven are forced by a named panel above. Leave out `shortcut`
(§3.3), leave out any `tick`/always-draw hook (§3.4b), leave out ordering — name
the exception instead (§3.4a). That is smaller than MODULARIZATION.md's sketch by
one field and larger than what is on disk by one, and every field can be pointed
at the panel that needs it.

**Two things the registry needs that are not fields.** One `void panels::open(const char *id)`
— because Test Lab opens itself, F4 *opens* the States window rather than
toggling it, and F8 opens it too (`gamepad_device.cpp:345-355`); today those are
three different spellings of one verb. And an **alias-on-load** step in
`loadOpenState()`, so the eleven keys already in users' `emu.cfg`
(`PianoRoll`, `InputViz`, `ShowHotkeyOverlay`, `StatesOpen`, and the six
`*Open` keys) are read once before `Panel.<id>` takes over — otherwise landing
the registry silently resets every panel, which is nbneo's post-mortem warning
arriving from the other direction.

---

## Deliverable 4 — migration order

### 4.1 Convert first: `Game` — the cheapest real proving case

It is the **only** window in our tree of the panel kind, and it is a real one:
dockable, cfg-gated, drawn in both streams. Converting it exercises every loop
against production code rather than the synthetic panels in `selfTest()`:

- **the stream loop**, which is the field that does not exist elsewhere and the
  one the two shipped defects were. `Game` is drawn from `gui_display_ui:4374`
  *and* `gui_display_osd:4517`, so `Stream::Both` is proved by both paths, and a
  regression shows up instantly and visibly — the picture jumps out of its dock.
- **the id/label split**, on the panel where they are currently the same literal
  string in a `DockBuilderDockWindow`-shaped position.
- **`persist`**, honestly: `Game` is `persist = false`. `dojo:GamePanel` is a
  *setting* (a renderer A/B switch with `dojo:DockGameViewport` beside it), not
  window state, and folding it into `Panel.game` would change its meaning and
  break `-config dojo:GamePanel=no`. **`Game` is therefore the proving case for
  the `persist=false` arm**, which is the arm nbneo's post-mortem says a registry
  gets wrong by defaulting.
- **the ordering exception**, which the fork already has and must keep: `Game`
  stays called by name via `panels::find("game")`, not from `drawStream()`.
  Proving the exception *is* part of proving the registry.

It costs one `panels::add()`, one `bool` for `open` fed from `dojo:GamePanel`,
and the two hand call-sites becoming `find("game")`. **No user-visible change is
the pass condition**, which is exactly what you want from a scaffolding proof.

`[OPEN]` `Game` has no close X and never should — its `open` is a setting the
user flips in a menu, not a window they dismiss. If the registry's menu loop
would render it as a checkbox in Windows, that is a UX change; either keep it
out of the menu loop (a `hidden` bool — a **seventh** field for one panel, so:
no) or accept the checkbox. Worth a decision before the conversion, not after.

### 4.2 Convert second, when it arrives: `Input Viz`

The first *ported* panel should be Input Viz, and the port order in
`DAVIDS-TAS-STUDIO.md` §7 already puts it early for independent reasons (*"by far
the best value-per-line … the only one that pays off with nothing else ported"*).
For the registry it is the ideal second case because it is the panel that
**forces `enabled()`** (§3.2), it is cfg-backed with no bool (so it proves the
"create a bool, drive the cfg from it" migration that Piano Roll will need too),
and it is dual-mode (so it proves §3.4c before ten more arrive).

### 4.3 Never convert

| window | why not |
|---|---|
| the **13 SCREENs** | mutually exclusive by construction — one arm of one `switch`. MODULARIZATION.md §S1 says this outright: *"Converting them is a real UX change wearing a refactor's clothes."* The registry's premise is independent coexisting bools; a screen has neither property |
| the **10 OVERLAYs** (`#pos`, `#one`, `#two`, `#one_input`, `#two_input`, `##osd`, `#pause`, `#button_check_title`, `#exit_description`) | not user-openable. Their visibility is a per-frame predicate on emulator state (`dojo.play_match`, `config::ReplayPositionOverlay`), not a bool a user toggles. Giving them an `open` flag would invent a control that does not exist |
| **`Profiler`** | the one that *looks* convertible and must not be. It draws in a **third ImGui frame stream** — `gui_display_profiler()` has its own `NewFrame` (`gui.cpp:4618`; the three `NewFrame` sites are `:4347`, `:4518`, `:4618`) and is called from `mainui.cpp:218` in the *else* branch, i.e. only when the UI is closed. `Stream` is a two-bit mask; `Profiler` fits neither bit. Converting it means a third bit that exactly one window would ever set |
| the **TRANSIENT class** entirely | `error_popup`, `gui_display_notification`, the mapping/confirm modals. ImGui's popup stack already owns their lifetime |
| **Lua `ui.begin`** windows | authored at runtime by a script; no descriptor can exist for them |

### 4.4 Order for the ported panels

`[REASONED]` Once `Game` and `Input Viz` have proved the loops, take the
remaining nine in the order `DAVIDS-TAS-STUDIO.md` §7 already sets — but take
each one's **registry row before its body**. A panel that lands as a row plus an
empty `draw()` is one line to review; a panel that lands as 1,500 lines *and* six
touch points is the thing this seam exists to prevent. The seven file-static
panels (Notepad, Input Sender, Snippets, Macros, Frame Skip Test, Test Lab,
Timeline) are the easy ones — their bool already exists and only its owner moves.
The two cfg-backed ones (Piano Roll, Input Viz) need a bool created and the cfg
key kept as an **alias on load**, so an existing `emu.cfg` does not silently
reset them — the same one-release asymmetry nbneo's post-mortem prescribes
(`panel.cpp` writes `Panel.<id>`, and `dojo:PianoRoll` / `dojo:InputViz` /
`dojo:StatesOpen` / the six `*Open` keys must still be read once).
`States` is last of the easy set only because its bool lives in another TU behind
an accessor pair — which is the case the registry is *best* at, since `open` is
just a pointer.

---

## `[MEASURED 2026-09-08]` The Input Visualizer's real dependency list

This section is the output of `docs/DAVIDS-TAS-STUDIO.md` §6b run for real:
the function was lifted into its own translation unit, compiled in-tree, and the
compiler's undefined symbols were taken as the answer. One build, no reading.

**The estimate was 372 lines, "the cheapest and cleanest".** The correction is
that it is 372 lines **plus nine shared helpers and three colour aliases**, and
the helpers are the actual substrate:

| symbol | lines | what it is |
|---|---|---|
| `tasStudioMode` | 4 | THE dual-mode switch - panel vs pinned overlay |
| `tasGroupSep` | 6 | separator |
| `tasInputCell` | 7 | draws one input cell - **shared with the piano roll** |
| `tasPreviewGhostFlags` | 7 | piano-roll preview state |
| `tasNotationEnsureLoaded` | 9 | notation tables, lazy |
| `tasSharedGameState` | 18 | the MvC2 state cache |
| `tasWindowUiZoom` | 19 | per-window zoom |
| `tasDriverCol` | 26 | READ/WRITE driver-model colour |
| `tasNotationFrame` | 63 | the notation switcher |
| `ScaledVec2` | — | UI scaling helper, lives in `core/rend/gui.cpp` |
| `TAS_NOTE_CE`, `TAS_P1_COL`, `TAS_P2_COL` | — | colour aliases missed by the first palette port |

They live at nine different line numbers spread across `dojo_gui.cpp`
(34, 174, 4338, 4453, 6696, 6751, 10076, 16915, 17107), which is the file-static
sprawl §S1 describes, seen from the inside.

**What this changes about the port order.** The Input Visualizer is not the
cheapest first panel; the nine helpers are, and they are shared - `tasInputCell`
alone is wanted by the piano roll too. They should land as one small shared unit
before any panel, exactly as the colour palette did. `core/dojo/tas_colors.h`
is the first third of that unit and is in.

**Two dependencies were already ours, byte-identical** (`tasmacro.{h,cpp}`,
`mvc2.{h,cpp}`, md5-verified), which is the survey's "the engine is already
ours" holding up under a real attempt.

The probe TU is kept out of the build deliberately: a file that does not compile
is worse than a file that does not exist, and the map above is what it was for.
