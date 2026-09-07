# CLAUDE.md — flycast-dojo-7 (TAS combo-video fork)

Guidance for Claude Code (and future me) working in this repo. It documents the TAS / combo-video
tooling built on top of flycast-dojo (dojo-7 base), how to build/run/test, and the hard-won
gotchas. Keep this file current when features change. (Last full refresh: 2026-08-22.)

## What this is

A fork of **flycast-dojo** (Sega Dreamcast / NAOMI emulator, dojo-7 base) turned into a
**PCSX2-rr–style TAS re-recording + combo-video capture tool** for **Marvel vs Capcom 2**.
Single-player only (netplay features are deliberately disabled/neutered); captures are clean (no
overlays). All TAS work lives on branch **`tas-tools`** in `core/dojo/`, `core/input/`,
`core/rend/`, `core/oslib/`, `core/nullDC.cpp`, `core/emulator.cpp`, `core/serialize.cpp`; the
rest of `core/` is upstream Flycast. The game ROM is `NoBGM_VMU.cdi` at the repo root —
**gitignored, never commit it** (~757 MB).

**The core loop (all working, all verified):** record a combo in Training with savestate retries
(true re-recording) → flip read-only → seek to the combo start → replay in perfect sync → dump a
full-res, 60fps-playable ProRes `.mov` — manually (F12) or fully headless (`.\test.ps1 -Capture`).

## Build & run (Windows, MSYS2 MINGW64)

```powershell
.\build.ps1               # configure on first run, then incremental -> build\flycast.exe
.\build.ps1 -EnableLog    # -DENABLE_LOG=ON (INFO_LOG/DEBUG_LOG compiled in; defines DEBUGFAST)
.\launch.ps1              # PREFERRED: interactive profile menu (see Launchers below)
.\run.ps1                 # raw launcher: boots MvC2 into Training + Bash log tail window
.\test.ps1                # automated replay regression tests (see Testing below)
```

- **Close the emulator before building** — a running `flycast.exe` locks the linker.
- Toolchain: MSYS2 **MINGW64** (gcc 16, cmake 4.4, ninja), Vulkan ON at build. Exe =
  `build\flycast.exe`. Incremental rebuilds are fast (ccache).
- **Renderer is DirectX 9** (`pvr.rend = 1` in emu.cfg) — DX11 renders MvC2 super backgrounds as
  pixel blotches. **AVI/MOV capture is wired for DX9 and DX11 only** (Vulkan/GL would record
  nothing). Internal res 6x (`rend.Resolution = 2880` → 3840×2880, "4K 4:3").
- VS Code: **F5** = build + open the TAS launcher menu (PowerShell extension; first F5 offers to
  install it). "Flycast: debug under gdb" is the second Run-and-Debug config. `.vscode/` is
  gitignored — config exists on disk only.

## Launchers & the logging system

`launch.ps1` = task-profile launcher (bare = numbered menu; or `.\launch.ps1 <name>`;
`.\launch.ps1 help` prints every profile AND a full config-flag reference):

| # | profile | what it does |
|---|---------|--------------|
| 1 | `combo`   | normal session: startup prompt, savestate-verify tripwire, quiet logs |
| 2 | `desync`  | desync hunt: VerifyState + SERMAP in the log |
| 3 | `avi`     | AVI/encode debugging: capture timing + ffmpeg lines, no SERMAP spam |
| 4 | `input`   | INFO-level INPUT/MAPLE traces, chatty channels muted |
| 5 | `vanilla` | plain boot into Training: no prompt, no verify |
| 6 | `tasmenu` | TAS-menu feature debugging (verify on, feature traces like TAS SCRUB) |

**Logging:** `build\flycast.log` is written in real time (never copy-paste logs — read the file).
On each launch the previous log is archived to `build\logs\<profile>_<timestamp>.log` (profile
from a `flycast.log.tag` marker). TAS traces use `NOTICE_LOG` (visible at the default
WARNING/NOTICE verbosity). The Bash tail window is **tied to the emulator's lifetime** (closes
itself when flycast exits; single-instance; tests skip it entirely via `run.ps1 -NoLogWindow`).

For the "something is firing and I cannot see what" class of bug (drifting stick, stuck D-pad on a
second pad, runaway scroll), run `.\launch.ps1 ghost` — input tracer on, pads out of menu nav.

Channel defaults are **baked into `LogManager::Init`**: the fork's own channels (INPUT, NETWORK,
SAVESTATE, COMMON, RENDERER, BOOT, AUDIO, GDROM, NAOMI) default on; the hardware-emulation
firehoses (DYNAREC, MEMORY, VMEM, PVR, AICA…) default off. An `emu.cfg` `[log]` entry still
overrides either way — which also means stale saved entries can shadow the defaults (a batch
written by the pre-fix broken checkboxes had to be purged once).

