# STATE-COVERAGE — what `dc_serialize` actually captures, and what it does not

`[2026-09-08]` Read-only investigation on `dojo7`. No code changed. Every claim
below is a citation into this checkout or a measurement already recorded in
`docs/SPIKE-machine-pool.md`; where I am reasoning rather than measuring, it
says so.

**The question.** Some savestate trouble may be an *unregistered memory region*
— EEPROM/flash/VMU-shaped machine state the serializer never captures, so a
load silently carries the pre-load timeline forward. `nbneo-rr`'s
`tools/state-torture.cpp` found exactly that (`eeprom_data[1024]` with its
registration commented out, `ZBuf` and three Z counters never handed to the
scanner; blob 374,075 → 558,328 B over 74 regions once fixed).

## Verdict, up front

1. **The named suspects are all registered.** Dreamcast flash (`DCFlashChip`,
   state byte + full array), VMU `flash_data[128 KB]`, the AICA RTC, ARM7
   registers, SH4 `icache`/`ocache`, GD-ROM drive registers and read buffer,
   modem, BBA, main RAM, ARAM, VRAM, the TA display list. See §1.
2. **The reasoning about the probe is correct, and it is worth stating
   formally:** `verifyLoadedStateIdempotent` is *structurally* incapable of
   detecting an unregistered region. Proof in §2.
3. **It does not explain anything we have actually measured.** The three fixed
   idempotency bugs were registered-region defects, and the pool drift that
   outlived them was solved and attributed to the *place* the save/load calls
   were made from, not to missing state (§3). One caveat about how that was
   eliminated is recorded there and it matters.
4. **The gap is real anyway, and it is a coverage gap in the TEST SUITE, not a
   known bug in the serializer.** Nothing in this tree can currently fail
   because of an unregistered region, and one existing measurement that *could*
   have is much weaker than it looks (§4). Two probes are missing: a
   differential-history restore, and a cold-boot-twice comparison (§4, §5).

---

## 1. The registration census

### The spine

`dc_serialize` (`core/serialize.cpp:21-66`) and `dc_deserialize`
(`core/serialize.cpp:116-166`) call, in order:

