# Working in flycast-dojo

> ## Branch `dojo7` — the unification
>
> Base **dojo-7-preview4**, carrying the `video-recording` Lua/emuapi work, the
> TAS fork's re-record engine and `tas_*` modules, the determinism layer and the
> video capture stack. **Read `UNIFIED.md` first.**
>
> `video-recording` is history now, not a branch to migrate away from.
> `UNIFICATION.md` is superseded and marked as such; it is kept because several
> of its conclusions were wrong in instructive ways.
>
> ### Provenance marks — adopted from `~/dev/anita/nbneo-rr/CLAUDE.md`
>
> This work retracted a lot. Six confident claims turned out to be wrong within
> a day of being written, so claims here carry their evidence:
>
> - **`[MEASURED <date>]`** — a number this work produced, with the command.
> - **`[SOURCE]`** — read out of code in this checkout. **Quote the line; the
>   line number is only a hint**, because line numbers rot.
> - **`[CORRECTED <date>]`** — the doc previously claimed something else, and
>   the old claim is shown rather than silently replaced.
> - **Unmarked** — reasoning, not evidence.
>
> ### Two rules this branch learned the hard way
>
> **A clean build is not evidence a feature is wired.** `SaveStateFrame` /
> `LoadStateFrame` compiled, linked, and were unreachable for several commits,
> because their only callers lived in a file that was only partly ported.
> Savestates silently carried no `.frame` sidecar. Likewise `avi_dump` sat as
> ~1,050 lines behind permanently-false guards, and `dojo:AutoCapture=yes`
> quietly did nothing.
>
> **An absent binding and a broken feature look identical from outside.** A test
> that flipped `flycast.config.dojo.RecordMatches` reported a false pass: the
> property was never bound, so the write went nowhere and the missing log read
> as a failing guard. Verify the *instrument* before trusting the reading.
>
> ### Automated testing must not touch the real desktop
>
> `scripts/isotest.sh`, and the order of preference matters: **drive it from Lua
> first** (no display at all), use the offscreen display only when a UI element
> must actually be seen, and never synthesise input on `:1`. This is
> `fbneo-rr/CLAUDE.md`'s rule, learned again here the expensive way — a stray
> `i3` with no `-c` read the real `~/.config/i3/config` and executed its
> autostart.
>
> ### Stale in the rules below
>
> They describe the OLD base: `core/oslib/audiostream.cpp` (now `core/audio/`),
> `gui_settings.cpp` and `core/wsi/{wgl,xgl}.cpp` (deleted upstream).

Workflow rules adapted from `nbneo-rr/CLAUDE.md`. Its architecture sections are
not reproduced — that project's `step / render / observe / present` model is a
fact about *that* codebase, and a second copy here would rot into a lie. What
carries over is the doctrine, and every rule below is illustrated with a defect
from **this** repository rather than that one.

---

## 1. Testing doctrine

Six rules. The expensive mistakes here have been skipped rules, not hard
problems.

**1. Every check must be able to fail. Make it fail once, on purpose, before
you trust it.** A check that cannot fail reads exactly like a passing one.

> `[MEASURED 2026-09-04]` The first run of `emuapi/conformance.lua`
> reported `NON-CONFORMING` with 243 stalls. The emulator was correct; the
> check asserted a *proxy* — that `frame.confirmed()` advances once per frame
> callback — whose premise only holds during a session. A suite asserting a
> proxy rather than the rule is worse than no suite: it manufactures distrust
> in code that is right.

> `[MEASURED 2026-09-04]` A test of the rofi launcher's five failure paths
> printed `exit=0` for all five, including the ones that had just printed their
> error. The script was fine; `$?` was reading `tail` at the end of a pipeline.

> `[MEASURED 2026-09-05]` The blank-video check parsed `YSTDEV` out of ffmpeg's
> `signalstats`. **That key does not exist** — the filter emits
> `YMIN/YLOW/YAVG/YHIGH/YMAX` and no standard deviation. The grep matched
> nothing, the value defaulted to zero, and a perfectly good 3.3 MB capture was
> reported as blank. Note the shape of the near-miss: the obvious repair is to
> loosen the threshold, which would have destroyed the check while making it
> pass. A measurement whose failure mode is *silently empty* must be built so
> that empty is an error, not a zero.

