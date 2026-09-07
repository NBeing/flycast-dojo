# SPIKE — a machine pool in flycast?

`[2026-09-07]` Started as feasibility only, then turned into work: SERMAP and
two savestate round-trip fixes landed out of it. Numbers measured on `dojo7`,
starting at `24a334e98`.

The concept is nbneo-rr's, stated by you there on 2026-09-04:

> *"i always envisioned it as a cps2 with rom loaded being a 'pure function'
> that can be exercised from a pool."*

`~/dev/anita/nbneo-rr/CPS2_POOLING.md`. A machine is a **value**:
`step(state, input) -> state'`, so many can exist and be pooled on demand.

---

## The obstacle is the same in both emulators, and flycast's is bigger

nbneo's blocker is file-scope statics holding machine state — a machine's state
sitting in globals rather than in a machine object, so a second instance would
share the first's guts. Their worklist was **320 machine-tier `.bss` symbols**,
swept to **4** across six named passes with a `statics-census.py` and an
`equiv-trace` oracle per pass.

`[MEASURED 2026-09-07]` flycast, same shape of count —
`nm -C --defined-only` over the 223 `core/` objects, excluding `core/deps/`,
counting `b B d D`:

```
1252   mutable statics/globals in core/
```

That is the **raw** population, not the tiered one — nbneo's 320 was already
filtered to machine-tier, so these are not like-for-like. But it is ~4x their
starting order, and flycast emulates far more hardware: SH4 with `icache` /
`ocache`, PVR, AICA + ARM7, Maple, GD-ROM, flash, modem, BBA, plus three
recompiler backends carrying their own code caches (`CodeCache`,
`TempCodeCache`, `smc_hotspots`, `sh4Dynarec`).

**A tiering pass at nbneo's rigour, at four times the population, on a codebase
with a live JIT, is a multi-month project.** That is the honest read of option 1
below.

---

## But flycast already has a machine-as-value function

This is the part worth knowing before costing anything: `dc_serialize()` /
`dc_loadstate()` already turn the whole machine into a blob and back. The
serializer is the authoritative enumeration of machine state — the very thing
nbneo had to *build* a census to discover.

`[MEASURED 2026-09-07]`, MvC2 on this branch:

```
Loaded state ver 843 ... size 27890707      (~27 MB per machine)
```

So "a machine is a value" is not aspirational here. It exists, it is exercised
constantly, and the determinism work has been hardening exactly it.

---

## THE BLOCKER — CLEARED `[2026-09-07]`

`[CORRECTED 2026-09-07]` The section below says the machine "is not a value
today". **It is now.** Three round-trip defects were found and fixed; the probe
reports `idempotent OK`. The original text is kept because the reasoning about
why it mattered still stands.

## THE BLOCKER, and it is not the statics

`[MEASURED 2026-09-07]`, the first real use of the ported idempotency probe:

```
nullDC.cpp:216 W[SAVESTATE]: STATE VERIFY: NOT idempotent -
  first diff at offset 2130953 (re-serialized 27890707 vs loaded 27890707 bytes)
```

**Same byte count, different content**, ~7.6% into the blob. Save → load →
save does not round-trip.

A pool rests on the machine being a value. flycast's own probe says it is
**not one today** — some subsystem is not fully captured, or is mutated by the
act of loading. Every pool design is downstream of fixing that, because a
pooled machine that does not restore identically is a machine that silently
diverges from itself.

This is also the strongest argument for porting **SERMAP** — the per-subsystem
offset dump I deliberately skipped as "the expensive half" of the determinism
work. It exists precisely to turn `offset 2130953` into a subsystem name. Right
now that number is unactionable.

---

## `[MEASURED 2026-09-07]` DO WE HAVE POOL CAPABILITY? No — and idempotency was not the last gate

Fixing the three round-trip bugs made the **snapshot** faithful. It did not make
the **machine** a pure function of it, and those are different claims.

`scripts/lua/pool-determinism.lua` runs the guarantee a pool actually needs:
restore three members from one blob, run each forward 300 frames, compare.

```
member A -> 1041975443
member B -> 1159361126
member C ->  803446832
VERDICT DIVERGED
```

