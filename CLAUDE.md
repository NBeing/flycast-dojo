# Working in flycast-dojo

> **Branch note — `dojo7`.** This branch is the unification: blueminder's
> `dojo-7-preview4` base, carrying the `video-recording` Lua/emuapi work, the
> TAS fork's re-record engine and `tas_*` modules, the determinism layer and the
> video capture stack. **Read `UNIFIED.md` first** — it says what is verified,
> what is stubbed, and what is deliberately absent. `video-recording` remains as
> the history this came from, not a branch to migrate away from.
>
> The rules below still hold; the ones about `core/oslib/audiostream.cpp`,
> `gui_settings.cpp` and `core/wsi/{wgl,xgl}.cpp` describe the OLD base and are
> stale here (dojo-7 moved or deleted all three).

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

## 7. Traps already paid for

`emuapi/INTEGRATION.md` is the running list for the Lua interface —
adapters must be idempotent to load, error propagation differs between wrapped
and raw bindings, colour packing is rarely what you assume. Every entry there
is something that actually went wrong, not a precaution.

Add to it rather than re-learning.