**Log semantics (this fork changed them):** `LogManager::IsEnabled` was rewritten so channel
toggles are a **real mute** — unticking a channel silences its Notice/Info/Debug lines. Errors and
warnings always print (you never mute a warning by accident). Verbosity now only decides whether
Info/Debug print on top of the notices — and those levels are compiled out of non-`-EnableLog`
builds entirely. Upstream's rule (everything ≤ WARNING always prints) meant no control ever touched
`NOTICE_LOG`, which is every TAS trace.

**cfg reads check the VIRTUAL section first** (`ConfigFile::get_entry`), and `cfgSave*` writes only
the regular section — so a `-config` launch flag permanently shadows every save a settings checkbox
makes, and the checkbox looks broken ("I can't turn it off"). Any UI toggle for a key that launch
profiles also set must write BOTH: `cfgSetVirtual(...)` to win now, `cfgSave*(...)` to persist. The
TAS logging panel does this.

Log channels, verbosity and the TAS traces are all toggleable at runtime from **Settings → TAS →
LOGGING** (channels go straight into `LogManager` so they apply to the next line; the TAS traces
are cfg flags their consumers read live, so those apply instantly).

**The emulator's own console is the default console** (`dojo:NativeConsole`, default yes): one
coloured window docked beside the game, dead with the process. `run.ps1` no longer pops the Git
Bash tail — `-LogWindow` opts back in (useful for scrollback that survives a crash on screen; the
log FILE always has it). Headless harnesses (`test.ps1`, `crashrepro.ps1`, `scrubstuck.ps1`) pass
`dojo:NativeConsole=no`.

The opt-in tail window pipes through `logcolor.awk`, so severity and the markers worth spotting
(TAS traces, hotkeys, `idempotent OK`, `GPF`/`BLOCKED`) are colour-coded. **The colour lives in the
terminal, never in the file** — `buildlycast.log` stays plain text, so grep, the VS Code reader
and every script keep working on it unchanged. Git Bash/mintty, Windows Terminal and modern
conhost all understand the escapes; no special terminal is needed and the emulator is not
involved, since it only ever writes the file.

**Debug-by-instrumentation is the house method:** every TAS feature logs its *measured* behavior
(`TAS SCRUB: N frames/s (target F)`, `AVI: N fps written (codec X ms, queue Y/5)`,
`STATE VERIFY: ...`, `TAS: savestate folder -> ...`, `TAS: recording rewound ...`). When something
misbehaves, the log names the layer. Extend this pattern for new features.

## Hotkeys (keyboard = P1; defaults in `core/input/keyboard_device.h`)

| Key | Action |
|-----|--------|
| **P** | Pause / unpause |
| **Space** | Frame advance — tap = exactly 1 frame; **hold = slow-motion scrub** at `HoldStepFPS` (TAS menu slider), after `HoldStepDelay` frames of debounce |
| **Tab** | Fast-forward |
| **Escape** | Menu (Settings → **TAS** tab lives here) |
| **F1** | Save state to current slot |
| **F3** | Load state — in READ-ONLY: seeks the movie there; in READ-WRITE: **rewinds the recording (re-record)** |
| **F2** / **Shift+F2** | Next / previous savestate slot (0–99, wrapping). **Holding it repeats** with acceleration (~6/s after a 0.4 s grace, ramping to 30/s) |
| — | *Previous savestate slot* is also its own bindable action, for pads (no Shift on a controller). Unbound on the keyboard by default since Shift+F2 covers it |
| **F4** | **States** window — the 100 slots as one thumbnail wall (a slider sets the card size; below 72 px they are badge-only tiles, the old board; a board-layout option puts ten per row), labels, sort/filter, a card row menu (load / save / lock / delete) and the clip's shared GENERATIONS pane under it. A dockable studio module |

The States window navigates with **arrows or the D-pad** (up/down move a row; the arrows set the slot as they move).
In the floating window Enter, A/cross or B/circle close it on the highlighted slot; the docked studio module (F5 up)
never closes on those keys or on a Load and takes the arrows only while it has the focus - Esc closes it when
focused. Either way the arrows only work while they cannot reach a movie being written: **paused** (the
emulator is stopped, so no frame advances) or **read-only playback** (the movie drives the guest).
While a recording is live and running it stays mouse-only, and it says so.
| **F9** | Show / hide the on-screen hotkey cheat sheet (or just **hold Shift** for a peek) |
| **R** | Toggle replay READ-ONLY (play) / READ-WRITE (record) — top-left HUD shows the mode |
| **F8** | **State backup**: copy the clip's replay + every state + sidecars + clip.json into the next `<clip>_gen_NN\`. Restore is pre-boot: Replays or Play Macro > the clip > GENERATIONS > “Replace live with this backup…” |
| **F12** | Start/stop capture (**Shift+F12** = re-pick the VfW codec, VfW backend only) |
| **1/2/3** | In the startup prompt: Record / Play a Movie / Just Play. In the replay browser: **1–9** play the Nth clip |

