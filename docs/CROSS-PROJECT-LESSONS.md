# What flycast-dojo should learn from its siblings

Read-only survey, `[2026-09-07]`, branch `dojo7`. Nothing here was changed by
the investigation. Siblings surveyed: `~/dev/emuapi`, `~/dev/fbneo-rr`,
`~/dev/anita/nbneo-rr`, `~/dev/anita/lemalta`.

## How to read the claims here

Per `CLAUDE.md` §2. `[SOURCE]` = read out of a checkout, quoted where the quote
is the citation and the line number only a hint. `[MEASURED]` = a command this
survey ran, named inline. Unmarked = reasoning.

**I did not build or run the emulator.** Every behavioural claim below is read
out of source or out of a sibling's own dated notes. Where a claim would need a
run to settle, it says so.

**Baseline: `2762188b3`.** Every `core/lua/lua.cpp` line number here was
re-derived against that commit at the end of the survey, because **the tree
moved while it was running**: HEAD advanced from `f0ddc8ac3` to `2762188b3` and
`lua.cpp` went 2,158 → 2,258 lines, shifting the late-file citations by ~73
lines. This is nbneo's rule earning itself again
(`nbneo-rr/CLAUDE.md:204-222`): *"the quote is the citation; the line number is
a hint"*, and *"in a tree with concurrent agents, 'I read the source' is not a
timestamp."* Where a citation below carries a quote, trust the quote. Where it
carries only a number, `git show 2762188b3:<path>` is the check.

**`scripts/testrun.sh` and `scripts/lua/testlib.lua` landed while this survey
was running** (untracked as of `2762188b3`), and they already implement most of
L2 and L10 from `TEST-TOOLING.md`'s recommendations: INCONCLUSIVE verdicts,
whole-line marker matching, SIGINT rather than SIGTERM, process-group kill,
deadline polling, evidence copied out before teardown, and a sandboxed
`XDG_CONFIG_HOME`. Those items below are re-scoped accordingly — the work left
is **migration and coverage**, not invention.

**This document does not restate `docs/TEST-TOOLING.md`.** That investigation
already costed the lemalta/fbneo harness borrowings and ranked them A1–A7 /
B1–B6. Several items below note that its recommendations *have not been
adopted*, which is a different claim from making them again.

**It also does not restate `docs/SPIKE-machine-pool.md`.** The machine-as-value
comparison against nbneo is done there, with numbers. §2 below only says what
the spike left open.

---

## First: what flycast-dojo does BETTER

This is not a one-way exercise. Nine items; three of them are things the
siblings should take back, and two exist only so that a later reader does not
mistake the defects in Tier 1 for a general standard of care here.

1. **Capture non-vacuity is measured in pixels, not in bytes.**
   `shell/linux/integration-tests:236-258` decodes every 20th frame to PNG and
   takes ImageMagick's greyscale standard deviation, requiring a peak `> 3.0`,
   with the reasoning inline: *"A black, white or single-colour frame has a
   standard deviation of zero and satisfies every structural check above this
   one."* lemalta's screenshot check is a **size floor only** —
   `lemalta.py:2645-2647`, `st_size > 1024`, with no pixel or histogram test
   anywhere in that repo. flycast is ahead here and the gap is not close.