> `[MEASURED 2026-09-07]` A negative test asserted that `startRecording` is
> refused during playback, and judged it by "no clip folder was created". That
> is ALSO what "the Lua script never ran" looks like, and the script had not
> run. It reported PASS. The fix is the general one: **every test's first
> assertion is that it is running in the state it claims to test** — the script
> now writes a marker BEFORE the call it is testing, so "did not run" and
> "refused" cannot produce the same evidence. Three more of the same family the
> same day: a probe whose callback never fired because flycast's
> `IsMouseClicked` is 1-based and threw on 0; a "still paused?" check that was
> equally consistent with the guard never being reached; and a report printed
> before the event it was reporting on.

> `[MEASURED 2026-09-05]` `conformance.lua` contained
> `ok(not present or true, ...)` — a constant. The entire "denied" half of the
> capability loop had never been able to fail, so a rename leaving a stub behind
> passed it. It had been reviewed, committed and run hundreds of times. **A
> check reads the same whether or not it can fail; only making it fail tells
> you.**

**2. Assert non-vacuity.** Frames advanced > 0, pixels not all one colour,
lists not silently empty.

> `[MEASURED 2026-08-09]` The first video capture produced a well-formed
> 8.7-second AVI — correct dimensions, frame count, duration, container — that
> was entirely black. Every structural check passed. Only decoding a frame and
> *looking at it* caught a PBO ring index that never advanced.

> `[MEASURED 2026-09-04]` Surface coverage read `54/54` while the adapter had
> never forwarded `ui.*`, so a fifth of the interface was unreachable. The
> denominator was a hand-maintained list; anything missing from *it* is
> invisible to both the report and the suite.

**3. Run the control.** Two runs of identical code disagreeing is the cheapest
check available — and so is running the case where a bug would be visible.

> Game-pixel overlays were verified at **960×480**, deliberately not 4:3. At a
> matching aspect ratio a broken coordinate mapping is pixel-identical to a
> correct one.

> `[MEASURED 2026-09-05]` That lesson had to be relearned the same day it was
> written down. A tour screenshot taken to verify the new draw surface showed
> `window 640x480` against a `640x480` game — a matching aspect, proving
> nothing. The window size had to be forced in `emu.cfg` before the run meant
> anything. **Setting up the control is a step you can silently skip and still
> get a green picture.**

> `[MEASURED 2026-09-06]` Two controls are sometimes needed, not one. An
> arbiter that *never* refuses and one that refuses *everything* both satisfy a
> single "the contested claim was refused" assertion. Only the pair
> discriminates: sabotage in each direction, and require that each breaks a
> different half.

**4. State coverage; green is not scope.** Bind each claim to an observable
boundary, or mark it open.

> The conformance suite prints `no rollback occurred in this session, so the
> gate was not exercised`. A rule that never ran has not been tested, and a
> green result that hides this is worse than a yellow one that says it.

**5. A skipped check is not a passing one, and must not report as one.**

> `shell/linux/integration-tests` exits **2** when a case is skipped, never 0.
> A suite that goes green because its prerequisites are missing is reporting on
> the machine rather than on the code.

> `[MEASURED 2026-09-10]` **ctest breaks this rule by default, and did.** It
> printed `100% tests passed, 0 tests failed out of 15` and exited 0, with
> `The following tests did not run: 4 - flycast.hotkeytest (Skipped)` in between.
> The test had never run once: `CMakeLists.txt` passed a bare display number
> and the script expected a leading colon, so its `Xvfb 147` answered
> "Unrecognized option: 147" and exited.
>
> **The same one-character bug was in `scripts/selftest.sh`, and that one
> PASSED** — its 331 claims run inside `flycast_init`, before `os_CreateWindow`,
> so they never needed the display the script insists on having. One bug, two
> harnesses, two different silent wrongs: one skipped and read as green, one
> passed for a reason adjacent to the intended one, and its stated prerequisite
> was a claim nobody could see was false.
>
> `SKIP_RETURN_CODE` is right and stays — a machine without a ROM genuinely
> cannot run those, and calling that a failure trains people to ignore it. The
> **summary** is what was wrong. `scripts/checks.sh` runs the suite and exits 2
> on any skip; use it rather than bare `ctest` when the answer matters.