Three restores, three answers, and the drift **accumulates per restore** —
member B reproduced exactly across separate processes, so it is deterministic
drift, not noise. Plain forward execution without any restore is reproducible
across processes too (`2608417142` twice). **It is restoring that perturbs.**

Ruled out: the `sh4_sched_ffts()` call the third fix adds to serialize. It does
mutate the machine, and `hash()` serializes, so it was the obvious suspect —
but disabling it and re-running still diverges, with different absolute hashes
and the same three-way disagreement.

**What this means.** Idempotency proves `serialize(load(blob)) == blob` — the
blob round-trips. Divergence here means something affecting execution is
**not in the blob at all**, so it is invisible to both the probe and the
comparison. Candidates, untested: the recompiler's block cache and
`smc_hotspots`, ARM7/AICA state outside the serialized set, host-side scheduler
residue.

That is precisely nbneo's problem class — machine state living outside the
machine — arrived at from the opposite direction. They found it by censusing
statics; this found it by testing the behaviour. **The 1,252 globals are back
on the table**, but now with a cheap oracle to bisect them: flip a candidate to
per-instance, re-run this script, see if the three members agree.

**`[MEASURED 2026-09-07]` The ruler is ruled out.** The script now carries two
controls: it hashes each member **immediately after restore**, before any frames
run, and reads the machine's own movie-frame counter at both ends so it can
prove each member ran the same number of GUEST frames rather than host vblanks.

```
member 1  H0=2966399345  frames 461->761 (300)  HN=4215541059
member 2  H0=2966399345  frames 762->1062 (300)  HN=399580219
member 3  H0=2966399345  frames 1063->1363 (300)  HN=665769862

start states identical? true
same guest frames run?  true
end states identical?   false
```

Identical start, identical frame count, different end. **The divergence is the
machine, not the measurement.**

Two further eliminations, from re-running with recording OFF:

- **Not the movie input stream.** `dojo.frame_number` is NOT restored by a
  savestate — it climbs 461 / 762 / 1063 across restores — and under recording
  it indexes `session_inputs`, so each member looked like it was being fed a
  different slice of inputs. But with recording off the three `HN` values are
  **byte-identical** to the recording run. Not the cause.
- **Not the RTC.** It is pinned in the recording run and live in the other. Same
  three hashes. Not the cause.

So the leak is deterministic, internal, and a function of *how many restores
have happened* — which is the signature of something accumulating outside the
serialized set. The recompiler's block cache and `smc_hotspots` are the leading
untested candidates.

Note `dojo.frame_number` not being restored is a real defect in its own right,
independent of this: it is why the TAS fork needs a `.frame` sidecar beside every
savestate.

## `[MEASURED 2026-09-07]` PROCESS-PER-MACHINE POOLING IS SAFE TODAY

The in-process result above is not the whole answer. Same state file, three
**separate processes**, each restoring once and running 300 frames:

```
process 1: HN=537135945  fc=760
process 2: HN=537135945  fc=760
process 3: HN=537135945  fc=760
```

Identical. So the leak is **intra-process accumulation**, and a fresh process
clears it. **Option 3 is verified working; option 2 is not, within one process.**

Practically: a pool of flycast processes, each holding one machine restored from
a shared blob, is sound right now — restore is 7.7 ms plus process cost, and
members agree. Checking out N variations of one moment in parallel works today.

### What was eliminated getting here

Each of these was tested by re-running `scripts/lua/pool-determinism.lua` under
the named condition. Every one produced **byte-identical** member hashes
(`4215541059 / 399580219 / 665769862`), which is itself the finding: the drift
is deterministic and independent of all of them.

| candidate | test | verdict |
|---|---|---|
| the measurement window | hash immediately after restore + compare guest-frame counts | **not it** — starts identical, frame counts identical |
| movie input stream | recording on vs off | **not it** — identical hashes |
| RTC | pinned vs live (same two runs) | **not it** |
| recompiler block cache, `smc_hotspots` | `Dynarec.Enabled=no` (interpreter) | **not it** — still diverges |
| threaded rendering | `rend.ThreadedRendering=no` | **not it** |
| audio backend pacing | `audio:backend=null`, `aica.LimitFPS=no` | **not it** |

### Where the divergence first appears

`scripts/lua/pool-bisect.lua` keeps each member's final blob and binary-searches
the first differing byte, then SERMAP names it:

- members 1v2 at **12,677,792** — inside `sh4`, within main RAM. A *downstream*
  effect: the game computed different values.
- members 2v3 at **27,791,852** — inside `sh4.cntx` at offset **308**, which
  `gdb ptype /o Sh4Context` gives as **`cycle_counter`**: the SH4's position
  within its timeslice. A phase difference, not a value difference.

### `[MEASURED 2026-09-07]` LOCALIZED: the drift is TIMESLICE PHASE, in the PVR's raster clock

Bisecting in time rather than at 300 frames changed the picture completely.

**It diverges after ONE frame**, and with five members the pattern is a perfect
parity alternation, not an accumulation:

```
H0 (right after restore): 2966399345  x5   identical
1 vs 2 -> differ    1 vs 3 -> IDENTICAL
1 vs 4 -> differ    1 vs 5 -> IDENTICAL
```

The first differing byte is at **2,581,258**. A new per-field sub-SERMAP inside
`pvr::serialize` (this commit) names it: `pvr.spg` starts at 2,581,257, so the
very first byte of the SPG block — **`clc_pvr_scanline`**, the PVR's raster
clock.

Tracing the SPG across each restore (`dojo:SpgTrace=yes`) settles what it is:

```
restore: clc_pvr_scanline=399   prv_cur_scanline=0  Line_Cycles=6355   (identical, every restore)
capture: clc_pvr_scanline=728 -> 280 -> 728 -> 280                     (after ONE frame)
```

Every restore begins at exactly 399 with every SPG field identical. One frame
later the sub-scanline phase differs by **448 cycles**, alternating. So the
machines ran slightly DIFFERENT NUMBERS OF CYCLES from an identical state.

That is a **timeslice-phase** difference, not a missing subsystem. It converges
with the earlier blob-diff, which put the 2-vs-3 difference at `sh4.cntx`
offset 308 = `cycle_counter` — the SH4's own position within its slice. Both
symptoms are the same cause seen from two sides.

The emulation loop runs in blocks; a restore lands mid-block and inherits the
LOOP's current phase rather than the phase the blob was saved at, because that
residue is not part of the savestate. Restores two vblanks apart therefore
alternate — and a fresh process starts at a clean phase, which is exactly why
process-per-machine pooling agrees and in-process pooling does not.

`[MEASURED 2026-09-07]` **THE GAP EXPERIMENT: cadence decides the outcome, so
the loop's phase is the carrier.** Both cadences were run in ONE session, same
blob, same process, so a changed pattern cannot be run-to-run noise. Each member
restores, runs exactly one frame, and is hashed; only the idle between restores
differs.

```
gap=0  (members  1-6):  A B A B A B     perfect alternation
gap=1  (members  7-12): C A A A B A     alternation DESTROYED
gap=0  (members 13-16): A C A B         and it does not snap back
```

Inserting a single idle vblank destroys the alternation, and returning to the
original cadence does not restore it. The outcome therefore depends on the
emulation history between restores, not on the restore count — which is the
definition of a carried phase. A third outcome (`C`) appears too, so the
divergence is a small set of states, not a toggle.

**The mechanism this implicates:** these restores run from a `vblank` callback,
which executes ON the emulation thread from inside the emulation loop. The
restore swaps the machine, but the loop's in-flight plan — how many cycles it
intended to run in the current slice — is a local of the loop, not part of the
savestate, so it survives. A fresh process has no such residue, which is exactly
why process-per-machine agrees.

### `[MEASURED 2026-09-07]` THE HANG: the emulator cannot be stopped from anywhere a script can reach

Restoring with the loop quiesced is the obvious fix, and it does not work. What
the hunt established, by bisection:

| what | result |
|---|---|
| pause / resume, no savestate at all | **fine** - 5 cycles, frames 190 -> 310 |
| restore while stopped, and do NOT resume | **fine** - drawing continued 30 frames past it |
| restore while stopped, then resume via the pause toggle | **wedges** - resume returns, process spins (`RNl`), no frame presented again |
| restore from a draw callback with a MODAL stop | **wedges** - the reason is taken (`mask 00 -> 08`) and nothing is presented again |
| pause from a `vblank` callback | **deadlocks outright** - `emu.stop()` joins the thread it is called on |
| restore in place from `vblank` | **fine** - this is what every pool probe uses |