2. **`core/determinism.{h,cpp}` has no counterpart in any sibling.** nbneo has
   two strong *oracles* (self-equivalence and golden-trace conformance) but no
   equivalent of the **sync manifest** — BizHawk's Settings/SyncSettings split,
   with a refusal policy (`determinism.h:137-140`: *"an unknown or missing key
   is a REFUSAL, not a warning"*) and, the part nobody else has, an
   **anti-drift audit**: `unclassifiedOptions()` / `staleClassifications()`
   (`determinism.h:147-161`) so a newly added option lands in a report rather
   than silently defaulting to "not sync-critical". `refreshCodegen()`
   (`determinism.h:91-104`) handles a hazard that only a JIT host has —
   a determinism decision baked into already-compiled blocks.

3. **`core/deferred.{h,cpp}` is a genuinely new result.** The finding that a
   savestate restore diverges from inside `vblank` but is exact from a hook
   drained between frames is flycast's own, and the implementation gets the
   hard part right: `deferred.cpp:22-33` swaps the queue and releases the lock
   *before* running anything, so an action may post another and may stop the
   emulator, and `deferred.cpp:37-45` contains a throwing action instead of
   letting it eat the ones queued behind it. nbneo's `cps2_frame_enter/leave`
   guard (`cps2_machine.h:823-855`) solves a narrower version of the same
   problem by *counting* violations; flycast's gives callers a safe place to
   stand instead.

4. **The savestate already IS the machine-as-value function.** `dc_serialize()`
   / `dc_loadstate()` are the authoritative enumeration of machine state — the
   artefact nbneo had to *build* `tools/statics-census.py` to discover. That is
   why the spike could measure pool primitives (7.7 ms restore) in an afternoon
   while nbneo spent six named passes going 320 → 4.

5. **Callback registration returns handles that count deliveries and faults.**
   `LUA_TODO.md:223-249`. fbneo-rr's single-slot model silently *replaces* a
   first subscriber with a second. nbneo goes further still (every hook stamped
   with the file that registered it, so `stop(owner)` is expressible) and
   `LUA_TODO.md:246-249` already records that as the open half.

6. **Lua error policy is ONE convention, not five.** Both dispatchers
   (`core/lua/lua.cpp:156-159` and `:171-174`) catch `LuaException`, log at
   WARN, push the message to the Lua console, and **continue**. fbneo-rr has
   **five different policies chosen by dispatch path**: a frame-advance
   coroutine error kills the script but leaves the VM open
   (`fbneo-rr/src/burner/lua/lua_core.cpp:439-486`); an `emu.register*` error
   calls `FBA_LuaStop()` and **`lua_close`s the VM**
   (`lua_core.cpp:371-398`); a draw-callback error is **swallowed and retried
   every frame forever**, with the notification rate-limited to 2000 ms
   (`lua_callback.cpp:100-133`); a `savestate.registersave` error makes the
   callback **unregister itself** behind a blocking `MessageBoxA`
   (`src/burner/luasav.cpp:661-670`); and a `cpu.setbreak` error reaches
   `bprintf` and not even the Lua console (`lua_debug.cpp:291-310`). Nothing
   there documents the difference. flycast's uniformity is worth defending.

7. **flycast's bindings do not return uninitialised stack.** fbneo-rr's
   `movie.length()` declares `return 1` with the push commented out and its own
   `@luadoc` admits it *"reads whatever garbage was already on the stack"*
   (`lua_movie.cpp:84-89`); `input.get()` pushes uninitialised memory as
   `xmouse`/`ymouse` (`lua_input.cpp:147-154`); `memory.setregister` returns 1
   after `lua_settop(L,0)` (`lua_memory.cpp:659`). Named only so that the
   contrast is on the record — flycast's `ui.*` no-op (L5) is a real defect, but
   it is not this class.

8. **`emu.supports()` is derived from the bindings that exist**
   (`emuapi/init.lua:155-170`), never a hand-maintained list. nbneo measured
   what the declared alternative costs: fbneo-rr's `LUA_API.md` is missing
   **30 of the 443 names its own source registers, 25 of them an entire
   namespace** (`nbneo-rr/CLAUDE.md:664-670`, `[MEASURED 2026-08-14]`).

9. **`scripts/tas-fork/test.ps1` writes a machine-readable results file**
   (`test.ps1:180-183`). lemalta has no results JSON at all.

---

## The one-sentence diagnosis

**flycast-dojo's written doctrine is at parity with nbneo-rr's — and in places
better, because it has six rules where nbneo has five. What it does not have is
any MECHANISM that makes the doctrine fail loudly when it is skipped.**

`CLAUDE.md` §1 rule 1 says *"Every check must be able to fail. Make it fail
once, on purpose, before you trust it."*

`[MEASURED 2026-09-07]` `grep -rn 'sabotage\|can_fail\|canfail' scripts/ shell/
emuapi/` returns **16 matches, all of them in one file pair** —
`emuapi/hosts/agnes/exec_hook.h` and `probe_exec_hook.c`, a second host's C
probe that landed during this survey. **Nothing in `scripts/`, nothing in
`shell/`, nothing in `conformance.lua`, nothing in the new
`scripts/testrun.sh`.** The mechanism exists in the tree exactly once, in the
newest thing in it, and has not reached any of the suites that gate real work.

`[MEASURED 2026-09-07]` `grep -n 'add_test' CMakeLists.txt` returns **zero
matches**; `ENABLE_CTEST` defaults `OFF` (`CMakeLists.txt:51`) and its body is
`include(CTest)` and nothing else (`CMakeLists.txt:80-82`). No workflow in
`.github/workflows/` runs `shell/linux/integration-tests` or the conformance
suite.

nbneo, for contrast: **119 `add_test` entries, 54 of them `*_can_fail` twins,
58 carrying `SKIP_RETURN_CODE 77`** — roughly one made-to-fail control for every
two real gates, and nearly every checker takes `--sabotage NAME` /
`--list-sabotage` with the **exit code inverted under sabotage: 0 means the
check FIRED, 4 means SABOTAGE FAILED TO FAIL**
(`nbneo-rr/tools/statics-census.py:20-28`).

Everything in Tier 1 below follows from that one gap.

---

# Tier 1 — high value, hours each

## L1. Sabotage arms, with an inverted exit code

**The model is already in this tree, and it is good.**
`emuapi/hosts/agnes/exec_hook.h:62-76` — a second host's exec-hook probe, added
`[2026-09-07]` and still untracked — implements the pattern properly:

> A hook that never fires and a hook that always fires both satisfy "the hook
> was called at least once", which is why the probe beside this file runs two
> controls rather than one, and requires that each breaks a DIFFERENT case. **The
> sabotage lives here, next to the thing it sabotages, so that a control which
> silently fails to apply is impossible:** `agx_sabotage` is read on the hot path
> and the probe asserts the mode it set is the mode in force.

That is `CLAUDE.md` §1 rule 3's `[MEASURED 2026-09-06]` two-controls lesson and
its "a control that fails to apply is the same defect wearing a lab coat"
corollary, both implemented rather than written down. And its `Makefile:27-30`
closes the last hole — `make check` runs `--self-test`, not the plain form,
*"because a green probe whose sabotages were never exercised is the failure mode
this whole package keeps writing rules about."*

**It has not reached anything else.** `[MEASURED 2026-09-07]` no sabotage arm in
`shell/linux/integration-tests`, `scripts/replay-bindings-test.sh`,
`scripts/isotest.sh`, `emuapi/conformance.lua`, or the brand-new
`scripts/testrun.sh` — which gets every other rule in `CLAUDE.md` §1 right and
still has no way to be made to fail on purpose. Meanwhile `CLAUDE.md` §1 records
five separate occasions where a check could not fail, including
`ok(not present or true, ...)`, a literal constant that *"had been reviewed,
committed and run hundreds of times"* — and L3 below is a live sixth.

**What the siblings add beyond the agnes probe** is the convention around it,
which is what makes it auditable at scale.
`nbneo-rr/tools/statics-census.py:20-28` and `:1328-1346`: `--sabotage NAME` /
`--list-sabotage`, the exit code **inverted** under sabotage (`0` = the check
fired, `4` = SABOTAGE FAILED TO FAIL), and five arms each targeting a
*different consulted input* — `add-unprotected` (regression), `empty-scan` (is
the cross-reference consulted at all), `ignore-retier` (is the tier list
consulted), `stale-retier` (*"an exemption that forgives nothing is one nobody
can audit"*), `drop-a-tier`. `tools/README.md` records that all ten of
`conform-trace`'s arms have been watched fire, **and that one failed to fail the
first time, and that this is in its CHANGELOG.**

And the refinement worth more than the mechanism
(`statics-census.py:1707-1725`): **a sabotage can prove a check is *present*
without proving it is *right*.** `empty-scan` fired and 44 symbols read as
carried, which "looks like a working parser" — but could not ask whether the
parser read the *name* or a fragment. An over-greedy regex was inventing 17
phantoms and missing 22 real registrations. The fix was a **hand-checked list of
10 known registrations**: *"a hand list is a second copy of a fact and here the
duplication IS the mechanism: derived from the parser it would agree with any
parser, including a broken one."*

**Change.** Generalise the agnes pattern outward, in this order:
`--sabotage NAME` / `--list-sabotage` with the inverted exit code on
`shell/linux/integration-tests`, `scripts/testrun.sh` and
`emuapi/run-conformance.lua`. Three arms first, each one a real incident from
`CLAUDE.md` §1: a **blank-video** arm (the 8.7-second black AVI), a
**script-never-ran** arm (the false PASS at `replay-bindings-test.sh:6-9`), and
a **matching-aspect-ratio** arm (the control that proves nothing). Then make
`suite.sh` (L9) run the self-test form, as `agnes/Makefile` does.
**Effort: a few hours. This is still the highest value-per-line item in the
document — but it is now propagation, not invention, which makes it cheaper
than it was when I started writing this.**

## L2. Three-valued verdicts, everywhere — not in one file

**Sibling.** nbneo: exit 77 = SKIPPED throughout, with `SKIP_RETURN_CODE 77` on
58 ctest entries. `fbneo-rr/dev-scripts/spec_suite.sh:36-42`: *"A clean exit is
necessary but not sufficient: an oracle that printed nothing must not count as a
pass"* → prints `INCONCLUSIVE - exited 0 but reported no verdict` and counts it
as a failure.

**flycast has the rule and now applies it in two files.**
`shell/linux/integration-tests:10-12` states it perfectly — *"A skipped case is
NOT a pass and does not exit 0"* — and `:306` implements it. The new
`scripts/testrun.sh` implements it too, at `:92` and `:132`, and its
`INCONCLUSIVE ... (finished with no SUMMARY - did it assert anything?)` is the
sharper phrasing of the two.

Everywhere else it is still absent:

- `scripts/tas-fork/test.ps1:192` is `exit $failed`. A run in which **every**
  clip is SKIP (`no .flyr`, `user session active`, `stopped by user mid-test` —
  `test.ps1:64,81,142,146`) **exits 0**.
- `emuapi/run-conformance.lua:65` is `os.exit(_G.EMUAPI_CONFORMS and 0 or 1)`.
  Two-valued. The verdict behind it (`conformance.lua:1469`) is `fail == 0` with
  no floor on `pass` and no gate on `skips`, so an all-skip run prints
  `RESULT: CONFORMS` and exits 0. (Its header at `run-conformance.lua:5` also
  reads *"a non-zero exit status of 0"*.)
- `scripts/replay-bindings-test.sh:83` — the final line of a script whose entire
  preamble is about avoiding false passes — prints
  `WARN: guard state unconfirmed` **and falls off the end with exit 0**.

**Change.** The convention now exists in the tree twice; propagate it rather
than design it. `2` (or `77` if these become ctest entries) for
skip/inconclusive, applied to the four above. The `WARN` at
`replay-bindings-test.sh:83` becomes exit 2. Better: port
`replay-bindings-test.sh` onto `testlib.lua` and delete its bespoke marker file
— `testlib`'s `ran()` is the generalisation of exactly the trick that script
invented (`replay-bindings-test.sh:6-9`, `testlib.lua:20-22`).
**Effort: under an hour.**

## L3. The conformance suite's capability check cannot fail

**flycast.** `emuapi/conformance.lua:203-205`:

```lua
local present = api.implements(name)
if emu.supports(name) then
    ok(present, g, name .. " claimed but absent")
```

and `emuapi/init.lua:167-170`:

```lua
function M.emu.supports(name)
    if unsupported[name] then return false end
    return M.implements(name)
end
```

Inside that branch `present` is true **by construction**. `ok(present, ...)`
cannot fail. A subagent confirmed it empirically: deleting `joypad.getdown` and
`memory.readword` from the loaded namespaces moved `pass` 190 → 188 with
`fail=0, skip=0, RESULT: CONFORMS`, and **no skip line named either**.

The bitter part is that this is the *repair* for a real bug.
`CLAUDE.md:236-242` records the original: the same question answered in two
places, which parted company when `gui.size` moved onto a method table. The fix
— route both through `api.implements()` — removed the disagreement by removing
the second opinion. `conformance.lua:199-202` even documents the reasoning. A
tautology is the one thing worse than two owners, and it is the exact shape of
`ok(not present or true, ...)` that `CLAUDE.md` §1 already records.

**Change.** The independent source already exists in the tree: `spec.lua`
declares the surface with `function ns.name(...)`. Have the check compare
`api.implements(name)` against **the spec's declarations**, not against
`supports()`. That is a real second opinion with a real owner, and it makes L12
below fall out for free. **Effort: an hour or two.**

## L4. A floor on how much of the suite ran

**flycast.** `conformance.lua:1469` / `:1483` — the verdict is `fail == 0`.
Nothing asserts that a minimum number of checks ran. The suite's own header
(`conformance.lua:79-81`) records the incident this rule exists for:

> `[MEASURED 2026-09-07]` this suite printed "RESULT: CONFORMS" while running
> 39 of 222 checks.

The fix chosen was per-check `pcall` so a throw becomes a FAIL rather than a
silence — correct, and it landed in the most recent commit
(`972b58c fix: a check that throws now FAILS instead of vanishing`). But it
covers **throws only**. A check that never ran because `needs()` skipped its
group, or because a name is absent from the hand-maintained list at
`conformance.lua:49-51`, still costs nothing. `conformance.lua:44-48` names that
second failure mode explicitly and does not gate on it.

nbneo's equivalent: the Lua runner treats **a spec registering no tests, and a
test body asserting nothing, as FAILED**, and reserves **exit 2 for a VACUOUS
run** (`nbneo-rr/tests/README.md`).

**Change.** Record the expected check count per group; fail when the total falls
below it. Print `ran N of M checks` in the verdict line. **Effort: an hour.**

## L5. `ui.*` silently does nothing when a config flag is off

This is the answer to the question in the brief, and it is the more serious of
the two symptoms named there.

**What the spec says.** `emuapi/spec.lua:98` — *"A function that silently does
nothing is worse than one that admits it does not exist."* `:187` — *"Never a
function that silently does nothing."* `:545-547` — *"ANY primitive that can
silently do nothing must expose how often it did something."* And flycast's own
`LUA_TODO.md:113-115` states the same rule as resolved tier 3.

**What the code does.** `[MEASURED 2026-09-07, at 2762188b3]`
`grep -c 'if (!config::ShowTrainingGameOverlay)' core/lua/lua.cpp` = **15**. The
guard sits at the top of `beginWindow` (:752), `endWindow` (:766), `uiText`
(:776), `uiTextRightAligned` (:784), `uiTextColor` (:793),
`uiTextColorRightAligned` (:801), `uiSameLine` (:810), `uiSameLineAt` (:828),
`uiSameLinePlaceholder` (:849), `uiSameLinePlaceholderRightAligned` (:857),
`uiBargraph` (:865), `uiBargraphColor` (:873), `uiButton` (:883), `uiRect`
(:1444) and `uiLine` (:1455).

**The guard is applied to half of one namespace.** The ImGui-passthrough family
is *not* guarded — `uiBegin` (:1310), `uiEnd` (:1320), `uiCalcTextSize` (:840),
and the Checkbox / Selectable / Slider / InputText / SetNextWindow* / GetMousePos
/ IsMouse* group between them. Both families are bound into the **same `ui`
namespace** (`lua.cpp:2080`), and `uiText`/`uiButton`/`uiSameLineAt` are bound
under **both** their ImGui name and their legacy name — `Text` at :2083 and
`text` at :2106 are the same C++ function, `Button` at :2085, `SameLine` at
:2086.

**The reachable symptom.** `config::ShowTrainingGameOverlay` defaults `true`
(`core/cfg/option.cpp:176`) but there is a user-facing button that flips it —
`core/rend/gui.cpp:1051-1055`, labelled *"Training Overlay On/Off"*. With it
off, `emuapi/components/pianoroll.lua` draws a titled window
(`ui.Begin` :134, unguarded) containing separators (:155, :188, unguarded) and
clickable cells (`ui.Selectable` :181, unguarded) — and **no frame numbers, no
column headers, no labels, no status text** (`ui.Text` at :141,153,160,163,169,
190,192,195,198, all guarded), with every cell collapsed onto one line because
`ui.SameLine` (:162, :171) is dead too. No error, no log line, no fault counted
on the handle.

That is precisely the failure the delivery counters were introduced to catch,
and they cannot see it, because delivery is not what stopped.

**Two aggravating details.** `uiButton` (`lua.cpp:883-896`) `return 0`s when
gated, so its Lua return arity changes and the click callback it was handed
never fires; and its `luaL_checkstring(L, 1)` sits **after** the guard (:888),
so tier-1 argument validation is itself contingent on a config flag.

**The adapter does not cover for it.** `emuapi/adapters/flycast.lua:536-543`
copies the names through in a loop. `grep -rn ShowTrainingGameOverlay` over
`~/dev/emuapi` returns **zero hits** — emuapi does not know this exists.

**Change, in preference order.** (a) Delete the 15 guards; the flag's job is to
hide the *training overlay*, and the script's own `gui.register` handle is what
turns a script's drawing off. (b) If the flag must keep gating scripts, then it
gates the *whole surface at the callback* — do not dispatch `overlay` at all
when it is off — so a painter that runs is a painter that draws. (c) Worst case,
make it a declared capability: `emu.supports("ui.Text")` answers false and the
call raises. **What is not acceptable is the current state**, and it is not a
judgement call: three lines of the project's own spec forbid it by name.
**Effort: an hour for (a) or (b).**

## L5b. `ui.IsMouseClicked` being 1-based: the verdict

The brief asks whether this and the silent no-op are symptoms of one pattern.
**They are not.** L5 is a defect. This one is a known collision between two of
the spec's own rules, correctly recorded and still open — and it is worth saying
so plainly, because treating it as sloppiness would lead to the wrong fix.

`emuapi/spec.lua:193-202` — *"Every INDEX a script sees is 1-BASED: players,
axes, slots, ports [...] an emulator's internal base is its own business, but it
MUST NOT leak."* `core/lua/lua.cpp:1414-1415` implements it deliberately, with a
comment saying so — *"Mouse buttons are 1-based here, per the interface's
indexing rule; ImGui's own are 0-based"* — and `checkMouseButton` (`:1416-1421`)
raises on 0 rather than answering false.

Against that, `spec.lua:987-992` records the cost, `[MEASURED 2026-09-07]`:

> This profile advertises ImGui's own names precisely so a script author can
> reuse what they know, which is what makes the off-by-one a trap: following
> ImGui's docs raises on every frame. It cost a test run today. **Treat 1 as
> left until an adapter normalises it.**

**No adapter normalises it.** `emuapi/adapters/flycast.lua:536-543` copies the
three mouse predicates through in a name loop with everything else. So the
spec's stated remedy has not been applied, and the trap is live.

**And it is untestable.** `conformance.lua`'s indexing group checks player 0/99,
axis 0 and slot 0 — `grep -n IsMouse emuapi/conformance.lua` returns **nothing**,
and `adapters/mock.lua:722` stubs all three to `false` regardless of argument
(`if n:sub(1, 7) == "IsMouse" then return false end`), so a
conformance run could not catch a host that got this wrong in either direction.

**Change.** Pick one and write the choice into `spec.lua` as settled: either the
`ui.*` profile is declared **ImGui-native** for arguments (names *and* bases,
one sentence, and the 1-based rule explicitly does not reach into it), or the
adapter shifts and the host stops being the thing a script sees. Then add the
conformance check that can tell the two apart — which requires the mock to
answer differently for button 1 and button 2. **Effort: an hour, and it is a
decision more than a change.**

## L6. `uiButton` can call `std::terminate`

**flycast.** `lua.cpp:213-217` states the rule, and states it correctly:

> For bindings registered as a raw `lua_CFunction`. Those are called straight
> from Lua with no wrapper, so a thrown exception unwinds past the interpreter
> and reaches terminate instead of becoming a catchable Lua error. `luaL_error`
> is the correct mechanism there.

`[MEASURED 2026-09-07, at 2762188b3]` Of the 13 `static int ui*(lua_State *L)`
bindings, **12 use `checkDrawContextL` and one does not**: `uiButton` at
`lua.cpp:885` calls `checkDrawContext("uiButton")`, which **throws**
(`lua.cpp:207-211`). Calling
`ui.Button` from outside the overlay callback therefore unwinds a C++ exception
through the Lua interpreter.

**Change.** One line: `checkDrawContext("uiButton")` →
`checkDrawContextL(L, "Button")`. **Effort: minutes.** Worth a `--sabotage` arm
per L1 so it cannot come back.

## L7. `eventCallback` reads `L` before taking the lock

**flycast.** `lua.cpp:116-122` documents the hazard and gets it right:

> L IS READ UNDER THE LOCK, NEVER BEFORE IT. This runs on the emulation thread
> while `term()` runs on the main thread, so an unlocked null check only proves
> L was non-null at some point in the past — `lua_close` can land between that
> check and the first use of the state.

Forty lines later, `eventCallback` (`lua.cpp:162-175`) does:

```cpp
if (L == nullptr)
    return;
lock_guard lock(mutex);
```

Inverted. This is the path `overlay()` uses (`lua.cpp:225-228`), dispatched from
the **render thread**, which is exactly the cross-thread case the comment above
describes.

**Change.** Swap the two lines. **Effort: minutes.** I have not reproduced a
crash from it — it is a narrow race on teardown — so treat this as `[SOURCE]`,
not `[MEASURED]`.

---

# Tier 2 — high value, about a day each

## L8. An observation stream with provenance

**Sibling.** `lemalta/lemalta/runlog.py:113-123` — one JSON object per line,
`sort_keys=True`, **flushed per record**:

```
{"f": <ms since run start>, "run": <run id>, "t": <dotted tag>, "d": {...}, "ts": <epoch s>}
```

The run id is `<kind>-<UTC timestamp>-<pid>` (`runlog.py:175-183`), and the pid
is there because *"a run id that collides makes `correlate.py --diff` compare a
run against itself and report a reassuring 'identical'."* Observability can
never kill the run: a non-JSON value is repr'd rather than raised
(`runlog.py:65-80`), an unopenable path warns once and no-ops (`:99-104`), a
write error closes the stream rather than raising per event (`:124-126`).

**The single best measurement lesson in any sibling** is `lemalta.py:4770-4784`:
`netcode.verdict` and `netcode.sync` are *separate records* because `verdict`
carries `advances`, which is wall-clock dependent, so diffing it made two
**passing** runs look divergent — *"a check that always fails gets ignored
exactly as fast as one that never does."* `netcode.sync` carries only
deterministic fields.

**flycast.** `test.ps1:183` writes
`{ranAt, game, results:[{Clip, Result, Detail}]}`. No commit SHA, no build id,
no numbers, no per-check granularity, no ordering, and only on the Windows TAS
path. The Linux side writes nothing machine-readable at all.

**Change.** Port `runlog.py`'s envelope, **and add the field lemalta itself
lacks** — its own cross-repo proposal
(`~/dev/anita/lemalta-crossrepo-status-proposal.md`) wanted commit provenance
and never shipped it (`status` is still registered with no arguments at
`lemalta.py:7688`). Emit `git rev-parse HEAD` and the build's mtime in every run
header. Then apply the verdict/sync split: keep wall-clock-dependent numbers out
of any record you intend to diff. **Effort: a day.**

## L9. Wire the harnesses to a gate, and give them one entry point

**flycast has good harnesses that nothing runs.**
`shell/linux/integration-tests` is the best test artefact in the tree — full
RUN/PASS/FAIL/SKIP contract, exit 2 on skip, pixel-level non-vacuity — and
`[MEASURED 2026-09-07]` no CI workflow references it, and there is no `add_test`
anywhere.

**Sibling.** `fbneo-rr/dev-scripts/spec_suite.sh` is one command that runs six
harnesses and prints one table, with `--quick` to drop the slow two-peer runs,
and the INCONCLUSIVE verdict from L2. Its comments carry the reason for each
line — e.g. the run-ahead A/B *"Proves it engaged before comparing, or the A/B
is vacuous"*, which is the same rule `scripts/replay-bindings-test.sh:6-9`
discovered independently.

nbneo's stale-binary defence belongs here too
(`nbneo-rr/tools/CHANGELOG.md:27-38`): `[MEASURED 2026-09-06]` deliberately
killing the P1 input latch left the conformance gate **green three times** —
the sabotage was real, the harness binary was nineteen hours older than the
library. Fixed by making every harness a CMake target. *"A gate that silently
tests a stale binary is worse than no gate."* flycast is exposed to exactly this:
`scripts/replay-bindings-test.sh:22` defaults `BIN` to
`../build-dojo7/flycast` with no check that it is newer than `core/`.

**`scripts/testrun.sh` is now the entry point for the Lua half** — it runs every
`scripts/tests/*.lua` in its own process, in parallel, and exits 0 only if all
PASS. What is still missing is (a) everything that is not a Lua test:
`integration-tests`, `run-conformance.lua`, `replay-bindings-test.sh`; (b) the
**stale-binary assertion** — `testrun.sh:37` takes `BIN` from the environment
and only checks `-x`, never that it is newer than `core/`, which is precisely
nbneo's three-times-green incident; and (c) any gate at all — nothing in
`CMakeLists.txt` or `.github/workflows/` runs it.

**Change.** One `scripts/suite.sh` above `testrun.sh` in the spec_suite shape,
adding the three non-Lua suites with per-line INCONCLUSIVE and one summary
table. Assert `$BIN` is newer than the newest file in `core/` before running
anything. Then `add_test` so `ctest` is the gate. **Effort: half a day now that
the runner exists.**

## L10. Poll conditions; kill by group; capture evidence before teardown

`docs/TEST-TOOLING.md` **already recommended all of this** (B5, and the
`ResourceStack` / `_wait_until` rows in its "Copy, don't reinvent" table), and
**`scripts/testrun.sh` has now adopted it** — deadline polling, `kill -INT`
against the process group (`:123`), evidence copied before teardown, all with
the reasoning in its header (`:11-33`). That header is the best-written thing in
`scripts/`; it is `nbneo-rr/tools/README.md`'s row format applied to one file.

**So this item is now about the harnesses that were not rewritten.**

`[MEASURED 2026-09-07]` `grep -n 'sleep\|pkill\|kill' scripts/isotest.sh
scripts/replay-bindings-test.sh`:

- `replay-bindings-test.sh:72` — `PID=$!; sleep 35; kill "$PID"`.
- `isotest.sh:152,159,190,206` — `sleep "$secs"` before every screenshot.
- `isotest.sh:221` — `pkill -f "Xvfb $DISPLAY_NUM"`, a **command-line match**.
  lemalta's rule (`lemalta.py:2128-2137`, quoted in `TEST-TOOLING.md`): *"every
  wrong kill this tool has made came from reading the command line."*
- Every kill is a bare SIGTERM, which per `TEST-TOOLING.md` §2 discards buffered
  stdout, and none is a process-group kill.

lemalta's replacements, for reference: `kill_group` polls `_group_alive` at
50 ms for 5 s before SIGKILL (`runtime.py:156-183`), with the comment naming the
code it replaced — *"`killpg(TERM)`, `sleep(0.8)`, then `killpg(KILL)`. 0.8 s is
a guess."* `ResourceStack` (`runtime.py:190`, `:346-365`) unwinds LIFO on
success, exception, `sys.exit` **and** SIGINT/SIGTERM/SIGHUP.
`_capture_evidence` runs **before** teardown, deliberately
(`lemalta.py:5183-5188`) — *"nuke() below takes the windows with it."*

flycast's `isotest.sh` already gets the hardest part right — `iso()` at
`isotest.sh:47` strips `I3SOCK`/`SWAYSOCK`/`WAYLAND_DISPLAY`, which is the
incident lemalta's `_i3_env` (`lemalta.py:2369-2381`) exists for. The polling
and scoping is the unfinished half.

**Change.** Do not write a second `wait_for`. Either move these onto
`testrun.sh`, or lift its polling/teardown block into a sourced
`scripts/lib/run.sh` that all three share — one owner per mechanism, which is
`CLAUDE.md` §4 applied to shell. The `pkill -f` at `isotest.sh:221` should go
regardless; it is the one line here that can kill something that is not ours.
**Effort: half a day.**

## L11. Docstrings next to the binding, and a generated reference with `--check`

**Sibling.** `nbneo-rr/CLAUDE.md:655-680`: `@doc` on the line above a binding,
`tools/gen-api-docs.py` writes `docs/API.md`, `--check` is a **ctest gate that
fails on a stale doc**, `--coverage` reports what has no `@doc` yet. The
argument: *"The binding and its description move together or they do not move at
all. A description one directory away is one that gets forgotten in the commit
that changes the behaviour."*

**flycast.** `[MEASURED 2026-09-07]` `grep -c '@doc' core/lua/lua.cpp` = **0**.
The API is described in three places that can disagree — the comments in
`lua.cpp`, `LUA_TODO.md`, and `emuapi/spec.lua` — with nothing comparing them.

The rot is already visible: `core/lua/lua.cpp` refers to **`docs/lua_api_spec.lua`
four times** (`:412`, `:899`, `:1897`, `:2079`, plus `emuapi/examples/tour.lua:46`)
and **that file does not exist**. `emuapi/README.md:238` records that it became
`emuapi/spec.lua`; the C++ was not updated. This is `CLAUDE.md` §4's
"one owner per fact" failing in the quiet direction.

**Take the design from nbneo, not from fbneo.** fbneo-rr has the same
marker-based idea (`@luadoc`, `tools/gen-lua-api-docs.py`) and it is **provably
broken right now**, in three ways that are each a lesson:

- **No `--check`, exit code always 0, and nothing in `makefile`/`build.py`
  invokes it** — every reference is a comment telling a human to run it. All
  three doc artifacts share one mtime; nine binding sources are newer. Missing
  from both `LUA_API.md` and `lua-api-data.json`: `emu.capability`, `emu.can`,
  `emu.frame_confirmed`, `emu.is_resim`, `emu.resim_steps`,
  `emu.dropped_pokes`, `emu.resim_misses`, `memory.pokebyte/word/dword`, and
  the whole of `quark.*`.
- **`lua_quark.cpp` is absent from the generator's `FILES` list**
  (`tools/gen-lua-api-docs.py:29-53`) and carries no `@luadoc` markers at all,
  so an entire namespace is invisible to a tool whose whole job is to see it.
- **An unscoped regex invents functions.** `parse_luaL_reg`
  (`gen-lua-api-docs.py:227`) matches `\{\s*"(\w+)"\s*,\s*(\w+)\s*\}` over
  whole file text, so it picks up `s_regmap[]` in `lua_debug.cpp:44-49` and
  emits **28 phantom functions** — `cpu.a0`, `cpu.d0`, `cpu.pc`, `cpu.vbr` —
  none of them callable. Every `(undocumented)` entry in the shipped Markdown
  is one of these.

That third one is the same defect nbneo found in its own census parser and fixed
with a hand-checked control list (L1). **A generator needs its own non-vacuity
check**: assert a known-present binding appears and a known-absent name does
not, or it will agree with any parser including a broken one.

**Change.** Fix the four dangling paths today (minutes). Then adopt `@doc` + a
generator, and wire `--check` into the gate from L9 **in the same commit** — an
opt-in write behind an unchecked generator is exactly what fbneo has.
**Effort: minutes, then a day.**

## L12. The coverage denominator is hand-maintained and has drifted again

**flycast.** `emuapi/init.lua:329` `M.surface` is a literal array — the exact
shape `CLAUDE.md` §1 rule 2 already names as a caught defect
(*"Surface coverage read 54/54 while the adapter had never forwarded `ui.*`"*).
The repair was to add names to the list, not to derive it.

`[MEASURED 2026-09-07]`, against the **live** copy at
`~/dev/flycast-dojo/emuapi/`:

- `M.surface` holds **94** unique names.
- `emuapi/README.md:113` and `:141` publish **`82/83 implemented`** as
  `[MEASURED 2026-09-07]`. Stale by 11 — in a README whose surrounding
  paragraph is *about* stale denominators.
- **20 functions declared in `spec.lua` are absent from the denominator**:
  `emu.apiversion`, `emu.can`, `emu.capability`, `emu.supports`, `input.get`,
  `input.registerhotkey`, `memory.readbytesigned`, `memory.readrange`,
  `memory.registerexec`, `memory.registerwrite`, `memory.unwatch`,
  `movie.getreadonly`, `movie.play`, `movie.rerecordcounting`,
  `movie.setreadonly`, `savestate.create`, `savestate.registerload`,
  `savestate.registersave`, `sound.voice`, `ui.GetScale`.

  Note the second one on that list. **`memory.registerwrite` is the spec's own
  headline example of a capability query** — `spec.lua:95`,
  `if emu.supports("memory.registerwrite") then ... end` — and the coverage
  report has never counted it.

Command used:
```
awk 'NR>=329 && /^}/{exit} NR>=329' emuapi/init.lua \
  | grep -o '"[a-zA-Z_]*\.[a-zA-Z_]*"' | tr -d '"' | sort -u   # the denominator
grep -o '^function [a-zA-Z]*\.[a-zA-Z_]*' emuapi/spec.lua \
  | sed 's/function //' | sort -u                              # the spec
```

(The reverse direction — `emu.run`, `gui.box`, `gui.text`, `joypad.UNSET` and
four others in the denominator with no `function` line in the spec — is mostly
explained: `gui.*` are deliberately surface methods, documented at
`spec.lua:1051-1057`. **`emu.run` is not explained**; it is in the denominator
and in `init.lua`'s tier table but has no declaration in `spec.lua`, so the
primitive that decides whether `frameadvance` is legal is absent from the LSP
stub and the docs.)

**Change.** Derive the denominator from `spec.lua`'s `function ns.name`
declarations — `spec.lua` is already loaded as a doc-only module and returns a
table. Same edit as L3; do them together. Then delete the published figure from
the README or generate it. **Effort: half a day, shared with L3.**

## L12b. When `emu.declare{tier=}` gets teeth, the gate needs a mechanical audit

`DETERMINISM.md:206-220` names this as *"a hazard nobody has written down"* —
Lua is itself a sync risk during recording, a `mutator` script should be
**refused or recorded, never silently permitted**, and nothing implements it
yet. `LUA_TODO.md:250` has the declaration model resolved. `DETERMINISM.md:306`
lists "give `emu.declare{tier=}` teeth during recording" as the last open item.

**fbneo-rr built exactly this and it leaks.** `SpecLuaRequire` is its
capability gate, and `[SOURCE]` it has **four call sites in the entire binding
layer**: `lua_fba.cpp:80`, `lua_memory.cpp:337`, `:374`,
`lua_savestate.cpp:191`. Ungated, despite being in scope per `spec_lua.h:21`:
`emu.pause`/`unpause`, `savestate.save`, `fba.hardreset()`,
`memory.setregister`, **the entire `cpu.*` namespace** — where
`cpu.writebyte/word/long` are direct `SekWrite*` calls and `cpu.debugbreak`
halts the machine — and **the entire `quark.*` namespace**, where
`quark.loadstate` loads a full machine state.

That is the failure mode of any gate applied by hand at call sites, and it is
the same shape as `determinism.h:29-32`'s argument against enumerating flags:
*"the list is copied, then one copy gains a case and the others do not. One
predicate, used everywhere, means the audit is a grep."*

**Change.** When the tier gate lands, it does not go at call sites. Either it
wraps registration (every binding classified once, at the table it is bound
into, `lua.cpp:1434-2019`) or there is a startup audit in the shape of
`determinism::unclassifiedOptions()` — a binding that appears in no tier is
**reported, not defaulted to permitted**. flycast already built that exact
anti-drift mechanism once, for config options; this is the same problem for
bindings. **Effort: half a day, but it must be designed in, not retrofitted.**

## L12c. Two open questions fbneo answers badly and flycast has not asked

Both matter more once scripted input injection lands (`CLAUDE.md` §"Still TODO",
`LUA_TODO.md` Part 1b).

- **A runaway script. flycast has the same gap, measured.** fbneo-rr has no
  watchdog and no panic handler — `lua_atpanic` and `lua_sethook` appear only in
  vendored libraries, and `lua_core.cpp:166-172` records that the watchdog was
  *"removed entirely rather than adapted for LuaJIT."* Its own
  `dev-scripts/test_luajit_watchdog.lua` demonstrates the result: `while true do
  end` with no `emu.frameadvance()` locks the process.
  `[MEASURED 2026-09-07, at 2762188b3]` `grep -rn 'lua_sethook\|lua_atpanic'
  core/lua/` returns **nothing**. A `while true do end` in a `vblank` callback
  hangs the emulation thread with no recovery, and `deferred::drain()` will
  never run again because the frame loop it hangs is upstream of it.
- **Sandboxing. Same answer.** `[MEASURED]` `core/lua/lua.cpp:2171` is
  `luaL_openlibs(L)` — the full standard library, `io`, `os` and `package`
  included. That is currently load-bearing: `scripts/replay-bindings-test.sh`'s
  probe uses `io.open`, and `emuapi/run-conformance.lua:65` uses `os.exit`. So
  this is a **stated policy** to write down, not a bug to fix — but it needs
  writing down before `TEST-TOOLING.md`'s A3 (a `.lua` positional argument)
  turns a script into something a user can be handed.

---

# Tier 3 — structural, weeks, and worth deciding rather than doing

## L13. Per-directory `README.md` + `CHANGELOG.md`, organised by FEATURE

**Sibling.** `nbneo-rr/CLAUDE.md:641-649`. Every substantive directory carries
both: `core/`, `lua/`, `tools/`, `script/`, `tests/`, `observe/`, `timeline/`,
`traces/`, `shell/`, `annotations/`, `environmental_desires/`. The rule:

> Not by date, not by commit — git already has both, and neither answers the
> question a reader actually has, which is *"what does this directory do now
> that it did not do before, and why."* [...] A changelog written later is a
> changelog written from the diff, and the diff is the one thing that cannot
> explain itself.

The format is visible in `nbneo-rr/traces/CHANGELOG.md:1-50`: a feature-shaped
heading, a bolded *"What this directory does now that it did not before"*
paragraph, the decision and its date, the measured evidence, and an `[UPDATED]`
box where the entry was later revised.

`nbneo-rr/tools/README.md` is the artefact to copy first: **one row per tool,
and each row carries the tool's exit-code contract, its sabotage arms, and its
measured findings.** It is the densest index in any of these repos and it is
what makes L1 auditable rather than aspirational.

**flycast.** `core/README.md` is inherited from upstream and describes a
directory layout from 2016 (it lists `oslib` as "Audio drivers"; audio moved to
`core/audio/` — a staleness `CLAUDE.md`'s own header already flags). There are
no CHANGELOGs.

**Change.** Do **not** roll this out tree-wide; most of `core/` is upstream and
a CHANGELOG there would be a second copy of flycast's git history. Do it for the
directories that are this fork's own work: `core/dojo/`, `core/lua/`, `scripts/`,
`emuapi/`, `shell/linux/`. **Effort: a day to start, then per-change.**

## L14. One owner per quantity — `docs/FACTS.md`

**Sibling.** `nbneo-rr/docs/FACTS.md`. Its origin: an audit measured **47
distinct quantities appearing in 2+ files, 22 in 3+**, and found the state-blob
size circulating with **five different values across 20 files**. The one number
that stayed clean did so because *"`tools/state-bench` exists and every citation
names it."* The rule (`docs/FACTS.md:20-22`):

> **A `[MEASURED]` number with a named instrument does not go stale. A
> `[MEASURED]` number without one cannot even be refuted, so it stays.**

Usage rules: cite the *owner*, not FACTS.md. The "last observed" column is a
dated observation, **not the fact** — if it disagrees with the instrument, the
instrument wins.

And the sharpest distinction in that repo: its companion tool
`tools/quantity-owners.py` **reports and never gates**, because *"a gate here is
satisfied by deleting the corroboration"* (`docs/FACTS.md:29-34`). Knowing which
checks may gate and which must only report is a real design skill and flycast has
no instance of it yet.

The relay rule matters as much as the register (`nbneo-rr/CLAUDE.md:239-289`):
*"A number measured once, inside a report, may be quoted once — at the place it
was measured — and then must be RE-DERIVED or DROPPED."* One day produced three
counts for "the Lua surface" (215 / 278 / 118) answering three different
questions; the command fell off in transit and three later agents each reported
a discrepancy that did not exist. *"A number is a token; its denominator is not,
so the denominator is what falls off in transit."*

**flycast.** No register. And it has the problem already: `82/83 implemented`
(L12); `1,252 statics` in `SPIKE-machine-pool.md` with a caveat that it is not a
worklist; `27 MB` / `27,890,707` / `27,793,819` / `27,797,723` for the state blob
in one document; the capture bitrates in `ABC_TEST_NOTES.txt`. All have
instruments; none says so in one place.

**Change.** `docs/FACTS.md` with one row per repeated quantity and its
instrument. Start with the state blob size, the statics count, the surface
coverage, and the capture bitrate. **Effort: half a day.**

## L15. Split `core/lua/lua.cpp`

`[MEASURED 2026-09-07, at 2762188b3]` flycast: **one file, 2,258 lines** —
and it grew 100 lines during this survey. fbneo-rr:
`src/burner/lua/` is **20 files, 7,871 lines** — `lua_memory.cpp`,
`lua_joypad.cpp`, `lua_gui.cpp`, `lua_imgui.cpp`, `lua_savestate.cpp`,
`lua_movie.cpp`, `lua_error.cpp` and so on, with `lua_error.cpp`'s header
explaining *why* shared error plumbing earns its own file rather than living
with lifecycle state.

The monolith is where L5, L6 and L7 all hid. Two families of `ui` bindings with
opposite guard conventions sit 600 lines apart in the same file, and the one
binding using the wrong guard sits between them.

**Three things in fbneo's layer are worth porting with the split**, each with a
shipped bug behind it:

- **`g_inHiresRender`** (`lua_gui.cpp:106`, set at `:1564`/`:1573`) turned a
  documented-but-unenforced "callbacks only" contract into an enforced one after
  a D3D9 state-corruption bug turned the window white. flycast's
  `inDrawCallback` (`lua.cpp:192`) is the same idea and already better — it is
  `thread_local`, so the emulation thread always reads false. Keep it; note that
  it is one guard where fbneo needed three (`g_inImGuiRender` at
  `lua_imgui.cpp:158-161`, `g_luaThreadBusy` at `lua_core.cpp:460-462`).
- **`lua_core.cpp:103-137` is a 35-line warning that a `luabridge::LuaRef`
  captured inside a callback binds to the COROUTINE, not the main state.** That
  was a real shipped access violation, bisected to `cfc675dae`, and the fix was
  to drop `LuaRef` for callbacks entirely (`lua_callback.cpp:1-20`). flycast
  uses LuaBridge `LuaRef` in exactly that position — `emuEventCallback`
  (`lua.cpp:129`), `eventCallback` (`lua.cpp:168`) and `uiButton`
  (`lua.cpp:891`, `LuaRef::fromStack(L, 2)`). flycast's host has no coroutines
  today, so the hazard does not bite; `emuapi/adapters/flycast.lua:869-894`
  synthesises `frameadvance` **with a coroutine**, on the Lua side. Worth an
  `[OPEN]` line and a test before anyone moves that machinery into C++.
- **A deferred UI-lifecycle slot** (`lua_console.cpp:113-150`, drained at
  `run.cpp:471` between frames) because load/stop reset the VM. flycast's
  `core/deferred.cpp` already generalises this and is better: it is a **queue**,
  where fbneo's `g_pendingKind` is **a single slot that a later request
  overwrites**.

**Change.** Split by namespace, starting with `lua_ui.cpp` — which forces a
decision on L5's two families in the process. **Effort: a day, mechanical.**

## L16. The statics tiering — decide, do not drift

`docs/SPIKE-machine-pool.md:380-402` already reached the right conclusion:
option 1 (in-process instances, nbneo-style) is months; options 2 and 3
(serialize as the pool primitive, process-per-machine) work today and compose;
and *"nbneo is the place that question is being answered properly, and flycast
can watch that result rather than pay for it twice."*

What the spike explicitly did **not** do is the tiering: the 1,252 figure is a
raw symbol count against nbneo's already-filtered 320.

The transferable part is not the refactor, it is **the tier table**
(`nbneo-rr/CPS2_POOLING.md:1063-1071`): five tiers are five *different reasons*
one copy is right, and each row names **when it stops being true** —
`host` (nothing in the machine reads it → until something on the trajectory
does), `shared` (every machine is the same game → until a pool holds two),
`unreachable` (no write can change its initialiser → until a real writer
appears), `bound` (relocated per machine → until the scheduler interleaves),
`input` (`step`'s argument, not its state → until something reads a latch across
frames). `nbneo-rr/docs/FACTS.md:413-418` keeps `unreachable` and `shared`
separate on purpose: *"the two die on different days and a merged tier could not
say which."*

**Change.** Not the tiering. Write down, in `SPIKE-machine-pool.md`, that the
decision is "process-per-machine, revisit when nbneo's result lands", so the
1,252 does not sit in the tree as an open worklist. And if the tiering ever
starts, produce the census **as a re-runnable script with a pinned output file**
from day one — `nbneo-rr/tests/statics-census.txt` opens with the contract that
makes it work: *"A LINE LEAVING THIS FILE IS THE WORK. A line arriving is a
regression."* The failure that pin exists to prevent is on the record: the
"1,283 driver statics" figure appeared in **26 files**, gating a standing "do
not start" verdict, and its only provenance was one sentence with no script
behind it (`nbneo-rr/docs/FACTS.md:375-395`).

## L17. Two working copies of emuapi, and one of them is stale

`[MEASURED 2026-09-07]` `~/dev/emuapi` and `~/dev/flycast-dojo/emuapi` are two
checkouts of the same remote (`git@github.com:NBeing/emuapi.git`). The vendored
one is **ahead**: `972b58c` at 17:38 vs `16be384` at 16:16, and `spec.lua`,
`conformance.lua`, `adapters/flycast.lua` and `init.lua` all differ.

Concretely: `spec.lua` is 1,143 lines vendored against 999 standalone;
`conformance.lua` 1,486 against 1,139. A survey reading `~/dev/emuapi` gets line
numbers and findings that do not correspond to what flycast actually loads.
(One of this investigation's own subagents did exactly that, which is how the
drift was found.)

This is not a defect in flycast so much as a workspace hazard, but it is the
same shape as `CLAUDE.md` §4 and it will keep costing time.

**Change.** Either delete `~/dev/emuapi` and treat the vendored copy as the
working tree, or add a one-line check that the two agree.

**And the check already has a model in the tree**: `emuapi/hosts/agnes/Makefile`
carries a `verify-vendor` target that sha256-checks the vendored agnes core
against a pinned `UPSTREAM` manifest, with the reason on the line above it —
*"A seam that quietly grew INTO agnes.c is the one change that would cost this
host the property it was adopted for."* The same shape, one level up, answers
"is the emuapi flycast loads the emuapi we think it is."
**Effort: minutes.**

## L18. Lua linting

fbneo-rr ships `.luacheckrc` (declaring the engine's global namespaces so real
typos are not drowned in false positives — its comment explains exactly that)
and `lint-lua.sh`, which lints `scripts/` and `dev-scripts/`.

`[MEASURED 2026-09-07]` flycast-dojo has neither, against roughly 4,000 lines of
Lua in `emuapi/`, `scripts/lua/` and `core/lua/flycast.lua`.

**And copy the mechanism, not the file — fbneo's has drifted.** Its
`.luacheckrc` header names `src/burner/luaengine.cpp`, **a file that no longer
exists**, and instructs the reader to cross-check against it; `read_globals`
(`:20-35`) is missing `imgui`, `canvas`, `config`, `socket`, `gekko`, `objtap`,
`cpu`, `sound`, `quark`, `print` and `tostring`, so existing dev-scripts get
false undefined-global warnings today. The intent of that header comment is
right and the list under it rotted, for the same reason as everything else in
Tier 1: **nothing runs it automatically.** Derive `read_globals` from the
binding registrations, or gate the lint, or it will rot here identically.

**Effort: an hour.**

---

# Two corrections to claims already in this tree

Both are the doc-rot class L11 and L14 are aimed at, found while cross-checking.

**`DETERMINISM.md` contradicts itself about FMA.** `:221` heads a section
*"FMA — fixed, and it was already half-solved upstream"*, and describes the fix
(the guard now uses `determinism::isDeterministicRun()`, plus `refreshCodegen()`
for the baked-block hole). `:285-289`, sixty lines later, lists FMA under
*"What no manifest can fix"* and closes *"**Not addressed on this branch.**"*
The later text is the stale one.

**Five source references point at a file that does not exist.**
`core/lua/lua.cpp:412`, `:899`, `:1897`, `:2079` and
`emuapi/examples/tour.lua:46` cite `docs/lua_api_spec.lua`. It is now
`emuapi/spec.lua`; `emuapi/README.md:238` records the move, and the C++ was not
updated with it.

---

# What NOT to copy

- **lemalta's locking, as documented.** `locks.py` and
  `lemalta/CLAUDE.md:29` both say locks cover "the exe symlink, the ini, the
  instances JSON". `grep 'locked('` over that tree returns **two** call sites,
  both on the netcode harness. The doc describes a design; the code implements a
  narrower one. (The *idea* is still worth taking: flock a **sidecar**, never the
  target, because *"a flock held on an unlinked inode protects nothing"* —
  `locks.py:26-32`.)
- **lemalta's screenshot verification.** Size floor only. flycast's pixel
  standard-deviation check is strictly better; do not regress to it.
- **fbneo-rr's single-slot callback registration.** Already rejected in
  `LUA_TODO.md:223-249`, correctly.
- **fbneo-rr's `LUA_API.md` as a model for documenting the surface.** nbneo
  measured it missing 30 of 443 registered names. That is L11's argument for
  generating the reference, not for writing one.
- **A gate on a "one owner per quantity" report.** `nbneo-rr/docs/FACTS.md:29-34`
  — a gate there is satisfied by deleting the corroboration. Report, never gate.
- **fbneo-rr's Lua error policy.** Five conventions chosen by dispatch path, and
  the difference is documented nowhere (see the "does better" list, item 6).
  flycast's single convention is the right one; do not let a new binding family
  introduce a second.
- **fbneo-rr's index bases.** They are inconsistent *within single files*:
  `sound.mailbox()` returns 1-based (`lua_sound.cpp:90`) while `sound.voice(i)`
  takes 0-based (`:59-61`), thirty lines apart; same split in `objtap.all()`
  (`:101`) vs `objtap.get(i)` (`:81-83`). Documented per function, never as a
  rule. flycast's 1-based-everywhere rule (`emuapi/spec.lua:193-202`) is the
  better design **and it is being followed** — the `ui.IsMouseClicked` case the
  brief asks about is not a symptom of drift, it is a known, recorded, still-open
  collision between two good rules. See the note below.
- **fbneo-rr's `joypad.get(which)`.** The `which` argument *is read nowhere*;
  the table is keyed by FBNeo input-name strings. An inherited FCEUX-shaped
  signature that its generated docs then propagate as truth. The general lesson
  is L11's: a generator that reads registrations rather than behaviour will
  document a lie faithfully.

---

# Suggested order

1. **L6, L7, L17, and the two corrections above.** Under an hour, all of it.
2. **L2** — three edits, one convention. Under an hour.
3. **L5** — decide which of the three options, then an hour. **L5b** is a
   decision to record, not a change to make; make it in the same sitting.
4. **L1** — three sabotage arms on the three existing suites. Half a day, and it
   is what makes every item above it stay fixed.
5. **L3 + L12 together** — derive the denominator from `spec.lua`, and the
   capability check falls out. Half a day.
6. **L4** — a check-count floor. An hour.
7. **L9** — `scripts/suite.sh` plus `add_test`, with the stale-binary assertion.
   A day.
8. **L8, L10, L11, L18.** A day each, any order. L11's `--check` lands in the
   same commit as the generator, never after it.
9. **L12c** — two sentences of stated policy. Ten minutes, and it is owed before
   `TEST-TOOLING.md`'s A3.
10. **L13, L14, L15** — structural, and worth doing while the tree is still small
    enough that the CHANGELOGs are short.
11. **L12b** — designed in when the tier gate is designed, not retrofitted.
12. **L16** — write the decision down; do not start the tiering.

---

# What this survey did not verify

- **Nothing was built or run.** L5's symptom (the piano roll rendering with
  `ShowTrainingGameOverlay` off) is read out of the guard sites and
  `pianoroll.lua`'s call list, not observed. L6's `std::terminate` and L7's race
  are `[SOURCE]`, not `[MEASURED]`.
- **L3's empirical confirmation came from a subagent**, and it ran against
  `~/dev/emuapi` — the stale checkout of L17. I re-verified the *code* in the
  live copy (`conformance.lua:203-205`, `init.lua:167-170`) and the tautology
  holds identically there, but the "190 → 188, still CONFORMS" run itself was on
  the stale tree.
- **Every nbneo and lemalta line number is quoted from a checkout I did not
  build**, and nbneo's own convention says to treat its numbers as dated
  observations with the quote, not the line, as the citation. Where I quote, the
  quote is the citation.
- **The `fbneo-rr` binding-layer claims come from a delegated sweep**, not from
  my own reading of `src/burner/lua/*.cpp`. I read its `CLAUDE.md`,
  `dev-scripts/spec_suite.sh`, `dev-scripts/determinism_smoke.lua`,
  `.luacheckrc`, `lint-lua.sh`, `src/burner/lua/lua_error.cpp` and the directory
  listing directly; the error-policy table, the index-base inconsistencies, the
  `SpecLuaRequire` call-site count, the 28 phantom functions and the doc-mtime
  comparison are that sweep's, unverified by me. They are consistent with
  everything I did read, and none of them is load-bearing for a flycast change —
  they are the "do not copy this part" half of the argument.
- **fbneo-rr's determinism ENGINE was not surveyed.** Only
  `determinism_smoke.lua`, whose own header is the interesting artefact: it
  records a **FAIL as the expected, informative result**, documenting the
  contract replay must honour (re-feed the exact input bits per frame; render
  with `pBurnSoundOut` non-null) rather than being softened until it passed. That
  is `CLAUDE.md` §1's "never weaken a check to make it pass", applied at the
  moment the check first goes red. flycast has no equivalent artefact — a test
  that is red on purpose, with the reason in its header. Possibly worth one.
- **One determinism question I opened and closed favourably, for the record.**
  fbneo-rr's `dev-scripts/determinism_smoke.lua:19-21` and nbneo's
  `CLAUDE.md:88-91` both hit the same bug — the machine's trajectory depending on
  whether anyone was listening to the audio. flycast does **not** have it: the
  mute short-circuit at `core/hw/aica/sgc_if.cpp:1511` sits *after* channel
  stepping and `dsp::step()` (`:1505`), so only the final mix is skipped. And
  `config::DSPEnabled`, which does gate `dsp::step()`, is already classified
  sync-critical (`core/determinism.cpp:102`, `SYNC_SETTINGS.md:132`). Both
  correct; recorded so nobody re-derives it.