> `[MEASURED 2026-09-06]` The conformance suite prints the *reason* beside every
> skip. flycast reports three: `main` states no size because the SH4 space is
> not a flat buffer; there is no `probe.unmapped` because unmapped reads answer
> 0, a known deviation; there is no `probe.emptyslot` because a real slot may
> hold a user's state. Each is a limitation said out loud. The failure this
> prevents is the quiet one — a check that stops running and keeps reporting
> green, which is indistinguishable from a check that runs and passes.

> A control that fails to apply is the same defect wearing a lab coat: the
> sabotage silently does nothing, the unmodified code runs, and the "control"
> passes. Assert that the patch changed something before trusting its result.

**6. Determinism failures are intermittent by nature.** One green run is not
evidence.

> The Lua `vblank` double-fire needed a connection bad enough to mispredict.
> It is invisible offline, which is why it survived until the interface work
> forced the question.

### Say how to run it. Every time.

> **Any test ships with its launch command and its pass condition — in the
> file, not in the reply.** A harness whose header disagrees with what it runs
> reports one failure shape as another, which is the most expensive kind of
> wrong.

`shell/linux/flycast-rofi` and `shell/linux/install.sh` both open with RUN /
PASS / FAIL lines. New harnesses do the same.

### Never weaken a check to make it pass

The conformance suite is the definition of conformance. When it fails, say
**why**, with evidence, and there are three legitimate answers: the
implementation is wrong, the check is wrong (as in rule 1 above), or the
specification changed and the check has not caught up. "Loosened the assertion"
is not one of them.

---

## `scripts/tests/conformance.lua` is not a duplicate — do not simplify it

`[2026-09-08]` It runs **emuapi's own suite** inside this emulator, against real
savestates, a real movie and real guest memory. It deliberately reproduces only
`run-conformance.lua`'s bootstrap and then lets vblank drive, because a second
copy of the suite would be a second definition of the word "conforms".

**What it bought, the first time it ran:** `pass=197 fail=4` → `pass=234 fail=0`,
nine defects, five of them in this repository. `ui.Button` returned nothing, so
`if ui.Button("x") then` could not work. `ui.Selectable` answered "was it
clicked" instead of `(value, changed)` — the convention `core/lua/lua.cpp`
states a few lines above its own baseline block. And `ui.End` called
`ImGui::End()` **unmatched**: asserts are compiled out here, so an unpaired
`End` did not fail, it silently corrupted the frame and surfaced somewhere else
entirely, which made a script typo look like a renderer bug.

Three things learned that generalise past this file:

- **A suite's first run on a new host reports the first LAYER of its bugs, not
  their number.** Fixing the first four exposed four more that had been masked
  behind them. "4 failures" was never the count.
- **That effect is invisible on a host written for the suite.** It was invisible
  on emuapi's mock and on agnes, because both were authored knowing the suite
  existed. A second host you wrote still has the property, only less of it.
- **A fake host is an instrument, not an approximation.** The `End` check came
  from agnes — no window, no GPU, no user — and was cheap to write *because*
  there is no window: Begin/End depth is a counter when nothing is drawn and is
  buried under a renderer when something is. It found a live corruption path in
  a shipping build that had thrown away the assert which would have reported it.

**Why it must keep running.** flycast could only ever be "a host nobody wrote
for that suite" once, and that is now spent. This test is what the property was
converted into: every future change on either side is measured against a real
emulator instead of asserted about one. Deleting it, or letting it drift into
re-implementing the checks locally, throws that away and cannot be earned back.

---

## 2. Provenance — a claim carries its mark

`[MEASURED <date>]` for something observed, `[REASONED]` for something derived
from source but not run, `[OPEN]` for something still unknown. The distinction
is not decoration:

