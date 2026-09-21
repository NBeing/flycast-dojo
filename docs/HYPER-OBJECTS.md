# The Team Hyper that lands 94 on David's build and 70 on ours - located to a frame

`[MEASURED 2026-09-20]` on David's own clip (RECIPE `[david_ironman]`: Iron Man/Storm/Thanos vs
Storm/Magneto/Amingo, his V48 states loading on our V49 build), correlated with his video.

## The correlation (his video's counter vs our per-frame combo series from his state 1)

| movie frame | David's video | ours |
|---|---|---|
| 15750 | 2 HIT | 2 |
| ~15951 | 18 HIT (Proton Cannon) | 18 |
| 16358 | LK+HK -> TEAM HYPER, 40 HIT | 40 at 16370 |
| 16395 (18.6 s) | 83 HIT, Storm still firing, sparks on Amingo | 53 |
| 16419 (19.0 s) | 89 | 59; Storm leaves state 29 -> 3 (anim 142, her exit) |
| 16443 (19.4 s) | **94 HIT -> MONSTER** | 65..70 at 16441..16461, then nothing; drop at 16544 |

Hit-for-hit through 40, then his hyper adds ~54 hits in 90 frames and ours adds 17.

## What the RAM says (tools/trace/*.sh, ctlserver reads per frame)

Storm (P1 slot B) runs her Hail Storm loop normally: `Animation_Value` cycles 87..90,
`Action_Flags` counts 1 -> 2 -> 3 -> 4 as her timer counts down each phase, and while the
shards land the combo climbs ~1 hit per 2-3 frames (34 -> 58 over 16344..16409). Her super is
not cut short by a timer.

The live OBJECT list is (`0x2C287DDE` = P1's object count, `0x2C287AEC` the pointer list; David's
`enumHitObjects` decode, HITBOX_OVERLAY.md):

```
frame  combo  p1objs      frame  combo  p1objs
16344    30      3        16408    56     63
16354    33      7        16410    58     60
16383    45      9        16411    58     62
16389    48     25        16412    58      3   <- EMPTY
16393    50     36        16414    58     59
16400    53     48        16415    58      3   <- EMPTY
16405    55     59        16416    59     60
16407    56     62        16417    59     59
```

From ~16410 the shard list reads EMPTY on alternate frames (62 / 3 / 59 / 3 / 60 ...) and the hit
rate collapses at the same moment (58, 58, 58, 59, 59 - one hit in ten frames where there were
five). At 16419 Storm exits. Thanos's Gauntlet (slot C, state 29 / attack 61) stays out to 16544
landing nothing; Amingo recovers (state 32 -> 23) and the string ends at 70.

`dump_objs.sh` at 16400 vs 16412: 48 shard objects (attack 90/95, spread over X/Y, facing 1) vs
exactly the three characters. The shards are not destroyed one by one; the list alternates
between populated and empty.

## What it is not

