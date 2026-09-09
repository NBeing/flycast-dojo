# Frame numbers: which clock, and when

There is no single frame number in this emulator, and treating any one of them
as "the" frame number is how three separate alignment bugs got written in one
day. This file says what each clock means, what it is measured to do, and which
question each one answers.

Everything marked `[MEASURED]` was observed on 2026-09-08 in a single-player
replay run (`scripts/tests`, MvC2 clip, auto-seek to state 0).

## The three clocks

| clock | Lua | C++ | answers | resets on |
|---|---|---|---|---|
| **movie index** | `flycast.frame.count()` | `dojo.frame_number` | *where are we in the movie* | a movie opens |
| **delivered-frame clock** | `flycast.frame.confirmed()` | `ggpo::confirmedFrame()` | *has a frame passed* | `startSession()` — **netplay only** |
| **callback count** | your own `n = n + 1` | — | *how many times was I called* | never (script-local) |

There is no fourth clock for "frames since this boot". Derive it:

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

## Related

`session_inputs.size()` versus `Dojo::MovieEnd()` is the same confusion one level
down — a **count** against a **one-past-last index**. `core/dojo/movie.h` exists
to stop that one; at least six sites compared `frame_number` against `size()`
because a netplay match recording is dense from frame 0 and the two happen to
agree there.
