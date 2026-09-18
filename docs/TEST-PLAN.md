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

- **`scripts/openarm.sh --self-test`** — the `WILL_FAIL` replacement and sole
  judge of both known-open entries, distinguishing three verdicts with no
  coverage of that discrimination. Now `flycast.openarm_can_fail`.
- **`shell/linux/integration-tests`** — now `flycast.integration`. Registering
  it required fixing it, and what it hid is the argument for this whole
  section. Three of its five cases had been failing and nothing ran them:

  - It set **`FLYCAST_ROOT`**, which nothing reads — `core/linux-dist/main.cpp`
    reads `XDG_CONFIG_HOME` / `XDG_DATA_HOME`. The emulator cases wrote their
    script where the emulator never looked, **and every run wrote into the
    developer's real `~/.config/flycast-dojo`**. The isolation was decorative,
    which is the rule in CLAUDE.md about not touching the real desktop being
    broken by the harness that models good practice.
  - **`( cd / && ... & echo $! )` captured the subshell's PID**, so every
    `kill -TERM` hit a process that had already exited and the emulator was
    never signalled. 23 orphans and a load average of 141 came from this.
    `pkill -x flycast-dojo` masked it while the binary carried that name.
  - The capture case waited a fixed 36 s for work that takes ~52 s here, and
    the video is finalised by an **external ffmpeg that outlives the emulator**.

  It also gained the ability to run one named case, which `--list` had always
  advertised and nothing accepted.

Still out, and ranked by what they guard:

- **`scripts/lib/hotkeys.sh`** — parses hotkeys out of emulator log text for
  two harnesses and has no self-test. If its parsing silently returns empty,
  those harnesses press nothing and blame the feature.
- **`scripts/stepprobe.sh`** — deliberately out for its timings, which are a
  fact about the machine. But the rationale does not cover its two `exit 1`
  shape assertions, which `docs/STEP-GRANULARITY.md` reasons from and nothing
  checks.
- **The stale-binary hole, which is not a registration gap but the same class.**
  `scripts/testrun.sh` takes `FLYCAST_BIN` from the environment and checks only
  that it is executable, never that it is newer than `core/`. **Every entry in
  the suite can pass against a binary that predates the change under test.**
  `shell/linux/integration-tests` defaults to `artifact/bin/flycast-dojo`,
  which on this machine was a week stale.

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

## 5. `[LANDED 2026-09-17]` The Surface Tour - the journey of journeys

Every harness above proves one feature's *infrastructure*. None walked the
surface the way a person does. The tour does, and it is **one run that is both an
automated test and a human-verified one**: `scripts/surfacetourtest.sh` scores it
headless; `scripts/surfacetourtest.sh --watch <clip.flyr>` plays the same run on
your real screen with an on-screen banner saying what is being tested, one step
per second, so you can watch it and agree.

**What it does, in order** (70 steps at the default; `dojo:TourSlow=yes` adds the
FST sweep and a branch export): load the game and David's savestate (slot 0,
BASE) and **step one frame to show it** - the whole tour runs Paused and a paused
renderer never presents, so without that step the screen kept the pre-load picture
(the user caught this watching: "the load state was never shown because of
pause"; a 1-frame show follows every load now, and is itself a frame-exact
claim); **rebind every window's hotkey through the real rebind engine** - fourteen
of them, `rebind::arm`, the engine's own 0.2 s arming gate, `rebind::tick`; **open
and close every window with those hotkeys**, interleaved so only one is up at a
time; then the features one by one - a roll edit and its undo, a States label
round-trip, save/load/delete of a scratch slot, the F2 cycle through the *live*
binding, the READ / READ-WRITE / WRITE driver, an Input Sender send and stop, a
Notepad analyze, a Snippet and a Macro placed through the edit funnel, a branch
created, checked out and left, a Test Lab test added and trashed, a capture
started and stopped. Then it puts everything back: every panel's open flag and
every binding are snapshotted first and restored last, so a run on a real config
ends exactly as it began - and it only ever runs against a *copied* clip in a
throwaway sandbox anyway, with `dojo:SavestateFolder` (new) pointing the states
at the copy.

**How it presses a key without a keyboard.** In-process, always: the tour calls
the keyboard device's own `gamepad_btn_input(code, pressed)` - the entry SDL
uses - so a rebind takes the real detect path and a hotkey the real dispatch,
headless and on a real display alike, and nothing is ever synthesised on `:0`.
That is the standing rule, and it is also why the same binary run is watchable:
there is no xdotool to keep off your desktop.

**The claims, and who backs them.** The tour scores itself
(`SURFACE TOUR: step n/N "<name>" -> PASS|FAIL|SKIP (<why>)` and a `RESULT` line),
but the harness does not take its word: it also counts the **engine's** own
traces - `PANEL TOGGLE: … -> open` (14), `HOTKEY REBIND: action … -> ` (14),
`gui_loadState: slot` (≥ 2) - so a tour that PASSed without the engine saying so
would fail. Every feature step is a hook that drives the real verb and *reads
back* the truth (a file, a frame, a mapping, a mode); each hook can be driven
alone with `dojo:TourHook=<name>[;<name>…]` and answers `TOUR HOOK: <name> ->
PASS|FAIL (<why>)`.

**Its arm.** `SurfaceTour=sabotage` injects the wrong chord on the very first
open step. Measured: `open: pianoroll -> FAIL (open=false captured=no)`, its
close SKIPs, every one of the 14 rebinds still PASSes, `opens=13`, `failed=1
skipped=1` - it reddens at the claim, not the setup. The twin exits 0 only on
exactly that shape.

**Measured on landing:** normal `passed=70 failed=0 skipped=0`, traces
`opens=14 rebinds=14 loads=5`, each show step `frame N presented`, ~3 min at
the human's 1 s bpm - and the same 66-step first cut passed 66/66 on the user's
real display before the show steps were added; `hotkeytest`
(real keys through the new 14-case dispatch) and `selftest` (the new
`SURFACE TOUR SELFTEST`, 12 claims on the pure step machine) still green;
`hotkeyaudit` 65 actions consistent. Two defects the tour found on its first
runs, both fixed: a harness that staged the clip outside `replays/<game>/`
made "macros: place" fail while the seeded macro sat right there (the Macros
browser scans that root - `macrostest.sh` already knew); and a PASS after a
polled verify printed the previous poll's reason.

**Built as three parallel tracks** behind one frozen header
(`core/dojo/surface_tour.h`): the runner, banner and the eleven new panel
hotkeys (`docs/HOTKEYS.md`); the 26 feature hooks, each compiled where its verb
lives; the harness, the `SavestateFolder` seam and these docs. No two tracks
touched a file in common; the header changed once, at the scaffold.

### 5.1 `[2026-09-17]` The oracle, the gate, and what a green tour does NOT mean

Before the next layer was written, a "do we already have this wheel" pass read
this tree, David's two trees, `~/dev/anita/nbneo-rr` and `~/dev/emuapi`, and
nbneo0909 (the session that built nbneo-rr's harnesses) answered six questions
about it. The result is that most of the layer is lifted or ported, and the
genuinely new pieces are two.

**The oracle - ONE header.** `core/dojo/oracle.h` declares `machineHash()`,
`movieHash()` and `machineStopped()`. It replaced six private fingerprints that
no C++ step could call: `sendequiv.cpp`'s `hashNow` (static; now MOVED here,
body verbatim, sendequiv calls it), `lua.cpp:1428`'s `hashState` (static,
Lua-only), the netplay MD5 in `nullDC.cpp`, `verifyLoadedStateIdempotent`'s
byte compare, `fsttest.sh`'s sha256-of-a-state-file, and `oracle_probe.lua`'s
FNV over guest RAM. `core/determinism.h:76-88` is the tree's own scar from
adding a duplicate. The rule the header keeps (emuapi `MODEL.md`): **a hash is
its DOMAIN, not its function** - `machineHash` is the bytes `dc_serialize`
writes and nothing else (not the movie, not the frame number, not lua's
trailer; same-build compares only, never keyed on, never persisted, read only
while stopped); `movieHash` is `Dojo::MoviePrefixHash` over every row,
inheriting its deliberate basis (`dojo.cpp:637-650`). The game-state oracle is
NOT wrapped: `mvc2.h`'s `peekCombo` / `comboPeak` / `mapValidated` /
`readRamSafe` already have one owner.

