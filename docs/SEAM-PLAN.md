# SEAM-PLAN — what to type

`[WRITTEN 2026-09-08]` The implementable half of `docs/MODULARIZATION.md`. That
document argued *why* six seams exist; this one says what to type, in what
order, and what test proves each step. It does not re-argue the case.

Marks follow `CLAUDE.md`:

- **`[MEASURED <date>]`** — a number this pass produced, with the command in §7.
- **`[SOURCE]`** — read out of this checkout. **The quote is the citation; the
  line number is a hint**, because line numbers rot.
- **`[REASONED]`** — derived from source, not run.
- **`[OPEN]`** — a question this pass could not settle.

## Scope

Out of scope, because they are done or underway:

- **S1** (panel registry) — **BUILT.** `core/rend/panel.{h,cpp}`,
  `core/dojo/tas_ui.{h,cpp}`, `core/dojo/tas_colors.h`. Every API below is
  written to match that header's style: a file comment that names the defect it
  prevents, `//!` on each declaration, and a `selfTest()` gated on a cfg flag.
- **S2** — **PARTLY done.** `[SOURCE]` `core/rend/gui.cpp:4642` now reads
  `if (dojo.frame_number == dojo.MovieEnd())`, and `core/dojo/dojo.cpp:1985`
  reads `dojo.frame_number == dojo.MovieEnd() - 1`. §1 finishes it — **and
  reports that those two converted sites do not agree with each other.**
- **S6** (the `Dojo` god object) is never a step. Each seam below carries a
  "where this turns into S6" note instead.

## The ranking, by value-per-risk

| | seam | value | risk | verdict |
|---|---|---|---|---|
| **1** | **S2** finish — `movie::` timeline | correctness, and the piano roll asks it per row | **lowest**: 8 live sites, 5 of them renames | **do first** |
| **2** | **S5** session predicates | S1's `enabled()` has no other home; 14 panels otherwise grow 14 conjunctions | low for the enum, high for the boot handoff | **do the enum; do NOT do the boot handoff** |
| **3** | **S3** `dojocfg` | the port doubles the key surface (57 → ~133) | moderate, and it is 184 mechanical edits | **do stages 1–2 only** |
| **4** | **S4** stopping | correctness, but the population that needs it has not arrived | **highest**: `deferred.h` records that the obvious version of this *does not work* | **marker yes, rewrites no** |

**The single highest-value stage in this document is S2 Stage 3** — the
`atEnd()` conversion of `gui.cpp:4642` and `dojo.cpp:1985`. Not because it is
the largest, but because it is the only stage that fixes a defect that already
exists in the tree today (§1.2), and it is ~6 lines.

**What I would not do:** the S4 stop/start rewrites (§4 Stage 3), and the S5
`BootHandoff` enum (§3, "declined"). Reasons in place.

---

## 1. S2 — finishing the movie timeline

### 1.1 Every remaining `session_inputs.size()`, classified

`[MEASURED 2026-09-08]` `grep -rn 'session_inputs\.size()' core --include='*.cpp'
--include='*.h' | grep -v deps` → 24 hits, of which 8 are comments and 16 are
live code. The question at each site is one of three:

- **COUNT** — "how many frames were authored". A stats number. `.size()` is
  right; it becomes `movie::count()` so the choice is visible.
- **END** — "where does the timeline end". `.size()` is **wrong** on any movie
  not keyed from frame 0. Becomes `movie::end()` / `movie::atEnd()`.
- **HAS** — "is frame N authored". Becomes `movie::has(n)`.

| # | site | expression | verdict | reasoning |
|---|---|---|---|---|
| 1 | `core/rend/gui.cpp:4624` | `frame_number >= session_inputs.size() - 5` | **COUNT** | Inside `if (cfgLoadBool("dojo","Receiving",false))` (`:4618`). A received stream arrives in order over TCP and is dense by construction; the comment already says so. *But see the underflow note below.* |
| 2 | `core/rend/gui.cpp:5016` | `dojo.buffering && frame_number == size()` | **COUNT** | `gui_open_step`. Guarded on `dojo.buffering`, which is only ever set true at `gui.cpp:4627` — inside the Receiving branch. Netplay-only. |
| 3 | `core/rend/gui.cpp:5065` | `dojo.buffering && frame_number == size()` | **COUNT** | `gui_open_pause`. Same `buffering` guard, same reasoning. |
| 4 | `core/dojo/dojo_gui.cpp:2441` | `buffering && (size() - frame_number) >= RxFrameBuffer` | **COUNT** | `show_pause`. Same `buffering` guard. Compares against `config::RxFrameBuffer` — a receive-buffer depth, so the question is unambiguously "how many arrived". |
| 5 | `core/dojo/dojo_gui.cpp:2525` | `size() == 0` | **COUNT** | `gui_display_stream_wait`, inside `if (cfgLoadBool("dojo","Receiving",false))` (`:2523`). |
| 6 | `core/dojo/dojo_gui.cpp:2529` | `(float)size() / RxFrameBuffer` | **COUNT** | Same block. A progress bar over the receive buffer. |
| 7 | `core/dojo/dojo_gui.cpp:2536` | `"%d / %d Frames", size(), RxFrameBuffer` | **COUNT** | Same block. |
| 8 | `core/dojo/dojo_gui.cpp:2542` | `size() > RxFrameBuffer` → `buffer_captured` | **COUNT**, with a caveat | This one is **outside** the `if (Receiving)` block. It is netplay-scoped only because the whole function is reached only from `GuiState::StreamWait`, entered at `gui.cpp:4337` under `cfgLoadBool("dojo","Receiving",false)`. A precondition three files away — the same shape §4 flags as S4's riskiest. Convert to `count()` **and put the guard in a comment at the call site.** |
| 9 | `core/dojo/replay.cpp:112` | log `"(%u of %u frames)"` | **COUNT** | `dojo:ResizeProbe` diagnostic. Genuinely "how many exist". |
| 10 | `core/dojo/replay.cpp:115` | log `"movie now %u frames"` | **COUNT** | Same probe, after `ApplyEditResize` renumbers (so the result is dense anyway). |
| 11 | `core/dojo/dojo.cpp:869` | `movie_len_at_begin = size()` | **END** | Paired at `:950` with `size() != movie_len_at_begin` to answer *"did the movie grow or shrink since the clip opened"*. A macro Full boot injects a roll keyed from State 0's frame (`InjectPendingMacroAt`, `:1913`), which changes `end()` and can leave `count()` unchanged if it replaces rather than extends. The declaration's own comment already says `0 = the roll arrives later, e.g. a macro Full boot` — it knows about the case and uses the wrong metric for it. **Convert both to `movie::end()`.** |
| 12 | `core/dojo/dojo.cpp:929` | `st["frames"] = size()` (clip.json) | **COUNT** | The comment above it is explicit and correct: *"The frame counter is a position, not a length."* `frames` and its derived `durationSeconds` are stats. Rename only. |
| 13 | `core/dojo/dojo.cpp:950` | `size() != movie_len_at_begin` | **END** | See #11. Same edit, same commit. |
| 14 | `core/dojo/dojo.cpp:1153` | `e["movieFrames"] = size()` (clip.json `generations[]`) | **COUNT** | schema 6. `[SOURCE]` `tas_clip.cpp:422` states the intent: *"stats.frames -> movieFrames (the movie's actual length as the session saw it)"*, i.e. the same quantity as #12. Keep them identical, whatever they are. |
| 15 | `core/dojo/dojo.cpp:1174` | `NOTICE_LOG(... "%u movie frames")` | **COUNT** | Prints #14. Must not disagree with the field it narrates. |
| 16 | `core/dojo/dojo.cpp:1214` | `snapshot_prompt_movie = size()` | **COUNT** | The F8 prompt's display copy of #14. |
| 17 | `core/dojo/dojo.cpp:697` | sidecar v2 `movieLen` | **`[OPEN]` — CANNOT DECIDE** | See §1.3. |
| 18 | `core/lua/lua.cpp:1783` | `getReplayFrameCount` | **COUNT, but the binding is broken in a second way** | See §1.4. |

