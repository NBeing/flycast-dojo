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
| **state liveness** | **covered `[2026-09-11]`** — `LIVENESS SELFTEST` (8 claims) for the rule, `flycast.livetest` for the wiring, and it was verified against the real wedge: Dead at +4.7 s on the auto-seek load, silent on a run with no load, Alive on a healthy one. Three arms, because "Dead after every load" satisfies the first two |
| **rewind + re-record** | **genuinely uncovered.** `LoadStateFrame` runs on every state load, so it is exercised; none of its RULES is asserted — no truncate on a WRITE load, a rewind is not itself a re-record, a state saved before a rewind below it is stale |
| the staged buffer | **covered** — `ROLLSTAGED SELFTEST`, 18 claims |
| undo / redo | **covered** — `ROLL ANCHORPROBE`, `PAINTPROBE`, `MASHPROBE` and `LIBPROBE` each drive a real edit through the funnel and assert `undo=yes` with restoration |
| generations restore | **does not exist.** There is no restore function; `StatesGenProbe` covers taking one. It is a feature to build, not a test to write, and calling it untested confused the two |

So there is ONE real hole, and it is the one that matters most: the re-record
rules. That is a smaller and sharper answer than the first version gave, and it
is the reason this section now says what instrument produced it.

**Exists but does not run:** `isotest.sh`, `replay-bindings-test.sh`, and until
now `recordtest.sh`. `stepprobe.sh` is deliberately out — it is a measurement
harness whose milliseconds are a fact about the machine, and it asserts only the
two shape claims `docs/STEP-GRANULARITY.md` reasons from.

---

## The order of work, and why this order

### 1. Land `recordtest` in ctest  — **the biggest single gain**

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

**STILL BLOCKING — the auto-seek's state load leaves the guest SPINNING.**
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

### 2. The re-record claims, on top of it

Each of these is a sentence from the TAS fork's own help text or comments, and
each is a test:

- *"READ seeks the movie to that frame — WRITE rewinds there to re-record."*
- **A WRITE load does not truncate.** The tail stays as the un-reached old take
  and is overwritten in place; the roll greys it.
- **A rewind is not itself a re-record.** The timeline event fires at the first
  write whose bytes actually DIFFER, with that divergence frame as the event.
- **A state saved before a rewind below it is STALE** — it will load and verify
  byte-perfect and the movie will still desync. Warn, but allow.
- **Loading an empty slot must not stop the emulator.**

The round trip that proves the lot: record a segment, rewind into it,
re-record different input, and require the movie to replay to the *new* hashes
while the frames outside the retry keep the *old* ones.

### 3. Generations RESTORE — a feature, not a test

`[CORRECTED 2026-09-11]` this item used to read "undo, staged, generations
restore — tier 1 first". Undo and the staged buffer are already covered, and
generations restore has nothing to test because it has not been built: taking a
generation works and is probed, restoring one does not exist. Build it the way
the rest of this tree is built - a pure core claimed at tier 1, then a probe
carrying it through the real funnel - and the test comes with it.

### 4. Register `isotest` and `replay-bindings-test`, or delete them

A harness that nobody runs is worse than no harness: it looks like coverage on a
list and rots in place. Decide per file — either it earns a ctest entry or it
goes.

---

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