**Slot 0 (BASE) is write-protected.** Once it holds a state, a plain F1 tap is rejected — F1 must
be HELD for `BaseHoldMs` (default 1 s) to overwrite it. The HUD blinks green with a filling bar
while the hold arms, flashes red “TAP IGNORED — HOLD F1” on a rejected tap, and green when the
write lands. BASE is the state every seek returns to; losing it costs a whole session.

TAS hotkeys are gated on `gui_keyboard_captured()` (an ImGui text field is active), **not**
`gui_is_open()` — they must keep working while frame-advance-Paused, which counts as “open”, but
typing a tag must never fire them (R/P/Space are letters).

Hotkeys are listed and **rebindable** in Settings → TAS → HOTKEYS, per action and per device:
a **Keyboard** column and a **Gamepad** column (click a cell to rebind, right-click to clear).
A gamepad binding accepts a button **or an analog direction** — push the stick and that one
direction is bound, XPadder-style, so left-stick-left and left-stick-right can drive different
actions. **An action can have several inputs** (R1 *and* R2 both frame-advancing): a click ADDS an
input, right-click removes one. Keyboard bindings can be **chords** (Shift+F2, Ctrl+…, Alt+…):
the modifier flags live in the high bits of the button code (`InputMapping::KEY_MOD_*`), so a chord
is just another code in the same map and saves/loads/displays with no special case. Two rules keep
it safe — a chord is only used when it is *actually bound* (otherwise the raw key is sent, so
holding Shift during gameplay can never swallow an input), and a key releases with whatever code it
pressed with, so letting go of the modifier first cannot strand a button down. `chordtest.ps1`
drives all three properties against the real emulator. Upstream forbade this in `InputMapping::set_button/set_axis`, which
clear the action first — `add_button`/`add_axis` don't, and `load()` uses them so a second binding
survives a restart (the file always wrote one line per input; only the load collapsed them). `-` means unbound, which is a legitimate state: keeping rare actions (R, F12) off the
pad is intentional. The engine side already supported this (`gamepad_axis_input` edges an
`EMU_BUTTONS` key at a 50% threshold); only the UI was keyboard-and-buttons-only.

**Rebinding is what compartmentalizes input.** A device in detect mode consumes the press inside
`gamepad_btn_input`/`gamepad_axis_input` and returns before it reaches the emulator, so "press a
key" can never also step a frame or move the guest. The panel arms only ONE kind of device at a
time on purpose: while waiting for a pad input the keyboard stays live so Escape can cancel.

