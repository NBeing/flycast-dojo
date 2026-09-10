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

## The game panel — landed on GL, unfinished elsewhere

`[2026-09-08]` The picture is now a dockable ImGui panel rather than a
full-window blit, so it splits, tabs and resizes like any other node.
**On by default** `[2026-09-08]`, once both halves below were understood: the
fallback makes the non-GL backends bit-identical to before, so defaulting it
changes only the renderer it was tested on. `dojo:GamePanel=no` reverts it. `scripts/docktest.sh` is the regression test — it drives a real
drag with xdotool on a private Xvfb and `--self-test` proves it can fail.

### [F] DX9, DX11 and Vulkan publish no frame texture
They fall back to the blit, so they behave exactly as before and nothing is
broken — but the panel is GL-only until this lands, and **DX9 is the Windows
capture path**, so this is what stands between the feature and the actual
workflow. It is also why turning the default on was safe: the flag is on
everywhere, the behaviour only changes where it has been tested.

Both DX backends already hold the frame as a texture (`framebufferTexture` at
`dx9/d3d_renderer.h:154`, `fbTextureView` at `dx11/dx11_renderer.h:114`), so
`Renderer::GetFrameTexture()` is a few lines each. The hook alone is NOT enough:
their present paths need the "do not blit" half too, or the picture draws twice
— once full-window underneath and once in the panel. Cannot be compiled or
tested from Linux; do it on the Windows box.

### [V] The menu-open case is fixed but not covered
Settings, Commands, Cheats, ReplayEnd and QuickMap now submit the dockspace host
and the panel, so the picture no longer jumps out of its dock when a menu opens.
Correct on inspection; **not proven by automation**. docktest's phase 2 SKIPS
with its reason printed because Escape does not reach the emulator under the
harness even with i3 running on the scratch display. Three earlier versions of
that phase passed with the fix deliberately removed, for three different
reasons — see the commit. Finish it or leave it skipping loudly; do not let it
quietly start "passing".

---

## [F] fbneo has never been run either

`[2026-09-08]` emuapi's `adapters/fbneo.lua` is now the only host in that
package whose claims are entirely unmeasured — its 21 `ui.*` declarations and
its whole unsupported table are read out of prose, and 16 of those names already
exist in fbneo's own C++ (`lua_imgui.cpp`). Its value widgets also return
`(changed, value)` against the spec's `(value, changed)`: reversed, so a script
unpacking two values gets a silently wrong pair rather than an error.

Running it is worth doing for the same reason running flycast was. That first
run went `pass=197 fail=4` and ended at `pass=234 fail=0`, and fixing the first
four exposed four MORE that had been masked behind them — a suite's first run on
a new host reports the first LAYER of its bugs, not their number. fbneo's
adapter has never been executed at all, where flycast's had at least been
written against a host somebody could start, so expect at least as much.

Only worth doing if David's studio port takes us into that tree anyway.

---

## [B] A movie that does not start at power-on cannot be replayed

`[2026-09-08]` `replay.startRecording()` from Lua produces a clip whose first
frame is wherever the machine was — and nothing can play it back. Its own
docstring already warns the clip "needs a savestate to be replayable"; what is
newly measured is that pairing it with one is **not enough**. Seeking such a
movie lands the state correctly and then never advances a frame.

**It is the replay model, not the seek.** This engine records NETPLAY MATCHES:
the `.flyr` header carries `Player`, `Opponent`, `Quark` and `Relay Key`, and at
least six sites compare a frame number against `session_inputs.size()` because a
match recording is dense from frame 0 — `core/rend/gui.cpp:769` names the
assumption outright, and `:4568`, `:4576`, `:4950`, `:4999` and
`core/dojo/dojo_gui.cpp:1033` all rest on it. A movie starting at frame 9948
violates it everywhere at once.

One of those six was a real bug and is fixed: the end-of-movie check used
`session_inputs.size() - 1` as "the last frame", which is also wrong for any
movie with a GAP — and gaps are reachable today, since re-recording leaves them.
The other five are correct-for-netplay and should not be touched piecemeal; see
`docs/MODULARIZATION.md`.