`core/dojo/dojo.cpp:2642` is inside a `/* … */` block and is not a site.

**The `- 5` underflow at #1.** `session_inputs.size()` is `size_t`; on an empty
map `size() - 5` wraps to `SIZE_MAX - 4`, and `frame_number >= that` is false, so
the branch is correct **by accident**. `movie::count()` returns `u32`, which
wraps to `0xFFFFFFFB` and is *also* false against a `u32` frame number — so the
rename is behaviour-preserving. `[REASONED]` Do not "fix" it in the rename
commit: a rename whose diff also changes behaviour cannot be reviewed as a
rename. If it is worth fixing, it is `movie::count() >= 5 && frame >= count()-5`
in a separate commit with its own reason.

### 1.2 The defect this seam already has — the two converted sites disagree

`[SOURCE]` The two live end-of-movie detectors are **both** on `MovieEnd()` now,
and they use different arithmetic:

```cpp
// core/dojo/dojo.cpp:1985   (emu thread, MapleApplyAction, before the frame is applied)
if (dojo.play_match && !macroReadSession && !dojo.session_inputs.empty()
        && dojo.frame_number == dojo.MovieEnd() - 1)

// core/rend/gui.cpp:4642    (render thread, gui_display_osd, after the frame)
if (dojo.frame_number == dojo.MovieEnd())
```

`[REASONED]` They are staggered by one frame and both fire, so today the visible
behaviour is "ReplayEnd, twice". That is survivable. What is not survivable is
that **the `±1` is not owned by anything**: it is written out twice, in two
threads, against two different phases of the frame, and the survey already
counted a third spelling at `dojo.cpp:1974`'s macro arm (`fn >= rbegin()->first`,
with no `+1` at all) and a fourth at `dojo.cpp:790,811` (`fn >= last`).

`MovieEnd()` closed the *metric* seam and left the *frontier* seam open. That is
exactly what `atEnd()` is for, and it is why the highest-value stage below is
six lines.

### 1.3 `[OPEN]` The sidecar's `movieLen` has no reader in this tree

`[MEASURED 2026-09-08]` `grep -rn 'movieLen' core` finds the write
(`dojo.cpp:697,700`) and the comment (`dojo.h:118`) and **no read**.
`LoadStateFrame` reads `frame`, `rerecordSeq` and `prefixHash`; the fourth u32 is
written for an external consumer (`CLIP_SCHEMA.md`, the VS Code extension).

**Verdict: leave it as `movie::count()` and say so in the schema.** Changing the
meaning of an on-disk field whose only reader is outside this repository is the
one edit in this whole document that cannot be tested here. The honest move is a
comment naming it a count, not a guess at what the extension wants.

### 1.4 `getReplayFrameCount` is broken in a way the rename does not fix

`[SOURCE]` `core/lua/lua.cpp:1781`:

```cpp
.addFunction("getReplayFrameCount", std::function<int()>([]() {
    if (dojo.play_match)
        return (int)dojo.session_inputs.size();
    return dojo.recording_started ? (int)dojo.frame_number : 0;
}))
```