`gui_loadState` (gui.cpp:4436) is the supported shape and it is
`emu.stop(); dc_loadstate(); emu.start();` entirely within `GuiState::Closed`.
It works because the hotkey path calls it from **outside any ImGui frame**. Both
Lua hooks are inside one: `vblank` runs on the emulation thread inside the
emulation loop, and the draw callback runs on the render thread inside the frame.
Stopping from either wedges, in opposite ways.

**So the blocker is not the restore - it is that there is nowhere safe to stop
from.** `[MEASURED 2026-09-07]` The deferred hook was built to provide that
place - `core/deferred.{h,cpp}`, drained at the top of `mainui_rend_frame`, the
same point `gui_loadState()` is already called from by the auto-seek block. **It
did not solve it.** Four variants, all from that point:

| attempt | result |
|---|---|
| `dc_loadstate` + `pausing::Scoped(MODAL)` | wedges |
| `dc_loadstate` + explicit `emu.stop()`/`emu.start()` | wedges |
| same, with `rend.ThreadedRendering=no` | wedges |
| `dc_loadstate` with **no stop at all** | wedges |

The last row is the informative one and it changes the diagnosis again: the load
ALONE wedges from the main thread, while the identical call from a `vblank`
callback is fine. So the deferred point is not automatically safe for mutating
the machine - loading there races the running emulation thread - and stopping
first fails for a *separate*, still-unidentified reason. Two tangled problems,
not one.

What `gui_loadState` does differently from that point is the open question. It
takes `guiMutex`, requires `gui_state == GuiState::Closed`, and goes through the
slot/file path rather than `dc_loadstate(Deserializer&)` directly. One of those
is load-bearing. **Next probe: call `gui_loadState()` itself from a deferred
action.** If that works, the difference is in the path, not the place, and it
can be bisected from there.

The hook itself is kept - it is sound, it runs actions between frames, and
`flycast.test.quit(code)` in the test-tooling proposal needs exactly it. The Lua
binding that used it (`savestate.loadLater`) was REMOVED rather than shipped
wedging.

Meanwhile a separate real bug was found and fixed on the way: `loadStateFromString`
called `dc_deserialize` rather than `dc_loadstate`, skipping the whole load-time
invalidation - ARM7 recompiler flush, MMU table flush, `bm_Reset()`, memwatch
reset, `sh4_cpu.ResetCache()`, `KillTex`. It restored memory behind compiled
blocks and caches built against the old contents. Fixing it changed the absolute
hashes (`1328393435 / 354582876`) and left the drift pattern **exactly** as it
was - `A B A B A B` - so it is not the drift either.

**`dojo.frame_number` is EXONERATED.** `[MEASURED 2026-09-07]` It was the
leading candidate — it is not restored by an in-memory savestate and climbed
461 / 762 / 1063 across three restores, the only thing found that was a clean
function of restore ordinal. That was a real bug and it is fixed (the movie
position now travels in the state blob; all three members start at 460 and run
to 760). **The drift did not change.** The members still end at
`4215541059 / 399580219 / 665769862` — byte-identical to the pre-fix run, and
to the ThreadedRendering and null-audio runs. Two bugs, not one.

With that gone the candidate list is thin, and the pattern to explain is
specific: the drift is *deterministic*, *accumulates per restore*, and *is
cleared by a fresh process*. What remains untested is state living outside the
serialized set — ARM7/AICA residue, PVR/TA, and host-side allocator or
scheduler ordering that a process teardown resets. A promising next probe is to
restore the SAME blob twice in a row with zero frames run in between and hash
immediately: if H0 is stable but the machines still diverge later, the
difference is in something the hash does not cover.

## Three options

| | what it is | verdict |
|---|---|---|
| **1. In-process instances** | tier and move 1,252 globals into a machine object, nbneo-style | **months.** 4x their population, plus a JIT with its own caches. Not now. |
| **2. Serialize as the pool primitive** | machines are 27 MB blobs; "instantiate" = deserialize | **works today**, once idempotency is fixed. Cost is ~27 MB and a state load per checkout. |
| **3. Process-per-machine** | N headless flycast processes, one machine each | **works today, and now VERIFIED** — three processes restoring one blob agree exactly |

