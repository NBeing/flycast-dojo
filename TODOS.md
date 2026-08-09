# TODOs

Working list for the `video-recording` branch and follow-on work.
Grouped by what blocks what, not by area.

Legend: **[V]** needs verification · **[B]** known bug · **[F]** new feature ·
**[?]** needs a decision

---

## Blocking — do before this branch is merged or shared widely

### [V] Confirm Lua overlay content lands in a recording
The one unproven claim in the whole feature. `lua::overlay()` only fires
in-game, and no ROM was available during development, so capture was proven
for ImGui content on the identical draw path into the identical buffer — but
never with an actual Lua overlay.

Test: load any game with this in `~/.config/flycast-dojo/flycast.lua`, record
~10s, scrub the AVI for the text.

```lua
callbacks = {}
callbacks.overlay = function()
    flycast.gui.text("LUA OVERLAY TEST " .. os.time())
end
```

If this fails, the whole premise needs revisiting. Everything else is detail.

### [V] Run DX11 and DX9 capture on real Windows
Both are type-checked against the real `d3d9.h`/`d3d11.h` via mingw
cross-compile, and confirmed to be inside the compiled region — but never
executed. DX11 is the Windows default renderer, so it matters most.

Watch for: wrong channel order (red/blue swapped), row-pitch handling on
non-multiple-of-4 widths, and behaviour across a device reset (DX9) or
swapchain resize (DX11).

### [V] Run `setup-and-build.ps1` on a clean Windows machine
Never executed — no Windows host or PowerShell interpreter was available. The
MSYS2 silent-install flags are the documented ones but unverified. The bash
half is syntax-checked and its guards exercised.

---

## Known bugs

### [B] Lua `vblank` callback double-fires during rollback
**Real bug, live today, not hypothetical.** `ggpo::rollbacking()` exists at
`core/network/ggpo.h:39-43` but has essentially no callers, and nothing on the
`Event::VBlank` dispatch path consults it. So Lua's `vblank` callback runs
again for every re-simulated frame, double-counting anything it accumulates.

`overlay` is naturally safe — rollback frames are dropped before render — but
`vblank` is not. This hits during **local dojo replay playback**, where Lua is
active, so it is not netplay-only.

Fix: gate the `VBlank` dispatch in `core/lua/lua.cpp` on
`!ggpo::rollbacking()`, and expose `flycast.state.isRollback()`.

Also audit the other `Event::VBlank` listeners for the same hazard:
`core/cheats.cpp`, `core/network/output.h`, `core/hw/naomi/naomi_m3comm.cpp`.

*(Reported by a codebase survey; the mechanism is well-evidenced, but confirm
by observation before building on it.)*

### [V][B] Verify whether `dojo.FrameNumber` drifts under rollback
`state.getFrameNumber()` returns `dojo.FrameNumber`, incremented in
`ggpo::endOfFrame()` which is reached during rollback re-execution, and it is
not among the fields restored by `load_game_state`. That would make it drift
upward across a rollback burst — the same defect fbneo-rr documents for its
own frame counter.

**Reasoned from source, not observed.** Measure it before acting. If
confirmed, add a rollback-stable `getConfirmedFrameNumber()` and decide
whether to fix `getFrameNumber()` in place (a behaviour change for existing
scripts) or add a sibling.

### [B] `EventManager` has no locking
`broadcastEvent` iterates its listener vector while `registerEvent` /
`unregisterEvent` mutate it, and `lua::init/term/reinit` assign and close `L`
without holding `lua::mutex`. A `VBlank` dispatch on the emulation thread
racing `lua::term()` on the main thread is undefined behaviour today.

Pre-existing, unrelated to capture. Worth reporting upstream separately.

---

## Capture — follow-on work

### [V] Confirm audio with a real game
Audio is implemented: teed off the AICA mixer in `WriteSample()`, written as
raw PCM alongside the video, muxed at stop. Verified so far: the no-audio
fallback, temp cleanup, and the exact mux command against a synthetic
44100 Hz stereo PCM.

**Not verified with real game audio** — no AICA samples are produced at the
menu, so this needs a ROM. Check: audio present, in sync at the start, still
in sync after 5+ minutes, and no clicks where silence padding was inserted.

### [V] Induce encoder backlog and confirm frame slots hold
Output is constant frame rate by construction: a dropped frame keeps its slot
via a repeat, so frame N of the file is emulated frame N. The logic is in
place but backlog was never actually induced during testing. Force it (slow
codec, or a tiny queue bound) and confirm the frame count still equals the
number of presents, and that audio stays aligned.

