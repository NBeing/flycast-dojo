# What granularity can a counterfactual be driven at, and what does it cost?

`[2026-09-10]` This answers the question the machine-pool plan
(`soft-juggling-whisper.md` Phase 4) marked **UNSETTLED** and named as its first
investigation: *"read the run loop and establish what granularity is reachable
from `aroundStopped`, and at what cost."*

**Answer: frame granularity is reachable, it costs ~94 ms per frame, and
batching does not help — because each `emu.start()` buys exactly one frame while
the render thread is blocked.**

Reproduce with `dojo:StepProbe=N` (and `dojo:StepProbeBatch=yes`), in
`core/rend/mainui.cpp`.

## The two step mechanisms already in the tree, and why neither is it

**`Emulator::step()` is one SH4 INSTRUCTION.** `[SOURCE]` it sets `singleStep`,
which `runInternal()` turns into a single `sh4_cpu.Step()`, and it does
`start(); stop();` around it. `stepRange(from, to)` is the same at instruction
granularity over a PC range. This is the debugger's path, and it carries its own
`// FIXME single thread is better`.

**`gui_open_step()` is a frame advance, but it is ASYNCHRONOUS.** `[SOURCE]` it
sets `dojo.target_step_frame = dojo.frame_number + 1`, clears the user pause and
calls `emu.start()` — then returns. The stop happens **later and elsewhere**, in
`gui_display_osd()`:

```cpp
if (dojo.stepping && dojo.frame_number == dojo.target_step_frame)
{
    dojo.target_step_frame++;
    emu.stop();
    gui_setState(GuiState::Paused);
}
```

So it is not a step primitive at all — it is *set a target, release the machine,
and let a later frame notice*. **It cannot be reused from the deferred point**,
because `deferred::drain()` runs at the **top of `mainui_rend_frame`** and
`gui_display_osd()` runs later in that same function on that same thread. A body
inside `drain()` is the thread that would have done the stopping.

## The mechanism that does work

`emu.start()` → spin on `dojo.frame_number` → `emu.stop()`.

The one fact this rests on is worth checking rather than reasoning about:
`dojo.frame_number` is a `std::atomic<u32>` `[SOURCE]` incremented in
`Dojo::MapleApplyAction`, which runs on the **emulation** thread. So the render
thread can watch it while blocked; the two do not depend on each other for the
counter to move.

`[CORRECTED 2026-09-10]` this document's first draft concluded the opposite —
that a synchronous frame advance would deadlock — by reasoning about which
thread was blocked instead of checking **who writes the variable**. That was
wrong, and the measurement below then contradicted the correction too. Both
errors came from the same habit: arguing about threads rather than looking.

## The measurements

All `[MEASURED 2026-09-10]`, 11,520-frame replay clip, private Xvfb, MvC2 disc.

| configuration | result |
|---|---|
| per-frame, `ThreadedRendering=yes` | **30/30 advanced, mean 94.11 ms, worst 154.65 ms** |
| per-frame, `ThreadedRendering=no` | **0/30 — every attempt timed out** |
| batch (1 start, target 30) | **1 frame in 10,053 ms**, then timeout |
| batch (1 start, target 3) | **1 frame in 10,073 ms**, then timeout |

### Threaded rendering is required, and it is OFF by default

`[SOURCE]` `Option<bool> ThreadedRendering("rend.ThreadedRendering", false)`.
Nothing forces it either way; a fresh config directory has it off.

Single-threaded there is no separate emulation thread — `emu.start()` only sets
state, and frames advance when the **render** thread drives the emulator. Since
that thread is inside the rollout, the counter never moves. Zero of thirty,
every time.

*(This also explains a first run that reported 0/30 with no override and briefly
looked like a contradiction. It was not: that sandbox had the default. Three runs
agree once the flag is read rather than assumed.)*

### Each start buys exactly ONE frame

Measured two independent ways: 30 starts produced 30 frames, and a single start
with a 30-frame target produced **one** frame and then sat for the full ten
second bound. Repeating with a target of **3** produced one frame as well, so it
is not an artifact of the target size.