| # | call | site | what it carries |
|---|---|---|---|
| 1 | `aica::serialize` | `core/hw/aica/aica_if.cpp:514-547` | ARM7 regs (`arm_Reg`, mode, IRQ/FIQ, `arm7ClockTicks`), AICA DSP state, 3 timers, **`aica_ram` (2 MB)**, `VREG`/`ARMRST`/`rtc_EN`/**`RealTimeClock`**, the whole `aica_reg` register file, then `sgc::serialize` |
| — | `sgc::serialize` | `core/hw/aica/sgc_if.cpp:1556-1589` | per-channel playback cursor, ADPCM predictor, AEG/FEG/LFO state, `beep`, CDDA sector/index, MIDI send buffer |
| 2 | `sb_serialize` | `core/hw/holly/sb.cpp` | system-bus registers |
| 3 | `nvmem::serialize` | `core/hw/flashrom/nvmem.cpp:370-374` | **`sys_rom`** and **`sys_nvmem`** |
| 4 | `gdrom::serialize` | `core/hw/gdrom/gdromv3.cpp:1364-1391` | drive FSM, sense keys, packet/ATA command, `read_params`, **`read_buff`**, `pio_buff`, CDDA, all task-file registers |
| 5 | `mcfg_SerializeDevices` | `core/hw/maple/maple_cfg.cpp:427-448` | maple DMA-out queue, plus every attached device's own `serialize` |
| 6 | `pvr::serialize` | `core/hw/pvr/pvr.cpp:78-106` | YUV converter, `pvr_regs`, SPG, `rend_serialize`, TA FSM state, **TA display-list contexts**, **`vram` (8 MB)**, ELAN |
| 7 | `sh4::serialize` | `core/hw/sh4/sh4_mmr.cpp:666-717` | `OnChipRAM`, CCN/UBC/BSC/DMAC/CPG/RTC/INTC/TMU/SCI/SCIF, `SCIFSerialPort`, **`icache`**, **`ocache`**, **`mem_b` (16 MB main RAM)**, interrupts, `sq_buffer`, `Sh4Context`, the SH4 scheduler |
| 8 | BBA + modem | `core/serialize.cpp:43-47` | `EmulateBBA` flag, then `bba_Serialize` if set; `ModemSerialize` unconditionally |
| 9 | `sh4::serialize2` | `core/hw/sh4/sh4_mmr.cpp:793-797` | TMU derived tables, MMU (`mmu_serialize`) |
| 10 | `libGDR_serialize` | `core/imgread/common.cpp:375-380` | `NullDriveDiscType`, `q_subchannel`, the GD-ROM sched entry |
| 11 | `naomi_Serialize` / `naomi_cart_serialize` | `core/hw/naomi/*` | arcade only |
| 12 | `Broadcast` / `Cable` / `Region` | `core/serialize.cpp:55-57` | sync-critical config, in the blob |
| 13 | `gd_hle_state.Serialize` | `core/reios/gdrom_hle.cpp` | HLE GD-ROM |

Measured size on MvC2: **27,890,707 bytes** (`docs/SPIKE-machine-pool.md`).

### The four suspects named in the hypothesis — all registered

**Dreamcast flash. REGISTERED.** `nvmem::init` gives `DC_PLATFORM_DREAMCAST` a
`DCFlashChip` for `sys_nvmem` (`core/hw/flashrom/nvmem.cpp:331-341`), and
`DCFlashChip::Serialize` writes the command-FSM `state` **and** the full array
past `write_protect_size` (`core/hw/flashrom/flashrom.h:389-399`).
`sys_rom` on Dreamcast is a `RomChip`, which inherits `MemChip`'s deliberately
empty `Serialize` (`core/hw/flashrom/flashrom.h:71-72, 95-98`) — a BIOS ROM is
immutable, so that is a **CORRECT** exclusion. (Atomiswave flips this: its
`sys_rom` is a `DCFlashChip` and does get written.)

**VMU. REGISTERED, and generously.** `maple_sega_vmu::serialize`
(`core/hw/maple/maple_devs.cpp:337-343`) writes `flash_data[128 KB]`,
`lcd_data[192]` and `lcd_data_decoded[48*32]`. `mcfg_SerializeDevices` walks all
4 ports × 6 sub-slots and tags each with its device type, so a VMU on any port
is covered. The `FILE* file` handle is not serialized — **CORRECT**, it is a
host handle, and the file is read exactly once at `OnSetup`
(`core/hw/maple/maple_devs.cpp:393-395`) and thereafter written through only
(`:621-627`). Nothing reads it back mid-session, so the disk file cannot leak
into the guest until the next `OnSetup`.

*The one real consequence, and it is not a desync:* a VMU block written during
an abandoned re-record attempt is already on disk and a savestate load cannot
un-write it. Same for `dc_flash.bin`. This is precisely the case nbneo
deliberately declined to "fix" for `eeprom_data` — NVRAM surviving a power cycle
is what the hardware does, so clearing it would be the *less* faithful choice —
and flycast lands in the same place for the same reason, except that our studio
sidesteps it entirely: `DETERMINISM.md` states initial conditions (BIOS, flash,
VMU, boot RTC) are pinned "by starting from a savestate anchor, not by anything
here", and a savestate anchor carries flash and VMU inside it.

**RTC. REGISTERED, and additionally pinned.** `RealTimeClock` is in the AICA
block (`core/hw/aica/aica_if.cpp:542`), and `GetRTC_now`
(`core/hw/aica/aica_if.cpp:37-42`) returns a constant under any
record/replay/netplay flag, so wall time cannot enter at boot either.

**ARM7 / AICA. REGISTERED.** Every mutable ARM7 global is written
(`core/hw/arm7/arm7.cpp`: `arm_Reg`, `armMode`, `armIrqEnable`, `armFiqEnable`,
`Arm7Enabled`, `arm7ClockTicks`); the only survivors are `cpuBitsSet`, a
constant LUT, and a static-init guard. The AICA "register mirror" globals
`CommonData`, `DSPData`, `SCIEB/SCIPD/SCIRE`, `MCIEB/MCIPD/MCIRE` and
`dsp_out_vol` are `const` pointers *into* `aica_reg`
(`core/hw/aica/aica_mem.cpp:13-23`), which is serialized whole — so they are
covered by construction, not by omission.

### What is NOT in the blob, and whether that is right

| region | site | verdict | argument |
|---|---|---|---|
| SH4 dynarec block cache, `smc_hotspots`, ARM7 recompiler code | `core/hw/sh4/dyna/*`, `core/hw/arm7/arm7_rec.cpp` | **CORRECT** | derived from guest memory, and `dc_loadstate` explicitly invalidates all of it before deserializing: `aica::arm::recompiler::flush()`, `mmu_flush_table()`, `bm_Reset()`, `memwatch::unprotect/reset`, then after: `mmu_set_state()`, `sh4_cpu.ResetCache()`, `KillTex` (`core/emulator.cpp:797-814`) |
| `sh4_sched_next_id` | `core/hw/sh4/sh4_sched.cpp:35` | **CORRECT** | recomputed in full by `sh4_sched_ffts()` (`:47-71`), which `dc_deserialize` calls last (`core/serialize.cpp:163`). It was in the blob up to V31 and was removed on purpose (`core/hw/sh4/sh4_sched.cpp:255`) |
| TA parser scratch — `BaseTAParser::CurrentList`, `CurrentPP`, `TaCmd`, `tileclip_val`, `FaceBaseColor*`, `lmr`, `VertexDataFP`, `vd_ctx` | `core/hw/pvr/ta_vtx.cpp:145-176` | **CORRECT** | per-parse scratch. `ta_parse_vdrc` calls `ta_parse_reset()` as its first act (`core/hw/pvr/ta_vtx.cpp:1197-1202`), so nothing survives one render; the authoritative input is the recorded display list, which *is* serialized (`serializeContext`, `core/hw/pvr/ta_ctx.cpp:217-235`) |
| TA context pool / render queue — `rqueue`, `ctx_pool`, `mtx_pool`, `frame_finished`, `RenderCount` | `core/hw/pvr/ta_ctx.cpp` | **CORRECT** | host render-thread plumbing; `ctx_list` and the current context *are* serialized (`:261-272`) |
| `pend_rend`, `fbAddrHistory` | `core/hw/pvr/Renderer_if.cpp:551-553` | **CORRECT**, but note the shape | not saved, and force-reset on load rather than restored. This is the mirror-image class nbneo hit ("saved but not initialised"): here it is "not saved, and initialised on load". Both are the save set and the init set disagreeing. These two are host-side framebuffer bookkeeping, so the constant is defensible — but it is a *decision*, and it is undocumented at the site |
| `maple_DoDma()::last_kcode` | `core/hw/maple/maple_if.cpp:159` | **UNCLEAR**, and irrelevant here | a function-scope static holding last frame's `kcode` for card-reader insert-edge detection. Guarded by `settings.platform.isNaomi()`, so it is dead for a Dreamcast `.cdi`. On NAOMI a load could synthesize or swallow one card insertion. Genuinely unregistered; genuinely not our platform |
| `SH4FastEnough`, `cpu_cycles[]`, `real_times[]`, `cpu_time_idx`, `fskip` | `core/hw/pvr/spg.cpp:34-42, 160-172` | **CORRECT to exclude, but it is a host clock in the loop** | `SH4FastEnough` is computed from `os_GetSeconds()` and consumed at `core/hw/pvr/ta_ctx.cpp:60` to decide whether to wait for the render thread — which, if it does not wait, can drop a frame and change `render_end` scheduling, i.e. guest-visible timing. It is reset by `spg_Reset` (`core/hw/pvr/spg.cpp:270-281`) and it only bites when `pvr.AutoSkipFrame != 0` (default 0) with threaded rendering on. **Already known:** both `pvr.AutoSkipFrame` and `rend.ThreadedRendering` are classified sync-critical (`core/determinism.cpp:138,152`; `SYNC_SETTINGS.md:141,161`). Serializing it would be *wrong* — the fix is the manifest, and the manifest already has it |
| `dojo.frame_number` | `core/dojo/dojo.h` | **WRONG in principle, already mitigated twice** | it indexes `session_inputs` under recording, so it is execution-relevant, and `dc_serialize` does not touch it. The disk path compensates with the `.frame` sidecar; the Lua/in-memory path appends a 12-byte `DOJOFRM1` trailer (`core/lua/lua.cpp:1173-1187`, read back at `:1198-1212`). Two mechanisms for one fact. This is a genuine instance of the hypothesis class — and the SPIKE explicitly **exonerated** it as the cause of the pool drift |
| main RAM, ARAM, VRAM, ELAN RAM under `rollback` | `core/serialize.cpp`, `core/hw/pvr/pvr.cpp:101`, `core/hw/aica/aica_if.cpp:538` | **CORRECT, and not our path** | skipped only when `Serializer::rollback()` is true, i.e. GGPO rollback snapshots, which restore bulk memory from `memwatch` page deltas instead (`TODOS.md:319-335`). `dc_savestate`/`dc_loadstate` construct their serializers with `rollback = false` (`core/nullDC.cpp:119`, `:338`), so a TAS savestate always carries full memory |

**Nothing found with the shape of `eeprom_data[1024]`** — a machine-tier array
whose registration exists but is disabled, or which was simply never added. I
looked specifically: 1,252 mutable statics across `core/` is the raw population
(`docs/SPIKE-machine-pool.md`), and I checked the machine-tier subset with the
largest counts (`aica_mem`, `sgc_if`, `ta_vtx`, `ta_ctx`, `spg`, `pvr_mem`,
`maple_if`, `arm7`, `sh4_mem`, `tmu`, `sh4_interrupts`, `serial`,
`Renderer_if`) symbol by symbol against the serializers. Everything mutable and
machine-tier was either in the blob, a constant LUT, an alias into a serialized
array, or one of the rows above.

That is a survey, not a proof. A tiering pass at nbneo's rigour over all 1,252
is the only thing that would be a proof, and `docs/SPIKE-machine-pool.md` costs
that at months.

---

## 2. Why `verifyLoadedStateIdempotent` cannot see this class

The reasoning in the hypothesis is correct. The formal version:

Let **R** be the registered set (everything `dc_serialize` reads) and **U** the
unregistered remainder. `verifyLoadedStateIdempotent`
(`core/nullDC.cpp:203-266`) is handed `blobA` — the bytes just loaded from disk
— re-runs `dc_serialize` into a fresh buffer (`:211-219`) and compares byte for
byte (`:225-227`).

After a load, the machine `M'` has `R(M') = R(M_saved)` and
`U(M') = U(M_before_load)`. The re-serialization reads **only R**. So the
comparison is `S(R(M_saved)) == blobA` — a statement about whether
`deserialize` inverts `serialize` on R, and **U does not appear on either
side**. A region that is never serialized is trivially stable across the round
trip.

The one way U leaks into the probe is if a `deserialize` routine *derives* an R
field from a U field. flycast's derivations run the other way — `sgc`'s
`UpdatePitch`/`UpdateAEG`/`UpdateLoop` (`core/hw/aica/sgc_if.cpp:1593-1636`)
recompute from `aica_reg`, which is in R — so there is no accidental coverage.

**Corollary worth writing down:** `flycast.savestate.hash()` is the same
function (`core/lua/lua.cpp:1300-1315` → `serializeStateWithTrailer` →
`dc_serialize`). Every Lua determinism assertion in this tree fingerprints R
only. A hash comparison can still catch an unregistered region — but only
*indirectly*, by letting the machine run and observing that U perturbed R. That
is exactly why the detection design in §4 is a **sequence over a window**, not
a comparison at the instant of load.

---

## 3. Does it explain anything we actually saw?

**No. Plainly: this is a separate class from every defect on record here.**

The three fixed bugs in `docs/SPIKE-machine-pool.md` were all
registered-region defects — deserialize failing to invert serialize — which is
the one thing the probe *is* built to see, and it saw all three:

| first diff at | subsystem | what it was |
|---|---|---|
| 2,130,953 | AICA | EG step handlers re-seeded on load, overwriting just-restored values (`RestoreAegState`/`RestoreFegState`, now at `core/hw/aica/sgc_if.cpp:1619,1625`) |
| 11,076,198 | SH4 SCIF | `updateBaudRate` rescheduling from "now" instead of honouring the restored sched entry |
| 27,884,792 | `Sh4Context::sh4_sched_next` | derived field written stale; fixed by calling `sh4_sched_ffts()` before the `cntx` write (`core/hw/sh4/sh4_mmr.cpp:709-711`) |

An unregistered region could not have produced any of those — it would have
produced *no* diff at all.

**The residue those fixes left is also accounted for, and not by this
hypothesis.** The pool divergence that survived them (`A B A B A B` parity,
first differing byte in `pvr.spg`'s `clc_pvr_scanline`, 448 cycles of
sub-scanline phase) was solved: both halves of the save/load had to happen at
the deferred point (`deferred::drain()` at the top of `mainui_rend_frame`),
because a restore from a `vblank` callback inherits the emulation loop's
in-flight slice and a **save** from that callback can *tear* — `emu.stop()`
cannot join the thread it is called on. Ten restores in one process then agreed
by gap, with `STATE VERIFY: idempotent OK` on every load. That is a complete
explanation of the symptom, and it needs no missing region.

### The caveat, and it is the one the sibling project paid for

`docs/SPIKE-machine-pool.md` also carries an elimination table — recompiler
block cache, `smc_hotspots`, threaded rendering, audio backend, RTC, movie
input stream — each "ruled out" by toggling **one** candidate and observing the
member hashes not change. nbneo's recorded experience is that this exact
experiment shape returns false negatives: two of their three cold-boot causes
**hid each other**, an earlier session tested one alone, saw the divergence not
shrink, and wrongly concluded it was not a cause — it was 60 of 67 differing
lines swamped by the other. **Any conclusion in this tree that rests on a
single-cause ablation is provisional.** In this instance it does not matter,
because the real cause turned out to be the save/load *place*, which confounded
every row of that table anyway.

**What does not rest on an ablation, and is therefore the load-bearing
evidence against the hypothesis:** the ten-member gap run is a *positive
reproduction* test. Members 2, 4, 6, 8 and 10 each restored the same blob after
a **different** preceding history (5, 9, 5, 7 and 20 frames of post-restore
execution) and each produced `hash=1609759635` at frame 465. Five different
pre-load timelines, one answer. If an execution-relevant region were carrying
the pre-load timeline forward, those five would have disagreed.

**Its scope, stated honestly.** The histories differed by 5–20 guest frames.
That is a real differential, but a small one, and it is nowhere near enough to
touch a memory card, a flash block, or anything else that changes on the scale
of a menu action. Borrowing state-torture's phrasing: a region this window does
not exercise staying green is **scope, not a pass**.

---

## 4. How we would detect it

### What the existing harnesses do and do not cover

**`scripts/tests/pool_determinism.lua`** — snapshot at vblank 400, then three
restores, each running exactly 6 frames, comparing three endpoint hashes.
- ✓ regression-guards the deferred save/restore path.
- ✗ **constant cadence**, so the pre-load history is nearly identical between
  members 2 and 3 — almost no differential.
- ✗ **endpoint hash, not a sequence** — cannot say *which* frame diverged, and
  cannot distinguish a run that never diverged from one that diverged and came
  back.
- ✗ **no vacuity gate.** It never checks that the hash *changed* across its
  window. A frozen machine passes it.

**`scripts/recordtest.sh`** — record a movie, replay it in a second process,
compare a per-frame hash sequence element for element, with an explicit vacuity
gate (`UNIQUE -le 1` → refuse). This is the better-built harness and its
per-frame-sequence + vacuity idiom is exactly right — but it compares **two
fresh processes that boot the same way from the same seed state**, so an
unregistered region holds the same value in both. It is structurally blind to
this class too, for a different reason than the probe is.

**`scripts/determinism_anchor.lua`** — hash at frame N, replay from power-on,
hash at frame N again. Closest thing we have, but it is movie-scoped, driven by
hand from the Lua console, and not in `scripts/tests/`.

So: **nothing in the tree can currently fail because of an unregistered
region.**

### The check: differential-history restore

An extension of `pool_determinism.lua`, not a new harness — same primitives
(`snapshotLater` / `takeSnapshot` / `restoreLater` / `hash`), same `testlib`
contract, same runner. Proposed as `scripts/tests/state_coverage.lua`:

1. `t.ran{...}` — a movie is playing, frames advance, the deferred API exists.
2. Warm up to a frame where the machine is *moving*. Snapshot → blob **B**.
3. **Trial A:** `restoreLater(B)`; record `hash()` once per frame for W frames →
   sequence `A[1..W]`. Also record `H0` — the hash immediately after restore,
   before any frame runs.
4. **Interlude:** run K frames of a *deliberately different* history, K ≫ W
   (300+), and if reachable, do something that touches persistent storage.
5. **Trial B:** `restoreLater(B)`; record → `B[1..W]`, and `H0'`.
6. **Assertions.**
   - `H0 == H0'` — the registered set restored identically. This is the control:
     if it fails, the fault is in R and this test is not the right instrument.
   - `A[i] == B[i]` for every `i`. Any mismatch is a region that influenced
     execution and was not restored. Report the first differing `i` and both
     hashes — `scripts/lua/pool-bisect.lua` already binary-searches the first
     differing byte and SERMAP already names its subsystem.
   - **Vacuity gate, borrowed verbatim from `recordtest.sh`:** `A[1..W]` must
     contain **more than one distinct hash**, and `A[W] != H0`. A still machine
     reproduces itself perfectly and proves nothing. Failing this is
     INCONCLUSIVE, not PASS.

Why it works where the probe cannot: at each restore, U holds whatever the
machine had, and trials A and B reach their restore through histories that
differ by hundreds of frames. If any byte of U influences execution, the two
W-frame sequences separate.

### The `_can_fail` twins — mandatory, and this needs two

**(a) The vacuity twin.** Run the same script seeded on a still screen. The gate
must trip and the verdict must be INCONCLUSIVE, not PASS. This belongs in
`scripts/tests/negative/`, whose README already states the rule
("a test harness that has never reported a failure is not known to be able to")
and whose files are named by the verdict they must produce.

**(b) The sabotage twin — the one that proves it detects *this class*.**
state-torture's `--sabotage N` splices one region out of the restore, so the
loaded blob is the save-point state *except* that region, which keeps its later
value. We can do the same with **no engine change at all**: in trial B only,
immediately after `restoreLater(B)` returns, use `flycast.memory.write32`
(`core/lua/lua.cpp:2047`) to poke one word of guest RAM back to a value the
machine held later in the interlude. That word is then, by construction, a
region the load failed to restore — produced through the real load path, not
simulated. Expected: trial B's sequence diverges, and the reported first
differing frame is at or shortly after the poke.

Pick the address by watching a word that changes every frame — the memory-watch
bindings (`watchCreate`/`watchChanged`) are already there for that. A sabotage
of a word this window does not read staying green is information too: it names
scope, not a pass.

### The missing probe, separately: cold-boot twice and compare

nbneo's diagnostic was **boot twice and compare** — a different instrument from
ours. It catches *init residue* (state left over from a previous game-init that
the new one does not clear) and says nothing about save coverage; ours says
nothing about init residue. Their causes were a CPU whose registers were zeroed
once per process rather than per game-init, and two fields saved into savestates
but never cleared at init — the mirror image of the hypothesis here, and both
are "the save set and the init set disagree".

**flycast has no equivalent test.** `docs/SPIKE-machine-pool.md` records one ad
hoc measurement in this shape (plain forward execution reproduced across
processes, `2608417142` twice) but it is not a harness. Recommended:
`scripts/tests/coldboot_twice.sh` in `recordtest.sh`'s idiom — two sandboxed
processes, cold boot from the same ROM with no savestate, per-frame hash
sequence over a moving window, vacuity gate, compare element for element.
Cheap, and it is the only probe that would catch a flycast analogue of their
causes 1 and 2.

The related audit it enables, which I did **not** do: walk every field written
by a `serialize` and confirm `reset(hard)` sets it. The one instance of the
shape I noticed is `rend_deserialize` (`core/hw/pvr/Renderer_if.cpp:551-553`)
forcing `pend_rend`/`fbAddrHistory` to constants on load rather than restoring
them — defensible, undocumented at the site.

---

## 5. The one experiment to run first

**Run the differential-history restore once, with an extreme history contrast,
in a single session.** It is the cheapest thing that can kill the hypothesis
outright, and if it does not kill it, `pool-bisect.lua` + SERMAP name the
subsystem in the same session.

Concretely, as a `scripts/tests/state_coverage.lua` driven by
`scripts/testrun.sh`:

- warm up to a frame where the fight is moving (the seed rule from
  `recordtest.sh`: a still screen makes the run vacuous);
- snapshot **B**; record `H0`;
- **trial A** — restore B, hash once per frame for **W = 12** frames;
- **interlude** — run **900** frames; if it can be reached from the script,
  spend some of them in a menu or on a save prompt, so anything VMU- or
  flash-shaped is actually touched;
- **trial B** — restore B, hash 12 frames;
- assert `H0 == H0'`, assert the two 12-element sequences are equal, and gate on
  trial A containing ≥ 2 distinct hashes.

**Reading the result.**
- *Sequences equal, gate satisfied* → the hypothesis is dead for everything a
  900-frame history difference perturbs, which is a far stronger statement than
  the 5–20-frame differential we currently have. Record the window as the scope.
- *Sequences differ at frame k* → we have the bug, in one run, with a frame
  number. Keep both trials' final blobs and hand them to
  `scripts/lua/pool-bisect.lua`, which binary-searches the first differing byte;
  SERMAP (`dojo:StateMapLog=yes`, plus the sub-maps in
  `core/hw/sh4/sh4_mmr.cpp` and `core/hw/pvr/pvr.cpp`) turns that offset into a
  subsystem name. Note the diff will land on a *downstream* effect in RAM, not
  on the unregistered region itself — the region is not in the blob to be
  diffed. Bisecting in **time** (shrink W to 1, then walk the interlude length
  down) localises the cause; the byte offset only localises the symptom.
- *Gate trips* → INCONCLUSIVE. Reseed and rerun; do not report it as a pass.

Do the sabotage twin (§4b) in the same sitting. A green run from a check that
has never gone red is not evidence.

---

## 6. RESULT — the experiment was run, 2026-09-08

`scripts/tests/differential_history.lua`, in the default suite (`ctest -R lua`).
**13 passed, 0 failed.**

    .. trial A  landed at frame 63  first=2264538091 last=1709169422
    .. trial B  landed at frame 63  first=2264538091 last=1709169422
    PASS  history does not leak: same state, same future, across 600 frames
          of emulation  12/12 hashes identical
    .. trial C  landed at frame 63  first=2496625779 last=1053831375
    PASS  the comparison DETECTS a one-word difference
    PASS  every trial resumed at the same frame  A=63 B=63 C=63

**Reading it, per §5.** Sequences equal and the gates satisfied, so the
hypothesis is dead for everything a 600-frame history difference perturbs. That
is the scope, and it is much wider than the 5–20 frames the ten-member gap run
covered — but it is still an interlude of ordinary emulation, not one known to
write persistent storage. §3's verdict stands unchanged: nothing here explains
the earlier savestate trouble, and the SPIKE's elimination table stays
provisional for the reason nbneo recorded.

600 rather than the proposed 900: the clip is seeked to a savestate near its end,
so the budget is the frames remaining after the seek. Overrunning ends the replay
and the callback stops, which reads as a timeout rather than a failure — a
harness artefact that would look like a result.

### The gates, and what each one caught

All three exist because a "12/12 identical" result is exactly what a test that
is not running would also produce.

- **Vacuity** — trial A's twelve hashes must not all be equal. A frozen machine
  makes every comparison below true for free.
- **The interlude moved the machine** — an interlude that changes nothing tests
  nothing.
- **Every trial resumed at the same frame** — the frame counter is itself
  restored state, and if the trials resumed at different frames the sequences
  were never comparable.

### Both sabotages, and the design bug the first one found

- *Trial B does not restore at all* → caught, but by **"the restore landed
  within 60 frames"**, not by the history claim. That is the right
  attribution — "no restore happened" is a different fact from "the futures
  diverged" — but it means the headline claim was still unproven, so:
- *Trial B perturbed by one word* → **"history does not leak" FAILS at hash 1**
  while "every trial resumed at the same frame" still passes (A=63 B=63 C=63).
  The failure is attributed to divergence rather than to misalignment, which is
  the property that makes the green run worth anything.

The first version of the test **failed**, and was right to. It aligned the trials
by waiting a fixed 8 frames after `restoreLater`, on the stated reasoning that
adaptive alignment would hide a real divergence. That reasoning was wrong. Trial
A ended at frame 81 and trial B at 82, so hash *k* of one was compared against
hash *k+1* of the other and the run reported "history leaks, first difference at
hash 1" — a false positive that looked exactly like the bug being hunted.

**When the host services a deferred restore is host scheduling, not guest
state.** The fix aligns on the guest's own clock: a restore makes the frame
counter jump backwards, and that edge is the moment the machine is at the
restored state. It means the same thing in every trial, and it is what made all
three land on frame 63.

Worth keeping in mind for §4's cold-boot-twice probe, which is still missing and
will need an alignment rule of its own.

---

## 7. The cold-boot-twice probe — what was built, and what is blocked

§4 named this as the missing probe: it is what caught nbneo's init residue (a
CPU zeroed once per *process* rather than per game-init, and sprite fields saved
into states but never cleared at init). It was attempted on 2026-09-08. **The
variant that would catch that class cannot be built yet**, for two independently
measured reasons, and the variant that *can* be built is a different test.

### Blocker 1 — the residue class needs TWO INITS IN ONE PROCESS

This is the part worth being precise about, because it is easy to build the
wrong thing and believe the question is answered.

Init residue is state a *process* carries into a second game-init. Two separate
processes each initialise exactly once, so **both carry identical residue and
therefore agree** — a cross-process comparison cancels the very effect it would
be looking for, and passes. Only a second boot inside one process can see it.

An in-process restart needs `emulator.stopGame()` from a Lua callback.
**[MEASURED] it wedges**: the call does not return and the run times out with
the game panel still drawing. That is the same wall recorded at
`core/lua/lua.cpp` ("NO savestate.loadLater HERE — IT DID NOT WORK"), where four
variants of a deferred load all wedged. Unblocking this is engine work on the
stop/restart path, not test work.

### Blocker 2 — a cold boot does not complete on this machine

`[MEASURED]` There is no BIOS present (`emulator.cpp:518`, "Did not load BIOS,
using reios"), and the HLE fallback stalls on this title: the emulator's own log
stops 0.4 s in at "REIOS: Booting up" and **no further frame is emulated in ten
minutes**, while the Lua callback fires once and never again.

This also explains, more concretely than "cold boot is nondeterministic", why
every test in this tree starts from a savestate: on this machine a cold boot
does not start at all.

### What was built instead: `scripts/reprotest.sh`

Cross-process reproducibility, which was **also** missing and is worth having on
its own. Every other test here runs in one process, so none of them can see
process-level nondeterminism — both halves of their comparison share it.

    ok   run1 == run2  (12 frames of hashes, identical)
    ok   the poked run differs - the comparison can fail
    reprotest: reproducible across processes

Two separate processes, same deterministic input, identical per-frame hashes
keyed by the **guest** frame number. `--self-test` adds the sabotage arm: a
third process pokes one word of guest RAM and must disagree, because a
comparison that cannot report a difference agrees with everything.

`--cold` is the same harness with the seek removed. It SKIPs (77) here, checking
for a BIOS first so the skip costs milliseconds rather than two stalled boots.
Registered in ctest as `flycast.crossprocess_cold` **even though it only skips**,
so the gap stays visible in test output rather than only in this file. When a
BIOS makes it run it will test cross-process boot reproducibility — still not
init residue.

### Gates, and the two that fired during construction

- **No samples is a SKIP, never a pass.** It fired immediately: an extraction
  bug (`^REPRO` anchored past the Lua console's indent) made both runs look
  empty, and the gate reported SKIP instead of "two empty files are identical".
- **`--runs 1` is refused.** One run walks past an empty comparison loop and
  prints "reproducible across processes" having compared nothing. Closed as a
  usage error.
- **The BIOS check was shown able to pass**, by planting a dummy `dc_boot.bin`
  and confirming the run proceeded to the next gate. A precondition that can
  only ever skip is indistinguishable from a disabled test.
- **The shared log is deleted before every run.** `testrun.sh` writes one fixed
  path, so a run producing no log leaves the PREVIOUS run's in place - and two
  reads of one file are identical, which is this harness reporting perfect
  reproducibility from a run that never happened. The no-samples gate cannot
  catch it, because the stale file has samples. Found by review, not by a
  failure; it had not fired.

One suspicion that did NOT survive checking, recorded because the reasoning
matters more than the outcome: the ctest entry finishes in 14 s, which looked
impossible for three emulator runs and exactly like the stale-log bug above.
It is legitimate. This script only needs `frame.count() >= 100`, the auto-seek
lands near frame 63 about 2.3 s into boot, so a run is ~5 s rather than the ~50 s
`differential_history` takes. The decisive evidence is that the poked run
*differed*: a shared stale log would have made all three identical.

### Standing recommendation

The strong probe remains unbuilt and is now blocked on one specific thing: an
in-process restart that does not wedge. That is the same defect as
`savestate.loadLater`, so fixing either likely fixes both, and it would buy the
deferred-restore work and this probe at once. Until then the init-residue
question is **open, not answered** — nothing in §1's audit found a candidate,
but that audit was by inspection, and inspection is what nbneo's earlier session
also relied on before measurement contradicted it.

---

## 8. Blocker 1 is fixed — the in-process restart works

§7 recorded the strong probe as blocked on "an in-process restart that does not
wedge". That is done, 2026-09-08. **`flycast.emulator.restartLater()`**, and
`scripts/tests/restart_inprocess.lua` is the regression test.

Two separate defects had to be fixed, and both presented identically — as an
emulator that boots and then does nothing.

### The deadlock

`vblank` is dispatched from `Emulator::vblank()` on the **emulation thread**.
`Emulator::stop()` calls `checkStatus(true)` → `threadResult.get()`, which blocks
until that thread finishes. From `vblank` that is the thread waiting for itself.
Not a race — it can never work from there.

`core/deferred.h` already documented this hazard in prose ("emu.stop() joins the
thread it is called from - deadlock"). The safe point existed; what was missing
was a restart that used it. `restartLater()` posts to `deferred::post`, which
drains on the main thread at the top of `mainui_rend_frame` — the same point the
auto-seek block already calls `gui_loadState` from.

Two details that are not obvious and both bite:

- **`gui_start_game`, not stop-then-start.** It calls `emu.unloadGame()` itself,
  and `gui_stop_game` branches on `commandLineStart`: with a ROM on the command
  line — every harness here — it calls `dc_exit()` and *quits the emulator*
  rather than returning to the menu.
- **The path is captured at post time.** `Emulator::unloadGame()` clears
  `settings.content.path`, so an action that read it when it ran would find it
  empty.

### The latch, which is the interesting one

With the deadlock gone the restart completed — and the second machine still
emulated nothing for two minutes. Every log line was identical to the first boot
up to one that was missing:

    00:00:959  TAS TEST: auto-play -> un-pausing the replay (no hotkey headless)

`autoPlayDone` and `autoSeekDone` in `mainui.cpp` were function-local statics, so
they fired once per **process**. A replay boots PAUSED and headless has nobody to
un-pause it; the second boot sat at `GuiState::Paused` forever. They now re-arm
on `Event::Terminate`, which `unloadGame` raises and every game start runs first.

**This is the class of defect this whole document is about — state that survives
an init because nothing had ever re-inited before — found in the harness rather
than in the emulated machine.** Worth keeping in view: the first thing an
in-process restart found was residue in the code doing the restarting.

### A guard, because the failure was undiagnosable

`stopGame`/`startGame` from `vblank` now refuse with one error line naming
`restartLater()`, instead of hanging. `scripts/tests/emu_thread_guard.lua` is its
test, and the regression it protects against is *no answer* rather than a wrong
one: **if that test ever times out instead of failing, the guard is gone.**

### The signal, reported and NOT claimed

The first run of the restart test produced this:

    boot 1: frame=10 hash=1521734835
    boot 2: frame=10 hash=2786697748

Two boots in one process, sampled at the same frame, different state — the shape
the cold-boot-twice probe exists to detect. **It is not yet evidence of init
residue**, and the test records it as a `limit`, not a pass or a failure. These
two boots are not a controlled pair: boot 2 loads a `skip.map` that boot 1 wrote,
and other host state carries across. Turning this into an answer means removing
those differences one at a time — and per §5's own warning, and nbneo's, a
single-cause ablation can return a false negative when two causes mask each
other.

What has changed is that the question is now **askable**. It was not before.
