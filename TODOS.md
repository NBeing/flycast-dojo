# TODOs

Working list for the `video-recording` branch and follow-on work.
Grouped by what blocks what, not by area.

Legend: **[V]** needs verification · **[B]** known bug · **[F]** new feature ·
**[?]** needs a decision

---

## Blocking — do before this branch is merged or shared widely

*Closed 2026-08-09: Lua overlay capture and real-game audio are both verified
end to end against Marvel vs. Capcom 2 booted through REIOS. See
DAVID_FEATURES.md for the evidence.*

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

### [x] Lua `vblank` double-fires during rollback — FIXED
`Event::VBlank` is now gated on `!ggpo::rollbacking()` in `core/lua/lua.cpp`,
so a re-simulated frame no longer re-runs a script's `vblank` callback.
`flycast.state.isRollback()` is exposed for scripts that deliberately want to
observe re-simulation.

**Scope correction.** This was filed as also hitting local dojo replay
playback, "so it is not netplay-only". That is wrong. `ggpo::active()` returns
true for `dojo.PlayMatch && replay_version >= 2`, but `inRollback` is written
in exactly two places (`ggpo.cpp:261,268`), both inside `advance_frame`, which
is a GGPO *session* callback. Replay playback reuses the ggpo input path with
`ggpoSession == nullptr`, so GGPO never calls it and no rollback occurs. The
bug was real but **netplay-only**, and needed a connection bad enough to
mispredict.

### [x] `dojo.FrameNumber` drift under rollback — CONFIRMED and addressed
The mechanism checks out: `endOfFrame()` increments `dojo.FrameNumber`
(`ggpo.cpp`) and is reached from `rend_start_render()`
(`core/hw/pvr/Renderer_if.cpp:402`), which is *not* gated by
`rend_enable_renderer(false)` — so re-simulated frames increment it — and
`load_game_state` never restores it.

`flycast.state.getConfirmedFrameNumber()` reports a number on the same scale
that does not drift, by counting how many increments happened during rollback
and subtracting. Offline nothing is re-simulated, so the two are equal
(verified: 117/117, 237/237, 357/357). `getFrameNumber()` is left alone rather
than changed under existing scripts.

### [ ] Audit of the other `Event::VBlank` listeners — done, no action taken
- `core/cheats.cpp:363` → `CheatManager::apply()`. Re-applies cheat values to
  memory during rollback. Idempotent (same values, and the writes are captured
  and rolled back like any other), so wasteful rather than wrong. Gating it
  would change when cheats land; left alone deliberately.
- `core/network/output.h:55` → `acceptConnections()`. Network IO, accumulates
  nothing. Doing it on re-simulated frames is wasted work during a rollback
  burst, which is the worst moment for it, but it is not a correctness bug.
- `core/hw/naomi/naomi_m3comm.cpp` → `NaomiM3Comm::vblank()`. Blocks up to
  100 ms waiting on comm-board data. Only for NAOMI multiboard setups, which
  are not a GGPO netplay configuration, so it cannot overlap in practice.

### [x] `EventManager` has no locking — FIXED
Two races, not one, and they needed different fixes.

**The container race.** `broadcastEvent` iterated its listener vector while
`registerEvent`/`unregisterEvent` mutated it. `EventManager` now holds a mutex,
and `broadcastEvent` **copies the listener list under the lock and dispatches
outside it**. Both halves are load-bearing: copying survives a listener that
unregisters itself from inside its own callback, and releasing before the call
is what keeps the two locks from being taken in both orders — `lua::term()`
holds `lua::mutex` and wants this one, while a broadcast wants `lua::mutex`.
Dispatching under the lock would also let `NaomiM3Comm::vblank`, which blocks up
to 100 ms, stall registration on another thread.

**The lifetime race.** `lua::init/term/reinit` assigned and closed `L` without
holding `lua::mutex`, and `emuEventCallback` read `L` *before* taking it — an
unlocked null check only proves `L` was non-null at some point in the past.
All three lifecycle functions now take the lock, and the null check moved
inside it.

Reachable, not theoretical: `core/rend/gui.cpp:1466` calls `lua::term()` from
the main thread when Training Lua is toggled off, while `Event::VBlank`
dispatches on the emulation thread.

`[MEASURED 2026-09-04]` 108/108 conformance checks pass against the locked
build with a clean shutdown — that exercises the changed dispatch path at 60 Hz
for the length of a session, so it is a regression gate on the fix.

`[REASONED]` **The race itself has not been demonstrated failing.** Both call
sites that tear Lua down mid-session are training-mode-gated and reachable only
through the GUI, so the concurrent toggle was not driven. What would settle it:
a ThreadSanitizer build, or hammering the Settings toggle with the emulator
running. Recorded here rather than counted as verified.

### [x] `lua::reinit()` built a weaker interpreter than `lua::init()` — FIXED
Found while fixing the above. The two functions each constructed their own
`lua_State`, and `reinit`'s copy was missing `package.path`, `SCRIPT_DIR`, the
console-aware `print`, and the console session reset — so a script loaded from
the Settings toggle silently got a different environment from the same script
loaded at startup, **including the working-directory bug `init` had just been
fixed for**. Both now call one `openState()`, so there is no second copy to
drift.

---

## Capture — follow-on work

### [V] Induce encoder backlog and confirm frame slots hold
1:1 frame correspondence is confirmed in normal operation (a per-frame Lua
counter reads N-1 in video frame N at 600/1200/1400/1900, constant offset),
and A/V drift measured 0.000 s over 41 s. What has *not* been exercised is the
repeat-on-backlog path itself, because backlog never occurred. Force it - slow
codec, or a temporarily tiny queue bound - and confirm the frame count still
equals the number of presents.

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
- **Counterfactual rollout / speculative stepping.** *Correction: this was
  filed as "blocked on 10–20 MB monolithic savestates". That is wrong — the
  delta snapshots it was waiting on already exist.*

  A rollback snapshot does **not** contain bulk memory. `Serializer` carries a
  `rollback` flag and every large region is skipped when it is set: AICA RAM
  and main RAM (`core/serialize.cpp:176,216`), VRAM (`core/hw/pvr/pvr.cpp:80`)
  and elan RAM (`core/hw/pvr/elan.cpp:1785`). Those are restored instead from
  page-granularity deltas: `memwatch` write-protects the regions, the fault
  handler (`core/linux/common.cpp:48`) copies each page's *pre-write* contents
  before letting the write through, and `load_game_state` walks those maps
  backwards from the newest frame to the target, undoing writes.

  The 10–20 MB in `save_game_state` is a worst-case *allocation*, not a copy;
  `*len = ser.size()` is what is actually used. Measured on this machine, the
  allocator costs 0.002–0.010 ms/frame (under 0.06% of a 60fps budget), so it
  is a footprint wart, not a speed problem.

  What actually dominates a rollback snapshot is the TA display list:
  `serializeContext` (`core/hw/pvr/ta_ctx.cpp`) writes
  `tad.thd_data - tad.thd_root` per context, bounded by `TA_DATA_SIZE` = 8 MB.

  So speculative stepping is more tractable than this entry claimed. Reassess
  against the TA payload, not against RAM size.

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