The studio never produces such a movie ("Movies record from power-on (frame 0);
savestate-seek is a bookmark into that timeline"), so this blocks nothing today.
It will matter if scripted recording becomes part of the studio — which the
port's own roadmap contemplates.

An attempt to support it in `core/rend/mainui.cpp` was **removed rather than
left half-working**: it made the seek fire before playback, which is necessary
and not sufficient, and support that looks real is worse than a recorded gap.

---

## The studio port — the piano roll

`[2026-09-09]` Five modules landed under `core/dojo/roll_*`, all with self-tests
and none of them naming flycast or Marvel vs. Capcom 2: `roll_host.h` (the four
host questions), `roll_profile` (game columns), `roll_select` (15 claims),
`roll_edit` (29), `roll_paint` (18). The panel is `roll_panel.cpp`, registered
through `panels::add`.

- [x] Paint wired into the panel. A press in an input column begins a stroke, a
  release commits one edit through `Dojo::ApplyEdit`. `scripts/rolltest.sh`
  drives real clicks and asserts the commit; `dojo:RollPaintProbe` drives the
  multi-row span in process. Both have failing arms.
- [x] Two adjacent rows reported hovered at one cursor position, so a stationary
  press painted two rows and one press logged two `begin` lines. Cause:
  `ImGui::Selectable` inflates its hit box by `ItemSpacing.y` so selectables
  tile with no click-gap, but a table lays rows out with `CellPadding` and
  flycast scales the style, so the boxes overlap. The roll now decides the
  hovered row **once per frame** from geometry, preferring an exact hit and
  falling back to nearest centre — the same shape as the column decision.
- [x] A drag committed only the row it started on. Cause was **not** the display
  server: `updateMousePositionWhileDragging()` in `core/sdl/sdl.cpp` overwrote
  every fresh `SDL_MOUSEMOTION` with a stale `SDL_GetGlobalMouseState` read on
  each input pump. It now applies only while the pointer is OUTSIDE the window,
  which is its stated purpose. `[CORRECTED 2026-09-09]` this was written up as
  "no pointer motion is delivered while a button is held under Xvfb + i3",
  measured four ways — every measurement went through `dojo:MouseDragTrace`,
  which traces the guilty function. **An instrument built on the defect will
  confirm the defect.** `scripts/rolltest.sh` now drives an 8-step drag and
  asserts the span; `scripts/docktest.sh` still passes.
- [x] **The production `roll::Host` exists.** `core/dojo/roll_slots.cpp`. Until
  2026-09-09 `setHost()` was called only by a self-test, so the savestate gutter
  had never shown a real slot — the third self-tested-seam-with-no-customer of
  the day. It carries the scan cache and the staleness memoisation the surveys
  said it needs (`docs/STATES-LIFT.md` G1, G3), and `scripts/rolltest.sh`
  asserts a real anchored slot with a failing arm.
- [x] **The cell and the pattern.** `core/dojo/roll_pattern.{h,cpp}` plus
  `Cell`/`cellApply` in the profile and the codec in `roll_edit`. `paintColumn`
  now delegates to it, and roll_edit's 29 claims and roll_paint's 18 pass
  unaltered — which is the evidence the two loops were one loop. 18 new claims,
  4 sabotages, each breaking its own. `docs/ROLL-EDIT-MODEL.md` §7.
- [x] **stretch / compress / reverse** — the fully generic tools. Stretch was
  the interesting one: it maps a row to `k` rows, a STRIDE and not a shift, so
  it is the case a single delta cannot express — `Remap` took it with one span
  per source row and nothing else changed, which is the evidence the span design
  generalises. Compress is built ON `deleteRows` rather than beside it, because
  the fork writes that loop twice with different phase. All three have panel
  buttons.
- [x] **Notation and mash.** `core/dojo/roll_notation.{h,cpp}` is the third
  profile chokepoint, and ONE dialect where the fork has five. It is built from
  the profile — buttons match its own column labels, the numpad comes from the
  four direction bits it names — so a second game needs no change here. Mash is
  `applyPattern` with a longer track and no second loop, which is
  `docs/ROLL-EDIT-MODEL.md` §2 cashed in. `dojo:RollMashProbe` covers the
  wiring the harness cannot type.
- [x] **Fill** — `applyPatternToRows`, the same pattern cycled over a SET of
  rows. Different from mash only when the selection is gapped, which is exactly
  when it is wanted. The fork's two "Fill" buttons disagree about untouched
  lanes and one wipes the other player; here an empty track means leave that
  lane alone, and a claim pins it.
- [x] **Brush / stamp** — `Paint::arm()`. The stroke carries a pattern instead
  of a column: same gesture, same phase, same gap, only the payload changes.
  No second stroke class and no second loop. Set-vs-erase deliberately does not
  apply while armed — a pattern says what to write, and asking the anchor cell
  would make one brush mean two things depending on where it started.
- [x] **The staged buffer** — `core/dojo/roll_staged.{h,cpp}`. A clip, an
  immutable baseline, and an ordered op queue replayed from it, which is what
  makes a LOSSY op reversible: compress by 3, pop the op, the frames come back
  because they were never dropped. An undo stack remembers what a document USED
  to be; an op queue remembers what was ASKED FOR, so the recipe can be edited.
  Both fork hazards avoided by construction — the ops have their own buttons
  rather than sharing the movie's, and the queue is never on Ctrl+Z.
  `place()` builds a Pattern, making it `applyPattern`'s sixth customer.
- [x] **Structural edits return a ROW REMAP.** `core/dojo/roll_remap.{h,cpp}`;
  `deleteRows`/`insertBlanks` return `Resize { edit, remap }` with the edit
  DERIVED from the remap, so there is one owner of "where did row f go".
  `Selection::remap()` is the first customer — the panel no longer throws the
  selection away after a resize. 17 remap claims, 6 new selection claims, 4
  edit claims, 4 sabotages.
- [x] **Savestate anchors follow a renumber.** `Host::rowsRemapped()` rewrites
  the `.frame` sidecars, atomically, preserving every other field and the file's
  version length; `dojo:RemapAnchors=no` turns it off. Undo restores them
  through dojo's `edit_meta_capture`/`edit_meta_apply` rails, which existed for
  bookmarks and had never been assigned. `dojo:RollAnchorProbe` reads the real
  file before and after, and `scripts/rolltest.sh` asserts it with two failing
  arms. `[OPEN]` with NO clip folder open the rewrite refuses and says so —
  the shared data path has two derivations that can disagree
  (`docs/STATES-LIFT.md` G13), and settling that is its own work.
- [x] **Bookmarks**, `core/dojo/roll_marks.{h,cpp}` — a remap customer by
  construction, panel buttons, a `*` in the frame gutter. A mark on a deleted
  frame is DROPPED rather than slid onto its neighbour, deliberately unlike the
  savestate anchors, which collapse: a savestate still exists and must be drawn
  somewhere, a bookmark is only a pointer.
- [x] **`roll_meta`** — dojo carries ONE opaque `gui_meta` blob for undo, and
  anchors plus bookmarks are two customers for one setter. A registry owns it;
  the blob is self-describing so an unknown provider is skipped rather than
  misparsed.
- [x] **`remapRegister` / `remapAll`** — holders register, callers make ONE
  call. Found by the probe: the panel had grown three lines after every resize
  and the probe, a fourth caller, had one of them, so the bookmark silently did
  not move. Three lines to remember is the fork's five hand-called fixups, just
  younger.
- [x] **Bookmarks persist**, as a `marks.txt` sidecar beside the clip — NOT in
  `clip.json`. The tree states the reason at `.label`: a sidecar "travels with
  F8 backups and clip renames for free, works when there is no clip folder at
  all, and needs no read-modify-write of a shared file". A bookmark save has no
  business being able to lose a generation record. One line per mark,
  `frame<TAB>label`, written atomically; no marks means no file.
  `dojo:RollMarkProbe` reads the FILE back, `dojo:MarksPersist=no` turns it off.
- [x] **The States window, first slice** — `core/dojo/states_panel.cpp`, the
  slot WALL. Reads through `roll::Host::slotView()`, which answers in slots and
  movie frames and names no file: slot, anchored frame, clean/stale/unjudged,
  size, when, label. Slot 0 shows as **BASE** rather than `0`, because flycast's
  own pause menu numbers slots from ONE and would show the same file as "1"
  (§G11). `haveFrame` is carried separately from `frame != 0`, so a power-on
  state anchored at frame 0 is not confused with one that has no anchor (§4.3).
  `scripts/rolltest.sh` asserts the wall and the roll's gutter agree.
- [x] **Rename a slot** (§G7) — `Host::setSlotLabel()`, the first WRITE in that
  interface, with a default that REFUSES rather than silently doing nothing. It
  gave `hostfs::saveSavestateLabel` its first caller. `dojo:StatesLabelProbe`
  makes the round trip the UI makes — write, force a rescan, read back through
  `slotView()` — because reading back the string just passed in is the cache
  agreeing with itself.
- [x] **§G2 — `savestate_epoch` is bumped where a state is written.** `dojo.h`
  described it as "bumped on every savestate write" and it was bumped in two
  places, neither of them one. The half-second safety tick was covering for it,
  which is to say the fallback was hiding the defect it was added for.
- [x] **Delete a slot** (§G8) — `Host::deleteSlot()`, the only irreversible
  operation in the interface. `hostfs::deleteSavestate` had zero callers and its
  own header names the side effects the caller owns; this is that caller.
  Two-step arm-and-confirm rather than a modal, because a modal stops the frame
  and this panel draws while a movie may run. `dojo:StatesDeleteProbe` runs in
  its OWN launch on its OWN copy of the clip, and also checks that a
  now-empty slot is REFUSED.
- [x] **The generations pane** — snapshots of the whole slot set, behind
  `Host::snapshotView()` and cached on `tas_clip::libraryVersion()` rather than
  rescanned per draw. The `#` column is a running count in DISPLAY order and not
  the stored number, which is the fork's own hard-won lesson (its folder numbers
  restart per kind, so a list of eight ended "06" — "8 or 6 backups?").
  Click to tag, Ctrl+click to annotate. `dojo:StatesGenProbe` MAKES a snapshot
  and watches the count move, because counting what is already there is vacuous
  on a fixture with none.