**Two identical controllers SHARE one mapping.** Both fall back to the same
`<api>_<name>.cfg` and `LoadMapping` caches by filename, so they end up on the same `InputMapping`
object — binding a hotkey on one binds it on the other. That is usually what you want (both pads
get the shortcut) and it is why "Copy to…" is hidden in that case; the panel says so instead, and
offers **"Give this pad its own bindings"** (`GamepadDevice::detachMapping`, which forks an
instance file keyed on the pad's unique id) for when you want them to differ.

**Multiple controllers, XPadder-style.** Every pad is its own `GamepadDevice` with its own
`InputMapping`, `name()`, `_unique_id` and `maple_port()`, and `handleButtonInput` does not gate
`EMU_BUTTONS` on the port — so **the same action can be bound on P1's pad AND P2's pad at once and
either fires it**. The panel has a device picker ("Gamepad column shows"), edits one pad at a time
so a rebind always lands on the pad you meant, marks an action `+N` when N other pads bind it, and
has **Copy to…** to mirror one pad's whole TAS set onto another.

The HOTKEYS panel is **generated**, not hand-maintained: rows come from `TAS_HOTKEYS[]`, key names
from the live `InputMapping`, and the "free function keys" line is computed by asking the mapping
which of F1–F12 are unbound. Only the explanatory paragraphs under the table are literal strings.
Hardcoded upstream (not remappable): **Alt+Enter / F11** = fullscreen, **LAlt+LCtrl** = mouse
capture. Game buttons: arrows = D-pad, X/C/S/D = A/B/X/Y, Enter = Start, Q = Coin, F/V = triggers.

## The combo workflow

1. Launch (`.\launch.ps1` → combo, or F5) → **TAS Combo Studio** prompt: **1** Record New Combo /
   **2** Play a Movie / **3** Just Play (+ Settings/Quit).
2. **Record** boots Training and auto-records a movie (`.flyr`) into a fresh **clip folder**
   `build\data\replays\<game>\<ISO-timestamp>\`. F1/F3 savestates land **in that folder** —
   movie + states travel together.
3. Build the combo PCSX2-rr style: frame-advance, save slots 1–99 as checkpoints, **F3 to retry a
   segment — this rewinds the movie with the machine and re-records in place** (the log/toast says
   `recording rewound to frame N (re-record)`).
4. **R** (read-only) → slot 0 → **F3** (seeks the movie to the combo start) → **P** → the combo
   replays in sync. **F12** before P to capture it.
5. Capture output: a single synced **.mov** (video+audio) — see Capture below.

**2P is native:** every movie frame stores BOTH ports' inputs (`MAX_PLAYERS = 2`, 28-byte frames),
so a second controller (e.g. P2 pushblock) records/replays automatically.

A state saved at or past the movie’s last frame dead-ends the instant you seek to it — playback
ends immediately. The seek says so with the numbers (`state is at/after the movie end
(3708 of 3605)`) instead of leaving you at a bare “End of Replay”.

**End of Replay is a hard stop.** No savestate HUD, no TAS hotkeys (`tasHotkeysBlocked()` in
`gamepad_device.cpp`), just **Restart replay** or **Close Game**. The movie is exhausted, so seeking
and slot cycling lead nowhere — and while they were nominally available they worked or silently did
nothing depending on whether that window happened to hold ImGui's keyboard capture at the time,
which is worse than not working. Restart re-boots the same ROM with the replay cfg still in place
(`Replay::Init` reads it live), so it plays from the top with savestates pointed back at the clip.

**Dead-timeline caveat (the one re-record foot-gun):** after rewinding and re-recording past frame
N, any savestate saved during the *abandoned* attempt at/after N no longer matches the movie —
re-save it before seeking to it. Re-saving slots as you go (the natural habit) avoids this.
A future guard (timeline counter in the `.frame` sidecar) is on the TODO list.

## Capture pipeline

**Default (validated by A/B test): ProRes LT, qscale 13, full internal res** — ~500 Mbps at
3840×2880/60, plays/scrubs at 60 fps, editor-native. The A/B/C/D methodology + results live in
`build/captures/ABC_TEST_NOTES.txt` (reference target: user's known-good clip at 617 Mbps).
CineForm cannot reach that band at full res (its floor ≈ 830 Mbps) — it remains selectable for
mastering.

Three backends (`dojo:CaptureEncoder`, TAS-menu "Capture encoder"):
- **`prores` (default)** / **`cfhd`** — ffmpeg-direct: the writer thread pipes raw frames
  (renderer-native BGRA, zero conversion) into a spawned `ffmpeg.exe` that encodes the final
  `.mov` **live**; the synced WAV is stream-copy muxed in at stop. **Needs no installed codecs —
  the bundled ffmpeg.exe next to flycast.exe IS the codec (the shareable setup).** ffmpeg stderr →
  `<output>.capture.log`.
- **`vfw`** — classic system-codec route (Lagarith etc., native Save-As + codec picker, remembered
  in `data/avi_codec.opts`); post-encodes to .mov via `dojo:PostEncode`. Enable the codec's own
  multithreading checkbox (Shift+F12 → Configure) for speed.

Mechanics: frames are read back from the renderer (DX9 `framebufferSurface` / DX11 `fbTex` — the
clean image, before OSD) and queued (bounded, 5 × ~44 MB) to a **writer thread**; when the encoder
falls behind the producer **blocks — frames are never dropped** (a capture contains every emulated
frame exactly once; same invariant as the movie itself). Capture speed ≈ **27 fps** at full res,
bounded by the DX9 `GetRenderTargetData` GPU-sync readback (encode is not the bottleneck).
Frame-advancing during capture writes no duplicate frames; audio is tapped per emulated sample.

**Headless auto-capture:** `dojo:AutoCapture=yes` = no dialogs; output auto-named
`build\captures\<replay-stem>.mov`; recording auto-starts after the `AutoSeekState` seek and
auto-stops+muxes at replay end. Driven by `test.ps1 -Capture`.

## TAS options menu (Settings → TAS)

All cfg-backed, most apply instantly (consumers read cfg live):
- **Frame advance:** Hold-Space scrub speed (1–60 fps), repeat delay, and ramp-up time sliders.
- **Savestates & movies:** verify-every-load tripwire; startup-prompt toggle; savestate
  thumbnails on/off + width.
- **Capture:** encoder picker (ProRes/CineForm/VfW), capture resolution (Internal default /
  2160p / 1440p / 1080p — ffmpeg scales in-process), ProRes profile+qscale, CineForm quality,
  keep-intermediates. This menu is expected to grow — pair every new option with a log trace and
  (if needed) a launch profile.

## Determinism & sync — the project northstar

**Never break record/replay sync.** The design that guarantees it:
- Determinism is keyed to **frame numbers, never wall clock**: inputs are recorded/applied per
  maple DMA per emulated frame (`session_inputs[frame_number]`, last-write-wins map — which is
  what makes in-place re-recording work).
- The **RTC is pinned** to a constant during record/replay (`aica_if.cpp GetRTC_now`), and the
  **SH4 clock is pinned to 200 MHz** under the same flags — wall time and overclocks cannot leak in.
- **Savestates are byte-verified**: `dojo:VerifyState=yes` re-serializes after every load and
  compares to the file (`STATE VERIFY: idempotent OK` / first-diff offset + `SERMAP` per-subsystem
  offsets to name the culprit). Two real desync bugs were found and fixed this way (SCIF timer
  reschedule and AICA envelope side-effects clobbering restored state on load).
- **Replay sessions are strictly read-only** (RecordMatches/Transmitting forced off) — no shadow
  recordings can steal the savestate folder.
- Movies record from **power-on (frame 0)**; savestate-seek is a bookmark into that timeline.
  Playing from frame 0 (browser, no F3) is the bulletproof/cross-machine mode.
- The slow-motion scrub and capture layers live entirely host-side (pacing sleeps, readback);
  they cannot perturb guest state.

## Automated testing (`test.ps1`, `crashrepro.ps1`)

```powershell
.\test.ps1                      # regression-test the 3 newest clips
.\test.ps1 -MaxClips 99         # the whole library
.\test.ps1 -Match 2026-08-22    # subset by folder name
.\test.ps1 -Capture             # ALSO headlessly dump each replay to build\captures\<clip>.mov
```
Per clip it boots the emulator headlessly into read-only playback (CLI flags; no GUI), auto-seeks
to state 0, and judges PASS/FAIL from log markers: correct `.flyr` loaded, savestate-folder
association, seek, `STATE VERIFY`, played-to-end, and (with `-Capture`) `.mov ready` + file size.
**Stop with Q/Esc**, or create `build\test.stop` (for external tools). Results also go to
`build\logs\test_results.json` (for the planned VS Code-extension UI). Safety: PID-scoped — it
refuses to run/kill anything if a user's flycast session is already running, and aborts if one
appears mid-test. NOTE: PASS verifies the *infrastructure*, not that hits connect — per-frame
input/state-hash verdicts (harness #2) are future work.

Claude can (and does) run the emulator headlessly for feature self-tests when the user isn't
using it: launch `run.ps1` with `-config` flags via `Start-Process`, poll `build\flycast.log`
for markers, kill by PID, screenshot the window if stuck (`System.Drawing CopyFromScreen`).

## Architecture of the TAS additions

- **`core/dojo/avi_dump.{h,cpp}`** — the whole capture stack: ffmpeg-direct + VfW backends,
  bounded-queue writer thread, WAV tap, post-encode/mux, auto-capture naming, codec persistence.
- **`core/dojo/replay.cpp`** — `Replay::Init` (playback init: live cfg read, savestate-folder
  override, read-only forcing, scratch wipe), `CreateReplayFile` (clip-folder creation).
- **`core/dojo/dojo.cpp`** — `MapleRecordAction`/`MapleApplyAction` (the input pipeline; playback
  guard, replay-end markers + auto-capture stop), `SaveStateFrame`/`LoadStateFrame` (the `.frame`
  sidecar: seek in read-only, **rewind/re-record in write mode**).
- **`core/dojo/dojo_gui.cpp`** — startup prompt (`gui_display_startup_prompt`, with the tag popup),
  rewritten local replay browser (`gui_display_replays`), TAS settings tab (`settings_tas_tab`,
  including the HOTKEYS panel), savestate HUD (`show_savestate_overlay`), clip rename.
- **`core/dojo/thumbnail.{h,cpp}`** — per-savestate PNG thumbnails: read back the last rendered
  frame, box-downscale, encode on a worker thread. Called from `gui_saveState` (render thread,
  emulator stopped) — NEVER from `dc_savestate`, which is also reachable from the emu thread.
- **`core/rend/mainui.cpp`** — hold-Space scrub (debounce + accumulator pacing + kick-off; the
  continuous-run pacing itself is in `gui_display_osd`'s stepping hook in `core/rend/gui.cpp`),
  `AutoSeekState` + auto-capture start, scrub diagnostics.
- **`core/rend/gui.cpp`** — `gui_open_step`/`gui_open_pause` (step state machine),
  `gui_loadState`/`gui_saveState` (work while paused), startup-prompt dispatch, `LastRomPath`.
- **`core/oslib/oslib.{h,cpp}`** — `savestateFolderOverride` (clip-folder redirection),
  `wipeScratchSavestates` (Just-Play states in `data\` are per-session scratch),
  `MAX_SAVESTATE_SLOTS` (compile-time storage ceiling) / `savestateCycleCount()` (runtime F2 wrap,
  `dojo:SlotCycleCount`) / `clampSavestateSlot` / `currentSavestateSlot`, and `scanSavestateInfo()` —
  ONE directory read yielding occupancy, size, mtime, movie frame and label for all 100 slots.
- **States window** — `gui_draw_slot_picker` in `core/rend/gui.cpp`: the 100 slots as one thumbnail wall
  (one scan, one label editor, a single-subject details column for the current slot, the shared GENERATIONS
  pane under it). Arrow-key nav walks the DISPLAYED order so it survives sorting/filtering.
- **`core/nullDC.cpp`** — savestate save/load + `verifyLoadedStateIdempotent` (the probe);
  `core/serialize.cpp` — SERMAP offset logging.
- **`core/rend/dx9/d3d_renderer.cpp` / `dx11/dx11_renderer.cpp`** — per-frame readback hooks
  (native-format row copy → `avi_dump.addVideoFrame`) and `GetLastFrameRGB` (thumbnails).
  GL/Vulkan return false there and simply get no thumbnails.
- **Scripts:** `build.ps1`, `run.ps1` (engine + log rotation), `launch.ps1` (profiles),
  `logwindow.ps1` (lifetime-tied tail), `test.ps1` (harness).

## TAS config reference (`dojo:` section; `-config dojo:Key=value` or the TAS menu)

| Key | Default | Meaning |
|-----|---------|---------|
| `StartupPrompt` | yes (emu.cfg) | Record/Play/Just-Play chooser at launch |
| `HoldStepFPS` / `HoldStepDelay` | 60 / 16 | scrub speed (fps) / frames held before auto-repeat |
| `HoldStepRampMs` | 1000 | ms to ramp a hold-Space scrub from 15 fps up to `HoldStepFPS` |
| `BaseHoldMs` | 1000 | how long F1 must be HELD to overwrite an existing slot 0 (BASE) |
| `StateThumbnails` / `ThumbnailWidth` | yes / 320 | write `<state>.png` per save; thumbnail width (height follows aspect) |
| `StatesOpen` / `StatesThumbW` / `StatesBoardCols` / `StatesPreview` / `StatesSnapshotsPaneH` | no / 132 / no / no / 260 | States window: open flag, card width (px), ten-per-row board layout, large hover preview, generations pane height |
| `SlotBrowserSort` / `SlotBrowserHideEmpty` | 0 / no | States wall sort mode, hide-empty |
| `SlotCycleCount` | 100 | how many slots **F2** walks before wrapping. A CYCLE limit only — all 100 slots keep existing and stay reachable from the F4 States window (slots past the cycle get a dim badge), so shrinking it can never lose a state |

Opening a clip (record **or** playback) resets the active slot to **0 / BASE** — via BOTH
`config::SavestateSlot.set(0)` and `cfgSetVirtual("config","Dreamcast.SavestateSlot","0")`, because
`Emulator::loadGame` re-runs `Settings::load(true)` mid-boot, which re-reads the Option from cfg
and stomped the plain `.set(0)` (the "replay opened on slot 1" regression). `config::SavestateSlot`
persists in emu.cfg across sessions and games, so a movie would otherwise inherit whatever slot was
last used — and F3 on an inherited slot can land on a state belonging to a different clip, or one
saved at/after this movie’s end (which dead-ends playback immediately).

`SlotCycleCount` does **not** limit what F8 backs up. A backup copies every state that exists in
the clip folder; empty slots simply have no files, so nothing is written for them and nothing
appears for them in `states[]`. Set the cycle to 0–12 and F8 still carries a state in slot 40.
| `ShowHotkeyOverlay` / `HotkeyPeekOnShift` | no / yes | cheat sheet pinned on / hold-Shift peek |
| `NativeConsole` / `ConsoleDock` | **yes** / right | an emulator-OWNED console (PCSX2-style) docked beside the game window, coloured with the same rules as `logcolor.awk`. Opened from `flycast_init` AFTER config load and docked after `os_CreateWindow` — it cannot be done lazily from the log path, where the first line predates both |
| `OverlayAlpha` | 45 | opacity of the box behind the overlay, in percent |
| `OverlayScale` | 100 | size of the top-left HUD and cheat sheet, in percent. Separate from the global UI scale so shrinking menus does not shrink the one overlay you need legible |
| `ConsoleCols` / `ConsoleRows` / `ConsoleFontPx` | 110 / 30 / 0 | emulator-console size in character cells and font height in px (0 = host default). Applied LIVE from the TAS menu (`os_SetNativeConsoleGeometry`/`Font`); `logwindow.ps1` reads cols/rows for the opt-in tail |
| `VerifyState` | **yes, always on** | byte-verify every savestate load. Deliberately NOT in the UI — never breaking sync is the northstar, so it is not something to leave off by accident. `=no` exists only as an escape hatch |
| `StateMapLog` | no | the SERMAP per-subsystem offset dump. Split off `VerifyState` because it is a firehose (every offset, on every save AND load) — the `desync` profile turns it on |
| `CaptureLog` | yes | write ffmpeg's output to `<capture>.mov.capture.log` beside the file |
| `InputTrace` | no (`tasmenu`/`ghost` profiles enable) | name every non-neutral input each frame — wheel accumulator, per-port kcode, stick positions, and what ImGui thinks is held. **Silent while everything is neutral**, so a quiet log is itself an answer; throttled to ~3/s |
| `MenuGamepadNav` | yes | pads drive ImGui menu navigation. Turn OFF when a drifting stick walks menus on its own — this fork binds analog directions to hotkeys, so a drifting stick both fires them and navigates |
| `MouseAsController` | no | upstream maps mouse left/right/middle to A/B/Start for lightgun games. Off here: a stray click on the game window would inject a face button into a recording |
| `CaptureEncoder` | **prores** | prores \| cfhd \| vfw |
| `ProResProfile` / `ProResQscale` | 1 (LT) / 13 | the validated recipe (~500 Mbps @ full res) |
| `CineFormQuality` | film1 | low…film3 (film3 ≈ 2 Gbps — mastering only) |
| `AviHeight` / `AviWidth` | 0 / 0 | 0 = internal render res; else output height/width |
| `PostEncode` | cfhd | VfW backend only: post-capture mux/encode (none\|cfhd\|prores) |
| `KeepAviWav` | 0 | keep intermediates after a successful mux |
| `AutoCapture` / `AutoSeekState` | no / -1 | headless capture / automated F3 to slot N |
| `RecordMatches` / `Replay` / `ReplayFilename` | — | movie record/playback (set by the UI, not by hand) |
| `LastRomPath` | — | written automatically; boot fallback for prompt/browser/replays |

Netplay leftovers deliberately OFF in emu.cfg: `Transmitting`, `AutoLoadNetState`,
`AutoLoadTrainingNetState` (each caused real bugs: shadow recordings, blocked headless boots).

## Gotchas (hard-won — read before debugging)

- **Config Options vs cfg store:** `config::X` Option objects CACHE the value loaded at startup;
  `cfgSetVirtual` writes only reach them at the next `Settings::load()`. For anything the UI sets
  just-in-time, read `cfgLoadStr/Int/Bool` (live). This bug shipped once (`ReplayFilename`).
- **Hotkeys only reach the focused game window** — clicking the log window swallows F-keys.
- **A guard must never swallow a key RELEASE.** `if (gui_keyboard_captured()) break;` at the top
  of a hotkey case also eats the keyup, which latches any hold flag forever. This stranded the
  frame-advance at End of Replay (that window takes keyboard focus, so the Space keyup was
  dropped and `step_held` stayed true, scrubbing a dead movie), and the same shape on `F1` would
  have left a BASE hold armed and overwritten it unattended. Handle `!pressed` FIRST, then guard.
  `Dojo::ReleaseTasHolds()` is the belt to that braces, called when a replay ends.
  `scrubstuck.ps1` is the regression test.
- **Never cache an `ImTextureID` across frames.** The render context is torn down and rebuilt on
  things as ordinary as toggling fast-forward (it changes the present interval), taking every GPU
  texture with it — a stored handle becomes a dangling COM pointer and the next `AddImage()` is an
  access violation. Ask `imguiDriver->getTexture(name)` each frame (the driver owns the map and
  rebuilds it) and only re-upload when it has forgotten the texture or the file changed.
  `crashrepro.ps1` drives this exact path end-to-end.
- **`keycodeToImGuiKey` (core/rend/gui.cpp) only maps a SUBSET of keys** to ImGui: arrows, Tab,
  Enter, Escape, Space, editing keys, clipboard letters, modifiers, digits, keypad and F-keys.
  Anything outside that switch is invisible to `ImGui::IsKeyPressed` no matter what the user
  presses — if an in-ImGui shortcut silently does nothing, check there FIRST.
- **Anything that must stay live while PAUSED cannot be clocked off `dojo.frame_number`** — the
  emulated frame counter stops when the emulator does, and pause is exactly when the TAS HUD,
  States window and hold-timers have to keep working. Use `os_GetSeconds()`. `dojo.savestate_epoch`
  is the companion: bumped on every savestate write so caches re-read immediately instead of
  waiting out a periodic tick.
- **Close the emulator before `.\build.ps1`** (linker lock) and before editing `build\emu.cfg`
  (flycast rewrites it on exit — but it does preserve unknown keys, so raw cfg keys persist fine).
- `core/deps/breakpad` shows as untracked build junk — never stage it; stage named files only.
- **The F: OSD counter is not a scrub/capture gauge** (it counts pause-frame renders); trust the
  `TAS SCRUB` / `AVI: fps written` traces.
- **Never use `%s` with wide strings in `swprintf` under MinGW.** With MinGW ANSI stdio, `%s` in a
  WIDE printf takes a NARROW string; a `wchar_t*` argument is read byte-wise and stops at the
  first NUL byte of the UTF-16 data — a path prefills as literal garbage like `C2.mov`. Use
  `%ls`, or better, build `std::wstring` by concatenation.
- PowerShell: a multi-line `Start-Process -ArgumentList` silently drops arguments — single-line
  it. PID-scoped process handling in all automation (never kill flycast by name).
- Replays are 60 fps movies keyed by frame number; the `.frame` sidecar next to each savestate is
  what makes seek/rewind work — don't separate states from their clip folder by hand.
- Sprite-seam gaps at high internal res are an upstream renderer tradeoff
  (`rend.FixUpscaleBleedingEdge`), not a capture bug.
- The gtest suite (`-DENABLE_CTEST=ON`) is bit-rotted into the app target (SDL_main collision) —
  not worth reviving; use `test.ps1` + in-emulator probes instead.

## ImGui docking (vendored v1.90.4-docking) — hard-won rules

The vendored ImGui was swapped to the docking branch (see `IMGUI_UPGRADE.md` for the full
patch inventory and every regression the swap surfaced). Rules that must not be broken:

- **Two ImGui frame streams exist**: `gui_display_ui` (menus/Paused; called when
  `gui_is_open()`) and `gui_display_osd` (gameplay; called by the RENDERERS from their
  present). Each does its own NewFrame/Render. `gui_display_osd`'s NewFrame is MIDWAY
  through the function — anything ImGui before it runs outside the frame and, with asserts
  compiled out in release builds, silently corrupts state instead of crashing.
- **The dockspace host (`tasDockspaceHost`) must be submitted inside the frame, before every
  dockable window, in the SAME stream as the windows** — right after the osd path's
  NewFrame, and at the start of the Paused case. Never in states that render no tools.
- `gui_keyboard_captured()` = `WantTextInput || (menus-open && WantCaptureKeyboard)`, where
  **Paused does not count as menus**: tool windows float over the game there and a focused
  one must not eat the TAS hotkeys (the "paused + F5 kills stepping" hang).
- **imgui.ini** (dock-layout persistence) lives next to the exe; every headless harness
  passes `-config dojo:UiIni=no` so it cannot overwrite the user's layout with a
  never-docked default. Any NEW harness that launches flycast.exe must pass it too.
- The game letterboxes into the dockspace central node (`gui_set/get_game_viewport`), wired
  into the GL and DX9 presents (DX11 not yet). `show_pause()` renders the whole overlay
  suite itself — do not call the show_* overlays again in the Paused case (doubles them).
- New keyboard hotkey defaults need a `migrate()` line in `core/sdl/sdl_keyboard.h` — saved
  cfgs never pick up new defaults on their own.

## Still TODO

- **Next phase: scripted/text input injection** — author inputs as text and inject them through the
  same `session_inputs[frame]` path recording and playback already use. MvC2-specific input grammar
  to design first. The dead-timeline guard (shipped, see above) is its prerequisite: scripted edits
  rewrite the timeline constantly, and stale states must be detectable.
- **Harness #2:** per-frame input/state-hash comparison for true gameplay-fidelity test verdicts.
- **Release packaging** for friends: zip of `buildlycast.exe` + `ffmpeg.exe` + MinGW runtime
  DLLs + `data\` skeleton (the code already prefers a bundled ffmpeg).
- Optional capture speed: GPU StretchRect downscale before readback for sub-4K captures (the
  remaining wall is the DX9 readback sync, ~27 fps at full res).
- **VS Code extension** (external, in progress on the user's side) consumes `clip.json` — schema 5,
  see `CLIP_SCHEMA.md`. New this phase for it: `states[]`, `generations[]`, `rewinds`, sidecar v2.