**Batching therefore buys nothing.** The emulation thread completes one frame and
then needs the renderer, which cannot run while the render thread is driving the
rollout.

### Where the time goes — measured, not guessed

`[MEASURED 2026-09-10]` `dojo:StepProbe` times the three phases of an advance,
and `Emulator::stop()` times its own three. Means per advance:

| phase | ms | |
|---|---|---|
| `emu.start()` | **0.06** | launching the `std::async` is free |
| the frame | **21–32** | the emulation itself, ~1–2 frame budgets |
| `emu.stop()` | **59–68** | **73% of the total** |

and inside `stop()`:

| | ms |
|---|---|
| **join** (`rend_cancel_emu_wait` + `checkStatus(true)`) | **61–79** |
| `nvmem::saveFiles()` | **0.3** |
| `EventManager::event(Event::Pause)` | **0.05** |

**The whole overhead is the join.** Nothing incidental is hiding in it:

- The **thread launch is free** — 0.06 ms, not the cost it looked like.
- The **disk write is not a factor.** `nvmem::saveFiles()` is unconditional on
  every stop `[SOURCE]`, which looked like an obvious removable cost for a
  stepping loop. It is 0.3 ms.
- **Audio is not a factor.** `InitAudio()`/`TermAudio()` do run inside the
  emulation thread's lambda per cycle `[SOURCE]`, but running the same probe
  against the `null` backend was **no faster** (99.59 ms vs 100.25 ms in the
  same run) and in an earlier pair was *slower*.

So the ~68 ms is the emulation thread unwinding, and `stop()` says why in its
own source: it calls `rend_cancel_emu_wait()` first, because the thread is
**waiting on the renderer** — the renderer the caller is preventing from
running. The coupling is not a side effect of the measurement; it is the
measurement.

The numbers are noisy: means of 80.13, 94.11 and 100.25 ms across runs, worst
cases from 99 to 347 ms. Treat the shape as the result, not the digits.

### Five wrong answers before the right one

Worth recording, because they were all the same mistake:

1. *"A synchronous advance will deadlock"* — reasoned from which thread was
   blocked, without checking that `dojo.frame_number` is an atomic written on
   the **emulation** thread. Wrong.
2. *"So it works"* — it does, for exactly **one frame per start**. Wrong by
   omission, and only the batch arm showed it.
3. *"The thread launch dominates"* — 0.06 ms. Wrong.
4. *"The audio teardown dominates"* — the null backend was no faster. Wrong.
5. *"The nvmem write dominates"* — 0.3 ms. Wrong.

Every one was plausible from reading the source, and every one was settled in a
single run. The measurements cost minutes; the reasoning produced five confident
answers, four of which contradicted each other.

## What this means for the plan

**A short counterfactual is affordable; a search is not.** Sixteen frames is
~1.5 s and a second of gameplay is ~5.6 s. And **none of that is removable
without breaking the coupling** — there is no incidental cost left to strip,
which the split above establishes rather than assumes. That supports "what if I had pressed
this here", and rules out anything that explores many branches.

**The real blocker is architectural, and it is the one pooling was for.** Every
number above comes from driving *the machine that is also being displayed*. The
render thread has to stop driving the renderer in order to drive the rollout,
which is why one frame per start is the ceiling. A pooled machine with **no
renderer attached** does not have this coupling at all — so the cost measured
here is a cost of the *shortcut*, not of the idea, and should not be used to
argue against it.

**It also explains the States wall wedge.** `[OPEN]` elsewhere in this tree:
loading a savestate from the wall wedges the machine when done from a deferred
point. That is the same shape — work on the render thread that the emulator
needs the render thread to finish — and this document is the evidence that the
shape is real rather than a suspicion.

## What was NOT established

- **Instruction granularity's cost.** `Emulator::step()` was read, never run.
  It does `start(); stop();` per instruction, so it plausibly pays the same
  ~77 ms overhead for one instruction instead of one frame — but that is
  `[REASONED]`, not measured.
- **Whether a pooled machine avoids the coupling.** Reasoned from the fact that
  the coupling is renderer-shaped; not built, not measured.
