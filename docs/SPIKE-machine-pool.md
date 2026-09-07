# SPIKE — a machine pool in flycast?

`[2026-09-07]` Feasibility only. No code was written for this; the numbers are
measured on `dojo7` at `24a334e98`.

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

## Three options

| | what it is | verdict |
|---|---|---|
| **1. In-process instances** | tier and move 1,252 globals into a machine object, nbneo-style | **months.** 4x their population, plus a JIT with its own caches. Not now. |
| **2. Serialize as the pool primitive** | machines are 27 MB blobs; "instantiate" = deserialize | **works today**, once idempotency is fixed. Cost is ~27 MB and a state load per checkout. |
| **3. Process-per-machine** | N headless flycast processes, one machine each | **works today**, and most of the harness exists (`scripts/isotest.sh`, headless replay, `AutoSeekState`, `savestate.hash`) |

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

## What I would do, in order

1. **Fix idempotency.** Nothing else is meaningful until save→load→save is
   byte-stable. Port SERMAP first so the offset names a subsystem.
2. **Then measure the real cost of option 2** — time a serialize, a
   deserialize, and a `savestate.hash` on this hardware. All three primitives
   exist; none has been timed.
3. **Only then** consider whether the in-process pool is worth 1,252 globals.
   nbneo is the place that question is being answered properly, and flycast can
   watch that result rather than pay for it twice.

## What this spike did NOT do

No code. No timing. The 1,252 figure is a raw symbol count, **not** a
machine-tier count — comparing it to nbneo's 320 flatters flycast's problem or
overstates it depending on how the tiering lands, and I did not do the tiering.
Treat it as an order of magnitude, not a worklist.
