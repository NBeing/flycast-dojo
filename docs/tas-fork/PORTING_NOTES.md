# The TAS fork's docs and scripts, on `dojo7` — read this first

Everything in this directory and in `scripts/tas-fork/` came from the TAS fork
(`~/dev/davids_fly`) unchanged. It is **reference material, not documentation of
this branch.**

The engine came across. The interface did not. So roughly half of what these
files describe is true here and half describes a program that does not exist on
this branch — and the two halves are not separated in the originals, because
there they were one working system.

Nothing here has been edited to match. Editing someone else's design notes into
half-truths is worse than leaving them intact and saying which half applies.

---

## What IS on this branch

| | where |
|---|---|
| Re-record engine — `session_inputs`, `ApplyEdit`, `.frame` sidecars, rewind timeline | `core/dojo/dojo.cpp` |
| Clip folders, `clip.json`, generations | `core/dojo/tas_clip.cpp`, `core/dojo/replay.cpp` |
| All eight `tas_*` modules | `core/dojo/tas_*.cpp`, `mvc2.cpp` |
| Savestate folder redirection, slot scanning | `core/oslib/oslib.cpp` |
| Determinism: RTC pin, SH4 clock pin, verify probe, sync manifest | `core/determinism.cpp`, `DETERMINISM.md` |

So `CLIP_SCHEMA.md`, the `CANON_*` design docs, `ROADMAP.md`'s T1–T6 and
`WRITE_MODE_RESTRUCTURE.md` describe machinery that is genuinely here.

## What is NOT

**The entire UI layer.** `dojo_gui.cpp` on this branch is dojo-7's own **2,544
lines**; the TAS fork's is **21,877**. Absent, therefore:

- the piano roll, the States window (F4), the input visualizer, the notepad,
  the Input Sender, the TAS settings tab, the savestate HUD, the hotkey
  cheat sheet, the startup "TAS Combo Studio" prompt
- `show_savestate_overlay`, `gui_draw_slot_picker` — **absent**
- hold-Space scrub (`HoldStepFPS`), `AutoSeekState` — **absent**

**Consequence for the hotkey table** at the top of `CLAUDE.tas-fork.md`: almost
none of it applies. Concretely, **the menu key here is Tab, not Escape** —
`core/input/keyboard_device.h:57`, `set_button(EMU_BTN_MENU, 43)`. That single
difference cost a long debugging detour; do not trust the table.

`ReleaseTasHolds()` survives in `dojo.cpp` and is called, but it releases hold
flags for hotkeys that do not exist here. It is an inert belt for absent braces.

---

## The architectural collisions

These are decisions still to make, not bugs to fix.

### 1. Two capture stacks, both live

| | origin | hooks | backends |
|---|---|---|---|
| `core/dojo/avi_dump.cpp` | TAS fork | `dojo.cpp:1953,1983` — stop+mux at movie end | DX9, DX11 |
| `core/rend/video_recorder.cpp` | video-recording | all 4 renderers, audio, gui, Lua | GL, Vulkan, DX11, DX9 |

They are independent and neither knows about the other. **On Windows both would
be live at once**: a replay's headless auto-capture would drive `avi_dump` while
the toolbar button and `flycast.video.*` drive `videorec`. On Linux `avi_dump`
is inert (no DX), which is the only reason this has not bitten yet.

They are also different products — `avi_dump` captures the clean pre-OSD image
for editing; `video_recorder` captures what the user sees, overlays included.
Both are wanted. The decision is whether they become two *modes* of one recorder
or stay two stacks with one clearly owning the headless path.

### 2. Two frame counters

`dojo.frame_number` (advances on the netplay session's schedule; stalls and
jumps offline) and the delivered-frame counter behind `ggpo::confirmedFrame()`
that the Lua surface uses. `tas_wave` and `tas_ruler` key off the former.

### 3. Determinism is half-converted

`determinism::isDeterministicRun()` exists and two guards use it
(`EmulateFramebuffer`, the SH4 clock pin). Roughly twenty other
`if (config::GGPOEnable)` sites still carry their own inline conditions. See
`DETERMINISM.md`'s audit table.

### 4. Config keys the scripts pass that do not exist here

Measured against `core/`:

| key | status |
|---|---|
| `dojo:AutoSeekState` | **ABSENT** — `test.ps1 -Capture` cannot seek |
| `dojo:HoldStepFPS`, `dojo:StatesOpen` | **ABSENT** |
| `dojo:AutoCapture`, `dojo:CaptureEncoder`, `dojo:VerifyState`, `dojo:SlotCycleCount` | present as live `cfgLoad*` reads (no `Option` object), which is how the TAS fork does it |

---

## The scripts specifically

`scripts/tas-fork/` is **PowerShell for Windows/MSYS2** and expects
`build\flycast.exe`. This branch builds `build/flycast` and has been exercised
on Linux only. None of them has been run here.

Beyond the platform, they judge PASS/FAIL from **log markers the UI emits**:

- `test.ps1` — needs `AutoSeekState`. **Will not work** as written.
- `guardtest.ps1`, `scrubstuck.ps1`, `chordtest.ps1`, `resizeguard.ps1`,
  `f5repro.ps1`, `crashrepro.ps1` — drive TAS hotkeys and GUI panels via
  `keybd_event`. **All need the UI layer.**
- `textguard.ps1`, `tastext.ps1` — exercise the text codec and `ApplyEdit`,
  which ARE here. **Closest to working**, and the first two worth reviving.
- `build.ps1`, `run.ps1`, `launch.ps1`, `logwindow.ps1`, `logwatch.ps1`,
  `logcolor.awk` — plumbing. `logcolor.awk` is platform-neutral and works now.

For automated testing on this branch use `scripts/isotest.sh`, which is Linux,
runs offscreen, and does not touch the real desktop. The TAS fork's harnesses
are the model to port *toward*, once there is a UI for them to drive.

## If you reimplement the UI rather than porting it

Then `CANON_driver_model.md`, `CANON_readwrite_model.md`, `CANON_macro_mode.md`
and `CANON_onenter.md` are the most valuable files here — they are the *design
arguments*, which survive a reimplementation, whereas `CLAUDE.tas-fork.md`'s
hotkey and menu tables describe an implementation that would not.
