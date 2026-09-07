# Determinism — on dojo-7

Ported from the `video-recording` branch onto the unified dojo-7 build.
Everything here is **built and run** against `NoBGM_VMU.cdi`, not just compiled.

## What changed in the port

**The RTC pin was already done.** dojo-7's `GetRTC_now` already reads
`GGPOEnable || RecordMatches || Replay || Receiving || Transmitting`. That
widening is blueminder's, in the base — not David's, and not needed here.

**The SH4 clock pin is the real gap on this base.** `config::Sh4Clock` exists
here (flyinghead `48acb03b8`) and upstream's guard covers GGPO only
(`emulator.cpp:750`). Widened to the predicate and instrumented:

```
$ ./build/flycast -config config:Sh4Clock=250 -config dojo:RecordMatches=yes
emulator.cpp:770  determinism: pinning SH4 clock 250 -> 200 MHz (record run)
```

With recording off, it correctly does not fire.

**A bug in the predicate was found doing this.** `config::Transmitting`
**defaults to true** on every flycast-dojo base — it means "will upload replays
if a session happens", not "a session is happening". Including it made
`isDeterministicRun()` return true in every plain single-player launch. Removed
from both this branch and the original. See the comment in `determinism.cpp`.

**`sb_mem.cpp`'s guard does not exist here** — dojo-7 restructured that function
away, so that part of the port simply does not apply.

**The classification needed rework, and the audit found it.** On its first run
against this base the startup audit reported **6 stale keys**:

| key | why |
|---|---|
| `Dreamcast.FullMMU`, `Dreamcast.ForceWindowsCE`, `Dynarec.idleskip` | removed upstream between the bases |
| `SOCDResolution`, `input.EnableDiagonalCorrection`, `dojo.ForceRealBios` | blueminder master-line additions dojo-7 never had |

And this base has sync-critical options the older one lacked, now classified:
`Dreamcast.RamMod32MB` (changes the **memory map**), `network.BattleCable`,
`network.MultiboardSlaves`, `config.PerGameVmu`, `config.Sh4Clock`.

Final: **23 sync-critical, 127 unclassified, 0 stale.**

That is the anti-drift check doing exactly the job it exists for, on its first
encounter with a codebase it was not written against — and it caught three more
mistakes when I guessed the wrong cfg *sections* for the new keys.

---

## Original notes (written against the video-recording base)

---

## What a movie actually is

A movie is a **recipe, not a video**. It stores "press Punch on frame 107" and
replays by re-running the game. That only reproduces the original if the machine
— and everything the machine was told — is identical both times.

Three buckets break it:

| bucket | examples | fixed by |
|---|---|---|
| **initial conditions** | BIOS, flash, VMU, RTC at boot | savestate anchor |
| **host leaking in** | RTC read mid-run, overclock, FMA rounding, uninit memory | the predicate + real fixes |
| **configuration** | cheats, threaded rendering, region, SOCD | the sync manifest |

---

## What shipped on this branch

### 1. One predicate instead of a flag list

`core/determinism.{h,cpp}` — `determinism::isDeterministicRun()`.

Upstream already solved much of this for rollback netplay: there are ~20
`if (config::GGPOEnable)` guards across the emulation core, each one a standing
admission that some subsystem is nondeterministic. **Local record/replay is the
same problem under a different name**, so every one of those guards is a
candidate for the predicate.

David's fork does this by enumerating flags inline
(`GGPOEnable || RecordMatches || Replay || ...`). That shape rots: the list gets
copied, then one copy gains a case and the others do not. One predicate means
the audit is a grep. The dojo dependency stays confined to `determinism.cpp`, so
the emulation core does not gain an include of the netplay layer.

Note `config::Replay` **does not exist in this base** — replay runs off
`dojo.PlayMatch`. Copying David's condition verbatim would not have compiled.

### 2. The RTC pin, widened

`core/hw/aica/aica_if.cpp` — was `if (config::GGPOEnable)`, now the predicate.
One line, and it is the whole of David's determinism contribution that this base
was missing.

**The SH4 clock pin was NOT needed.** `config::Sh4Clock` does not exist here;
`SH4_MAIN_CLOCK` is a compile-time `#define (200 * 1000 * 1000)` at
`core/build.h:238`, so the clock is pinned by construction. This becomes a debt
only on rebasing to dojo-7, which inherits flyinghead's slider **and** its
GGPO-only guard (`f8d5517b8`) — widen it at the same time.

### 3. The savestate idempotency probe

`core/nullDC.cpp` — `verifyLoadedStateIdempotent()`, ported from the TAS fork.
Re-serializes after a load and byte-compares against the blob; the first
differing offset names the subsystem that failed to round-trip.

Defaults **on** whenever `isDeterministicRun()` — never breaking sync is the
point, so it should not be something to remember to enable. Override with
`-config dojo:VerifyState=yes|no`.