**2 and 3 compose**: a pool of processes, each holding a machine, checked out
and restored by blob. That is not the same object as nbneo's in-process pool —
it cannot share memory, and per-machine cost is a process rather than a struct
— but it delivers the *use* (many machines on demand, exercised as pure
functions) without the refactor.

Where they differ concretely: nbneo's version can hold thousands of cheap
machines and fork them per frame; a process pool holds tens and costs
milliseconds per checkout. If the goal is a DAG of every frame, that gap
matters. If it is "run this combo from 40 different starting states in
parallel", it does not.

---

## `[MEASURED 2026-09-07]` The pool primitives, timed

MvC2 on this machine, median of 5, from Lua via `savestate.tostring` /
`fromstring` / `hash` — the in-memory path a pool would actually use:

```
blob           27,797,723 bytes
serialize          79.0 ms      checkout
deserialize         7.7 ms      restore
hash               56.0 ms      fingerprint
checkout+restore   86.6 ms  ->  11.5 machines/sec sequential
```

**Restore is 10x cheaper than checkout**, which decides the shape of any pool
here: snapshot once, fan out many. Restoring from an existing blob runs at
~130/sec; snapshotting fresh each time caps you near 12/sec.

Note this corrects a figure carried from the TAS fork's notes, which describe a
state load as "~500 ms". That is the **disk** path — open, decompress,
deserialize. In memory it is 7.7 ms, some 65x faster, and a pool would never
touch disk.

Hashing every checkout would roughly double the cost (56 ms on top of 7.7 ms),
so verification wants to be sampled rather than universal.

## `[MEASURED 2026-09-07]` The blocker, localized — and two of three fixed

SERMAP is ported, and it named the culprit on its first run.

| | first diff at | subsystem | fix |
|---|---|---|---|
| before | 2,130,953 | **AICA** (`aica @ 8` .. `sb @ 2,137,629`) | ported David's `sgc_if.cpp` `RestoreAegState`/`RestoreFegState` — the EG step handlers were being re-seeded on load, overwriting just-restored values |
| after that | 11,076,198 | **SH4**, early | ported David's `serial.cpp` SCIF fix — `updateBaudRate(reschedule=false)` on load, so a restored sched entry is not recomputed from "now" |
| then | 27,884,792 | **`Sh4Context::sh4_sched_next`**, byte 296 of cntx | **new — unfixed in either fork.** `sh4_sched_ffts()` before the cntx write |
| now | — | — | **`STATE VERIFY: idempotent OK (27,793,819 bytes round-trip)`** |

Both ported fixes moved the needle, which is itself evidence they were real.

The remaining one is **new**. Diffing every `core/hw` and `core/oslib` file
against the TAS fork turns up only `Renderer_if.h` and `mem_watch.h`, neither
savestate-related — so David never hit this or never chased it.

**Hypothesis, and it is a good one:** the sh4 block ends with
`interrupts_serialize`, `sq_buffer`, `cntx`, then `sh4_sched_serialize`
(`sh4_mmr.cpp:678-692`). That last one writes `ffb` plus ten scheduler entries —
aica, rtc, gdrom, maple, aica dma, `tmu_sched[3]`, render_end, vblank — as
`tag/start/end` each, which is about the right size for a 280-byte tail. David's
SCIF fix was exactly this bug for **one** scheduler client; the same shape for
any of the other ten would land here.

## What I would do, in order

1. ~~Fix idempotency. Port SERMAP first.~~ **Done for two of three.** The third
   is localized to the sh4 tail with a named hypothesis above.
2. ~~Measure the cost of option 2.~~ **Done**, see the table.
3. **Only then** consider whether the in-process pool is worth 1,252 globals.
   nbneo is the place that question is being answered properly, and flycast can
   watch that result rather than pay for it twice.

## What this spike did NOT do

`[CORRECTED 2026-09-07]` This section opened "No code. No timing." Both became
false within the hour: the spike grew into porting SERMAP and two savestate
fixes, and the primitives are timed above. What remains true is the caveat
below.

The 1,252 figure is a raw symbol count, **not** a
machine-tier count — comparing it to nbneo's 320 flatters flycast's problem or
overstates it depending on how the tiering lands, and I did not do the tiering.
Treat it as an order of magnitude, not a worklist.