- Not timing: the movie replayed from state 1/2/3 is 70 each; the macro over four phases is
  42/13/2/71 (David's 1-in-4, measured) - a phase changes WHICH hits land, never past 71.
- Not the fixture: it is his state and his keystrokes, and the cast/health/stage match his video.
- Not Storm's super ending early: her loop and flags run; the shards stop being SEEN.

## What it probably is, and how to settle it

An object list that is rebuilt or double-buffered per game tick, read on the wrong half on
alternate frames from a point ~50 frames into a 60-object hyper - on our build only. Candidates:
(a) the object-pool word restored wrong from a V48 state on a V49 build (the layouts differ by
8 bytes; the verify probe reports the skew but cannot say which field); (b) an object-table
capacity or allocator difference in the emulated game state (48/63 objects is far past a normal
match's ~10); (c) the MvC2 skip cadence (rate 4) vs a per-frame rebuild - a frame-parity effect.

Settle (a) first, cheaply: author the same Team Hyper on a V49-native base (the CSS tour can
pick Storm; a Hail Storm from a fresh match) and trace the object count - if the list still
alternates, the state's version is not the cause. Then bisect on the emulator side with
`tools/trace/trace_objcount.sh 1 16340 16425` as the oracle (green = the count never reads 3 while
Storm is in state 29).

## `[MEASURED 2026-09-20, later]` Closed to a 195-frame window of pure emulation

The user: "David's TAS studio does nothing sub-frame. If you have the inputs parsed
correctly and a save state, what could go wrong? Is this a dynarec issue?" - and: "can't you
take a save state every frame and see if they are exactly the same?" Done, both ways.

**Per-frame hashes from his state 1** (`reprotest --sweep` shape, 1300 frames, twice per core):
the dynarec is deterministic against itself (identical), the interpreter against itself
(identical), and the two cores diverge at frame **15331** - six frames after the load, before
any hit, with identical input digests. So the cores differ, but which one is David's?

**Fingerprints against HIS states** (`tools/trace/fingerprint.sh`: 24 game bytes - every
character's state/attack/animation/health/position, both meters, both object counts, the timer,
the skip rate/counter, the combo byte):

| at frame | his state, loaded directly | our replay from his state 1 (dynarec) | (interpreter) |
|---|---|---|---|
| 15703 (state 2) | `14 469 2 … 71 130 … 2 5 58 1 1 59 4 2 0` | **identical, every byte** | anim 468/72, timer 61, skip counter 3 - one game tick behind |
| 16215 (state 3) | `20 14 616 2 … 630 44 … 4 5 126 1 1 54 4 2 26` | **identical, every byte** | - |

The dynarec reproduces David's machine byte-for-byte through 890 frames of his combo, into
the Team Hyper, at his last checkpoint. The interpreter mis-counts frameskip ticks (David's
hunch, true of the interpreter) - which is why it lands 2 hits - but it is not the 94-vs-70.

**From his state 3** (provably his machine), our dynarec, `tools/trace/objtrace.sh 3 16395 16420`:
```
16408 56 63   16410 58 60   16411 58 62   16412 58 3   16413 58 3   16414 58 59
16415 58 3    16416 59 60   16417 59 59   16418 59 3   16419 59 59  16420 59 3
```
The same alternation, 195 frames after an identical machine, under identical inputs (the
four-button THC chord `ZXVB` at 16350..16357 is in the .flyr and the macro alike). Not the
state, not the inputs, not the phase (the empty frames fall on skip counters 1,4,2,3,1).

**Verdict: an emulation defect in this build on the code path a ~60-object super takes.** The
oracle is exact and cheap: `objtrace.sh 3 16395 16420` is green iff `p1objs` never reads 3
while Storm's state is 29. Candidates, in order: the dynarec's block cache / SMC detection
(MvC2's sprite engine writes near code per object); an SH4 load-width or sign path on the
object pointer list past ~48 entries; a flycast-dojo change since the dojo-7 fork point in
core/hw/sh4 or rec-x64. Bisect over those with the oracle; a run that stays green through
16420 while the combo keeps climbing past 70 is the fix.

## `[MEASURED 2026-09-20, late]` Not the block cache; and David's OWN fork binary does it too

Two runs of the oracle from his state 3 (a bisect agent's, its data recovered after it was
stopped - it had burned its budget on a git bisect the user judged pointless: "much more likely
this bug was latent"):

| frame | default | `dojo:LoadKeepBlockCache=yes` |
|---|---|---|
| 16410 | 58 **60** | 58 63 |
| 16411 | 58 62 | 58 60 |
| 16412 | 58 **3** | 58 62 |
| 16413 | 58 **3** | 58 **3** |
| 16414 | 58 59 | 58 **3** |
| 16415 | 58 **3** | 58 59 |
| 16416 | 59 60 | 58 **3** |
| 16419 | 59 59 (Storm exits) | 59 **3** |
| 16420 | 59 3 | 59 59 (Storm exits) |

Keeping the block cache across the load shifts the WHOLE pattern by exactly one frame (the §1a
two-cycle phase, made visible) and does not remove it. The shard bug is NOT §1a's block-cache
residue.

**And the reference build of David's own fork (`/home/nbee/dev/flycast-rr-oracle`, `edca8915b`,
the public strip) reproduces our oracle table byte for byte** (the agent's line: "David's own fork
binary reproduces our result byte-for-byte"). So the defect is in the SHARED emulator - upstream
flycast-dojo as both forks carry it - latent until a replayed 60-object super from a loaded
state. David's Windows build lands 94 on the same code: the remaining difference is the
platform/compiler build of the dynarec (his MSYS2 MINGW64 x64 vs our Linux x64), or something
his build enables that ours does not. The earliest measured split between our two CPU cores is
frame 15331 (docs/HYPER-OBJECTS.md above) - six frames after the load, before any hit - which
is where a cross-core, cross-platform investigation of the dynarec starts.