This is the load-bearing piece. Until save→load→save is byte-stable, a state
fingerprint is hashing noise and every assertion built on it is meaningless.

### 4. The sync manifest

`determinism::captureManifest()` / `applyManifest()` / `serializeManifest()`.

BizHawk's SyncSettings idea: options split into cosmetic (change freely) and
sync-critical (recorded **in the movie**; changing them invalidates it). See
`SYNC_SETTINGS.md` for the full classification argument.

Two pieces of existing machinery made this cheap, and neither was built for it:

- **The registry already exists.** Every `Option` self-registers in its
  constructor, so the manifest is *derived* by walking `settings.options`,
  never hand-maintained.
- **`override()` is already the apply mechanism** — it is exactly what upstream
  uses for its own GGPO pins.

So values move through the cfg layer (`cfgSetVirtual` + `Settings::load`), the
same path `-config` launch flags already use. `core/cfg/option.h` gained one
non-pure virtual (`optionKey()`) and one const accessor; nothing existing
changed behaviour.

**On cheats:** a cheat is a memory write at a defined point — perfectly
deterministic and perfectly recordable. Upstream bans them under netplay because
syncing cheat state between peers is more work than forbidding it. That is a
netplay convenience, not a law. The fix is to record that they were on.

### 5. The anti-drift audit — and it found its own answer

`determinism::auditClassification()`, called from `Emulator::init()`. Verified
on a real boot:

`[MEASURED 2026-09-06]` on the video-recording base:

```
determinism.cpp:291 N[COMMON]: determinism: 24 sync-critical, 153 unclassified, 0 stale
```

`[MEASURED 2026-09-07]` on dojo7, after the classification was reworked for
this base: **23 sync-critical, 127 unclassified, 0 stale**.

**`0 stale` is the result that matters**: all 24 classified keys resolve to real
registered options, so the classification has no typos and the key format is
right. The 153 are options nobody has ruled on yet — a new option lands *there*
rather than silently defaulting to "safe", which is the failure mode a
bool-on-the-option design would have had.

### 6. The anchor assertion — in Lua, not C++

`scripts/determinism_anchor.lua`.

A savestate is a photograph of the machine. Photograph at frame N, replay from
power-on, photograph again, compare. Equal means frames 0..N reproduced
**exactly** — proven, not hoped. With several anchors the first mismatch
**bisects** the movie and names the stretch that broke.

The payoff: a TAS clip already carries up to 100 savestates, so an existing combo
library is already a corpus of assertion points. This runs over work that already
exists with nothing new authored.

**A duplicate was written and then removed.** An earlier pass added an MD5
`stateHash()` to `determinism.cpp` before noticing that `hashState` already
exists in `core/lua/lua.cpp` and the emuapi adapter already maps it to
`savestate.hash` (commit `7e8aa00e1`). The emuapi README's "missing:
savestate.hash" line is **stale** and should be regenerated.

That it lives in Lua is the better outcome: the assertion is now an emuapi
conformance test that runs against any conforming host, rather than flycast C++.
Caveat: `hashState` uses XXH32, so it is a 32-bit fingerprint — fine for
detecting divergence, not a cryptographic identity.

---

## Audit of the remaining guards

Inspected directly, not inferred.

| site | what it does | verdict |
|---|---|---|
| `emulator.cpp:957` | `GGPOEnable && ThreadedRendering` → force `EmulateFramebuffer` off | **MISS — use the predicate.** Framebuffer emulation is disabled for rollback but left on for local recording. Clearest gap found. |
| `sb_mem.cpp:237` | `online \|\| dojo.PlayMatch \|\| RecordMatches` → per-game flash setup | **Already widened by hand.** Replace with the predicate so it cannot drift. Good precedent — someone already reached this conclusion. |
| `cheats.cpp:448,572` | cheats disabled when online | **Change, but via the manifest** — record cheat state rather than forbidding it. |
| `Renderer_if.cpp:238` | `!ThreadedRendering && !ggpo::active() && !GGPOEnable` → `sh4_cpu.Stop()` | **Needs measurement.** Governs when the emu thread yields to the renderer. |
| `nullDC.cpp:166` | `dc_savestate` early-returns when online | **No change.** You want savestates while recording; correctly netplay-only. |
| `nullDC.cpp:61,94` | `lua::init/term` skipped when online | **No change** — but see the Lua hazard below. |
| `emulator.cpp:741,788` | `NetworkHandshake::term()` | **No change.** Netplay plumbing. |
| `mem_watch.h:292,311` | memory watch off under GGPO | **No change.** Performance (`51758b965`), not determinism. |
| `naomi_cart.cpp` (×10), `imgread/common.cpp` | ROM/disc digests | **No change.** Proves two peers loaded the same ROM. Irrelevant locally. |