- [ ] The States window, the rest: **no thumbnails** (nothing in this tree
  writes one, and `GetLastFrameRGB` is DX9/DX11 only — §G6); no restore of a
  generation (pre-boot only in the fork, and the wall's own clip is always
  live); no save/load actions from the wall (`dc_loadstate` from a deferred
  point is documented as wedging the emulator — see `core/lua/lua.cpp`).
- [ ] `[OPEN]` the fork's 100-slot wall in its `core/rend/gui.cpp` has still not
  been lifted; only its generations pane has, so the 131/17 figures describe the
  PANE and this slice was built from the gaps survey instead.
- [ ] Report two davidrr bugs upstream: missing `core/deps/glslang/CHANGES.md`,
  and no headless auto-play.

## Session kinds — TAS, rollback, training and plain play are conflated

`[MEASURED 2026-09-09]` `docs/SESSION-KINDS.md` is the census: **170 raw
decision sites against 2 that use `session::`.** Read it before touching any of
this; the counts and the evidence are there, not here.

- [ ] The predicates are unused because they answer questions nobody asks — the
  census names **14** distinct questions with no predicate. Name those first.
  `session::livePeer()` is the first one added under that reading.
- [x] **Training is in `Kind` now**, below Replay — an ordering read out of
  `dojo_gui.cpp`, which already spells the pair as `if (play_match) … else if
  (Training)`. And **34 raw reads of the key migrated to one owner**, including
  the 11 copy-pasted lines in `gamepad_device.cpp`.
  **TWO predicates, deliberately not one:** `training()` is the KIND
  (exclusive); `trainingEnabled()` is the TOGGLE, which is what all 34 sites
  actually ask — they read it as `Training && ShowTrainingInputDisplay` or as an
  arm beside `play_match`, never as "what kind of session is this". Collapsing
  them would have silently changed behaviour wherever the toggle is set under a
  higher kind. A self-test claim pins the difference and a sabotage confirms it.
