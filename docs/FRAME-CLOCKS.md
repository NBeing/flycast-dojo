# Frame numbers: which clock, and when

There is no single frame number in this emulator, and treating any one of them
as "the" frame number is how three separate alignment bugs got written in one
day. This file says what each clock means, what it is measured to do, and which
question each one answers.

> **`core/frame_clock.h` is the owner now `[2026-09-11]`, not this file.** Each
> clock is its own C++ TYPE - `frames::Movie`, `frames::Delivered`,
> `frames::Vblank` - so mixing two is a compile error instead of a
> plausible-looking number, and the type at a call site says which clock the
> caller is on. `scripts/clocktest.sh` hands the compiler four mixtures and
> requires it to refuse each, plus one legal snippet it must accept.
>
> This document became prose ABOUT code rather than the only place the
> distinction lived, because prose cannot be checked and this one drifted the
> first time it was tested - see the correction below.

Everything marked `[MEASURED]` was observed on 2026-09-08 in a single-player
replay run (`scripts/tests`, MvC2 clip, auto-seek to state 0).

## The three clocks

`[CORRECTED 2026-09-11]` **There are four, not three.** This table previously
ended "There is no fourth clock for *frames since this boot*", and one was added
the day before without this file noticing - which is the entire argument for the
types. The old advice, deriving it from `confirmed()` as a delta, is still
correct from Lua, where there is no binding for the fourth.

| clock | Lua | C++ type | reads | answers | resets on |
|---|---|---|---|---|---|
| **movie index** | `flycast.frame.count()` | `frames::Movie` | `dojo.frame_number` | *where are we in the movie* | a movie opens |
| **delivered** | `flycast.frame.confirmed()` | `frames::Delivered` | `ggpo::confirmedFrame()` | *has a frame been handed over* | `startSession()` — **netplay only** |
| **vblank** | — | `frames::Vblank` | `frames::vblank()` | *did the machine complete a frame* | never |
| **callback count** | your own `n = n + 1` | — | — | *how many times was I called* | never (script-local) |

From Lua, "frames since this boot" is still a derived number:

    -- frames since THIS boot, correct across an in-process restart
    if base == nil then base = flycast.frame.confirmed() end
    local since = flycast.frame.confirmed() - base

## What each one actually does

### movie index — `frame.count()`

`[MEASURED]` **It does not tick until the game polls maple: 142 frames late at
boot.** Sampling "early" therefore reads 0 in every run, and two runs agreeing on
0 means only that neither has started.

It is the movie's own position, so it also jumps. `[MEASURED]` across one boot,
sampled every 100 callbacks: `0, 60, 9967` — the jump is the auto-seek to state
0 landing at the state's frame. A clock does not jump; an index does.

It re-counts re-simulated frames under rollback, and resets to 0 when a movie
opens. Use it for "where in the movie", for keying a comparison between two runs
of the same movie, and for anything the movie file itself indexes.

### delivered-frame clock — `frame.confirmed()`

`[MEASURED]` **Exactly 1:1 with the `vblank` callback**, and **monotonic across
an in-process restart**: two boots in one process read 100, 200, 300 then 400,
500, 600. It does not reset per boot, because the only reset is in
`ggpo::startSession()` (`core/network/ggpo.cpp:529`) and a single-player replay
never starts a GGPO session.

So it is a **process** clock, not a machine clock. That is what makes it the
right basis for "frames since this boot" — subtract the value you saw at the
boot's first callback — and the wrong thing to compare between boots directly.

There are two implementations. The one that compiles here returns the counter;
the other, under `#else // LIBRETRO` (`ggpo.cpp:1094`), returns
`dojo.frame_number`, i.e. the movie index. **On a LIBRETRO build the two clocks
in this table are the same number.** Do not assume the distinction holds
everywhere.

### vblank — `frames::vblank()`

Every vblank since the PROCESS started, re-simulated frames included, never
reset. `[SOURCE]` incremented by `frames::countVblank()` at the top of
`Emulator::vblank()`, which `rend_vblank()` calls unconditionally from spg.cpp's
scanline-0 handler on `sh4_sched` timing - so it advances whenever the SH4 does,
with no dependency on what the guest code is doing.

**It is not `delivered` with a different name**, though the two are incremented
three lines apart in the same function. `delivered` excludes re-simulated frames
and resets in `startSession()`. The difference reads as a quibble until it
bites: `core/liveness.cpp` asks "did this machine survive the state load", and a
machine busy re-simulating is ALIVE - `delivered` would call it dead.

This is the right clock for *is the machine running at all*, and the wrong one
for anything that indexes the movie or has to agree with a netplay peer.

`[MEASURED 2026-09-11]` What it caught the week it was added: after an auto-seek
state load, zero vblanks in 3 s while the SH4 advanced **3,422,661,312 cycles**.
Since `rend_vblank()` has no condition on it, cycles-without-frames means a
SCHEDULED EVENT did not survive the load, not that the guest is in a bad loop -
a distinction no other counter in this table can draw.

### callback count

Script-local, and `[MEASURED]` equal to `confirmed()` deltas in a single-player
replay. That equality is an observation, not a contract: `vblank` is not
dispatched for re-simulated frames (`emuEventCallback` returns early while
`ggpo::rollbacking()`), so under rollback the two diverge. Prefer `confirmed()`.

## Choosing

- *"Where are we in the movie?"* → `count()`.
- *"Has a frame passed / how many since X?"* → `confirmed()`, as a delta.
- *"How far into this boot?"* → `confirmed()` minus its value at the boot's
  first callback. **Never `count()`** — it is 142 frames late and starts at 0
  again anyway.
- *"Are these two samples the same moment?"* → compare `count()`. It is the only
  one of the three that identifies a moment in the emulated timeline rather than
  a position in the host's dispatch history.

## The bugs this file exists to prevent

All three were written on one day, by reaching for whichever counter was nearest:

1. `differential_history.lua` aligned two restores by waiting a fixed **8 host
   frames**, on the written reasoning that adaptive alignment would hide a real
   divergence. When the host services a deferred restore is host scheduling, not
   guest state: trial A ended at frame 81 and trial B at 82, so hash *k* was
   compared against hash *k+1* and it reported "history leaks at hash 1" — a
   false positive shaped exactly like the bug being hunted. Fixed by aligning on
   the guest's own clock (a restore makes the frame counter jump backwards).
2. `reprotest.sh` keys its cross-process comparison on `count()` — correct, and
   deliberately so: two processes do not start their Lua at the same point in
   boot, so the host dispatch count is not comparable between them.
3. `restart_inprocess.lua` sampled at callback 150 and read `count()` = 10.
   150 − 142 = 8 ≈ 10: it had hit the documented startup offset exactly, and the
   number was reported as though the two boots agreed on a frame when in truth
   the movie index had barely started.

## What the types do not cover

`dojo.frame_number` is still a bare `std::atomic<u32>` read directly in dozens
of places inside `core/dojo/`. Those were not converted: the movie engine is the
one component where "the frame number" is unambiguous, and a mechanical rewrite
of that many sites buys type safety where confusion was never the problem while
risking a real behaviour change. **New code outside `core/dojo/` uses
`frames::movie()`**, which is the same number with its clock attached.

## Related

`session_inputs.size()` versus `Dojo::MovieEnd()` is the same confusion one level
down — a **count** against a **one-past-last index**. `core/dojo/movie.h` exists
to stop that one; at least six sites compared `frame_number` against `size()`
because a netplay match recording is dense from frame 0 and the two happen to
agree there.