> A survey reported that the `vblank` double-fire "hits during local dojo
> replay playback, so it is not netplay-only." It was marked as reasoned rather
> than measured, and checking took two greps: `inRollback` is written in
> exactly two places, both inside a GGPO *session* callback, and replay playback
> runs with `ggpoSession == nullptr`. The bug was real but **netplay-only**.
> Repeating it unchecked put a wrong scope in `TODOS.md` for a day.

**Verify a claim about this codebase before repeating it**, including one you
made yourself earlier in the same session.

---

## 3. Measure before optimising

> `[MEASURED 2026-08-10]` Rollback snapshot work was planned around a 10–20 MB
> per-frame `malloc` that looked obviously wasteful. Measured, the allocator
> cost **0.002–0.010 ms/frame** — under 0.06% of a frame. The real cost was
> **SIGSEGV round trips at 4.32 µs each**, ~2.2 ms/frame at 512 dirty pages:
> roughly 90% of the total, and invisible from reading the code.

The first benchmark written for that comparison was itself wrong — it compared
one hot reused buffer against eight cold ones and measured cache locality
rather than allocation. Control for what you are not testing.

---

## 4. Don't restate authoritative data

One owner per fact. `core/rend/game_scanner.h` owns which file extensions
flycast will open; `shell/linux/flycast-rofi` parses them out of it on every
run rather than holding a list, because a second copy rots in the quiet
direction — a format the emulator gains simply never appears in the menu, which
reads as "unsupported" rather than as a stale script.

The same rule is why `emu.supports()` in `emuapi/init.lua` is derived from the
bindings that actually exist rather than from a declared list.

> `[MEASURED 2026-09-05]` "Does this host implement this name?" was answered in
> two places — `emu.supports()`, and the conformance suite's own `rawget`. They
> agreed for months and parted company the moment a name moved onto a method
> table: the suite reported `gui.size` as "claimed but absent" while
> `supports()` could see it perfectly well. Two implementations of one rule do
> not disagree when you write them; they disagree when one of them is changed.
> The fix is `api.implements()`, which `supports()` is now built on.

Generated files are not committed when the only difference between two
checkouts is a path: `shell/linux/flycast-dojo-rofi.desktop.in` is the
version-controlled thing, and `install.sh` renders it.

---

## 5. Know which layer you are in

Three layers, and most bad additions are not wrong — they are in the wrong one.

| layer | maps | portable across |
|---|---|---|
| the tool | concepts → workflows | emulators AND games |
| a game profile | machine bytes → fighting-game concepts | emulators, per game |
| `emuapi` | emulator → abstract machine | emulators |

**The tool never reads a memory address.** It asks the profile; the profile
reads addresses; the profile calls emuapi. The moment the tool knows a number it
is welded to one game, and the value of the whole arrangement — N profiles plus
one tool, instead of a trainer per game per emulator — is gone.

> `[MEASURED 2026-09-05]` The conformance suite, which is supposed to be
> neutral, was poking `0x8C010000`. An SH4 address. It passed everywhere it had
> ever run because it had only ever run on a Dreamcast. **A neutral layer with
> one host's facts inside it looks exactly like a neutral layer until a second
> host arrives.**

The reasoning, the four-way classification of what belongs where, and the test
for a new idea are in `emuapi/ARCHITECTURE.md`. Read it before adding surface.

---

## 6. Start a session by reading the backlog

Three documents, all kept current:

- `TODOS.md` — the branch's work: capture, rollback, Windows verification.
- `LUA_TODO.md` — the cross-emulator Lua interface and the port backlog.
- `emuapi/ARCHITECTURE.md` — what the interface is FOR and where a new idea
  belongs. Read this one first; it is the only document that can tell you an
  addition is in the wrong layer, which is the most common way to be wrong here.

Both mark what is **verified**, what is **reasoned**, and what was **never
run**. The three unverified items have stayed explicitly unverified across many
sessions rather than quietly becoming "done": DX9/DX11 capture on real Windows,
`setup-and-build.ps1`, and the rollback gate under a live netplay session.