- [ ] Migrate against the denominator (170), so a missed site is loud. A partial
  migration reads exactly like a complete one. **Progress `[2026-09-10]`:** 34
  `Training` reads → `session::trainingEnabled()`; `MapleApplyAction`'s
  `tasWriteGrow` → `session::writeGrow()` (its first caller); `livePeer()`
  removed as a duplicate of a corrected `netplay()`. Adopted predicates 2 → 5.
- [ ] Work §4's **11 latent disagreements** as a bug list. **#9 fixed
  `[2026-09-10]`** — `kind()` now discriminates on whether a movie is driving
  the guest, which `maple_if.cpp` makes exact, and treats Receiving as netplay
  explicitly. Three self-test claims, one sabotage. That also deleted
  `session::livePeer()`, a predicate added a day earlier to work around the
  defect: fixing `kind()` made the two identical, and a synonym would have been
  two owners of one question. **10 remain.**
- [x] §6 — the two `!play_match` guards in `dojo.cpp`. Both said "edits require
  `!play_match` so replay never reaches this", which stopped being true when the
  roll's gate was corrected. The clause was standing in for "the UI cannot get
  here", not for a rule about locks — a locked range protects frames whatever
  kind of session edits them, and a paused replay is exactly when a user relies
  on that. Now `!history_replay` alone. **Inert today** either way, since
  `gui_locked_ranges()` is a stub (§G10), which is why it had to be found by
  reading rather than by a test.

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