### [F] Make DX9 capture asynchronous, or accept it
D3D9 has no async readback: `GetRenderTargetData` is synchronous and
`LockRect` blocks. Currently costs a stall per frame while recording.
Mitigation would be a second surface and a frame of latency, which D3D9 does
not cleanly support. May be simplest to document and steer users to DX11.

### [F] Survive window resize instead of stopping
Currently a resize stops the capture cleanly rather than corrupting it.
Better would be to restart the encoder at the new size, or letterbox into the
original frame size.

### [F] Hotkey binding for record toggle
Toolbar, Settings and Lua all work, but there is no keyboard shortcut. Most
useful mid-match, when the UI is closed.

---

## Observability — from the fbneo-rr survey

Assessment of porting `~/dev/anita/fbneo-rr`'s "observable architecture".
Note: **that exact phrase appears nowhere in that repo** — it was interpreted
as `src/burner/spec/` (the speculation / observer-tier engine, ~1,100 lines)
at roughly 85% confidence. Confirm that is what was meant before investing.

### Licensing — settle this first
- **flycast-dojo is GPL-2.0-or-later**, and is a fork of `blueminder/flycast-dojo`, so it cannot be relicensed.
- **FBNeo's licence is not GPL-compatible.** It imposes field-of-use
  restrictions ("may not sell… may not ask for donations"), and GPLv2 §6
  forbids adding restrictions. **FBNeo-derived code cannot be combined into
  flycast-dojo and distributed.**
- **`src/burner/spec/` is 100% your own commits**, so you may relicense that
  yourself. Confirm none of it was transcribed from existing FBNeo code.
- **Do not copy `lua_memory.cpp`** — its `TieredRegion` is upstream
  FCEUX/FBA-lineage code, not yours.
- `phobos` has no LICENSE file; add one before shipping `reactive.lua` into a
  GPL project.

**Port the ideas and your own `spec/` code. Reimplement everything else from
the design, never from FBNeo source.**

### Phase 1 — stop the double-fire (~1–2 days)
The rollback gate above, plus `isRollback()` and a confirmed-frame counter.
Best value/effort on the list: it closes a real bug and the primitive already
exists. Ship it with a test that fails first.

### Phase 2 — Lua capability tiers (~1 week)
Replace the blunt "Lua off entirely in netplay" switch
(`core/nullDC.cpp`, `core/rend/gui.cpp`) with OFF / OBSERVER / MUTATOR / FULL
tiers, a per-binding requirement check, refusal counters, and
`flycast.state.capability()` / `can()`.

**Biggest user-visible win available** — a training-tool fork whose scripts do
not work in netplay is exactly the problem those tiers were designed for. It
would also let recorded netplay matches show Lua overlays.

### Phase 3 — frame-safe observation primitives (~1–2 weeks)
A reactive Signal/Stream layer driven from a confirmed-frame-gated `vblank`,
with emu-thread observers buffering and `overlay` draining on the render
thread. Observers must never touch ImGui directly.

### Phase 4 — page-diff memory watch (~2–3 weeks, only if needed)
Build change-notification on the existing `core/hw/mem/mem_watch.{h,cpp}`
dirty-page tracking, evaluated once per confirmed frame. Page granularity, no
PC context — document the gap rather than calling it an fbneo-equivalent hook.

### Explicitly skipped
- **A single frame-step choke point.** flycast has no run-ahead, so
  `ggpo::rollbacking()` already covers every re-simulation. High cost, marginal
  benefit.
- **Exec hooks / `cpu.setbreak`.** flycast's only breakpoint mechanism patches
  emulated memory and disables the dynarec — desync risk under rollback, and
  watchpoints are unimplemented stubs.
- **Per-access memory read/write hooks.** The SH4 dynarec inlines memory
  access into generated code; hooking every access means dynarec surgery or
  forcing the interpreter.
- **Counterfactual rollout / speculative stepping.** Blocked on 10–20 MB
  monolithic savestates. Revisit only if Phase 4 yields delta snapshots.

---

## Housekeeping

- [?] Decide whether to open a PR against `blueminder/flycast-dojo`. Hold
  until DX9/DX11 have run on real Windows.
- [ ] `core/deps/breakpad` was found with its entire working tree deleted
  (all files staged as deletions) and was restored with
  `git -C core/deps/breakpad reset --hard HEAD`. If that deletion was
  deliberate, say so — it is mandatory on Linux and Windows, and configure
  fails without it.
- [ ] Consider adding `mingw-w64-x86_64-ffmpeg` to the documented Windows
  package list, since capture needs it at runtime.
- [?] The mux runs at stop, so stopping a long capture is not instant (stream
  copy, not re-encode). If that becomes annoying, it could move to a
  background thread with a "finalising" indicator.
