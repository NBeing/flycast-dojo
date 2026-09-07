# ImGui docking swap — plan and regression contract

**Goal**: replace vendored ImGui 1.90.4 (master) with **v1.90.4-docking** (same version,
docking branch, API superset) so Piano Roll / input visualizer / overlays become drag-dockable
and tabbable, ahead of further TAS-UI growth. Started 2026-08-24; git tag `pre-imgui-docking`
marks the last commit before the swap — diff or roll back against it.

## What is vendored and what is patched

`core/deps/imgui/` — in-tree, NOT a submodule. Compiled files per CMakeLists: imgui.cpp,
imgui_demo.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp (+ per-renderer backends:
opengl3, vulkan, dx11, dx9). `imconfig.h` is OURS (config header — never overwrite from
upstream).

**Local patches found by diffing vendored files against a clean v1.90.4 checkout**
(inventory below filled in during the swap; every one must be re-applied to the docking
sources):

- `imgui.h`: `ImGuiWindowFlags_DragScrolling = 1 << 21` — predicted to collide with the
  docking branch's `NoDocking`, but that landed on bit 19; **bit 21 is free there too**, so
  the flag kept its value.
- `imgui_internal.h`: `bool DragScrolling;` member.
- `imgui.cpp`: `DragScrolling = false;` init.
- `imgui_widgets.cpp` (30 lines, found by full diff): the touch drag-scroll CORE —
  ButtonBehavior gains a drag-detector that finds an ancestor `DragScrolling` window and
  flags it (releasing the button), and Selectable is suppressed while its window drag-scrolls.
- `imgui_internal.h` also carries `ImVec2 ScrollSpeed;` (inertial scroll state).
- Backends were NOT swapped: flycast's are heavily customized (853 diff lines in opengl3) and
  version-tolerant against the docking core with viewports off. Kept as-is; all compile.
- All patch sites are marked `[FLYCAST PATCH]` in the new files.
- **The ButtonBehavior patch was corrected during the swap** (deliberate divergence from the
  pre-swap version): it used to `ClearActiveID()` on EVERY held drag past 5px before checking
  for a DragScrolling ancestor — which killed dock-splitter drags after 5px (splitters are
  ButtonBehavior without the scrollbar exemption). ClearActiveID now fires only when a
  DragScrolling window is actually the target; flagged-window behavior is unchanged.

## What the swap actually broke (both fixed in the swap commit)

1. **Real docking regression**: docking-branch ImGui sets `io.WantCaptureKeyboard` whenever
   ANY window holds nav focus — including gameplay OSD overlays. The moment the Shift-peek
   cheat sheet appeared, every TAS hotkey died (guardtest: keys stopped at the first
   Shift+F2 and never recovered). Fix: `gui_keyboard_captured()` now means "the user is
   typing (`WantTextInput`) or the real menus are open and focused" — never a bare overlay.
2. **Latent, unrelated, caught by finally re-running guardtest**: a Port=All keyboard never
   updated `kb_shift[]` (the write is guarded to ports 0–3), killing every Shift+hotkey
   chord since the keyboard was flipped to All for P1+P2 play. Fix: port-All parks modifier
   state in slot 0 (consumers OR across all slots).
3. **Dockspace submitted OUTSIDE the ImGui frame** (found via the user's 3-step repro:
   running=no edge docking, paused=yes, one step=layout breaks): `gui_display_osd` calls
   `ImGui::NewFrame()` midway through, and the "host first" placement put the host at the
   function top — before NewFrame. Release builds compile ImGui asserts out, so it silently
   corrupted docking state instead of crashing. The host lives immediately after each path's
   NewFrame now. **Rule: the host must sit INSIDE the frame, before every dockable window,
   in the same ImGui frame stream as the windows.**
4. **Paused counted as menus-open in `gui_keyboard_captured()`**: a focused tool window
   while paused (clicking the roll's Step button; F5 re-showing the UI restores focus)
   raised WantCaptureKeyboard and every TAS hotkey blocked — the "paused + F5 breaks
   stepping, hiding the UI fixes it" hang. Paused is gameplay: only real menus (and
   WantTextInput anywhere) capture.
5. **The harnesses clobbered imgui.ini**: every headless run saved its never-docked default
   layout over the user's. `dojo:UiIni=no` (all harnesses pass it) keeps IniFilename NULL
   for those runs.

## Post-swap wall (2026-08-24): test.ps1 PASS · tastext PASS · textguard PASS · guardtest PASS

The drag-scroll *behavior* lives in flycast code (`core/rend/gui_util.cpp`:
`windowDragScroll()` / `scrollWhenDraggingOnVoid()`), which reads the flag/member — only the
declarations live in the vendored tree.

## Integration points to re-verify by hand

- `gui_init` (`core/rend/gui.cpp` ~L140): nav flags. Add `ImGuiConfigFlags_DockingEnable`.
  Viewports stay OFF (single OS window).
- `io.IniFilename = NULL` — with no ini, **dock layouts will not persist across runs**.
  Follow-up decision: point IniFilename at a file next to emu.cfg so layouts stick.
- The TEST-mode per-frame nav reconcile (`gui_display_settings`) must keep working — it
  rewrites `io.ConfigFlags` and must not clear `DockingEnable` when it restores nav bits.
- Backends: only the compiled-in ones matter (OpenGL3 + DX11 in our build; Vulkan is OFF).

## Regression contract (run all after the swap, and again after any docking follow-up)

Automated wall — all must PASS:

```
.\test.ps1          # replay sync + fidelity (magnetoNew)
.\tastext.ps1       # text codec round-trip
.\textguard.ps1     # ApplyEdit funnel end-to-end (guard verdicts + .flyr append + reload)
.\guardtest.ps1     # scripted rewind guard (keybd_event rig - exercises real input + UI states)
```

Hands-on checklist (the user drives; known-risk areas called out):

- Pause menu (5-button core), Settings tabs (TAS / General / Video / Audio / Advanced / About)
- Settings > TAS: CONTROLLERS section (device rows, identify flash + listen line, Ping-less),
  mapping window: both panes, P1/P2 radios on Port=All keyboard, Unmap All, conflict flags,
  TEST mode (keyboard lights rows, pads locked out of nav, auto-break on Map)
- Piano roll (Settings > TAS checkbox / the Windows menu - no default key): grid, heat map click-jump, PLAY lock behind R, cell edit in read-write
- Savestate HUD + States window (F4: the wall, badge-only tiles below 72 px, Board layout (10 per row), the card row menu, the SNAPSHOTS pane and its splitter; docked: Load / Enter keep it open, arrows only while focused, the boot handoff fronts the tab), studio show / hide (F5), input visualizer (Settings > TAS), hotkey sheet (F9)
- Replay browser, startup prompt (Record / Play / Just Play), tag dialog Enter/Esc
- NEW: drag one window onto another → they dock/tab; drag out → they split

## Rollback

`git checkout pre-imgui-docking -- core/deps/imgui` plus reverting the gui_init flag line.