`[MEASURED 2026-09-05]` This is not only a user resizing the window. **Booting
a game triggers it**: a capture started 60 frames in produced **3 packets** and
stopped, because the resolution changes as REIOS hands over to the game. The
integration test now starts recording at frame 900 to sit entirely inside the
game, which is a workaround in the test rather than a fix in the code - anyone
scripting "record from launch" hits this.

### [x] Integration tests — `shell/linux/integration-tests`
Five cases, each covering something a unit check cannot: mock conformance with
no emulator, the package running from a wrongly-named directory, conformance
inside the emulator, a script loading with an unrelated working directory, and
a captured video having pictures in it.

`[MEASURED 2026-09-05]` 5 pass, 0 fail, 0 skip.

A skip exits **2**, not 0: a suite that goes green when its prerequisites are
missing is reporting on the machine rather than on the code.

The blank-video case was made to fail on purpose before being trusted. A
240-packet, structurally perfect, entirely black AVI - the 2026-08-09 failure
reproduced with ffmpeg - is caught at stddev 0, while a real capture reads
49-93. Its first implementation parsed `YSTDEV` from ffmpeg's `signalstats`,
which **does not exist** in that filter, so the grep matched nothing, the value
defaulted to zero, and a good 3.3 MB capture was reported blank. A check that
fails when the thing works is worse than no check.

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

## Testing

### [x] Integration tests — `shell/linux/integration-tests`
Five cases, each reproducing a defect that actually shipped. `--fast` runs the
two that need no ROM or display. **A skip exits 2, not 0.**

`[MEASURED 2026-09-06]` 5 pass, 0 fail, 0 skip.

### [x] The Lua interface is a separate package and repository
`emuapi/` is a submodule of `github.com/NBeing/emuapi` (private for now, see
`.gitmodules` for the one-line command to open it). It carries its own history,
its own conformance suite, and a mock host so the suite runs with no emulator:

    lua emuapi/run-conformance.lua        # ~1s, no ROM, no window

`[MEASURED 2026-09-06]` mock 189 pass / 0 fail / 0 skip; flycast 183 / 0 / 3,
every skip naming its reason.

**Note for anyone cloning this fork while the package repo is private:** the
`emuapi/` directory arrives empty, so the Lua scripts and three of the five
integration cases will not work until someone with access runs
`git submodule update --init`.

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