One name, two quantities: a **length** during playback and a **playhead** during
recording. `docs/STUDIO-IN-EMUAPI.md` §5 already names this
(`movie.framecount()`'s documented defect). The playback arm's verdict is COUNT —
the name says count. **The recording arm is the bug**, and fixing it is a
behaviour change to a published Lua binding, so it is its own stage (§1.6 Stage
5) with a conformance note, not a line in a rename commit.

`[SOURCE]` The correct binding already exists beside it: `movie.length()` →
`getMovieLength()` → `dojo.MovieEnd()` (`lua.cpp:582`, `:1870`).

### 1.5 The API

```cpp
/*
	core/dojo/movie_timeline.h

	HOW LONG IS THE MOVIE - which is three questions, and giving them one name
	is what this file exists to stop.

	`session_inputs` is a SPARSE, frame-keyed, last-write-wins map
	(dojo.h:105), and the .flyr body agrees: each record is [u32 frame][bytes]
	and the parser does `session_inputs[frame_num] = inputs` (dojo.cpp:2626).
	What is DENSE is a netplay match, and the consumers were written for one:
	they read `.size()` as "the last frame". The two coincide exactly when the
	movie runs 0..N-1 with no gaps, which every movie that had ever been tested
	did.

	THE THREE QUESTIONS, and they are asked at 16 live sites today:

	  count()  how many frames were authored          - a STATS number
	  end()    one past the last authored frame       - the FRONTIER
	  has(n)   is this exact frame authored           - a HOLE is not the end

	dojo.h:151's MovieEnd() already answers the second and states the bill for
	getting it wrong: "a Play-Macro-Full roll is keyed from State 0's frame
	(A..A+len-1), so size() undercounts and every frontier test ... fired early
	(macro-parity audit)."

	WHY A HEADER AND NOT JUST MORE MovieEnd() CALLS. MovieEnd() closed the
	METRIC seam and left the FRONTIER seam open: the two live end-of-movie
	detectors are now BOTH on MovieEnd() and still disagree, one reading
	`== MovieEnd() - 1` on the emu thread before the frame is applied
	(dojo.cpp:1985) and the other `== MovieEnd()` on the render thread after it
	(gui.cpp:4642). The +-1 belongs to ONE function. That function is atEnd().

	THIS IS A VIEW, NEVER A STORE. Same rule as rend::game_viewport - derived
	on every read, so it cannot go stale, and there is no invalidation to get
	wrong. It reads dojo.session_inputs directly and holds nothing.

	THREADING. Every function here is a read of session_inputs, which the emu
	thread owns whenever it runs (dojo.h; lua.cpp:613 states the rule). Calling
	from the GUI thread while the machine runs is the same race it always was -
	this header does not add a lock and does not pretend to. See S4.
*/
#pragma once
#include "types.h"

namespace movie
{

//! Does the movie hold anything at all? `end()` and `first()` both answer 0 on
//! an empty movie, and 0 is a legal frame number, so the emptiness test is its
//! own function rather than a sentinel somebody has to remember.
bool authored();

//! The first authored frame. 0 for a power-on movie, which is every movie the
//! studio records; non-zero for a Lua-started clip (lua.cpp:1888) and for a
//! Play-Macro-Full roll injected at State 0's frame (dojo.cpp:1913).
u32 first();

//! ONE PAST the last authored frame. Identical to dojo.h's MovieEnd(), which
//! this replaces - that method stays as the implementation for one release so
//! the conversion is a rename per site rather than a rename plus a deletion.
u32 end();

//! HOW MANY frames are authored. A STATS NUMBER, never a frontier: it is what
//! clip.json's stats.frames and generations[].movieFrames mean, and what a
//! netplay receive buffer is counting. On a movie with a leading gap it is
//! SMALLER than end(), which is the entire bug this header exists to make
//! un-writable.
u32 count();

//! Is this exact frame authored? A HOLE MID-MOVIE IS NOT THE END, and the
//! difference is why dojo.cpp:2011's `find(fn) == end()` guard keeps its own
//! distinct meaning instead of folding into atEnd().
bool has(u32 frame);

//! THE ONE FRONTIER TEST. "Has playback run out of movie at this frame?"
//!
//! IT OWNS THE +-1, and that is the whole point. Two live detectors spell the
//! same question two ways today - `== end() - 1` before the frame is applied
//! and `== end()` after it - because each call site did its own arithmetic
//! against its own phase of the frame. Both become atEnd(frame_number) and the
//! stagger becomes a fact of one function instead of a coincidence between two.
//!
//! DEFINED AS `frame >= end()`, not `==`: a seek can land past the end
//! (dojo.cpp:811 already handles that case separately), and a frontier test
//! that only fires on equality steps over it.
bool atEnd(u32 frame);

//! count() == end() - first(): no gaps. The netplay/power-on shape.
//!
//! EXISTS TO BE ASSERTED, not to branch on. The four receive-buffer sites are
//! correct BECAUSE a received stream is dense, and today that argument lives in
//! a comment. A diagnostic that checks it turns the comment into a check.
bool dense();

//! Exercise every function above against a synthetic map, print one line per
//! claim. Gated on `dojo:SeamSelfTest`, alongside the panel registry's - same
//! reasoning: it runs before any frame exists and against the real functions,
//! not a copy.
void selfTest();

}	// namespace movie
```

### 1.6 Migration, staged

Each stage compiles and ships alone.

**Stage 1 — the header, the `.cpp`, and `selfTest()`. No call sites.**
Add `core/dojo/movie_timeline.{h,cpp}` to `CMakeLists.txt:1006-1041` (the single
`${PROJECT_NAME}` target — do **not** make a library; `MODULARIZATION.md` §"What
is NOT worth abstracting" declines that and is right). Call
`movie::selfTest()` from `core/nullDC.cpp:64`, beside `panels::selfTest()`.

> **Test.** `dojo:SeamSelfTest=yes` on any boot. The self-test builds three
> synthetic maps and asserts against them **directly, not through
> `dojo.session_inputs`** — a dense `0..9`, a leading-gap `100..109`, and an
> interior-hole `0..4, 7..9`:
>
> | map | `first` | `end` | `count` | `dense` | `atEnd(9)` | `atEnd(109)` |
> |---|---|---|---|---|---|---|
> | dense 0..9 | 0 | 10 | 10 | true | false | true |
> | gap 100..109 | 100 | 110 | 10 | true | true | false |
> | hole 0..4,7..9 | 0 | 10 | 8 | **false** | false | true |
>
> The dense row is the **control**: it is the only shape the tree has ever run,
> and on it `count()` and `end()` are equal, so a self-test containing only that
> row would pass against an implementation where `end()` merely called `size()`.
> The gap row is what discriminates.
>
> **Sabotage.** Change `end()`'s body to `return count();`. The dense row still
> passes; the gap row must fail on `end`, `atEnd(9)` and `atEnd(109)` — three
> claims, in both directions (one false-negative, one false-positive). If
> fewer than three break, the table is not discriminating and the missing rows
> are the bug. Second sabotage, the other direction: make `dense()` return
> `true` unconditionally and require the hole row to fail; an arbiter that
> never refuses and one that refuses everything satisfy the same single
> assertion (`CLAUDE.md` §1 rule 3, "two controls are sometimes needed").

**Stage 2 — the eleven COUNT renames. Behaviour-identical by construction.**
Sites #1–#10, #12, #14–#16 from §1.1 become `movie::count()`. At #8
(`dojo_gui.cpp:2542`) add the comment naming its `GuiState::StreamWait`
precondition. Nothing else changes.

> **Test.** `scripts/testrun.sh` (whole suite) plus `scripts/recordtest.sh`,
> before and after, on the same clip. Every log line that prints a frame count
> must be byte-identical.
>
> **Sabotage.** Make `count()` return `end()`. On a power-on clip **nothing
> changes** — and that is the point: this stage's test *cannot* fail on a dense
> movie, so **do not claim it as evidence.** State it as what it is: a
> mechanical rename whose safety comes from `count()` being `size()`, verified
> by Stage 1's self-test, not from the replay suite. A rename stage whose test
> cannot fail is honest only if it says so.

**Stage 3 — `atEnd()` at the two live detectors. THE HIGHEST-VALUE STAGE.**

```cpp
// core/dojo/dojo.cpp:1985
-if (dojo.play_match && !macroReadSession && !dojo.session_inputs.empty()
-        && dojo.frame_number == dojo.MovieEnd() - 1)
+// atEnd() owns the +-1. This site fires BEFORE the frame is applied, gui.cpp's
+// after it; each used to spell the frontier for its own phase and the two
+// spellings drifted apart while both were "already converted to MovieEnd()".
+if (dojo.play_match && !macroReadSession && movie::atEnd(dojo.frame_number + 1))

// core/rend/gui.cpp:4642
-if (dojo.frame_number == dojo.MovieEnd())
+if (movie::atEnd(dojo.frame_number))

// core/dojo/dojo.cpp:2011   (keep its distinct meaning)
-auto tas_sit = dojo.session_inputs.find(dojo.frame_number);
-if (tas_sit == dojo.session_inputs.end() || ...)
+// NOT atEnd(): an unauthored frame in the MIDDLE of a movie is a hole, not an
+// end. movie::has() is the question this line is actually asking.
+auto tas_sit = dojo.session_inputs.find(dojo.frame_number);
+if (!movie::has(dojo.frame_number) || ...)
```

`movie::authored()` subsumes the `!session_inputs.empty()` guard at
`dojo.cpp:1985` (`atEnd` on an empty movie: `end()==0`, so `atEnd(n)` is true for
all n — the guard must stay, expressed as `movie::authored() && …`).

> **Test.** A new `scripts/tests/movie_frontier.lua`, run by
> `scripts/testrun.sh`. **Its first assertion is that it is running**, per
> `CLAUDE.md` §1 rule 1's `[MEASURED 2026-09-07]` entry: it writes a marker
> before the call it is testing, so "the script never ran" and "the detector
> never fired" cannot produce the same evidence.
>
> It then asserts, on every replayed frame, that **both detectors agree**:
> `movie.length()` (→ `end()`) against the frame at which `GuiState::ReplayEnd`
> was entered, from the `TAS: replay end at frame %u` NOTICE_LOG. On a power-on
> clip the two must land within one frame of each other and the movie must play
> to completion (**non-vacuity**: frames advanced > 0, `end() > 0`).
>
> **The sparse half.** Drive it with a movie whose first frame is not 0. `[OPEN]`
> `TODOS.md` and `core/rend/mainui.cpp:144` both record that such a movie
> **cannot currently be replayed at all** — "seeking such a movie before
> playback lands the state correctly and then never advances a frame; three
> separate patches here did not change that." So the sparse case cannot be
> driven through the replay path today. Drive it through
> `movie.setButtons(frame, …)` (`lua.cpp:1874`) at frames 100.. on a live
> recording and assert `movie.length() == 110` while `#authored == 10`. **Say in
> the test file which of the two routes was taken** — a check that quietly never
> ran is the failure this project keeps paying for.
>
> **Sabotage.** Restore `gui.cpp:4642` to `== dojo.session_inputs.size()` and
> require the sparse assertion to fail while the dense one still passes. If the
> dense one also fails, the harness is measuring something else.

**Stage 4 — `movie_len_at_begin` → `end()`** (sites #11, #13).
Two lines, `dojo.cpp:869` and `:950`. Changes the "editedSince" verdict on a
macro-Full clip and nothing else.

> **Test.** A macro-Full boot (`Dojo::LoadMacroFull`) into a clip restored from
> a generation, then read `clip.json`'s `restoredFrom.editedSince`.
> Pre-change it is `true` on a clip nobody edited (the roll's injection at a
> non-zero base changes `end()` but can leave `count()` equal); post-change it
> is `false`.
> **Sabotage.** Revert one of the two lines. `movie_len_at_begin` is then set
> with one metric and compared with the other, and `editedSince` must go
> permanently true. A change that only touches one of a matched pair is the
> most common way this edit goes wrong.

**Stage 5 — the Lua binding (§1.4). A behaviour change, alone, last.**
`getReplayFrameCount`'s recording arm returns `movie::count()` instead of
`dojo.frame_number`. Note it in `LUA_TODO.md`.

> **Test.** `scripts/tests/conformance.lua`. Record 30 frames, seek back to
> frame 10 with a savestate, and assert `getReplayFrameCount() == 30`, not 10.
> **Sabotage.** Revert the arm; the assertion must fail with `10`. Without the
> seek the two values are equal and the test cannot fail — the seek *is* the
> control.

### 1.7 Where S2 turns into S6

`movie::` reads `dojo.session_inputs` directly and that is deliberate: it is a
free function over a global, not a method, so it does not add a member to `Dojo`.
**The trap is `MovieEnd()`.** Once `movie::end()` exists, `Dojo::MovieEnd()` is a
duplicate — leave it as a one-line forwarder for one release, then delete it. If
instead the pattern becomes "add `MovieCount()`, `MovieFirst()`, `MovieAtEnd()`
to `Dojo`", the seam has grown the god object by four methods and bought nothing.
`[SOURCE]` `MODULARIZATION.md` §S6's one early action is *"stop `gui.cpp` and
`lua.cpp` touching `session_inputs` directly"* — Stages 2, 3 and 5 do exactly
that for 8 of the 12 sites; `lua.cpp:594,632,642,687` (the piano-roll read/write
path) are the remainder and are S2's interface anyway.

---

## 2. S3 — one owner for `dojo:` config

### 2.1 The measured surface — and a correction to the survey

`[MEASURED 2026-09-08]` (commands in §7):

| | count |
|---|---|
| `cfg*("dojo", …)` call sites | **184** |
| distinct `dojo:` keys read through `cfgLoad*`/`cfgSave*`/`cfgSetVirtual` | **57** |
| `config::X` Options declared in the `dojo` section | **43** |
| **keys reachable through BOTH mechanisms** | **19** |
| keys that are cfg-only (the TAS-native set) | **38** |
| keys that are Option-only | 24 |

`[CORRECTED 2026-09-08]` `MODULARIZATION.md` §S3 says *"Fifteen `dojo:` keys are
read through more than one mechanism"* and lists them. The set is **nineteen**;
the four it omits are **`SpectatorIP`, `SpectatorPort`, `RelayPort`,
`RelayForceTunnel`**. Full dual set:

> `AutoLoadNetState Delay Quark Receiving RecordMatches Relay
> RelayAddressHistory RelayForceTunnel RelayKey RelayPort RelayServer Replay
> ReplayFilename SpectateKey SpectatorIP SpectatorPort TestGame Training
> Transmitting`

**All nineteen are netplay keys.** Not one TAS-native key is dual-mechanism —
`MacroMode`, `PlayMacro`, `AutoCapture`, `CaptureEncoder`, `GamePanel` and the
other 33 are read live and only live. That is the fact that decides the
migration order below, and it is the opposite of what the raw counts suggest.

`[SOURCE]` The three-path hazard, unchanged: `ConfigFile::get_entry`
(`core/cfg/ini.cpp:126`) checks the virtual section first and returns; `save()`
(`ini.cpp:261`) iterates only `sections`; `-config` parses straight into
`cfgSetVirtual` (`core/cfg/cl.cpp:53`) while `cl.cpp:80`'s help text claims
virtual values yield to a later write, which the code does not do.

`[CORRECTED 2026-09-08]` The survey says the write-both pattern *"does not exist
in this tree at all — zero call sites pair them on the same key."* It exists now,
in the S1 work: `[SOURCE]` `core/dojo/tas_ui.cpp:126`
(*"BOTH stores, per the cfg gotcha in CLAUDE.md: cfgSetVirtual wins now (and
beats a -config flag's shadow), cfgSave persists"*), `:143`, and
`core/lua/lua.cpp:1897` writes `config::RecordMatches` **and** `cfgSetVirtual`
for the same reason. Three hand-written copies of one rule, in two files, added
within a week. That is the seam arriving on schedule.

### 2.2 The API

```cpp
/*
	core/dojo/dojo_settings.h

	THE OWNER OF `dojo:` CONFIG. One table, one read path, one writer.

	"The current value of a setting" is answered three ways in this tree and
	they do not agree:

	  (a) config::X Option objects, which CACHE at Settings::load()
	  (b) cfgLoadBool/Int/Str, a live read of the store
	  (c) cfgSetVirtual, which writes a shadow section that WINS EVERY READ and
	      is never persisted and never cleared (ini.cpp:126 / :261)

	Each of the three has already shipped a bug here.

	  (a) replay.cpp:10 - "the Option caches the value loaded at startup ...
	      picking any clip silently played the most recently recorded one
	      instead (and pointed F3 at the wrong folder)."
	  (c) cl.cpp:80 promises virtual values yield to a later write. They do
	      not. A -config launch flag shadows every cfgSave* for the life of the
	      process, so the settings checkbox looks broken and the user reports
	      "I can't turn it off."
	  (a)+(c) replay.cpp:23 gets it right and needs five lines of comment to
	      explain why BOTH a .set() and a cfgSetVirtual are required, because
	      Emulator::loadGame re-runs Settings::load(true) mid-boot
	      (emulator.cpp:492) and stomps the .set().

	`[MEASURED 2026-09-08]` 184 call sites, 57 keys. The port adds ~76 keys and
	~200 sites. Every one of those sites repeats its default literal, and the
	only reason the defaults have not drifted yet is that nobody has typed them
	twice wrong. Nothing prevents it.

	WHAT THIS IS NOT. It is not a replacement for config::X. Those Options are
	flycast-wide machinery - Settings::load, the settings UI, determinism's
	manifest (determinism.h:48 uses cfgSetVirtual + Settings::load DELIBERATELY,
	and that stays). The rule this file enforces is narrower and checkable:

	    a `dojo:` key has exactly ONE mechanism, and it is this one.

	READS ARE LIVE. There is no cached copy, because a cached copy is what
	config::X already is and it is what silently played the wrong clip.
*/
#pragma once
#include "types.h"
#include <string>

namespace dojocfg
{

/*
	THE TABLE. Name, type and default, stated ONCE.

	`Key` is generated from it by X-macro, so a misspelled key is a compile
	error rather than a silent fallback to a default that reads like a
	deliberate setting. That is the failure this replaces: cfgLoadBool("dojo",
	"MacrMode", false) compiles, runs, and answers false forever.

	The `launchable` column is not decoration - it is what makes set() correct.
	A key a launch flag may set needs the virtual write; a key no flag touches
	does not, and writing the shadow anyway makes the value unpersistable for
	the rest of the process.
*/
#define DOJO_KEYS(_)                                                          \
	/*     key                type    default        launchable  */          \
	_(MacroMode,              Bool,   false,         true)                    \
	_(PlayMacro,              Bool,   false,         true)                    \
	_(AutoCapture,            Bool,   false,         true)                    \
	_(AutoSeekState,          Int,    -1,            true)                    \
	_(CaptureEncoder,         Str,    "prores",      true)                    \
	/* ... 33 more; see the cfg-only set in docs/SEAM-PLAN.md 2.1 ... */
	// (the 19 dual-mechanism keys are DELIBERATELY ABSENT - see 2.3)

enum class Key { /* generated from DOJO_KEYS */ };

//! LIVE reads. Virtual-aware, because that is what the store does; the default
//! comes from the table, not from the call site.
bool               getBool(Key k);
int                getInt (Key k);
const std::string& getStr (Key k);

//! THE WRITER, and the reason this namespace exists.
//!
//! Writes the persisted entry ALWAYS, and the virtual entry as well for a
//! `launchable` key - in that order, because a -config flag shadows the
//! persisted value for the life of the process and a UI toggle that only
//! persists looks broken to the user pressing it.
//!
//! NOT A "WRITE BOTH" HELPER. Writing the shadow unconditionally would make
//! every key permanently unpersistable after its first UI toggle: the virtual
//! entry wins every later read and ConfigFile::save() never emits it, so the
//! value on disk becomes invisible. The table decides, not the caller.
void set(Key k, bool v);
void set(Key k, int v);
void set(Key k, const std::string& v);

//! Is this key's value currently coming from the shadow section (a -config
//! flag or a set() on a launchable key)? For the settings UI, which should be
//! able to SAY "this is overridden from the command line" rather than showing
//! a control that appears not to work.
bool isOverridden(Key k);

//! The key's name as it appears in emu.cfg. For log lines and for the
//! generated settings panel; never for building a second lookup.
const char *name(Key k);

//! Exercise the table and the read/write round trip. Gated on
//! `dojo:SeamSelfTest`. See the comment on its definition for why it uses the
//! real cfg store rather than a mock: the bug being prevented lives in the
//! store's virtual-section precedence, and a mock would model it correctly by
//! definition.
void selfTest();

}	// namespace dojocfg
```

### 2.3 Which keys migrate, and the order

**Migrate the 38 cfg-only keys. Do not migrate the 19 dual-mechanism keys.**

That is the whole call, and it inverts the survey's framing. The dual keys are
the *dangerous* ones, but they are dangerous because they are netplay keys with
an `Option` that netplay code, `determinism.cpp:45-48`, `aica_if.cpp:40` and
`Settings::load` all read. Converting them means either deleting the Option
(which breaks `determinism::isDeterministicRun()` and the sync manifest) or
having `dojocfg` write through to it — at which point `dojocfg` owns a fourth
mechanism instead of replacing three. **`[REASONED]` The cfg-only 38 are the
whole win and carry none of that.** They are also exactly the set the port grows.

**Stage 1 — the header, the table with the 38 keys, `selfTest()`, no call sites.**

> **Test.** `dojo:SeamSelfTest=yes`, three claims:
> 1. **the default comes from the table** — `getBool(Key::MacroMode)` on a
>    cfg with no such entry returns the table's default;
> 2. **`set()` persists** — `set(Key::CaptureEncoder, "cfhd")`, then
>    `cfgLoadStr("dojo","CaptureEncoder","")` reads `"cfhd"` from the **regular**
>    section (`cfgIsVirtual` false for a non-launchable key);
> 3. **`set()` beats a launch flag** — `cfgSetVirtual("dojo","MacroMode","yes")`
>    to simulate `-config dojo:MacroMode=yes`, then `set(Key::MacroMode, false)`,
>    then `getBool(Key::MacroMode)` must be **false**.
>
> Claim 3 is the one that matters, and **it fails against every UI toggle in the
> tree today** — which is how you know it can fail. Verify that before trusting
> it: write the same three lines against raw `cfgSaveBool` and watch claim 3
> report `true`. `CLAUDE.md`: "verify the instrument before trusting the
> reading."
>
> **Sabotage.** Drop the virtual write from `set()`. Claim 3 must fail and
> claims 1–2 must still pass. Then the other direction: make `set()` write the
> virtual entry for **every** key, and require a fourth claim — "a
> non-launchable key's value survives into `emu.cfg`" — to fail. One sabotage
> in each direction, because a `set()` that always writes the shadow and one
> that never does both satisfy a single round-trip assertion.

**Stage 2 — convert the 38 keys' call sites, one commit per subsystem.**
Capture (`avi_dump.cpp`, ~12 sites), macro (`dojo.cpp`, ~10), diagnostics
(`ViewportTrace`, `StateMapLog`, `SpgTrace`, `MemTrace`, `MemHunt`,
`FidelityDump`, `ResizeProbe`, `MacroProbe`, `TextRoundTrip`, `TextApply`,
`VerifyInputs`, `PanelSelfTest`, ~18), harness (`UiIni`, `AutoPlay`,
`AutoSeekState`, `AutoCapture`). Mechanical.

> **Test.** For each subsystem commit: launch with the subsystem's flag set via
> `-config` and grep the log for the trace it enables. `scripts/testrun.sh`
> already passes `-config dojo:UiIni=no` to every harness, so the `UiIni`
> commit is self-testing — if it breaks, every test's dock layout starts
> writing `imgui.ini`.
> **Sabotage.** Point one key's table row at the wrong default and require the
> subsystem's flag-off launch to start emitting traces.

**Stage 3 — the generated settings panel.** Deferred; it needs a per-key label
and range that the table does not carry yet. Adding those columns before there
is a consumer is the shape `MODULARIZATION.md` warns about ("wait until two
components want one").

### 2.4 Where S3 turns into S6

Nowhere, if the 19 dual keys are left alone. The moment `dojocfg` grows a
write-through to `config::X` to absorb `Training` or `RecordMatches`, it becomes
the fourth mechanism and every argument for it is gone. **If that is wanted, the
correct move is to delete the `Option` and fix `determinism.cpp` — a separate
project with `DETERMINISM.md` and `SYNC_SETTINGS.md` as its brief, not a stage
of this one.**

---

## 3. S5 — what kind of session is this

### 3.1 Where each predicate is computed today

`[MEASURED 2026-09-08]` `play_match` is referenced 74 times; `MacroMode`,
`PlayMacro`, `RecordMatches`, `Replay`, `Training`, `Receiving` and
`Transmitting` together 81 times.

| predicate | computed at | spelling |
|---|---|---|
| **replaying** | `core/dojo/replay.cpp:142`, `core/rend/gui.cpp:881` (set); read 74× | `dojo.play_match` |
| **recording (movie)** | `core/dojo/dojo.cpp:455`, `:2169`, `replay.cpp:171`, `:518` | `config::RecordMatches \|\| config::Transmitting` |
| **recording, the real one** | **`core/dojo/dojo.cpp:1936`** | `!play_match && !online && !ggpo_session && (cfgLoadBool("RecordMatches") \|\| cfgLoadBool("PlayMacro") \|\| replay.HasAppendTarget())` — named `tasWriteGrow`, with eight lines of comment. **This is the tree's most complete answer and it is a local.** |
| **read-only vs read-write** | `core/dojo/dojo.h:298-302` | `play_match` = READ; `macro_armed` = READ-WRITE; `!macro_armed` = WRITE. Three states in two bools, documented only in that comment block. |
| **macro session** | `dojo.cpp:449`, `:1007`, `:1155`, `:1974`, `:2842`, `:2885` | `cfgLoadBool("dojo","MacroMode",false)`, six times, each with its own default literal |
| **play-macro vs record-macro** | `dojo.cpp:789`, `:1936` | `cfgLoadBool("dojo","PlayMacro",false)` |
| **netplay** | `core/determinism.cpp:53` | `config::GGPOEnable \|\| settings.network.online` |
| **determinism-relevant run** | `core/determinism.cpp:21-49` | **already a single owner** |
| **run kind, as a string** | `core/determinism.cpp:51-57` | `"netplay"\|"replay"\|"record"\|"off"` — **already a single owner, and incomplete** |
| **test lab** | `core/dojo/tas_clip.cpp:38` `labIsActive()` | already a function |

**The finding that shapes the API:** `determinism::runKind()` already exists,
already returns a session-kind string, and already has one owner. It is missing
the macro cases and the read/write axis. `[REASONED]` Building a second
`Session` enum beside it is exactly the "two implementations of one rule" defect
`CLAUDE.md` §4 spends three paragraphs on — they agree today and part company
the moment one is changed. **The session enum should be `runKind()` grown up, in
`determinism.h`'s neighbourhood, with `runKind()` reimplemented on top of it.**

### 3.2 The API

```cpp
/*
	core/dojo/session.h

	WHAT KIND OF SESSION IS THIS - one answer, derived, never stored.

	Today it is an ad-hoc conjunction at every site that asks. The most
	complete version in the tree is a LOCAL VARIABLE inside MapleApplyAction
	(dojo.cpp:1936, `tasWriteGrow`) carrying eight lines of comment about why a
	replay the user flipped to READ-WRITE is a writing session; nothing else can
	see it. The status pill in the fork being ported computes the same thing
	inline with a four-arm ternary (dojo_gui.cpp:17948 there).

	TWO INDEPENDENT AXES, and conflating them is the mistake this prevents.

	  KIND  - what the session is FOR: JustPlay, RecordMovie, Replay,
	          RecordMacro, PlayMacro, TestLab, Netplay.
	  MODE  - who drives the guest RIGHT NOW: Read, ReadWrite, Write.

	They are not a product: a Replay session flipped with R becomes MODE=Write
	while KIND stays Replay, which is precisely the case dojo.cpp:1936 exists to
	handle and the case a single enum would have to spell as a seventh kind.
	dojo.h:298-302 already states the MODE rule; this gives it a name.

	IT IS BUILT ON determinism::runKind(), NOT BESIDE IT. That function
	(determinism.cpp:51) already answers a coarser version of KIND with one
	owner, and a second implementation of one rule does not disagree when you
	write it - it disagrees when one of them is changed (CLAUDE.md s4,
	emu.supports() vs the conformance suite's rawget). runKind() is
	reimplemented as a switch over kind() in the same commit that adds this.

	DERIVED PER CALL, like rend::gameViewport(). The inputs are cfg reads and
	two bools; caching them would need invalidation on every R press, every
	boot handoff and every Settings::load, which is three more things to get
	wrong than the read costs.
*/
#pragma once

namespace session
{

enum class Kind
{
	JustPlay,		//!< no movie: the plain single-player boot
	RecordMovie,	//!< dojo:RecordMatches, a .flyr being written from power-on
	Replay,			//!< dojo.play_match: a .flyr driving the guest
	RecordMacro,	//!< dojo:MacroMode without PlayMacro
	PlayMacro,		//!< dojo:MacroMode with PlayMacro
	TestLab,		//!< tas_clip::labIsActive(hostfs::savestateFolderOverride)
	Netplay,		//!< GGPO or settings.network.online - INCLUDING spectate
};

//! WHO DRIVES THE GUEST, which is not the same question as Kind.
enum class Mode
{
	Read,		//!< the movie drives; the pad is ignored (dojo.play_match)
	ReadWrite,	//!< the movie drives; a live SIGNAL stomps the active cell
	Write,		//!< advancing overwrites every frame it passes
};

//! PRECEDENCE IS DECLARED, not emergent. TestLab wins over everything (a lab
//! session is a lab session whatever else is set), then Netplay, then the macro
//! pair, then Replay, then RecordMovie, then JustPlay. Writing it down is the
//! point: the fork's inline ternary has the same order by accident of how the
//! `?:` chain was typed, and nothing there says so.
Kind kind();
Mode mode();

//! The predicates the ~14 ported panels' `enabled()` will ask for. Each is one
//! line over kind()/mode(), and each exists so a panel does not re-derive it:
//! a panel that spells its own conjunction is the 25 raw call sites coming
//! back one panel at a time.
bool recording();	//!< kind is RecordMovie or RecordMacro
bool replaying();	//!< kind is Replay or PlayMacro
bool macro();		//!< kind is RecordMacro or PlayMacro
bool netplay();		//!< kind is Netplay
bool readOnly();	//!< mode == Read

//! IS THE MOVIE GROWABLE PAST ITS LAST AUTHORED FRAME? dojo.cpp:1936's
//! `tasWriteGrow`, promoted out of a function body. Deliberately its own
//! function rather than `!readOnly()`: it also excludes online and ggpo
//! sessions, and the reason (running off the end must GROW the roll rather
//! than pin frame_number, which killed pause/step on a sibling path) belongs
//! with the predicate, not at one of its call sites.
bool writeGrow();

//! For the status pill, logs and the HUD. Never nullptr.
//!   "JUST PLAY" | "RECORD MOVIE" | "REPLAY" | "RECORD MACRO" | "PLAY MACRO"
//!   | "TEST LAB" | "NETPLAY"
const char *label();

//! Exercise the precedence table against synthetic cfg states. Gated on
//! `dojo:SeamSelfTest`.
void selfTest();

}	// namespace session
```

### 3.3 Migration, staged

**Stage 1 — the header, the `.cpp`, `selfTest()`. No call sites.**

> **Test.** `dojo:SeamSelfTest=yes`. The self-test sets cfg keys through
> `cfgSetVirtual`, calls `kind()`, and asserts one row per **precedence pair**,
> not one per kind:
>
> | set | expect | proves |
> |---|---|---|
> | nothing | `JustPlay` | the floor |
> | `RecordMatches=yes` | `RecordMovie` | |
> | `RecordMatches=yes`, `MacroMode=yes` | `RecordMacro` | macro beats movie |
> | `MacroMode=yes`, `PlayMacro=yes` | `PlayMacro` | |
> | `play_match=true`, `MacroMode=yes` | `PlayMacro` | macro beats replay |
> | `play_match=true` | `Replay` | |
> | `GGPOEnable`, `RecordMatches=yes` | `Netplay` | netplay beats record |
> | lab folder + `RecordMatches=yes` | `TestLab` | lab beats everything |
>
> The single-flag rows are the **control** and cannot discriminate an
> implementation with the precedence reversed; the four conflict rows are the
> test. A table with only the single-flag rows would pass against any ordering.
>
> **Sabotage.** Reverse two adjacent arms of the precedence chain (macro and
> replay). Exactly the two conflict rows that straddle them must fail, and the
> six others must pass. If a single-flag row also fails, the arms were not
> adjacent and the chain is not what the table claims.

**Stage 2 — reimplement `determinism::runKind()` over `session::kind()`.**
Same commit or the next. Four lines.

> **Test.** `runKind()` is printed in the determinism trace. Boot each of
> record / replay / netplay / off and diff the log line before and after — it
> must be byte-identical, because this stage is meant to change nothing.
> **Sabotage.** Map `RecordMacro` to `"off"` in the new switch and require the
> macro boot's trace to change. Without that, "byte-identical" is equally
> consistent with the new function never being called — which `CLAUDE.md`
> `[MEASURED 2026-09-07]` records as a false pass this project has already paid
> for.

**Stage 3 — convert the six `MacroMode` reads and the `tasWriteGrow` local.**
`dojo.cpp:449, 1007, 1155, 1974, 2842, 2885` → `session::macro()`;
`dojo.cpp:1936-1938` → `session::writeGrow()`, with the eight-line comment moving
to the header (it is already in the API above).

> **Test.** `scripts/recordtest.sh` on a macro clip: record, quit, and diff
> `clip.json`'s `mode`, `macroFile`, `macroBase` and the written
> `<clip>_macro.txt` against a pre-change run. **Non-vacuity: assert the macro
> file is non-empty and has more than one row** — the `[MEASURED 2026-09-05]`
> blank-video lesson, in text form. A silently-empty macro file is what a
> broken `session::macro()` produces, and it is also what "the test did not
> record anything" produces.
> **Sabotage.** Make `session::macro()` return false. `clip.json` must lose
> `mode: "macro"` and `<clip>_macro.txt` must not be written at all. If only one
> of the two changes, one of the six sites was missed.

**Stage 4 — `label()` at the one place that needs it**, when the ported status
pill lands. Not before: a `label()` with no consumer is a string nobody reads.

### 3.4 Declined: `BootHandoff`

`MODULARIZATION.md` §S5 proposes a `BootHandoff` enum replacing the five flags
`boot_ready_arm`, `replay_bootload`, `macro_fullload`, `onenter_ff`,
`clip_ready_pending`. **Do not do it, and here is the reason rather than a
preference.**

`[SOURCE]` They are not five states of one machine. `dojo.h:145-147` and
`:336-338` describe them and `dojo.cpp:2894-2903` (`Reset`) clears them, and the
clearing is *not* uniform: `macro_pending` is cleared alongside `macro_fullload`
because *"an aborted Play Macro Full load (quit mid-boot) must not leak its
deferred State-0 arm"*, `loaded_macro_path` and `loaded_macro_rr` are cleared as
a pair for a different reason, and `macro_armed` is cleared at `:2915` with a
third reason (*"teardown cleared play_match but never macro_armed - a stale
READ-WRITE could bleed into the next Record Macro"*). Three different lifetimes.

An enum forces one lifetime. `[REASONED]` The transitions have never been
written down, so enumerating them means **inferring** them from six clearing
sites and two consuming files (`gui.cpp:4364-4473`, `:5627`, `:5995`, `:6025`,
`:6049`, `gamepad_device.cpp:119`) — and a wrong inference here produces a boot
that hangs frozen with no banner, which is a state the user cannot escape and a
harness cannot distinguish from a slow boot. It is a large diff with no
observable, which is the shape this repository's testing doctrine ranks lowest.

**The cheap correct move instead:** write the transitions down. A comment block
in `dojo.h` above the five flags, listing for each one who arms it, who consumes
it, who clears it and why — derived from the six sites, reviewed once. Then, if
the port makes the machine bigger, the enum has a specification to be built from
instead of an archaeology exercise.

### 3.5 Where S5 turns into S6

`session::` is free functions over the existing globals — it adds nothing to
`Dojo`. The trap is `Mode`: `mode()` is derived from `play_match` and
`macro_armed`, and the tempting next step is to **store** a `Mode` member on
`Dojo` and have the R key set it. That replaces two documented bools with a third
piece of state that has to be kept in step with them, in the class the survey
says not to grow. Derive it; never store it.

---

## 4. S4 — stopping the machine

### 4.1 The four idioms, with sites

| idiom | sites | verdict |
|---|---|---|
| **`pausing::`** (the arbiter) | `gui.cpp:824, 5024-5025, 5058, 5074`; `lua.cpp:515, 631, 1265-1267, 1741, 1751`; `emulator.cpp:650`; `dojo.cpp:2894` | **the intended answer** |
| **`deferred::post`** | drained at `mainui.cpp:97` | the intended answer for "outside both loops" |
| **raw `emu.stop()/start()`** | `gui.cpp:4769-4771` (`gui_loadState`), `:4784-4786` (`gui_saveState`), `:4532` (bare `emu.start()` at the end of `gui_display_ui`), `:4656` (`emu.stop()` in the stepping hook), `:5033` (`gui_open_step`), `:5048`+`:5070` (`gui_open_pause`), `:794`+`:850` (`gui_open_settings`), `dojo_gui.cpp:2445` (`show_pause`), `nullDC.cpp:85`, `sdl.cpp:780` | mixed — see below |
| **`aroundStopped()`** | `lua.cpp:1214`, used by the `deferred::post` savestate bindings | a second hand-written stop/start pair, added one day after the arbiter |

**`dc_loadstate`/`dc_savestate` with NO stop at the call site** — the riskiest
shape, unchanged since the survey:

- `core/rend/gui.cpp:971` — `Load State` button, `gui_display_commands()`
- `core/rend/gui.cpp:997` — `Save State` button, same
- `core/rend/gui.cpp:1063` — `Load Net State` button, same
- `core/emulator.cpp:591, 593, 658` — inside the loader/teardown, where the
  machine is stopped by construction

`[SOURCE]` The three GUI ones are safe **only** because `GuiState::Commands` is
reachable only through `gui_open_settings()`, which stops the emulator at
`gui.cpp:794`. Nothing at any of the three call sites says so. Worse, each one
calls `gui_setState(GuiState::Closed)` *first* and `dc_loadstate` *second* — and
`gui.cpp:4532` restarts the emulator at the end of `gui_display_ui` when
`gui_state == GuiState::Closed`. `[OPEN]` Whether that restart can be reached
between the two statements depends on frame ordering this pass did not trace; if
it can, these three are a live race, not a documented-precondition risk. **That
question is worth an hour before any of the rewrites below.**

### 4.2 The API — a marker, not a mechanism

```cpp
/*
	core/machine_still.h

	A PRECONDITION THAT LIVES ON THE DECLARATION.

	The mechanisms already exist and are good: core/pause.h is the arbiter
	("each owner sets and clears only ITS OWN reason"), core/deferred.h is the
	place to stand outside both loops. What is missing is a way for a function
	to SAY it needs the machine still, at the point where a caller reads it.

	The shape being prevented, exactly: gui_display_commands() calls
	dc_loadstate / dc_savestate at gui.cpp:971, :997 and :1063 with no stop of
	any kind at the call site. It is safe because GuiState::Commands is only
	reachable via gui_open_settings(), which stopped the emulator at :794 - a
	precondition stated nowhere, three files from the code that depends on it.
	The port adds ~14 render-thread panels that mutate movie state through
	Dojo::ApplyEdit, whose safety today is one sentence in dojo.h.

	IT IS NOT A LOCK AND MUST NOT BECOME ONE. session_inputs has no lock and
	that is a deliberate, argued design (lua.cpp:613: "The emu thread owns
	session_inputs whenever it runs"). A marker that quietly acquired something
	would be a fifth idiom, and the bespoke locks that DO exist - clipMutex,
	locked_ranges_mtx, lua::mutex, the avi_dump queue - are each correctly
	argued for their own data and must not be merged.

	IN RELEASE IT IS A COMMENT THAT CANNOT DRIFT, because it lives on the
	declaration and moves with it.
*/
#pragma once

//! Declare that a function requires the emulation thread to be stopped.
//!
//! Place it on the DECLARATION, so a reader of the header sees the
//! precondition without opening the .cpp:
//!
//!     void gui_saveState() MACHINE_STILL;
//!     s64  Dojo::ApplyEdit(const std::map<u32, std::vector<u8>>&,
//!                          const char *source) MACHINE_STILL;
//!
//! and assert it at the top of the DEFINITION:
//!
//!     void gui_saveState() { ASSERT_MACHINE_STILL(); ... }
//!
//! TWO MACROS, NOT ONE, and the split is forced: an attribute cannot check
//! anything and a check cannot appear on a declaration. Marking without
//! asserting gives documentation that rots; asserting without marking gives a
//! check nobody reads before writing the fifteenth call site.
#define MACHINE_STILL	/* requires the emulation thread stopped */

//! Debug builds only. Fires when the machine is running and NO pause reason is
//! held - i.e. nobody stopped it and nobody is about to.
//!
//! IT MUST BE ABLE TO FIRE, which is why it is not `!emu.running()` alone:
//! that would pass in every configuration where the emulator merely happens to
//! be between frames, and a check that passes for the wrong reason is the one
//! this project keeps paying for. `pausing::mask() != 0` is the half that says
//! somebody OWNS the stop.
#ifndef NDEBUG
	#define ASSERT_MACHINE_STILL()	machine_still::assertStopped(__func__)
	namespace machine_still { void assertStopped(const char *fn); }
#else
	#define ASSERT_MACHINE_STILL()	((void)0)
#endif
```

### 4.3 Migration, staged — and where it stops

**Stage 1 — the header, plus `ASSERT_MACHINE_STILL()` in the four functions
whose precondition is already believed true.** `gui_loadState`, `gui_saveState`,
`Dojo::ApplyEdit`, `Dojo::ApplyEditResize`. No behaviour change; the assert is
debug-only.

> **Test.** A debug build, `scripts/testrun.sh --self-test`, then a normal run
> of the whole suite. Zero assertions must fire.
> **Sabotage — and this is the stage's real content.** Add
> `ASSERT_MACHINE_STILL()` to `gui_display_commands()`'s three `dc_loadstate` /
> `dc_savestate` call sites and run the Commands screen. **If it does not fire,
> the assert is not checking anything** — either those sites are genuinely
> stopped (in which case §4.1's `[OPEN]` is answered and the risk is lower than
> the survey thought) or the assert's condition is wrong. Either answer is worth
> the stage. **Do not ship the marker without running this**: an assert that
> cannot fire reads exactly like a codebase with no violations.

**Stage 2 — `aroundStopped()` → `pausing::Scoped`.** One function,
`lua.cpp:1214`, three call sites in the same file. `pausing::Scoped`'s
"only restarts what it stopped" rule already subsumes `aroundStopped`'s
`wasRunning` bookkeeping.

> **Test.** `scripts/tests/conformance.lua`'s savestate section, which drives
> `deferred::post`-ed saves and loads. It must stay green.
> **Sabotage.** Replace `Scoped` with a bare `pausing::set(MODAL)` and no clear.
> The second savestate operation in the same script must hang or report a
> stopped machine. If the suite stays green with the pause never released,
> it is not exercising the restart and this stage has no test.

**Stage 3 — `gui_loadState`/`gui_saveState` on `pausing::Scoped`. DO NOT DO
THIS.**

`[SOURCE]` `core/deferred.h:18` names `gui_loadState`'s
`emu.stop(); dc_loadstate(); emu.start();` as **"the supported shape"**, and
`core/lua/lua.cpp:2247` records the elimination table for the alternatives,
measured:

```
  dc_loadstate + pausing::Scoped(MODAL)          wedges
  dc_loadstate + explicit emu.stop()/start()     wedges
  ... with rend.ThreadedRendering=no             wedges
  ... with no stop at all                        wedges
```

`[OPEN]` *"Why the identical call is fine from a `vblank` callback and wedges
from the drain point is still unidentified."*

So the exact substitution this stage proposes — `dc_loadstate` under
`pausing::Scoped(MODAL)` — is a shape that has been **measured to wedge the
emulator** from a neighbouring call site, for a reason nobody has found. The
survey's own cost note says the rewrite "needs care"; the measurement says it is
a coin flip on an unexplained mechanism. **The value is tidiness. The risk is
the one path in the tree that is known to work.** Skip it. The marker in Stage 1
delivers the correctness benefit — the precondition at the call site — without
touching the working path.

If it is ever attempted, the prerequisite is closing `lua.cpp:2247`'s `[OPEN]`
question first, not adding a fourth row to its table.

### 4.4 Where S4 turns into S6

`machine_still.h` has no state and touches no class, so it cannot. The risk is
the opposite one: the marker is *only* worth having if it is applied to the
ported panels' mutation entry points as they land. A marker on four functions
and none of the fourteen is a header nobody reads. **Its acceptance criterion is
a line in the panel-porting checklist, not a commit.**

---

## 5. What could go wrong, across all four

1. **A rename stage claiming a test it does not have.** S2 Stage 2 and S3 Stage 2
   are mechanical and their suites cannot fail on the shapes the tree can
   currently produce. Each is written above with that stated. The failure mode is
   a session summary that reports them as "verified by the replay suite".

2. **`dojocfg` absorbing the 19 dual keys** because they are the ones that look
   dangerous. That makes it mechanism number four. §2.3.

3. **`session::Mode` becoming a stored member of `Dojo`.** §3.5.

4. **The S4 rewrites being done because the marker felt incomplete without
   them.** §4.3.

5. **`BootHandoff` inferred rather than specified.** §3.4.

6. **Two owners for "session kind"** — a `session::Kind` beside a still-hand-
   rolled `determinism::runKind()`. §3.2 makes reimplementing `runKind()` part
   of the same commit for exactly this reason; if that half is dropped as "not
   needed yet", the seam has created the defect it was named after.

---

## 6. The order

```
S2/1  movie_timeline.h + selfTest                      no call sites
S2/2  the 11 COUNT renames                             mechanical
S2/3  atEnd() at the two detectors        <-- HIGHEST VALUE, ~6 lines
S2/4  movie_len_at_begin -> end()                      2 lines
S5/1  session.h + selfTest                             no call sites
S5/2  runKind() reimplemented over kind()              4 lines
S5/3  the 6 MacroMode reads + tasWriteGrow
S3/1  dojo_settings.h, 38 keys, selfTest               no call sites
S3/2  the 38 keys' call sites, per subsystem
S4/1  machine_still.h + 4 asserts + the sabotage probe
S4/2  aroundStopped -> pausing::Scoped
S2/5  getReplayFrameCount's recording arm              a Lua behaviour change
--- then port panels, one per commit, each a TAS_PANELS[] row ---
```

`S5/1-2` before `S3` because `session::kind()` reads cfg keys and would
otherwise be converted twice. `S4/1` after `S5` because `session::` is what the
ported panels' `enabled()` predicates will call, and the marker's population is
those same panels.

**Not in the list, deliberately:** S4 Stage 3 (§4.3), S5 `BootHandoff` (§3.4),
S3 Stage 3 (§2.3), the 19 dual keys (§2.3), and anything named `Dojo`.

---

## 7. The commands behind the numbers

```sh
# S2: every session_inputs.size() site
grep -rn 'session_inputs\.size()' core --include='*.cpp' --include='*.h' | grep -v deps
grep -rn 'MovieEnd' core --include='*.cpp' --include='*.h' | grep -v deps

# S3: the key partition (57 / 43 / 19 / 38 / 24)
grep -rhoE 'cfg(Load|Save|Set)[A-Za-z]*\(\s*"dojo"\s*,\s*"[A-Za-z0-9_.]+"' \
  core --include='*.cpp' --include='*.h' | grep -v deps \
  | sed -E 's/.*"dojo"[^"]*"([A-Za-z0-9_.]+)"/\1/' | sort -u > /tmp/cfgkeys.txt
grep -n '"dojo")' core/cfg/option.cpp \
  | sed -E 's/.*\("([A-Za-z0-9_.]+)",.*/\1/' | sort -u > /tmp/dojoopt.txt
comm -12 /tmp/cfgkeys.txt /tmp/dojoopt.txt   # dual-mechanism: 19
comm -23 /tmp/cfgkeys.txt /tmp/dojoopt.txt   # cfg-only:       38
grep -rn 'cfg[A-Za-z]*(\s*"dojo"' core --include='*.cpp' --include='*.h' \
  | grep -v deps | wc -l                     # call sites:    184

# S4: the four idioms
grep -rn 'emu\.stop()\|emu\.start()' core --include='*.cpp' | grep -v deps
grep -rn 'dc_loadstate\|dc_savestate' core --include='*.cpp' | grep -v deps
grep -rn 'pausing::' core --include='*.cpp' | grep -v deps

# S5: the predicates
grep -rn 'play_match' core --include='*.cpp' | grep -v deps | wc -l   # 74
grep -rn '"MacroMode"\|"PlayMacro"' core --include='*.cpp' | grep -v deps
```
