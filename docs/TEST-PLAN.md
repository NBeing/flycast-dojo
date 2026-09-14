# The testing plan

`[2026-09-11]` Written after a measurement, not as a wish list. Every gap below
was found by asking the tree what it covers, and the numbers are reproducible:

    scripts/checks.sh                     # the whole suite, exits 2 on a SKIP
    grep -rl <symbol> scripts/            # what, if anything, exercises a thing

---

## The rule

**Write the claim before the code wherever the tier is cheap.** That is tiers 0
and 1 below — they cost seconds, and running them red first is what makes them
worth having. Tiers 3 and 4 are minutes per cycle, so the arm goes in first and
the implementation follows, which is most of the benefit at a bearable price.

This is not a preference. `[MEASURED 2026-09-10]` writing the hold-repeat claims
first found three things that writing them after would not have:

- **six of ten claims were vacuous** — every one a NEGATIVE claim ("fires
  nothing", "is harmless"), and a do-nothing stub satisfies all of those,
  because it cannot tell "correctly refused" from "never did anything". Green
  would have reported ten passing claims;
- **a semantic disagreement** — the implementation deferred the first repeat by
  one period and the claim said it fires at maturity. The test is the spec;
- **a float-boundary bug**, `100.6 - 100.5 == 0.09999999999999432`.

And `[MEASURED 2026-09-11]` the counter-example: `scripts/recordtest.sh` was
written carefully AFTER its feature, never registered in ctest, and had rotted
to a hard failure by the time anyone ran it again — while the 128-line
re-recording core it existed to protect had no coverage at all.

**A test that is not in `ctest` does not exist.** Registering it is part of
writing it.

---

## The tiers, cheapest first

| tier | what | cost | TDD |
|---|---|---|---|
| **0 — audits** | source-level consistency (`hotkeyaudit`, `configaudit`) | **0.1 s** | always red-first |
| **1 — self-tests** | 352 claims, 20 in-process suites, **no ROM** | **16 s** | always red-first |
| **2 — Lua** | `flycast.lua`, a booted emulator driven by script | ~250 s | arm first |
| **3 — input/UI** | `rolltest`, `statestest`, `hotkeytest`, `docktest` — real clicks and keys on a private Xvfb | 30–90 s each | arm first |
| **4 — round trips** | record → replay → compare; cross-process determinism | 5–10 min | arm first |

**`[MEASURED 2026-09-11]` Tier 1 cannot see a feature that is never called, and
that is not a theoretical limit.** The liveness watchdog shipped eight green
tier-1 claims while emitting no verdict at all against the defect it was built
for, because the check had been put in `mainui_rend_frame()` and in
single-threaded rendering that loop *is* the SH4 - a wedged guest never returns
to it. A pure unit proves a RULE is right. It is silent on whether anything
consults the rule, and it cannot see a thread at all. Every tier-1 suite whose
subject is wired into a running emulator therefore wants one cheap tier-2 or
tier-3 arm that asserts the WIRING through an observable the feature produces:
`flycast.livetest` is the pattern - one healthy load, assert the verdict exists
AND that nothing cried wolf.

Tier 1 is where most new work belongs and where it is nearly free. The pattern
that makes it possible is **a pure function over a value**: `roll_edit`'s
transforms take a `std::map` and return one, `movie::firstFrame` takes a map,
`HoldRepeat` takes its own `now`. None of them needs an emulator, so none of
them costs 90 seconds to check.

---

## Where we actually are

**19 ctest entries**, 8 of them `_can_fail` twins or carrying their own control.

**Covered:** the piano roll's edit model, the sequence library, savestate
anchors, bookmarks, the States wall, hotkeys and chords, the edit funnel,
docking, boot residue, cross-process determinism, the Lua interface and its
conformance suite.

**Not covered.** `[CORRECTED 2026-09-11]` the first version of this section
listed four subsystems and was wrong about three of them, because it asked
`grep -rl <symbol> scripts/` — whether a NAME appears in a harness — instead of
whether behaviour is asserted. Most coverage here lives in C++ probes and
self-tests that the harnesses only read a verdict from, so the symbol is never
in `scripts/` at all. A proxy again, and a bad one.

What the better question — *does a claim or a probe assert this?* — answers:

| subsystem | verdict |
|---|---|
| **the States panel's clicks** | **covered `[2026-09-13]`** — `flycast.statesuitest` aims a real mouse at a real generations cell and asserts the MODIFIER: plain click opens tags, Ctrl+click opens notes. Its paired arm drives the identical gesture with no modifier and requires notes NOT to open. It found two defects on its first run, below |
| **state liveness** | **covered `[2026-09-11]`** — `LIVENESS SELFTEST` (8 claims) for the rule, `flycast.livetest` for the wiring, and it was verified against the real wedge: Dead at +4.7 s on the auto-seek load, silent on a run with no load, Alive on a healthy one. Three arms, because "Dead after every load" satisfies the first two |
| **rewind + re-record** | **genuinely uncovered.** `LoadStateFrame` runs on every state load, so it is exercised; none of its RULES is asserted — no truncate on a WRITE load, a rewind is not itself a re-record, a state saved before a rewind below it is stale |
| the staged buffer | **covered** — `ROLLSTAGED SELFTEST`, 18 claims |
| undo / redo | **covered** — `ROLL ANCHORPROBE`, `PAINTPROBE`, `MASHPROBE` and `LIBPROBE` each drive a real edit through the funnel and assert `undo=yes` with restoration |
| generations restore | **does not exist.** There is no restore function; `StatesGenProbe` covers taking one. It is a feature to build, not a test to write, and calling it untested confused the two |

So there is ONE real hole, and it is the one that matters most: the re-record
rules. That is a smaller and sharper answer than the first version gave, and it
is the reason this section now says what instrument produced it.

**Exists but does not run.** `[CORRECTED 2026-09-14]` this list named
`isotest.sh`, `replay-bindings-test.sh` and `recordtest.sh`; all three are
registered now. A census of all 20 harnesses against every `add_test` found the
list had simply not kept up, and found two more that mattered:

- **`scripts/checks.sh --self-test`** — the gate that enforces "a skipped check
  is not a passing one" was itself run by nothing. The detector had the defect
  it detects. Registered as `flycast.checks_can_fail`.
- **`scripts/reprotest.sh --oracle`** — the only differential check against a
  second implementation, unregistered since it was written, which also left
  `scripts/tests/repro/oracle_probe.lua` as the only `.lua` in the tree nothing
  ran. Registered as `flycast.oracle`.

Still out, and ranked by what they guard:

- **`shell/linux/integration-tests`** — `docs/CROSS-PROJECT-LESSONS.md` calls it
  the best test artefact in the tree, and it is in no ctest and no CI workflow.
  Its `--fast` arm needs neither ROM nor display.
- **`scripts/openarm.sh`** — the `WILL_FAIL` replacement, sole judge of both
  known-open entries, distinguishing three verdicts with no coverage of that
  discrimination. A judge with no arm of its own is the specific thing
  `selftest.sh`, `livetest.sh` and `checks.sh` each had to learn.
- **`scripts/lib/hotkeys.sh`** — parses hotkeys out of emulator log text for
  two harnesses and has no self-test. If its parsing silently returns empty,
  those harnesses press nothing and blame the feature.

`stepprobe.sh` is deliberately out — its milliseconds are a fact about the
machine. Note the rationale covers the timings and not its two `exit 1` shape
assertions, which are what `docs/STEP-GRANULARITY.md` reasons from and which
nothing checks.

---

## The order of work, and why this order

> **`[2026-09-12]` THE BLOCKER IS GONE.** Everything below was written while a
> state load could wedge the emulator, which is why `recordtest` could not be
> registered: every re-record claim rests on loading a state and playing on, and
> that did not work. Root cause found and fixed - `handle_cb` clears a scheduled
> event's deadline for the duration of its callback, so a savestate written from
> a vblank hook records a machine whose raster is switched off, and only the
> raster can re-arm the raster. `spg_RepairSchedule` rescues those states on
> load; `docs/GDB.md` has the four readings that closed it. The seek-then-play
> path now runs a clip to `replay end at frame 10007 (movie exhausted)`.
>
> `recordtest` is unblocked - its replay phase now completes, where before it
> wedged. `[CORRECTED 2026-09-12]` the sentence that stood here said the rest was
> "a harness question rather than an emulator one". Wrong, and the correction is
> the useful part of this entry: three real harness bugs WERE found and fixed
> (the anchor saved from inside a vblank hook; `join(1)` silently giving up at
> the 9999 -> 10000 collation boundary; collection starting before the auto-seek
> landed), and it still diverged. The remaining cause is in the emulator, and it
> now has a name and a test of its own - see the item below.

### 1. `[LANDED 2026-09-13]` `recordtest` is in ctest

    flycast.recordtest            PASS - 59 frames recorded and replayed to the
                                  same state, hash for hash, 100 distinct states
                                  in the window
    flycast.recordtest_can_fail   PASS - the sabotaged anchor was detected

**It records from a RESTORED machine, not a continuing one**, and that change is
what unblocked it rather than any further emulator work. The earlier shape
demanded that a restored machine agree with one that kept running - which is 1a,
still open. What a re-record tool actually needs is weaker: load an anchor, apply
inputs, get the same result every time. That was already true before any of the
2026-09-12 work (58/58 frames, measured), so the test had been waiting on a
property it did not need.

The sabotage arm goes through the ARTIFACT: record normally, replace the anchor
with the seed state, require the hashes to diverge. A judge that has stopped
reading the file cannot pass both arms.

Three harness defects had to be fixed first, all found only by being able to run
it at all - see the git log for 2026-09-12/13: the anchor was saved from inside a
vblank hook; `join(1)` silently stopped pairing at the 9999 -> 10000 collation
boundary; and both phases began sampling before their state load had landed.

### 1. Original plan — land `recordtest` in ctest

It is the only harness that records, and it is the vehicle for everything in
section 2. `[MEASURED 2026-09-11]` reviving it found two things, one fixed and
one not:

**FIXED — a movie that starts late read as a movie that had ended.** A clip
recorded from a savestate has frames numbered from where recording began
(9948–10007 here). Replaying it asked `movie::has(0)`, got false, declared the
movie finished one second in, then **wedged**: `AutoSeekState` waits for
`GuiState::Closed` and `ReplayEnd` never gives it back. `movie::has()` answers
false for "before the start", "a hole in the middle" and "past the end" alike,
and the caller meant only the last. `movie::firstFrame()` and `beforeStart()`
now separate them, and the playhead seeks instead. Verified: with the auto-seek
out of the way the clip replays to `movie exhausted` at 10007.

**`[FIXED 2026-09-12]` the auto-seek's state load left the guest spinning.** Root
cause: `sh4_sched`'s `handle_cb` clears an event's deadline for the duration of
its callback, and `Emulator::vblank()` runs inside `spg_line_sched`, so a state
saved from a vblank hook records a raster that is switched off - and only the
raster can re-arm the raster. `spg_RepairSchedule` repairs such states on load;
`docs/GDB.md` has the four readings that closed it. The elimination table below
is kept because every row in it was right, and because the last row of reasoning
drawn from it was **wrong**: "the restored state is INTERNALLY INCONSISTENT
rather than mis-restored" - it was neither. The state was faithful and the
scheduler was off, and the guest's loop around `0x8c191c90` was it waiting for a
vblank interrupt that could never arrive. A correct measurement, a wrong
inference from it.

**ORIGINAL DIAGNOSIS, kept for the eliminations:**
`[OPEN]` With `AutoSeekState=0`, the movie index stops advancing after the load
and never moves again. What it is NOT, each measured rather than assumed:

| ruled out | evidence |
|---|---|
| the load threw | `gui_loadState` traces `running yes -> stopped yes -> restarted yes` |
| the emulator is stopped | `emu.running()` is true; `gui_loadState` completed its restart |
| a deadlock | **197% CPU, main thread 99.4% in `R`, 30 threads** — it spins |
| threading | identical with `rend.ThreadedRendering` on and off |
| the movie | with `AutoSeekState=-1` the same clip replays to `movie exhausted` |
| `LoadStateFrame` | completes, takes its normal branch, logs the seek |

`[NARROWED 2026-09-11, gdb]` and now measured directly rather than inferred:

    break Dojo::MapleApplyAction if dojo.load_seq > 0     -> NEVER FIRES

`LoadStateFrame` bumps `load_seq`, so that breakpoint is live only after a load
and needs no timing. It does not fire once in ~117 s. **The guest performs no
further maple DMA after the state load** - it is not that the movie counter is
stuck while frames go by; the poll that would advance it never happens.

`[NARROWED AGAIN 2026-09-11, SH4 stub]` **the guest is in a normal game loop,
not waiting on hardware.** Sampled eight times while stalled, its PC stays inside
a ~2.5 KB range around `0x8c191c90`, and the disassembly reads only MAIN RAM
(`0x8c32…`) through an indirect call — no hardware register anywhere. It is
walking a structure in its own memory and never finishing.

So the restored state is INTERNALLY INCONSISTENT rather than mis-restored.
`STATE VERIFY: idempotent OK` proves save→load→save is byte-stable, which is a
claim about the serialiser and says nothing about whether the machine that was
saved made sense.

Identifying the structure needs game knowledge or a RAM diff against a state
that loads cleanly. `docs/GDB.md` carries the recipe, the disassembly, and three
gdb fixtures that measured nothing.

`[CORRECTED]` an earlier version of this said the next step was "how the
`Debug.GDBEnabled` option is plumbed". The plumbing was fine — gdb printed the
option object showing `value = true, overridden = true`. The SH4 debugger simply
**was not compiled in**: `ENABLE_GDB_SERVER:BOOL=OFF`. It is now ON.

    scripts/recordtest.sh          # records 60 frames from 9948, then hangs in replay

This is the same open question `scripts/tests/deferredslot.lua` records, where
three causes were eliminated (not refused, not the wrong slot, not a missing
file) without finding the fourth. What is new is a **reliable reproduction**,
which that note did not have. Until it is understood, recordtest cannot be
registered, and everything in section 2 waits behind it — which is why this is
the top item rather than the re-record claims themselves.

### 1a. `[OPEN 2026-09-12]` `cycle_counter` is two cycles out after a restore

The new blocker, and far sharper than the wedge it replaced.

    scripts/testrun.sh scripts/tests/open/replay_determinism.lua

Two passes in ONE process over the SAME movie frames, inputs from the same clip,
nothing different but the restore. They diverge - and the blob diff says the
divergence is **one byte out of 27,793,699**.

| | |
|---|---|
| where | `Sh4Context::cycle_counter`, SERMAP `sh4.cntx` + 308 |
| what | 188 vs 186 - two SH4 cycles of phase |
| everything else | identical, all 27.79 MB of it |

So this is not "the machine walks a different path". The machine is byte-for-byte
the same and its cycle budget is not. Still fatal for a re-record tool, because a
state hash is how every one of its claims is judged - but a very small target.

**Three things it is NOT**, each measured rather than argued:

| ruled out | evidence |
|---|---|
| a regression from the 2026-09-12 scheduler fix | the tree before that commit fails identically, the same two hashes |
| a race | the same two hashes on two different builds |
| the movie playhead (host state, not in a savestate) delivering different inputs | the test asserts the same `kcode` on every shared frame - 56/56 |
| restoring being lossy at reproducing | with `RD_BOTH_RESTORED=1`, two restores of the same slot are **56/56 identical**, so a restore reproduces itself perfectly - registered as `flycast.restore_determinism` and green |
| the dynarec's block boundaries shifting after `dc_loadstate` resets the block cache | `-config config:Dynarec.Enabled=no` diverges too, at frame 69, with its own hashes - and the flag was verified to apply, since `-config Dynarec.Enabled=no` without the section prefix is silently rejected |

**`[CORRECTED 2026-09-12]`** the first reading of that offset said **424**, inside
the 136 bytes of `u64 raw[64-8]` padding that no named field of `Sh4Context`
covers - which would have made this test a *proxy* failure and the emulator
innocent. A change to stop serializing that padding was written, built and
measured before the mapping was rechecked. It was wrong by 116 bytes: the SERMAP
block used came from a **different serialization** than the one
`savestate.hash()` uses. This tree emits several sizes - 27793147, 27793571,
27793687, 27934131, 36181651 and 44570871 all appeared in one run - so **match
SERMAP's `END` against the blob length before trusting any offset from it.** The
padding change was reverted; it fixed nothing.

**Which side is odd, and it is not the one the name suggests.** The
`RD_BOTH_RESTORED=1` control makes the two passes differ in *nothing* - both
restored from the same slot - and they agree 56/56. So the restore is not lossy
at reproducing. The odd one out is the **continuing** machine.

**BISECTED `[MEASURED 2026-09-12]`.** `dojo:PostSaveInvalidate=<mask>` applies
`dc_loadstate`'s invalidation list to the LIVE machine right after its save, so
the continuing machine ends up in the same condition as a restored one. Six runs:

| mask | entries | result |
|---|---|---|
| 255 | all | **56/56 identical** |
| 15 | custom_texture, ARM flush, `mmu_flush_table`, `bm_Reset` | diverges |
| 240 | memwatch, `mmu_set_state`, `ResetCache`, `KillTex` | **identical** |
| 48 | memwatch, `mmu_set_state` | diverges |
| 192 | `ResetCache`, `KillTex` | **identical** |
| **64** | **`sh4_cpu.ResetCache()` alone** | **identical** |

So on the dynarec the residue is the **block cache**: `sh4_cpu.ResetCache()` is
`bm_ResetCache()`, and clearing it on the continuing machine makes the two agree.
That is measured and stands.

**`[CORRECTED 2026-09-13]` THE MECHANISM WAS INVENTED AND IS WRONG.** This
paragraph claimed `blockmanager.cpp` charges `cycle_counter -= 100` when a block
must be found or compiled, and concluded that "JIT cache warmth is billed to the
guest's cycle budget". **There is no such charge.** `[SOURCE]` both `-= 100`
sites in that file sit inside `#ifdef USE_WINCE_HACK`, emulating Windows CE's
`GetTickCount` and `QueryPerformanceCounter` syscalls, and are reachable only
when `mmu_enabled()` - the function early-returns to `bm_GetCode()` otherwise.
Dreamcast titles do not use the MMU, so those lines never executed in any run
here. There is no other cycle charge anywhere in the dynarec path.

The bisect was right and the story told about it was not. **`[MEASURED
2026-09-13]` the cause is now confirmed from both directions**, which is as far
as measurement can take it without a mechanism:

| experiment | result |
|---|---|
| clear the cache on the CONTINUING machine (`PostSaveInvalidate=64`) | 56/56 identical |
| do NOT clear it on the RESTORED machine (`LoadKeepBlockCache=yes`) | 56/56 identical |

So the block cache is the whole of the dynarec-side difference, and nothing else
in `dc_loadstate` contributes. That rules out a correlation.

**It does not give a fix.** `LoadKeepBlockCache` is a DIAGNOSTIC and unsafe as a
setting: the cache maps guest PC to compiled code, and a state load replaces RAM
wholesale without going through the write path that normally invalidates it, so
keeping it leaves translations that may no longer match their source. That is
why `dc_loadstate` clears it. (Rollback is the interesting exception - `[SOURCE]`
ggpo.cpp calls `dc_deserialize` directly and never clears, which is presumably
judged safe because a rollback rewinds only a few frames.)

WHY a rebuilt cache changes cycle accounting is still **`[OPEN]`**. The leading
candidate is block BOUNDARIES: the dynarec accounts guest cycles per compiled
block, so a cache rebuilt from a different entry point can split code differently
and reach the same instruction having spent a different budget. Not measured, and
marked as the hypothesis it is.

**AND IT IS NOT THE WHOLE STORY** - re-measured `[2026-09-12]` after the seek
artifact in 1b was found, because the first version of this paragraph rested on a
run with no guard:

    interpreter + mask 255 (the WHOLE list):
      the measurement ran uninterrupted   restarted 0x, final run clean
      the two passes sampled the same instants   53/53 frames seen equally often
      a restored machine walks the same path     FAIL, frame 69, same two hashes

So with the SH4 interpreted, clearing every entry of `dc_loadstate`'s
invalidation list on the continuing machine does **not** close the gap, and this
reading is clean rather than seek-contaminated. `sh4_int_resetcache()` is an
empty function, so the dynarec's culprit cannot be the culprit here.

**THE INTERPRETER'S CAUSE, FOUND `[MEASURED 2026-09-12]`.** It is a host global,
and the same defect class as the dynarec's.

`[SOURCE]` `core/hw/sh4/sh4_cycles.h` declares `extern Sh4Cycles sh4cycles`, and
that object carries `lastUnit` and `memOps` **across instructions**. They feed
`countCycles()`, which feeds `Sh4cntx.cycle_counter`. Grepping `sh4_mmr.cpp` and
`serialize.cpp` for `sh4cycles` finds **nothing**: it is neither serialized nor
reset by `dc_loadstate`. So a continuing machine carries its own execution
history in it, and a restored machine carries whatever the process happened to
have at *its* load point.

Two measurements, and the first is the one that makes the second mean something:

| what | result |
|---|---|
| reset `sh4cycles` on the CONTINUING machine only (`PostSaveInvalidate=256`) | still diverges, **but pass A's hash changed** (`1811270343` -> `2422850379`) |
| reset it on BOTH sides (`+ dojo:LoadResetCycles=yes`) | **53/53 frames identical** |

The first is not a null result. Resetting only one side proves the global feeds
execution - the trajectory moved - while making the two differ *differently*,
because a load does not reset it either. Only with both sides reset at the same
point in the timeline do they agree.

**And the two causes are genuinely separate.** The dynarec arm with the *same*
flags still diverges at frame 71 with the same two hashes, so `sh4cycles` is the
interpreter's residue and `bm_ResetCache` is the dynarec's. Same class - host
state that bills the guest's cycle budget and is not in the blob - two instances.

`fastmmu.cpp` is a third instance waiting to happen: five mutable file-scope
scalars, no serialization, and `[SOURCE]` `cycle_counter -= 164` on a lookup
miss. Not measured; listed so it is not rediscovered from scratch.

What it is NOT, each measured with the seek guard in place:

| ruled out | evidence |
|---|---|
| `dc_loadstate`'s invalidation list | mask 255 diverges, frame 69, `restarted 0x`, instants 53/53 |
| `verifyLoadedStateIdempotent` perturbing the restored machine | `-config dojo:VerifyState=no` diverges identically |
| the seek artifact that produced the withdrawn 1b | `restarted 0x` on every run above |
| the two passes sampling different instants | 53/53 frames seen equally often |

**One defect, two causes, BOTH FOUND.** Note this is not the withdrawn 1b: that
was restore-vs-restore and measures clean at 70/70. This is
continuing-vs-restored, and it failed on both CPU cores for two different
reasons.

**The interpreter half is FIXED `[2026-09-12]`.** `lastUnit` and `memOps` are
serialized as `sh4.cycles`, savestate version **V49 (844)**, authorised as
pre-alpha work where a format bump is acceptable. Measured with no probe knobs
set: **53/53 frames identical**.

Compatibility, measured rather than assumed:

| direction | result |
|---|---|
| an old V48 state in a V49 build | **loads** - one run shows `ver 843` and `ver 844` side by side. The gate is additive; older states take a `reset()` branch |
| a V49 state in a V48 build (David's fork, upstream) | **refused** - `[SOURCE]` `if (_version > Current) throw Exception("Version too recent")`. This is the direction that breaks, until they take the change |
| David's states specifically | his `core/serialize.h` reads `V8 = 803, Current = V48` - the **same lineage**, so his states are ordinary V48 and load here unchanged |

**What a "port" of old states cannot do.** The pipeline state was never written to
those files, so no conversion can recover it: re-saving an old state at V49 only
records the `reset()` value. The practical consequence is narrower than it
sounds - an old anchor is perfectly usable *going forward*, because every load of
it now starts from the same known pipeline, so a clip re-recorded from it after
this change replays exactly. What will not hold is a clip recorded BEFORE this
change replaying hash-identically against its old anchor.

**The dynarec half is still open, and `[CORRECTED 2026-09-13]` it is NOT the
judgement call this section used to describe.** It said the fix was to stop
billing block lookup to `cycle_counter`, at the cost of changing SH4 timing for
every user. Both halves of that were built on a charge that does not exist - see
the correction above. There is nothing to remove, and therefore no timing
trade-off to weigh.

What is true: clearing the block cache on the continuing machine makes the two
agree (bisected to that one bit), and WHY is unknown. Measured after the V49 fix,
the dynarec arm still diverges at frame 71 with new hashes, so the two causes
remain independent.

`[CORRECTED 2026-09-13]` this paragraph said deciding what to do about the
dynarec one was a judgement call, weighed the `-= 100` against SH4 timing for
every user, and listed three options. All of it rested on a charge that does not
exist. There is no decision here and there never was - only an unexplained
measurement. Next step is to find what `bm_ResetCache()` actually changes about
cycle accounting, starting with whether recompiled blocks split at the same
boundaries.

### 1b. `[WITHDRAWN 2026-09-12]` "the interpreter is not reproducible at all"

**This item was wrong and is retracted the same day it was written.** It is kept
rather than deleted because the way it was wrong is worth more than the claim was.

It reported that two passes differing in nothing diverged at the first frame they
shared when the SH4 is interpreted, and offered four supporting measurements: the
inputs matched, the sampling instants matched at the divergence frame, disabling
the per-frame hashing changed nothing, and the result reproduced exactly - the
same two hashes across separate runs.

All four were true. The conclusion was still wrong, because a fifth thing was
happening that none of them could see:

    Saved  .../NoBGM_VMU_6.state            <- pass A's save
    Loaded .../NoBGM_VMU_6.state            <- pass A's restore
    Loaded .../NoBGM_VMU.state  27934131    <- slot 0. Nobody asked for this.
    Loaded .../NoBGM_VMU_6.state            <- pass B's restore

`[SOURCE]` `scripts/testrun.sh:254` passes `-config dojo:AutoSeekState=0` to
**every** Lua test, and that seek fires on **wall clock** - about two seconds in -
not on a frame count. On the dynarec it lands before this test's stages. Under
the interpreter the emulator is far slower, the same two seconds is far fewer
frames, and it landed in the middle of pass A, loading slot 0 and jumping the
machine somewhere else in the movie. Samples either side of that jump come from
two different timelines.

**Reproducibility was the trap.** A seek at a fixed wall-clock offset reproduces
perfectly, so "the same two hashes on separate runs" - which had been treated as
ruling out timing noise - was equally consistent with the artifact. What found it
was reading the emulator's own log for events *the test had not caused*, rather
than reading the test's verdict.

**Fixed in the harness, not worked around.** `AutoSeekState` cannot simply be
turned off: `-config dojo:AutoSeekState=-1` makes this test TIME OUT, because
without the seek the machine is still booting and never reaches steady playback -
the seek is how a test gets in-game at all. So the test now detects a backwards
jump in the movie index, which nothing else produces, and **restarts the whole
experiment**; each pass's own deliberate restore is excluded so it cannot
self-trigger. The dynarec arm reports `restarted 0x`, which is what says the
confound was interpreter-specific rather than everywhere.

**Re-measured with the guard, and the retraction is confirmed by a positive
result, not just by doubt:**

    restarted 1x after a seek; final run clean
    the two passes sampled the same instants   70/70 frames seen equally often
    a restored machine walks the same path     70/70 frames identical

The interpreter reproduces itself perfectly. It restarted once - exactly where
the seek used to corrupt the reading - and the clean run agrees on every frame.

**1a survives this.** Re-run with the guard: `restarted 0x` (so the seek never
touched that arm), sampling instants 56/56 equal, the same divergence at frame
71, the same two hashes.

### 2. The re-record claims, on top of it

Each of these is a sentence from the TAS fork's own help text or comments, and
each is a test:

- *"READ seeks the movie to that frame — WRITE rewinds there to re-record."*
- **A WRITE load does not truncate.** `[DONE 2026-09-14]` `scripts/recordtest.sh`:
  movie length **9963 -> 9963** across the load. Its precondition is asserted,
  not assumed - a READ load keeps the movie too, so "the length did not shrink"
  is satisfied by the wrong gesture. The record phase logs 1 `TAS: WRITE load`
  and the replay phase 0, which is the same grep with the gesture absent.
- **A rewind is not itself a re-record.** `[DONE 2026-09-14]` Both halves, in one
  run: a rewind plus IDENTICAL re-writes confirms **none**, and one run of six
  differing frames confirms **exactly one**, named at **9953** - the first
  differing frame, inside the driven window and not the rewind's frame (9949).
  Either half alone is satisfiable by the wrong build: count a rewind and the
  counter inflates on every seek; miss a divergence and the timeline has no event
  where the take changed, so stale anchors survive.

  `[MEASURED 2026-09-14]` the first attempt drove the divergence 40 frames after
  the rewind and confirmed nothing - **a divergence is only possible where a
  frame is being OVERWRITTEN.** `[SOURCE]` the detector needs
  `session_inputs.find(frame) != end()` AND differing bytes, so past the old
  take's last frame the recorder is appending and there is nothing to differ
  from. The test asked for a divergence in a region where one cannot exist and
  reported the emulator broken.
- **A state saved before a rewind below it is STALE** `[DONE 2026-09-14]` — the
  nastiest of the five, because the symptom is silent: such a state loads and
  verifies byte-perfect and the movie desyncs anyway. `scripts/recordtest.sh`
  saves a second anchor HIGH and before the rewind, then drives the divergence
  BELOW it:

      high anchor @9958: clean -> stale (divergence at 9953, below it)
      stale anchor loaded: 10049 -> back to 9958 -> ran on to 10048

  **Clean first, or the claim is untestable** - an anchor that was stale all
  along satisfies "it went stale" by itself. And "warn but ALLOW" is asserted
  too, because refusing to load a stale state would take the artist's own work
  away, which is worse than the desync it warns about. The still-loads check
  needs BOTH conditions: below where the load was asked from (the load moved the
  machine) and above the anchor (it ran on afterwards). One alone passes against
  a dead machine, the other against a no-op.
- **Loading an empty slot must not stop the emulator.** `[DONE 2026-09-13]`
  `scripts/tests/open/emptyslot.lua`. The claim HOLDS, on the direct path and on
  the deferred one through `gui_loadState`'s stop/start. A stricter property does
  not, and the test is an open arm for it: **a failed load mutates the machine**,
  at byte 284 of `Sh4Context` - `old_sr`, the saved status register - measured
  twice at the same context offset in blobs of different sizes. The instrument is
  controlled: four serializes with no load between them are byte-identical, so it
  is the load and not the hashing. `[SOURCE]` `dc_loadstate` returns before
  touching the machine when the file will not open, and `luaSavestateSlot` calls
  it with no stop/start around it - so the mechanism is not visible in the code
  and is recorded as open rather than explained.

The round trip that proves the lot: record a segment, rewind into it,
re-record different input, and require the movie to replay to the *new* hashes
while the frames outside the retry keep the *old* ones.

`[DONE 2026-09-14]` `scripts/recordtest.sh` is exactly that shape end to end -
record, rewind, drive a real divergence, replay - and asserts all of it in ONE
run:

    WRITE load: yes; movie length 9963 -> 9963
    rewind armed: 2; re-record events: 1
    divergence driven at frames 9953..9958; confirmed at 9953
    high anchor @9958: clean -> stale (divergence at 9953, below it)
    stale anchor loaded: 10049 -> back to 9958 -> ran on to 10048
    below the rewind: 00000000000000000 -> 00000000000000000
    inside the retry: 00000000000000000 -> 10000000000000000
    PASS - 59 frames recorded and replayed to the same state, hash for hash

**The last two lines are the half the hash comparison cannot see.** That
comparison requires the REPLAY to match the RECORDING, so a bug that corrupted
frames outside the retry would still pass it - both sides would carry the same
corruption. Only values captured BEFORE the edit catch that, and the inside pair
is their non-vacuity: if the retry rewrote nothing anywhere, "the frames outside
were left alone" is true and empty.

`[MEASURED 2026-09-14]` the sampled frame was first guessed as `first + 2` and
that was wrong: `taken` counts SAMPLES and the movie index REPEATS when the guest
does not poll maple, so the third sample landed on 9953 rather than 9951. The
test compared an untouched frame with itself and the whole claim read as "the
retry changed nothing" - exactly the vacuity it exists to rule out. It now reads
the old take in the same callback that drives the divergence, on the frame
actually being rewritten.

## Section 2 is complete `[2026-09-14]`

All five claims are asserted, every one of them a sentence out of the TAS fork's
own help text or comments. Four hold; the fifth - an empty-slot load - holds as
written and fails a stricter property, tracked as an open arm.

### 3. `[DONE 2026-09-14]` Generations restore — it existed, and had no way in

This item said the feature "does not exist. There is no restore function; it is a
feature to build, not a test to write." **`[CORRECTED 2026-09-14]` that is half
right and the wrong half.**

`[SOURCE]` `Dojo::RestoreClipDir` and `tas_clip::restore` are both declared AND
defined, complete with guardrails - and **nothing called either**. No UI, no
hotkey, no script. Compiled, linked, unreachable, which is the shape CLAUDE.md
opens with: `SaveStateFrame` compiled, linked and sat unreachable for several
commits while savestates silently carried no `.frame` sidecar. From outside,
"never built" and "no way in" are indistinguishable, and this plan recorded the
wrong one.

The engine is also better than a fresh one would likely have been: it moves
live-only files to `.trash/<utc>/` rather than deleting them, copies everything
except `clip.json`, then MERGES that so tags, notes and the sequence clock do not
regress to backup time.

**Reached via `flycast.replay.restoreGeneration(clipDir, genName)`**, gated at the
call site and asserted refused at observer tier in `scripts/tests/tiers.lua` - a
prefix in the tier table is not enforcement, each call site is.

It takes the directory EXPLICITLY because restore is pre-boot by design: a live
restore would leave the loaded movie, the replay writer, the rewind log, undo and
bookmarks stale in memory, and their next write would undo it. A no-argument
"restore mine" would always be refused and be useless.

`scripts/gentest.sh`, written first and red for the right reason (five failures,
`attempt to call a nil value`):

    PASS  the restore binding exists and returned          n=1
    PASS  the backup's state overwrote the live one        OLD
    PASS  the live-only orphan was TRASHED, not deleted
    PASS  ...and is gone from the clip directory
    PASS  restoring the OPEN clip is refused               n=-1

The guardrail arm needs a real clip OPEN. Without one
`hostfs::savestateFolderOverride` is empty, the refusal can never fire, and the
claim would pass for a reason unrelated to the guard.

**Still no UI.** The panel that lists generations is the States window, which
shows the OPEN clip - exactly the case restore refuses. A usable UI path is
pre-boot, in the clip browser, and is not built.

### 4. `[PART DONE 2026-09-14]` register `isotest` and `replay-bindings-test`

**Neither was dead code, and "delete them" was never really an option.**
`isotest` is cited by `CLAUDE.md` itself as the house rule for automated testing
that must not touch the real desktop; `replay-bindings-test` is referenced by
four docs and the fixtures README. Deleting a harness the doctrine points at
would leave the doctrine pointing at nothing.

**`replay-bindings-test` is registered `[DONE]`** as `flycast.replay_bindings`,
and the reason it had sat unregistered was mundane: it took three positional
arguments and **none of them had a default**, so it could not be invoked without
being told where everything was, while every other harness here discovers its own
fixture. It does now, preferring a clip for the ROM under test - a mismatched
clip fails in the safe direction (an unloaded clip leaves `replay_loaded` false,
so `startRecording` would be ALLOWED and the test fails) but a harness whose
verdict moves with whichever clip is newest is one nobody can read.

No paired arm: it already carries both halves. Its PASS requires the marker to
say `startRecording=false` **and** the clip-folder count to be unchanged, and its
own header records that "no clip folder was created" is also what "the Lua never
ran" looks like - a false pass that took four attempts to make honest.

**`isotest` is a TOOL, not a test, and that is the third option this item did not
offer.** `isotest.sh run <rom> [seconds]` boots, screenshots and exits; it has no
pass condition for ctest to judge. What IS testable is the guarantee it exists to
provide, and that is worth a self-test rather than trust:

`[SOURCE]` `assert_isolated()` refuses when the target display is the real
session - a real guard, and the one to assert. Its **other** branch is a no-op:

    if [ -n "${I3SOCK:-}${SWAYSOCK:-}" ] && [ "${ISOTEST_ALLOW_WM_SOCKETS:-}" != "1" ]; then
        : # iso() strips them per command; this just records that they are present
    fi

The protection there is real but lives elsewhere - `iso()` runs every command
under `env -u I3SOCK -u SWAYSOCK -u WAYLAND_DISPLAY`. So a function named
`assert_isolated` asserts half of what its name claims, and the half it does not
assert is exactly the hazard `CLAUDE.md` records as having moved the user's
Firefox once. **`[OPEN]`** - the self-test to write is the pair: the real display
must be refused, a private one must not be.

## Two disciplines that are not optional

**Every check must be able to fail, and be seen to fail once.** At tiers 0–1
that is red-first. At tiers 3–4 it is a sabotage arm — and the sabotage must
fail the claim it was written for. `[MEASURED 2026-09-11]` a sabotage failing
the WRONG claim is how an eleven-instance pipeline race was found: every matcher
was `log | grep -aq`, which under `set -o pipefail` reports 141 when the match
SUCCEEDS, depending on how big the log is.

**A skipped check is not a passing one.** `ctest` reports a skip as green and
exits 0. `scripts/checks.sh` exits 2 on any skip, and it caught a real
intermittent skip within hours of being written.