When you finish a session, update the item you touched and move its status. An
item that silently changes from "not run" to absent is the failure this rule
exists to prevent.

---

## The game is a dockable panel, not a hole in the UI

`[LANDED 2026-09-08]` The picture is an ImGui window like any other. It docks,
splits, tabs and resizes, and the dockspace does all the layout arithmetic.

    window  >=  content area  >=  game viewport

The UI publishes the AREA, the renderer publishes the ASPECT, and the viewport
is the product — derived on every read in `core/rend/game_viewport.{h,cpp}`,
never stored. Before that module existed the same letterbox arithmetic was
written out **twice**, in `gles/gldraw.cpp` and in `lua/lua.cpp`, both against
the raw window size and kept in step by a comment promising they matched.
Docking made both wrong at once.

Rules that cost something to learn:

- **`Renderer::GetFrameTexture()` returning 0 means "keep blitting."** GL
  publishes a texture; DX9, DX11 and Vulkan do not yet, so they fall back and
  behave exactly as before. Both DX backends already hold the frame as a texture
  (`framebufferTexture`, `fbTextureView`), but the hook alone is not enough —
  their present paths also need the "do not blit" half, and half of it draws the
  picture **twice**.
- **The offscreen buffer must be a TEXTURE, not a renderbuffer.** `Rotate90`
  already needed this (a rotated present is `drawQuad`, not a blit), so
  `needsSampleableFrame()` asks once for both. Get this wrong and the panel
  silently falls back.
- **`yUp` is reported, not assumed.** The GL rule of thumb — row 0 is the
  bottom, flip v — is *wrong* for flycast's offscreen buffer, which is rendered
  with an already-flipped projection. Flipping "because OpenGL" draws the game
  upside down, and the flip lives in a projection matrix nowhere near the blit,
  so the code cannot tell you. Found by looking at a screenshot.
- **`SetNextWindowDockID` must be `FirstUseEver`, never `Always`.** A per-frame
  dock id drags the panel back every time the user moves it — the same mistake
  as a per-frame `SetNextWindowPos`, which defeated docking entirely once.
- **Every GuiState that shows the running game must submit the host AND the
  panel**, or the picture jumps out of its dock when that screen opens. The list
  is enumerated in `gui_display_ui`, deliberately: "is a game loaded" is the
  wrong question, since Main and SelectDisk can be reached with one loaded and
  neither shows it.

`scripts/docktest.sh` drives a REAL drag with xdotool on a private Xvfb — never
the user's display — and judges from the emulator's own traces. `--self-test`
runs the same drag against the old behaviour and requires it to fail.

`dojo:GamePanel` is **on by default** `[2026-09-08]`. Safe to default because a
backend that publishes no frame texture falls back to the blit on its own, so
DX9, DX11 and Vulkan behave exactly as they did and only GL — where this is
tested — changes. `dojo:GamePanel=no` returns the picture to a full-window blit.

Diagnostic flags, off by default: `dojo:DockGameViewport=no` (revert the
content-area publish at runtime) and `dojo:ViewportTrace` (window / central node
/ game rect, the present's mode, and GuiState transitions — logged only on
change).

---

## 7. Traps already paid for

`emuapi/INTEGRATION.md` is the running list for the Lua interface —
adapters must be idempotent to load, error propagation differs between wrapped
and raw bindings, colour packing is rarely what you assume. Every entry there
is something that actually went wrong, not a precaution.

Add to it rather than re-learning.

## The emulator's stdout is NOT a text file — use `grep -a`

`flycast` writes NUL bytes into its output, so a redirected log is `data` to
`file(1)` and **plain `grep` silently prints nothing** — no matches, no "binary
file matches", no error, exit status 1. `sed`, `tail` and `cat` all show the
text fine, which makes it look like the line you are grepping for was never
logged.

[MEASURED 2026-09-07] This produced two confidently wrong conclusions in one
session: "the NOTICE_LOG never fired" (it had, 10 times) and "no COMMON channel
output" (there was). Always `grep -a` a captured flycast log, and distrust a
zero-match result you have not confirmed with `sed`/`tail`.
