# Working in flycast-dojo

Workflow rules adapted from `nbneo-rr/CLAUDE.md`. Its architecture sections are
not reproduced — that project's `step / render / observe / present` model is a
fact about *that* codebase, and a second copy here would rot into a lie. What
carries over is the doctrine, and every rule below is illustrated with a defect
from **this** repository rather than that one.

---

## 1. Testing doctrine

Five rules. The expensive mistakes here have been skipped rules, not hard
problems.

**1. Every check must be able to fail. Make it fail once, on purpose, before
you trust it.** A check that cannot fail reads exactly like a passing one.

> `[MEASURED 2026-09-04]` The first run of `docs/adapters/conformance.lua`
> reported `NON-CONFORMING` with 243 stalls. The emulator was correct; the
> check asserted a *proxy* — that `frame.confirmed()` advances once per frame
> callback — whose premise only holds during a session. A suite asserting a
> proxy rather than the rule is worse than no suite: it manufactures distrust
> in code that is right.

> `[MEASURED 2026-09-04]` A test of the rofi launcher's five failure paths
> printed `exit=0` for all five, including the ones that had just printed their
> error. The script was fine; `$?` was reading `tail` at the end of a pipeline.

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

**4. State coverage; green is not scope.** Bind each claim to an observable
boundary, or mark it open.

> The conformance suite prints `no rollback occurred in this session, so the
> gate was not exercised`. A rule that never ran has not been tested, and a
> green result that hides this is worse than a yellow one that says it.

**5. Determinism failures are intermittent by nature.** One green run is not
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

The same rule is why `emu.supports()` in `docs/adapters/emuapi.lua` is derived
from the bindings that actually exist rather than from a declared list.

Generated files are not committed when the only difference between two
checkouts is a path: `shell/linux/flycast-dojo-rofi.desktop.in` is the
version-controlled thing, and `install.sh` renders it.

---

## 5. Start a session by reading the backlog

Two running lists, both kept current:

- `TODOS.md` — the branch's work: capture, rollback, Windows verification.
- `LUA_TODO.md` — the cross-emulator Lua interface and the fbneo-rr port.

Both mark what is **verified**, what is **reasoned**, and what was **never
run**. The three unverified items have stayed explicitly unverified across many
sessions rather than quietly becoming "done": DX9/DX11 capture on real Windows,
`setup-and-build.ps1`, and the rollback gate under a live netplay session.

When you finish a session, update the item you touched and move its status. An
item that silently changes from "not run" to absent is the failure this rule
exists to prevent.

---

## 6. Traps already paid for

`docs/adapters/INTEGRATION.md` is the running list for the Lua interface —
adapters must be idempotent to load, error propagation differs between wrapped
and raw bindings, colour packing is rarely what you assume. Every entry there
is something that actually went wrong, not a precaution.

Add to it rather than re-learning.