### A hazard nobody has written down

**Lua is itself a sync risk during recording.** A script that writes memory or
presses buttons is changing the run, and none of it is in the movie. Upstream
sidesteps this by disabling Lua entirely when online (`nullDC.cpp:61`), which is
not an option here — scripting is the whole point of the emuapi work.

emuapi already has the right mechanism: `emu.declare{tier=}` returns a granted
tier that may be narrower than requested. A `mutator` script during a live
recording should be **refused or recorded**, never silently permitted. Nothing
implements that yet, and it is the first thing that will bite once scripts and
recording are used together.

---

## FMA — fixed, and it was already half-solved upstream

Earlier notes here called FMA unfixable: "a host CPU feature, not
configuration". That was wrong, and upstream had already done the hard part:

```c
// core/rec-x64/rec_x64.cpp:449
if (cpu.has(Cpu::tFMA) && !config::GGPOEnable)
    vfmadd231ss(rd, rs2, rs3);
```

flyinghead disables FMA for rollback netplay, where two machines must agree.
A recorded movie has the same requirement across time and across machines, so
that guard now uses `determinism::isDeterministicRun()`.

FMA fuses the multiply and add, keeping full intermediate precision instead of
rounding twice. It is *more* accurate and a *different* answer - so the same
movie on an FMA host and a non-FMA host diverges, and neither is wrong.

**And a hole in that fix, closed.** The recompiler bakes the decision into
compiled blocks, so flipping the mode is not enough on its own - blocks
compiled while the answer was "no" keep their FMA. Reachable, not theoretical:
`dojo_gui.cpp:833` offers a "Record All Sessions" checkbox.
`determinism::refreshCodegen()` (called once per frame from `Emulator::vblank`)
compares the mode to the last observed value and resets the block cache when it
changes, exactly as upstream does for the SH4 clock in `setNetworkState`.

Verified by flipping the flag mid-session from Lua:

`[MEASURED 2026-09-07]`, by flipping `flycast.config.dojo.RecordMatches` from
Lua mid-session:

```
determinism.cpp:73  determinism: mode -> on,  resetting the block cache
determinism.cpp:73  determinism: mode -> off, resetting the block cache
```

That test first reported a FALSE PASS. `flycast.config.dojo` bound only
`ShowTrainingGameOverlay`, so the Lua write went nowhere and the *absence* of a
log looked like the guard failing. **An absent binding and a broken guard are
indistinguishable from the outside** - which is why the flag is now bound.

`config::RecordMatches` and `config::Replay` are now bound as
`flycast.config.dojo.*`, because the mode was otherwise reachable only by
driving the UI - and emuapi wants `movie.record()` to be a call, not a
checkbox.

## The guard audit, corrected

An earlier version of this file said "~20 `if (config::GGPOEnable)` sites still
carry their own inline conditions", implying twenty conversions. **That was
wrong.** Triaged one by one, almost all are netplay plumbing:

| site | what it is | verdict |
|---|---|---|
| `rec_x64.cpp:449` | FMA | **converted** |
| `nvmem.cpp`, `maple_jvs.cpp`, `maple_cfg.cpp`, `naomi_cart.cpp` | MD5 digests proving two peers loaded the same data | no change |
| `mem_watch.h:293,312` | rollback page-dirty tracker | no change |
| `emulator.cpp:195,695,730`, `maple_devs.cpp:1767` | handshake, analog-axis count, netplay roles | no change |
| `nullDC.cpp:104` | savestates blocked online | already right - you want them while recording |
| `cheats.cpp:604` | non-builtin cheats skipped online | **manifest work, not guard work** - record the cheat state rather than banning it mid-recording |

So the real answer was **one** conversion, not twenty.

## What no manifest can fix

- **FMA** (`879372cb7 rec-x64: use FMA when available`). A host CPU feature, not
  configuration — the same movie diverges between a CPU with FMA and one without.
  Either disable it in the recompiler under `isDeterministicRun()` (portability
  beats speed for TAS) or record the CPU feature set and refuse on mismatch.
  **Not addressed on this branch.**
- **Uninitialised memory.** Must actually be fixed. The TAS fork hit this exact
  class: its recorder left uninit stack garbage in packet dead bytes, which is
  why legacy movies export as `@raw` hex.
- **Thread races.** Single-thread the deterministic path.

---

## Next

1. Point `emulator.cpp:957` and `sb_mem.cpp:237` at the predicate.
2. Classify the remaining 153 — mechanically, using `compareRuns()` in the
   anchor script to settle the six in `needsMeasurement()` empirically rather
   than by argument.
3. Embed the manifest in the movie header / a `sync{}` block in `clip.json`.
4. Wire `applyManifest()` into replay startup with the refusal policy.
5. Decide the FMA question.
6. Give `emu.declare{tier=}` teeth during recording.