**The gate - ported, not designed.** Every step now carries a facts ledger
either side of its act (machine hash, movie hash, the panels' open mask, the
bindings, slot, mode, frame) and is judged by a port of nbneo-rr's
`flow_gates()` (`shell/gui/capture.cpp:30273-30404`, ~90 journey steps of
debugging behind it):

- **CHANGED is the TUPLE, not the hash alone** - a UI step legitimately leaves
  the machine alone and a mover legitimately leaves the windows alone, so the
  gate reads the pair (machine, movie) each step was declared to care about.
- **A mover that changed nothing is VACUOUS** (verdict overridden to FAIL: "a
  step whose verb did not fire"); **a UI step that moved the machine or the
  movie is a LEAK** (a chord, a panel or a mode flip reached the guest). Both
  directions are asserted - the false half catches the quiet leak.
- **Mover distinctness with declared convergence as an ASSERTION**, not an
  exemption: movers land on pairwise-distinct hashes except where a step
  declares it must converge (load slot 0 with the setup hash; a branch
  checkout with slot 0, because the branch's slot 0 is a copy) - and then it
  must actually converge.
- **A mover floor** - too few measured movers means the distinctness gate
  "compares nothing and passes by saying nothing".
- **nbneo0909's two-assertion rule:** a per-mover non-vacuity floor that CAN
  FIRE under the mode being tested (their own floor once could not, and "a
  guard that can't fire is worse than none"), and a distinctness assert
  across arms - two neutral arms then fail both.
- **Assert the guard fired FIRST** (`tools/bind-guard.cpp`'s inversion): a
  violation counter reading 0 is exactly what a guard that never ran reports,
  so the harness checks every settled step got a gate verdict before it
  believes any of them.

Expectation classes (the exact per-step table lives in `surface_tour.cpp`):
**mover** (loads, the 1-frame shows, captures, checkouts), **ui** (rebinds,
opens/closes, mode flips, labels, slot cycling, notepad, lab), **netzero**
(flip + undo: the movie ends where it began), **idempotent** (load slot 0 ==
the setup hash), **identity** (load slot 99 == the hash taken when it was
saved), **any** (a branch checkout's movie is a copy - asserted neither way,
and said so). Trace grammar, lifted from nbneo-rr's
`tests/transport-target-check.py`: per step `SURFACE TOUR: gate n/N "<name>"
machine=.. movie=.. expect=<class> -> ok|VACUOUS|LEAK|unmeasured`, a summary
block of `  ok  G1  <claim, measured count inlined>` / `  FAIL G3  <measured>
<why>:` with one indented offender per line, and an APPEND-ONLY RESULT line
(`... gate_ok= vacuous= leak= unmeasured=`). Exit codes, also lifted: 0 pass ·
1 a claim failed · 2 usage · 4 a sabotage failed to fire (the gate is
decorative) · **5 VACUOUS, its own code** - "0 failed" is also what a tour
that never ran looks like · 77 skip. `scripts/surfacetourtest.sh` prints its
eight gates in the same grammar and, against a binary whose RESULT carries no
gate fields, exits 5 rather than taking the tour's word.

**`[MEASURED 2026-09-17]` The WRITE clobber.** In WRITE, a frame-step
overwrites the row it advances INTO with neutral - the rule was already
written (`docs/tas-fork/CANON_readwrite_model.md:98`: "the stomp lands on the
frame you advance INTO"), named (`session::mode()`, `core/dojo/session.cpp:54`),
enforced (`core/dojo/dojo.cpp:582`) and ceremonially applied by `sendequiv`
(`sendequiv.cpp:160-186`) and the FST (`fst.cpp:417-458`). Nothing TESTED it.
The tour's four `show:` steps ran in WRITE after `driver: WRITE` and each
silently neutralised one movie row; the harness was green. The feature phase
now runs READ-WRITE (`gui_set_driver(1)` + `macro_armed`), the driver cycle
ends in READ-WRITE, and the show steps' expectation - machine moves, movie
does NOT - is what makes the rule a test: revert the fix and the gate reddens
those four steps as leaks.

**A CORRECT PICTURE OF THE WRONG THING.** Adopted verbatim from nbneo-rr
(`CLAUDE.md:169-172`) as the named failure mode: "not a crash and not a blank
- it is right, steady, and about something else, which is why review does not
catch it and a screenshot does not either." Two boundaries follow, both from
nbneo0909's ack of this style:

1. **A green tour certifies the SURFACE, never emulation correctness.** The
   verb did what the button does; the mapping, mode, file and frame read back
   true. A presented frame proves a frame was presented, not that it is the
   right frame. Game-level claims (state-hash A/B against a neutral arm from
   one base state, the MvC2 RAM oracle) are a DISTINCT claim class with their
   own grammar and arms, and a green tour is never to be quoted as "the
   emulation is right".
2. **Determinism is intermittent - sweep a window, not a point.** One green
   A/B is not evidence. `[MEASURED]` in nbneo-rr (`capture-scenes-check.py:150-158`):
   six runs of every scene found 12 varying; two runs of those same 12 found
   8. Harness #2 sweeps N >= 6 and reports how many pairs disagreed and the
   first divergence frame.

**Wheels we did not reinvent** (file refs are where the wheel lives):
`machineHash` from `sendequiv.cpp:126` · `Dojo::MoviePrefixHash`
(`dojo.cpp:651`, declared `dojo.h:221`) · the combo oracle `mvc2.h:60-71` ·
the gate from nbneo-rr `flow_gates()` and `FlowStepFacts` (`capture.cpp:444,
30273`) · the gate grammar and exit table from `tests/transport-target-check.py:104-107,
1778-1810` · the sabotage judge for the next item from emuapi `arms.lua:119-160`
/ `serve_arms.py:270-330` (three rules + INCONCLUSIVE, one shared copy) · for the
fixture item, David's 426 converted combo macros (`0915/flycast-rr/mvc2_data/*/
Combo_*_SS/*_macro.txt`), his `mcp/brute.py` (restore -> apply -> run ->
`score_peak` + `dropped_at`) and his `tas_test` per-frame combo/RAM series
(`0915/.../core/dojo/testrun.{h,cpp}`), plus nbneo-rr's fixture manifest
(`fixtures/vsavj/RECIPE.toml`, result pinned by TWO hashes because the picture
can be identical while the state is not) · for harness #2, `scripts/reprotest.sh
--runs N` + `scripts/tests/repro/hash_sequence.lua` (per-frame, guest-frame-keyed,
N runs, poke sabotage, empty-run SKIP), `replay_determinism.lua` (already names
the known 1-byte `cycle_counter` divergence), and `trace-offset-sweep.py`'s
first-divergence report shape.

**Genuinely new:** the oracle header; the gate port; later, a flycast
`buffers`-equivalent (self-describing named RAM blocks with extents, so a
divergence harness carries no addresses) and the combo-connects oracle with
its fixture. Nothing exists today that lands a combo: no clip, state, macro or
manifest on this machine records the combo byte >= 1, and David's own notes
say his PASS "verifies the infrastructure, not that hits connect".

### 5.2 `[LANDED 2026-09-17]` Sabotage per module - one restored defect per class

Item 1 had one arm (the wrong chord on the piano roll's open). One arm proves
one guard. This item gives every guard class its own arm and ONE judge, so a
guard that goes decorative is caught by name, and so the judge itself is the
same one emuapi already uses (`arms.lua:119-160`, lifted byte-for-byte into
`scripts/lib/arms.sh` - same three rules, same lines, same markers, so a reader
of either tree greps for the same thing).

**The arm rules** (surface_tour.h v2, the frozen contract):

1. An arm is a RESTORED DEFECT that shipped or was measured, never an invention.
2. It lives in the tour's step table or hook - NEVER behind a switch in a
   feature's shipping code (that would ship a way to turn the guard off).
   Hooks in other translation units ask `surfacetour::sabotaged("<cls>")`; one
   parser, in the runner.
3. Each arm names the ONE step it must redden and ONE control step that must
   stay green - and the control must have RUN (a SKIPped control did not run;
   arms.lua rule 3). The runner declares both on the log
   (`SURFACE TOUR: arm <cls> must_break="…" must_not_break="…"`) and lists what it
   knows (`SURFACE TOUR: arms known: …`); the harness checks its own list against
   that line and falls back to a table, loudly, only if a declaration is absent.
4. The machine/movie hash oracle has NO named sabotage - it is falsified by a
   hand edit, once, and recorded (§5.1).
5. Vacuity is not an input to the judge under an arm: a run that is supposed to
   be broken has no business reporting on its own coverage. What the GATE made
   of the arm is a separate, per-class check (below) - because the gate is the
   second instrument, and an arm that the gate files under the wrong verdict is
   an arm that only one instrument caught.
6. An arm makes its own precondition true - it never borrows it from the
   fixture's current state. `[MEASURED 2026-09-17]` `fixtures-check --sabotage
   recipe` claimed "a fake pin with no measured_on" but only faked the pin; it
   was green while `measured_on` happened to be empty, and the day the hunt
   filled it the arm broke nothing (ctest `can_fail_recipe`, exit 4). Both
   halves of the lie are now the arm's own doing.

**The classes** - `dojo:SurfaceTour=sabotage[:<cls>[+<cls>]]` (`+`, because the
`-config` parser cuts at the first comma; bare `sabotage` == `open`):

| class | where | the restored defect | must_break | must_not_break | gate signature |
|---|---|---|---|---|---|
| `open` | runner | wrong chord on the piano roll's open | `open: pianoroll` | `rebind: pianoroll -> Ctrl+F1` | vacuous=0 leak=0 (a FAIL, not a vacuity) |
| `rebind` | runner | press inside the engine's 0.2 s deaf window | `rebind: macros -> Alt+F6` | `rebind: snippets -> Alt+F5` | vacuous=0 leak=0 |
| `show` | runner | skip the 1-frame step, fake frame+1 | `show: the harness base state (1 frame)` | `load slot 0 (the harness base)` | **vacuous >= 1** `[MEASURED, Track A]` - a mover that moved nothing; G3 IS the second catch |
| `write-clobber` | runner | run the feature phase in WRITE | `show: slot 99 (1 frame)` | `savestate: load slot 99` | inconclusive-by-design on a fixture the phase does not clobber -> exit 2 |
| `gate-can-pass` | runner | NOTHING - the inverse arm | (none) | `open: pianoroll` | the WHOLE gate green: failed=0, vacuous=0 leak=0, gate_ok >= floor, no `FAIL G` in the runner's block; else exit 4 |
| `flip` | roll_panel.cpp hook | the UNDO is skipped while reporting success | `roll: flip a cell + undo` | `states: label round-trip` | **leak >= 1** `[MEASURED]` - see below |
| `label` | hooks | `setSlotLabel` skipped, the intended label reported | `states: label round-trip` | `roll: flip a cell + undo` | vacuous=0 leak=0 |
| `save` | hooks | the save lands in slot 98 while claiming 99 | `savestate: save slot 99` | `states: label round-trip` | vacuous=0 leak=0 |
| `branch` | hooks | `tas_branch::create` skipped, reported as done | `branch: create from slot 0` | `test lab: add test from slot 0` | vacuous=0 leak=0 `[MEASURED]` (the count read-back fails the step outright) |
| `base` | hooks | "F1 saves on press" - the tap step calls `gui_saveState()` instead of the key (the defect the BASE guard exists for; docs/PORT-DEFECT-CENSUS.md #17) | `base: tap blocked` | `base: hold writes` | the file-bytes + guard-counter read-back fails the step outright |

**The BASE guard, in the tour** `[LANDED 2026-09-17]`. Slot 0 is the state every
seek returns to, and dojo7 had no guard at all - F1 saved on press
(docs/PORT-DEFECT-CENSUS.md #17). Ported onto `HoldRepeat`'s sibling
`hotkeys::baseHold()` (`core/input/hold_repeat.h`): once slot 0 (or a branch fork
point, `tas_branch::forkSlots`) holds a state, a tap is BLOCKED and a hold of
`dojo:BaseHoldMs` (default 1000) writes, the release handled first and
unconditionally. Two tour steps drive the F1 KEY itself (`base: tap blocked`,
`base: hold writes` - polled, the captures-stop shape) and read the truth back two
ways, the guard's own counters AND the slot-0 file's bytes; each restores slot 0
from its own copy, so the `base` arm's control cannot SKIP when its target reddens
(`[MEASURED]` it did with `needsPrev`, and the arm was INCONCLUSIVE - rule 6 again).
Measured: 72/72, gate_ok=72; `tap BLOCKED, slot 0 bytes unchanged (fnv
d0cca4b6cf51f23b)`; `hold matured at 2010-2047 ms, slot 0 rewritten (d0cca4b6cf51f23b
-> 1ac7b3942bbdb65e) then restored`; arm `base` BEHAVED AS PREDICTED; `BASEHOLD
SELFTEST: 6 passed, 0 failed`.

**The exit convention is INVERTED for an armed run** (`scripts/surfacetourtest.sh
--sabotage <cls>`; `--self-test` == `--sabotage open`; `--list-sabotage` prints
the classes): **0** the arm fired as predicted (`SABOTAGE BEHAVED AS PREDICTED`) ·
**4** it FAILED TO FIRE - the target stayed green, the guard is decorative ·
**2** INCONCLUSIVE - the target never ran, or inconclusive-by-design
(`SABOTAGE INCONCLUSIVE`) · **1** it fired but BROKE ITS CONTROL (a demolition,
not a measurement) or the tally lies (`SABOTAGE DID NOT BEHAVE AS PREDICTED: N`)
· **5** no gate on the RESULT line · **77** SKIP, never confused with any of these.
ctest registers `flycast.surfacetourtest_can_fail_{open,flip,save,branch,gate-can-pass}`;
the rest run on demand.

**`[MEASURED 2026-09-17]` flip must skip the UNDO, not the flip.** The header's
one-line sketch says "build the roll edit, skip ApplyEdit". Measured against the
gate, that arm would be caught only by the hook's own `changed` read-back, and
the gate would see a step that moved nothing - a vacuity, the weakest verdict.
The runner's expectation for `roll: flip a cell + undo` is movie NET-ZERO and
machine unchanged. Skipping the UNDO instead (and lying `undone=restored=true`)
leaves the flipped row in the movie, the movie hash moves, and the gate reddens
the step as a leak - the log line is
`step 46/70 "roll: flip a cell + undo" -> FAIL (leak: the movie moved under a
step that must leave it alone)`, RESULT `failed=1 gate_ok=69 vacuous=0 leak=1`.
So for this class `leak >= 1` is the arm WORKING and the harness requires it;
`leak=0` would mean the row was undone after all, or the oracle is not looking
at the movie. It is the one arm whose signature is a leak rather than a FAIL.

**Measured, one line per arm, all against the runner's own declarations:**

- `open`: `step 6/70 "open: pianoroll" -> FAIL (open=false captured=no)`, close
  SKIPped, `failed=1 gate_ok=69 vacuous=0 leak=0`; 14/14 rebinds PASSED.
- `flip`: as above; the label round-trip (control) PASSED.
- `label`: `step 47/70 "states: label round-trip" -> FAIL (wrote=1 readback=0
  restored=1 back=1)` - the honest read-back exposes the lie; `failed=1 gate_ok=70
  vacuous=0 leak=0`; the flip+undo (control) PASSED.
- `save`: `step 48/70 "savestate: save slot 99" -> FAIL (no file at …_99.state)`,
  load/show 99 SKIP via needsPrev, and `step 70/70 "savestate: delete slot 99"
  -> FAIL (nothing to delete …)` - an honest downstream consequence, not a second
  arm; `failed=2 gate_ok=68 vacuous=0 leak=0`. No stray `*_98.state*` in the
  sandbox after the run: the thumbnail worker is drained (`tas_thumb::flush()`)
  before the stray is removed, because without that the `.state.png` landed a
  moment after the remove and the branch step copied it.
- `branch`: `step 61/70 "branch: create from slot 0" -> FAIL (branches 0 -> 0,
  create returned '')`, checkout/show/back/show SKIP via needsPrev; `failed=1
  gate_ok=66 vacuous=0 leak=0`. Side effect worth knowing: with the branch
  checkout and return skipped, `load slot 0 (the harness base)` loses its convergence
  partner and the runner's own G5b goes red (`gates_red=1`) - a true reading of
  a tour that skipped four steps, and not what the arm is judged on.
- unarmed: still `passed=70 failed=0 gate_ok=70 vacuous=0 leak=0`, all eight
  harness gates ok.

**Deviations from the header's sketch, both toward a stronger instrument:**
`flip` skips the undo, not the edit (above); `branch` reads back the COUNT of
`<clip>/branches/*` before and after the create rather than "verifying the root
exists" - a root that exists proves nothing about a create that did nothing.

### 5.3 `[LANDED 2026-09-17]` The combo fixture - built, because none was found

**What exists.** Nothing on this machine lands a combo. The audit found no clip,
state, macro or manifest that records `Combo_Meter_HitsToOpponent >= 1`, and
David's own notes say his harness PASS "verifies the infrastructure, not that hits
connect". David ships no DC savestates and no clips; what he ships is raw material
and a vocabulary: `SPREADSHEET.json` (DC-verified addresses, by name), ~25 DC-native
snippets in `roll_library`'s format (the `fastVS` boot seeds among them), the
charselect node graph (`charselect_nodes.json`, cardinals emulator-verified), and
444 PS2-converted macros that are CANDIDATES, never fixtures (122 are truncated).

**What is built** (`8abee22be`, `df37d12e8`, `7daea7e4a`, `7b0bac068`, `20d3ffeeb`;
the hunt itself is Track A's, `5de4a3875`/`1b3d9b901`):

- `core/dojo/combohunt.{h,cpp}` - the contract and RESULT grammar
  (`COMBO HUNT RESULT: found=yes|no candidate= phase= d= peak= base= after=`),
  emuapi's exploration loop on the FST's ceremony, swept over the four phases.
- `core/dojo/mvc2_data/SPREADSHEET.json` - the oracle's NAMES, md5-pinned; the
  combo byte is resolved BY NAME (`TAS MVC2: combo oracle by NAME - …`).
- `scripts/fixtures/mvc2/vmu_save_A1.bin` - the card, MADE by `fixtures-check
  --make-vmu` (131072 bytes, md5 `de5110…`; byte-deterministic across makes).
  **The fixture is the ROM AND the VMU** (below). It was David's card for one
  commit (`7b0bac068`, md5 `08baab93…`); a recipe replaced the artifact.
- `scripts/fixtures/mvc2/candidates/Combo_Dhalsim97_pcsx2_macro.txt` - David's
  PS2-converted candidate (6707 frames, `seqHashMacro` `2cbafba30a4c25fc`, markers
  4212-5699): a CANDIDATE by provenance, a fixture only because the hunt observed
  it connect here.
- `scripts/combohunttest.sh` - the hunt held against the RECIPE: H1 found=yes, H2
  candidate, H3 peak, H4 after hash, H5 base hash, H6 phase - every expected number
  READ FROM THE RECIPE, none typed in the script. Arm `window` (the pre-combo
  window 84-1500 must give found=no).
- `scripts/fixtures/mvc2/RECIPE.toml` - the fixture, pinned per nbneo-rr's rules:
  the ROM by its bytes (size + sha256; three copies exist, the name is not the
  identity), the vocabulary by md5 with the two addresses the tree hardcodes pinned
  by NAME, the savestate-free base by `seqHashMacro`'s own FNV-1a 64 over the
  `.txt`, the charselect expected sequence, and the result pinned by TWO hashes
  (machine hash AND the combo byte) plus the PHASE. **Every field the hunt has not
  measured says `"unmeasured"`**, and the never-regenerate rule is in the file.
- `scripts/fixtures-check.sh` - four claims, the surface-tour grammar, the
  `arms.sh` judge, exits 0/1/2/4/77:
  - F1 fastVS parity, NO emulator: `tas_macro::FromText` + `seqHashMacro`
    transliterated; `fastVS.txt` 633 / `4db6bf3690958f6b`, `fastVS_mcp.txt` 697 /
    `442574011190bd6b`.
  - F2 RECIPE honest: every pin is `unmeasured` or well-shaped WITH a
    `measured_on`; pinned addresses agree with the spreadsheet by name; the
    charselect ids agree with `ID_2`'s `Note2` enum; the unmeasured fields are
    LISTED on every run (10 the day it was written; 0 now).
  - F3 vocabulary: `SPREADSHEET.json` md5 == pin.
  - V1 vmu: the VMU image present, size + md5 == the `[vmu]` pins; F4 stages it into
    the sandbox's `<XDG_DATA_HOME>/flycast-dojo/` before boot.
  - F4 charselect, the first expected SEQUENCE on the machine: boot the globe seed
    (`dojo:OnEnterFile`), handoff pause, `set_mode READWRITE`, read `ID_2` at the
    spreadsheet-resolved address through ctlserver (RubyHeart 19), inject
    D,D,R,R by `input`+`step` (Venom 14), press D (Hulk 13) - David's verified
    `v_down`. Every verb is one `ctltest.sh` already proves.
  - `--verify-roms` (size + sha256, measured ok), `--regenerate` (refuses without
    `FIXTURES_REGENERATE=iknow`; rewrites only no-emulator pins, prints
    before/after), `--list-sabotage`, `--sabotage hash|recipe|charselect|vmu`.

**The honest state of each claim** `[MEASURED 2026-09-17]`:

| claim | state | measured |
|---|---|---|
| F1 parity | ok | both seeds and the candidate match the pins; `library.json` says `fastVS_mcp` is 633 / `4db6bf…` - a STALE INDEX (the file grew by LK x4 at 634..637, the stage pick). The RECIPE pins the file, not the index - this is the failure the check exists for |
| F2 honest | ok | 34 shape/name claims; 0 fields unmeasured (10 the day it was written) |
| F3 md5 | ok | `23c1827fc4fe3b04313ee8c944565b20` |
| V1 vmu | ok | `vmu_save_A1.bin` 131072 bytes, md5 `de5110ca408f30398762c1b32ad8881d` - made by `--make-vmu` (one Start on the create-save prompt; accepted only because F4 passed on it; two makes identical) |
| F4 charselect | **ok** | `ID_2 19 -> 14 -> 13 == RubyHeart -> Venom -> Hulk (… mode=READWRITE seed=742 frames @0x2C268341)` - the first time this tree predicted what the game would do and read it back. Two things had to land first (below): the `SeedOnEnter` wire and the VMU |
| arm `hash` | fired | F1 red, F3 green - BEHAVED AS PREDICTED (exit 0) |
| arm `recipe` | fired | F2 red, F1 green - BEHAVED (exit 0) |
| arm `charselect` | fired | `FAIL F4 … after D=13 (want 14)`, F1 green - BEHAVED (exit 0); INCONCLUSIVE until F4 could run |
| arm `vmu` | fired | `FAIL F4 charselect: never reached the globe - globe=0 (want 19) …`, V1 green - BEHAVED (exit 0) |
| the hunt | **found=yes** | `COMBO HUNT RESULT: found=yes candidate=Combo_Dhalsim97_pcsx2[4212-5699] phase=0 d=0 peak=19 base=27FA5D20 after=64FF89AB` (36 s); `combohunttest` H1-H6 all ok against the RECIPE |
| arm `window` | fired | window 84-1500: `found=no … peak=0` on all four phases, H1-H4/H6 red, H5 (the base hash) green - BEHAVED (exit 0, 112 s) |

Full `fixtures-check.sh`: `passed=5 failed=0 skipped=0`, exit 0. A run with any
SKIP exits 77, never 0, and `checks.sh` will say so.

**The `SeedOnEnter` wire.** `Dojo::SeedOnEnter()` was ported into dojo7 with its
whole handoff (fast-forward arm, the step-stop, the WRITE restore) and
`dojo:OnEnterFile` registered - and **nothing called it**. F4's first measured run
SKIPped on exactly that: the game booted and never logged `TAS ONENTER: seeded`.
David's `gui.cpp:1072` calls it in `gui_start_game` after the Play-Macro branch;
the coordinator wired the same, plus the handoff at the step-stop (David's `>=`
overshoot fix, ff arm/drop, WRITE restore). It now logs `TAS ONENTER: handoff at
frame N (WRITE|READ-WRITE)`.

**The VMU finding.** With the seed wired, F4 STILL failed. The seed presses Start
at frame 119; a screenshot at 118 showed the VMU prompt - "A Memory Card with 5
blocks … Press the Start button to create a file". A sandbox's fresh
`XDG_DATA_HOME` has an EMPTY VMU, the menu flow shifts by one screen, and every
seed press lands on the wrong screen. David's seeds presume a card that already
holds the MvC2 save. So **the fixture is the ROM and the VMU**: the card is in
the tree, pinned by bytes (V1), staged before every boot, and the `vmu` arm boots
without it and must produce exactly `never reached the globe - globe=0`.

**The hunt, measured.** From slot 0 of the tour's clip (frame 9928, Sonson(B) vs
Marrow(B), in a match, `skip=1/4`), David's Dhalsim97 window connects: peak 19,
end hash `64FF89AB`. **Phase-insensitive on this base**: Track A's `all` sweep gave
peak 19 on all four phases with four DISTINCT end hashes - the candidate is robust
to the skip phase here, and the four hashes say the machine really did differ. The
phase stays pinned because the RULE stands; this candidate just does not exercise
it. The pre-combo window (84-1500) gives peak 0 on all four phases, so the arm is
real.

**The phase caveat.** MvC2 skips every 4th frame (`peekSkip`, rate 4). A combo that
straddles a skip boundary connects on ONE of four phases - David measured "~1/4 of
the time". A fixture that does not pin its phase is ~75% flaky, so the hunt sweeps
all four and `[phase].value` is part of the result, not a detail of the run.

**The V48 caveat.** David's savestates are V48; this build serializes V49 (`843` vs
`844`, `serialize.h`). A V48 state loads but cannot round-trip, so a fixture may
never pin a state FILE's digest - it pins `oracle::machineHash` AFTER the load, on
this build. The charselect claim's base is a savestate-free boot snippet for
exactly this reason: no round-trip, no RTC, no ROM-identity ambiguity, no dead
timeline. The hunt's base is David's V48 slot-0 state (the seeds reach the globe,
not a fight; the combo needs a fight) - pinned as `27FA5D20`, the machine hash
after the load on this build, and the verify probe reports the version skew
(`843` vs `844`) rather than a serializer fault.

**Wheels not reinvented.** The hash is `seqHashMacro` (David's, byte for byte);
the parser is `tas_macro::FromText`'s rules; the transport is `ctltest.sh`'s
client; the judge is `arms.sh`; the fixture rules are nbneo-rr's; the exploration
loop the hunt runs is emuapi's; `combohunttest.sh` is Track A's smoke runner
dressed in this tree's conventions. The two things genuinely missing were a call
site and a memory card.

## 6. `[LANDED 2026-09-18]` Tour v4 - the intent modules: each feature asked what the FIGHTER did

The user, having watched the 77-step tour on his own display: *"nothing opened starting
on your roll tests... we don't have any real "meat" to these tests. each feature should
be tested with its INTENT."* Section 5's tour proved every window opens by its hotkey
and every verb changes the six-member tuple. Nothing asked the game whether the fighter
cared. Section 6 does, for eleven features, on ONE rule:

**Four beats per feature.** OPEN the window by the hotkey the tour rebound (the human
sees it). ACT through the feature's OWN verb - the panel's button body, never a hook
that bypasses it. INTENT - run the game and read what the fighter did: the combo byte
(`Combo_Meter_HitsToOpponent`, resolved by name, §5.3), the machine hash, a derived
state. CLOSE. The fixture is §5.3's: the tour clip's slot 0 as BASE and David's
`Combo_Dhalsim97` window as THE COMBO, which lands **peak 19** there (RECIPE.toml).

### 6.1 The rails (`784f6e171`, `35422af2c`)

- `core/dojo/intent.{h,cpp}` - the combo hunt's ceremony (§5.3) lifted out as named
  steps: `begin` (pause, WRITE-authoring, snapshot the roll, reload BASE) → `settled`
  (polled; base facts read once) → `placeCombo(phase,d)` / `clearCombo` (baked from the
  SNAPSHOT through `ApplyEditResize`) → `runToStop` (fast-forward to t0+len+60) → `peak`
  (while stopped) → `end` (the snapshot restored through the funnel, BASE reloaded).
  ONE implementation for three modules - arms.lua's lesson about three drifting copies.
- `surface_tour.h` v3 (additive): a `Module{steps, arms, expectations}` registered from
  its own TU. The runner appends module steps after `roll: redo the flip + undo`,
  consults module expectations before its prefix table, judges module arms like its
  own, lists them in `arms known`. `dojo:TourModules=all|none|<a>+<b>` /
  `TOUR_MODULES` picks which run.
- Three TUs, one per builder: `intent_clip.cpp` (branches, generations, test lab,
  captures), `intent_roll.cpp` (roll edit, undo/redo, macros/snippets, ruler),
  `intent_send.cpp` (sender/notepad, FST, state machine). Name order = run order.
- Harness: G9 reads the runner's own `gates_red` (`[MEASURED 2026-09-18]` a green
  108/108 carried `gates_red=1` and the harness said PASS - the runner's seven gates
  were never read); `TOUR_WAIT_S` 300 → 1500 (a 300 s budget killed the roll module at
  step 77 twice, and "the tour never reported" was the harness giving up).

### 6.2 What each feature makes the game do - measured

Every line below is a `SURFACE TOUR: step` line from the runs of 2026-09-18 (roll
108/108 gate_ok=108 gates_red=0; send 94/94; clip 122 with one expectation fixed).

| module | David's intent (his words) | measured |
|---|---|---|
| roll edit | "flip-twice restores the exact bytes" | place → `peak=19`; Blank the window through the roll → `peak=0`; panel Undo → `peak=19` |
| undo/redo | "an undo/redo is itself an edit" | Redo → `peak=0`; Undo → `peak=19` (`64FF89AB` every time with the hit, `A4E4A31C` without - the identity IS the hash) |
| macros | "State 0 = Macro Frame 0 (REQUIRED binding)" | the window placed at BASE+1 through the Macros panel → `peak=19` |
| ruler | "rows `x` (skip frame)" | `60 skip frames in 240 seen of 9929..10169 (rate 4 -> 60 expected)` |
| snippets | (drives menus) | placed through the panel, undone - a movie-mover; no in-match intent, said so |
| branches | "root .flyr byte-identical after a branch+checkout round-trip" | main `peak 19` → branch created/checked out → clear on the branch → `peak 0` → back to main → `root ef6fceef4a6fb4b2 unchanged, branch 0872beef968541d2` → main `peak 19` again |
| generations | "generations are immutable" | Shift+F8 → `tourclip_gen_01`; damage → `peak 0`; restore → `peak 19, hash 64FF89AB == the pre-damage hash` |
| test lab | "BASE is the permanent fixture" | `TEST_01` added from slot 0, bound (`test BASE at frame 9928, hash 27FA5D20`), the combo on its roll → `peak 19`; trashed |
| captures | "every emulated frame exactly once" | `1540 frames written for 1540 emulated (85 paused duplicates skipped)` |
| notepad | "text becomes inputs" | 60 rows rendered, `analyzed (60 frames, 0 errors), parsed back equal`; `tas_va2::SelfTest()` passed every spec vector |
| sender | "build a queue... then send it" | `1487 tokens through patternToCanon, live at 9929` → `the SENDER landed it: peak 19, after=7FA36D18` |
| FST | "which of the four phases connects" | `phases 0..3 peaked 19/19/19/19 - phase-insensitive on this base` |
| state machine | `Being_Hit = Knockdown_State==32 && Hitstop2>0` | `P2 entered Being_Hit on 32 of 1548 polls (first @10857, first hit @10857), P1 never before the hit` |

Findings, stated as findings: David's "connects 1 in 4" is Magneto on his base, not
this combo - four phases, four 19s, and a `macro-anchor` arm (the window 30 rows late)
was DECORATIVE for the same reason (dropped; `intent_roll.cpp` header). The sender's
`after=7FA36D18` differs from the placed combo's `64FF89AB` although both peak 19: the
sender is P1-only and skips the source's 20 P2-input rows - same hits, different
machine. Two hashes, one byte: "the picture can be identical while the state is not".

Honest limits: the test lab here is BASE-only (no roll→macro writer, no lab runner), so
`results.jsonl` / `peakP1` is unmeasured and the step says so; the generation restore
ceremony ("back up live first?") is a pre-boot UI in David's tree, not asserted; the
evaluator is v1 = the on-point character only; "the states REVALIDATE" is not measured.

### 6.3 Arms - eight restored defects, one per module claim

| arm | restores | must redden | control |
|---|---|---|---|
| `roll-clear` | the clear edits `session_inputs` directly (no funnel, empty undo history) | undo the clear | the combo lands |
| `macro-window` | the file's first rows placed instead of its CLIP window | the macro lands | the combo lands |
| `branch-leak` | the checkout skipped - the clear lands on main | main still lands after the branch | create a branch |
| `gen-noop` | the restore copies nothing | the combo is back after restore | Shift+F8 archives |
| `capture-dup` | `CapturePausedFrames=yes` - the pcsx2-rr duplicate bug | frames written == emulated | start the recorder |
| `send-noop` | the send skipped after the parse | the sent combo lands | notepad round-trip |
| `fst-phase` | the sweep at BASE+200 where nothing is placed | FST four phases | the sent combo lands |
| `state-field` | `Knockdown_State==31` | Being_Hit sampled | FST four phases |

Rule 6 (§5.2) held once more on the way: the first arm run died on `WHAT: unbound
variable` - the harness's per-arm label table did not know the eight, and under `set
-u` the judge never ran. Verdicts are in the run log this section landed with.

### 6.4 ctest

`flycast.surfacetourtest` = the 77-step surface (`TOUR_MODULES=none`, ~2-3 min);
`flycast.surfacetourtest_intent_{clip,roll,send}` one module each (`slow`, ~5-8 min);
`flycast.surfacetourtest_can_fail_<arm>` for the eight arms above, each with only its
module. The full 170-step tour is `TOUR_MODULES=all` by hand (~25 min) - and `--watch`
on the user's display is how it was first seen.

## 7. `[LANDED 2026-09-18]` The CSS tour - David's character-select utility on DC, and the base it authors

§6 asked every feature what the fighter did - on a base that was a HARNESS ARTIFACT
(`404543fcf`: the tour's slot 0 was written by `scripts/testrun.sh`, Sonson vs Marrow,
and the "combo" was David's Dhalsim97 input stream driving Sonson). The user: "we don't
have the right save state for this combo" - and there is none anywhere: David ships no
DC savestates, his `.p2m` movies are PS2 power-on input grids, and his menu timing does
not transfer (his whole macro booted from power-on lands in Training on the wrong cast
at his "beforecombo" frame). Then: "does David not have a char select utility?" He does.

### 7.1 The utility, and what transfers

`0915/flycast-rr/mcp/charselect.py` (verified by David on DC 2026-09-12): the select
globe as a vertical torus of rows [8,8,8,6,6,6,6,8] anchored at RubyHeart, vertical
moves straight in cols 0-5 and FOLDING col6->4 / col7->5 into a 6-wide row, horizontal
wrapping per row width; `plan(target)` = vertical in the start column then horizontal
in the target row; `build_picks(player, picks)` = for each of 3 picks, navigate from
HOME (the cursor resets after every lock), SELECT with the palette button, DOWN x assist
index, CONFIRM, wait out the ~60 f lockout - on a TIMING table {navgap 1, preselect 8,
postsel 24, astgap 12, preconf 12, postconf 72}; `merge_players` zips P1 and P2 into
one CE letter stream. **Measured on this build from power-on, first try:** fastVS_mcp +
those streams -> `ID_2 P1 37/23/52, P2 0/39/2` (Dhalsim/Cable/Sentinel vs Ryu/Ken/Guile),
Start at SPEED SELECT -> in-match, health 144/144, Dhalsim on point. So the utility
transfers whole - it is his MENU timing (the PS2 boot's Start-mashing) that does not.

The port is `core/dojo/css.{h,cpp}` (pure; CSS SELFTEST pins byte parity with the stream
his Python generated, `scripts/fixtures/mvc2/css/dhalsim_team_picks.txt`, 378 frames,
seqHashMacro `3b736c94ef884bbd`). The team is OURS, not his (`css/PICKS.toml` says why:
his files never name his team, and inferring it from his P2-lane cursor letters is
unreliable because his walk starts from a different menu state).

### 7.2 The tour (`core/dojo/css_tour.cpp`, module `css`; `scripts/csstour.sh`)

Boots with NO clip: `RecordMatches=yes MacroMode=yes OnEnterFile=css/seed_globe.txt
OnEnterHandoff=742` (fastVS_mcp + 45 neutral; the pause lands ON the globe). Two traps
designed around: the runner's `Setup` would have killed the seed's run-to-handoff
(`gui_pause_for_checkout` clears `dojo.stepping`), so `ready()` waits for the handoff;
and a Record-MOVIE handoff drops READ-WRITE to WRITE, after which the neutral pad
clobbers every remaining row - MacroMode keeps READ-WRITE. Every press goes through
`tas_auto::playLive` (the sender's path); every claim is a byte the game reports
(`ID_2` / `Assist_Value` / `PaletteID_2` per slot through `tas_mvc2::readRamSafe`):

| step | the claim, read back |
|---|---|
| on the globe | `ID_2 P1_A==19 RubyHeart, P2_A==23 Cable` - David's START |
| walk to Dhalsim, press by press | `plan("Dhalsim")` = D,L,L,L; after each press the cursor reads the graph's prediction: Hayato 18, Anakaris 4, Jin 55, Dhalsim 37 (his `verify_grid` claim, on DC) |
| pick + confirm | `PaletteID_2==0 (LP)`, `Assist_Value==0 (alpha)`; after confirm slot B is the live cursor, reset to home (19) |
| the team | all six `ID_2` == the RECIPE's pins |
| speed select -> fight | Start; `Is_Point` on Dhalsim, health 144/144, skip rate 4 |
| save the base | slot 0 written at the first playable frame: `CSS BASE: slot 0 @ frame F hash=H folder=<clip>` |
| finding | David's Dhalsim97 rows 4212-5699 on THIS base, through `intent`: the peak, whatever it is - optional, reported, never a pass condition |

Harness gates C1-C5 (`csstour.sh` header); arms `css-walk` (plan fed the wrong start),
`css-timing` (navgap 0 - a repeated direction never re-presses), `css-team` (P2's stream
skipped), judged by `arms.sh` from the runner's own declaration. A tour that reports but
ran no `css:` steps is SKIP 77, never a pass.

### 7.3 The base as a RECIPE

`RECIPE.toml [dhalsim_base]`: kind authored, the seed and the picks pinned by
seqHashMacro (F1), the six `ID_2` pins validated by NAME against SPREADSHEET's `ID_2
Note2` (F2), `machine_hash` / `machine_frame` "unmeasured" until
`fixtures-check.sh --make-dhalsim-base` runs `csstour.sh --keep-base css/base/` and pins
them (refused over a pin without `FIXTURES_REGENERATE=iknow`). The `.state` is NOT
committed - V49-locked, ~10 MB, `css/base/` is gitignored; the inputs regenerate it,
which is better than a savestate (it survives any serializer version).

### 7.4 Measured

(the run this section lands with - see the commit message; the harness prints every
`css:` step line and the `CSS BASE:` line)

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

### 5.4 `[LANDED 2026-09-17]` Harness #2 - per-frame divergence, swept

**What existed** (the wheel, ~70% of it): `scripts/reprotest.sh` (two processes,
same input, same hashes required; `--cold`, `--oracle <other-fork>`, `--self-test`
poke arm, `--runs N`, empty-run SKIP), `scripts/tests/repro/hash_sequence.lua`
(the per-frame machine hash keyed by the GUEST frame - the lesson
`differential_history.lua` paid for), and `scripts/tests/open/replay_determinism.lua`,
which already names the one known in-process divergence (the 1-byte
`cycle_counter` on restore-vs-continue). nbneo-rr had no generic N-arm sweep
either; what was lifted from it is the measurement that sizes N ("6 runs found
12 divergences, 2 found 8") and the rule that non-vacuity comes BEFORE equality.

**What this adds** (`66b85bed5`, `d6514c4ca`):

- **The record grew.** `REPRO <frame> <machine-hash> in=<input-digest> c=<p1>/<p2>`.
  The input digest is the MOVIE ROW at that frame for both players
  (`flycast.movie.getButtons`, names sorted, `absent` when there is no row -
  never digested as empty), a multiplicative hash mod 2^32 so it needs no bit
  operators. **Its domain is the row the guest was fed, not what the guest did
  with it.** It travels so a disagreement can be told apart: same row, different
  machine = emulation nondeterminism (this harness's subject); different row =
  an INPUT DESYNC (a movie/replay defect, said so on the line). `c=` is the MvC2
  combo counters, resolved BY NAME from SPREADSHEET.json in the harness and
  handed to the Lua as `FLYCAST_REPRO_COMBO_P1/P2` - the game-state series
  riding in the same sequence, "it comboed on frame N" beside "the machine was
  identical on frame N". The Lua hardcodes no address.
- **All pairs, first divergent frame, a class.** `--sweep` = 6 runs; every mode
  compares every pair and prints `first divergence run<i>/run<j> at frame F
  (machine|input|both)`. One classifier line: `REPRO CLASS: reproducible` /
  `deterministic-divergence at frame F` (every disagreeing pair diverges at the
  SAME frame - a bug you can bisect) / `nondeterministic (first divergence
  moves: F1,F2)` (a race, a clock, host state) / `input-desync`. RESULT line,
  append-only: `REPROTEST RESULT: runs= pairs= disagree= class= first= vacuous=
  mode= seq=`.
- **Exit codes kept apart**: 0 reproducible · 1 NOT reproducible · 2 usage ·
  3 the harness failed to run (a run died, timed out, or sampled different
  frames - not a verdict) · 5 VACUOUS (a run whose hashes never moved, checked
  on EVERY run before any diff; two frozen machines agree) · 77 skip.
- **Arms** (`--sabotage`, `--list-sabotage`; `--self-test` is `--sabotage poke`
  and keeps its legacy lines - docs/STATE-COVERAGE.md greps them): `poke` (a
  fixed-frame perturbation: the class MUST be deterministic-divergence at that
  frame), `moving` (two runs perturbed at START+3 and START+7 via
  `FLYCAST_REPRO_POKE_AT`: the class MUST be nondeterministic with a moving
  frame), `gate-can-pass`. Judged by `scripts/lib/arms.sh` over R1 non-vacuity /
  R2 unarmed pairs (control) / R3 all pairs (target), plus the class each arm
  predicts. Inverted exit 0 fired / 4 decorative / 2 inconclusive.

**The claim class, in nbneo's words.** A green sweep certifies that this build
reproduces ITSELF across processes on this input. It never certifies emulation
correctness: "A CORRECT PICTURE OF THE WRONG THING" is reproducible too.
Correctness is the oracle's (§5.1) and the fixture's (§5.3) claim - a different
class, and a green here must not be read as one.

**Measured 2026-09-17** (tour clip `2026-09-08T02_38_29Z`, START 100, SEQ 12):

| run | verdict | time |
|---|---|---|
| default (2 runs) | `ok run1 == run2 (12 frames of hashes, identical)` · `REPRO CLASS: reproducible` · `reprotest: reproducible across processes` - the legacy lines byte-identical | 9.7 s |
| `--sweep` | `runs=6 pairs=15 disagree=0 class=reproducible first=- vacuous=0 mode=from-state seq=12` | 29 s |
| `--cold --sweep` | `runs=6 pairs=15 disagree=0 class=reproducible ... mode=cold` | 29 s |
| `--sweep`, `FLYCAST_REPRO_SEQ=600` (one-off, 2026-09-17, HEAD `24367e9e0`) | `runs=6 pairs=15 disagree=0 class=reproducible first=- vacuous=0 mode=from-state seq=600` | ~3 min |
| `--cold --sweep`, `FLYCAST_REPRO_SEQ=600` (same) | `runs=6 pairs=15 disagree=0 class=reproducible first=- vacuous=0 mode=cold seq=600` | ~3 min |
| `--sabotage poke` | `first divergence run1/poke at frame 100 (machine)`, same for run2 → `deterministic-divergence at frame 100` → BEHAVED AS PREDICTED, classifier named it | 14 s |
| `--sabotage moving` | pokeA at 103, pokeB at 107 (all five pairs) → `nondeterministic (first divergence moves: 103,107)` → BEHAVED, classifier named it | 20 s |
| `--sabotage gate-can-pass` | reproducible → the gate CAN pass | 9 s |

The record: `REPRO 100 753832078 in=93521b66 c=0/0`. No real nondeterminism was
found on this build at this depth (12 frames from the state, 6 processes, warm
and cold). That is a statement about SEQ=12 from frame 100 - `FLYCAST_REPRO_SEQ`
and `FLYCAST_REPRO_START` widen it, and a wider sweep is the next thing to
measure, not to assume.

**Not built, said plainly:** the `input-desync` branch has no arm. Feeding one
process a different movie row mid-replay needs `movie.setButtons` on a movie
that is not editable in READ mode, or a second clip; neither is an honest
one-line arm. Until one exists, that branch is reasoned, not measured.

### 5.5 `[LANDED 2026-09-17]` The boot handoff - what a boot ARMS is CONSUMED

**What existed.** David's boot handoff (`rend/gui.cpp:4679-4743` in his tree) sits at the
step-stop and consumes what the pre-boot code armed: `replay_bootload` (Play a Movie ->
State 0 at the boot pause), `macro_fullload` + `macro_pending` (Play Macro Full/Stage ->
State 0 then the macro injected RELATIVE to it), `boot_ready_arm` (the READY banner, the
States window, `TAS READY`), `PlayTestLocked`, and `dojo:OnEnterHandoff` in `SeedOnEnter`.
dojo7 had ported every producer and NO consumer (docs/PORT-DEFECT-CENSUS.md #1-#5, §2,
§3): Play a Movie froze at power-on and never sought State 0 (a human pressed F3; every
harness passed `AutoSeekState`, so no test saw it), the Macros panel's "Load full" armed
rows `Reset()` dropped at the next boot, the banner never fired.

**What landed.** The handoff, at our `onenter_ff` stop in `gui_display_osd` (one stop, not
two); pre-boot staging in `gui_start_game` (`PlayMacroClip`/`PlayMacroFile`/`PlayMacroStage`,
one-shot, `else SeedOnEnter()`); `if (MacroMode) macro_armed = true` at boot (David's
macro-parity audit); `OnEnterHandoff`; `PlayTestLocked` (its consumers `macro_locked` /
`base_prelock` / `locked_slots` exist here). ONE OWNER of the replay seek: with
`dojo:AutoSeekState=N` set the handoff DEFERS (`TAS REPLAY BOOT: State 0 seek deferred to
AutoSeekState=N`) - the surface tour's slot-0 mover stays exactly as measured (72/72,
gate_ok=72 after the port). The Macros panel's "Load full" now STAGES the pair and restarts
the game (`deferred::post(gui_start_game)`) instead of arming a flag in-session; its label
says so. `ctlserver` gained a `movie` verb (has/p1/p2/begin/end at a frame - the roll read
back, independent of the trace that claimed the inject). NOT ported: `TestLabBoot` - its
consumer is David's pre-boot lab-scratch launcher, and ours' Test Lab is in-session; no
consumer here, so no key.

**Traces.** `TAS REPLAY BOOT: State 0 loaded at the boot pause -> frame F` / `... no State 0
in the clip - frozen on the boot frame` / `... seek deferred to AutoSeekState=N`;
`TAS MACRO FULL: injected N macro frames at State 0's frame F (relative)`; `TAS PLAY TEST:
landed READ / locked at frame F`; `TAS READY: READY - <clip> (<mode>) at frame F of E  |
live = ...`; `TAS ONENTER: handoff at frame N (WRITE|READ-WRITE)`.

**The harness** - `scripts/boothandofftest.sh` (ctest `flycast.boothandofftest` + twins):

| claim | measured 2026-09-17 |
|---|---|
| B1 replay boot, no AutoSeekState | `frame=9928 paused=True (want 9928, True); TAS REPLAY BOOT: State 0 loaded at the boot pause -> frame 9928` |
| B2 PlayMacroClip+File staged | `frame=9928 paused=True injected_at=9928 (want 9928); movie[9928]: has=True p1=16 (want True, 16)` |
| B3 OnEnterHandoff=300 on the 633-frame fastVS seed | `handoff=300 frame=300 paused=True` |
| arm `noload` | B1 `frame=0`, B2 `injected_at=0 ... has=False`, B3 green -> BEHAVED AS PREDICTED (exit 0) |
| arm `absolute` | B2 `injected_at=0 ... has=False`, B1 green -> BEHAVED (exit 0) |

The arms live in the RUNNER: `surface_tour.cpp`'s `sabotaged()` now also honours a tour-free
key `dojo:TourArm=<cls>[+<cls>]` (same grammar, parsed lazily - the boot handoff can fire
before the first tick), so the switch stays in the test-only TU and the handoff only asks
(§5.2 rule 2). Two harness lessons paid for on the way: ctlserver BASELINES on the first
`cmd.json` it sees (prime with a seq-0 query, as fixtures-check does), and a `$(send ...)`
runs in a subshell, so the sequence number is the caller's to bump - both are now comments
in the script.

### 5.6 `[LANDED 2026-09-17]` The small ports - eight census items, seven landed, one already here

docs/PORT-DEFECT-CENSUS.md "Port, small", one commit each, each measured where a surface existed:

| item | commit | measured |
|---|---|---|
| Undo / Redo reach the user (Ctrl+Z / Ctrl+Shift+Z + buttons) | `67ac52ba3` | tour step 47/74 `roll: redo the flip + undo` PASS; `--sabotage flip` BEHAVED; RollEditProbe walks undo->redo->undo |
| `<state>.wave` on save - **and the audio tap that feeds it** | `ce5495cc4` | thumbtest W1: 0/4 states carried a `.wave` before the tap, 4/4 after. THE FINDING: David's `audiostream.cpp:53` `tas_wave::onSample` was never ported, so `measured` stayed 0 and this tree had NEVER written an `audio.env` or a `.wave` - every consumer, no producer (one line in `core/audio`, outside the batch's file list, stated) |
| paused sidecar autosave tick (audio.env / skip.map / .flyr tail, 2 s) | `588d7f32d` | thumbtest S1: both files present + 4 `TAS SIDECAR` lines BEFORE teardown (the sandbox copies neither) |
| macro-save stamp on the Timeline | `346fc6bdc` | TIMELINE SELFTEST 12/12 (5 formatting claims); the failure path is measured only by WriteMacroFile's log line - the read-only-dir sandbox was not built |
| MERGE sends (boot seed + checkbox) | `363e6c6f3` | SENDER SELFTEST 18/18 (+2: the key seeds the atomic on/off) |
| Wait for Frameskip + skip+N + arm + release trace | `363e6c6f3` | tour step 61/74 `sender: send held for frameskip` PASS `(released; frame 9930 -> 9938)` - inside the 8 stepped frames, i.e. a skip frame (deadline +60); declared a machine+movie mover, tour 74/74 gate_ok=74 |
| States: "live = gen NN (restored)" | `396b3c982` | a status-row text line; not separately measured (stated) |
| `load_fail_*` (F3 on an empty slot) | - | NOT PORTED: `dc_loadstate` already warns + toasts "Save state not found" here |

Rule kept: a claim that needs a surface the tree does not have is reported, not faked. selftest.sh 39 suites /
658 claims / 0 failed; sendequivtest PASS after the Sender change; tour FLOOR 72 (74 steps).
