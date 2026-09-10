# What kind of session is this? — a census

`[MEASURED 2026-09-09]` A census of `core/` and `shell/` (excluding `core/deps/`
and `build*/`) for every site that decides something from a session-kind signal.

## Why this document exists

Four session kinds overlap in this tree — TAS re-recording, GGPO rollback
netplay, training mode, and plain play — and the code asks "which is this?"
ad hoc at 170 places. `core/dojo/session.h` exists to be the one owner of that
answer. It is not being used, and the census found out why.

**The trigger.** `session::readOnly()` had exactly one caller in the tree, and
that caller was wrong. `roll_panel.cpp` asked it to mean *"may I rewrite the
movie file?"*; it actually answers *"is the movie driving the guest right now?"*
Because `dojo:Replay=yes` sets `play_match`, every edit tool in the piano roll
was unreachable in the only mode where a movie exists — and the panel drew
`edits need a writable session` and looked like a working gate.

That is not one bad call site. It is the shape of the problem: **the predicates
answer questions nobody is asking.**

---

## 1. The denominator

| category | sites |
|---|---|
| **A** — who drives the guest right now (input routing) | 8 |
| **B** — may the tape be rewritten | 35 |
| **C** — what feature is this session for (UI / panels / labels) | 38 |
| **D** — netplay safety / GGPO lifecycle / determinism | 65 |
| **E** — training-mode-only behaviour | 21 |
| **F** — doesn't fit | 3 |
| **total raw sites** | **170** |

A *site* is one source line whose expression reads a session-kind signal in
order to decide something. Assignments, declarations and comments are excluded.
`session.cpp` / `session.h` are the owner and are not counted.

### Adopted versus raw

| predicate | callers |
|---|---|
| `session::kind()` | 1 — `core/determinism.cpp` `switch (session::kind())` |
| `session::mode()` | 0 |
| `session::recording()` | 0 |
| `session::replaying()` | 0 |
| `session::macro()` | 0 |
| `session::netplay()` | 1 — `core/dojo/roll_panel.cpp` |
| `session::readOnly()` | 0 (was 1; that caller was the bug above) |
| `session::writeGrow()` | 1 `[2026-09-10]` |
| `session::label()` | 0 |

**2 decision sites adopted against 170 raw** at the time of the census;
**5 as of `[2026-09-10]`**, plus 34 raw `Training` reads migrated to one owner. `writeGrow()` was written
specifically to promote `tasWriteGrow` out of `MapleApplyAction`'s body; that
local still exists at `core/dojo/dojo.cpp` and is still the live code path:

    const bool tasWriteGrow = !dojo.play_match && !settings.network.online && !replay.ggpo_session
            && (cfgLoadBool("dojo", "RecordMatches", false) || cfgLoadBool("dojo", "PlayMacro", false)
                || replay.HasAppendTarget());

For scale, the neighbouring `determinism::isDeterministicRun()` has 4 callers —
and its own body is itself one of the 170.

---

## 2. The questions `session::` cannot answer

The most valuable part of the census. Fourteen distinct questions are being
asked that no predicate covers. Until these are named, adoption cannot happen,
because there is nothing to adopt.

**Q-a. "May the TAPE be rewritten?"** — the question `readOnly()` was mistaken
for. Three different answers live in the tree today: `roll_panel.cpp` says
`paused && !netplay`; `core/lua/lua.cpp` says `return !emu.running();`; and
`core/dojo/replay.cpp`'s `TextApply` and `ResizeProbe` push `ApplyEdit` into a
replay **with no gate at all**.

**Q-b. "Is a file attached that appends land in?"** — `HasAppendTarget()`, the
clause `writeGrow()` swallowed and nobody can ask on its own. Ten sites; three
of them spell it inline as `!filename.empty()` rather than calling the accessor.

**Q-c. "Is this a TCP stream session (spectate / transmit), as distinct from
rollback?"** — `Kind::Netplay` is documented as "GGPO or `settings.network.online`,
**INCLUDING spectate**", so nothing can separate them. 17 sites.

**Q-d. "Is this TRAINING mode?"** — **ANSWERED `[2026-09-10]`, and it turned out
to be TWO questions.** `Kind::Training` now exists, below Replay. But all 34 raw
sites ask a *toggle*, not a kind — `Training && ShowTrainingInputDisplay`, or an
arm beside `play_match` — so `session::trainingEnabled()` is the one owner they
migrated to, and `session::training()` (the exclusive kind) is kept separate.

That distinction is the census's own thesis in miniature. Collapsing the two
would have changed behaviour silently wherever the toggle is set under a higher
kind, and the obvious migration — "replace the raw reads with the new
predicate" — is exactly the one that does it.

**Q-e. "Must this run be byte-reproducible?"** — `determinism::isDeterministicRun()`
plus every ROM/BIOS/VMU digest guard. Overlaps `kind()` but is not derivable
from it.

**Q-f. "Is a rollback session LIVE right now, as opposed to configured?"** —
`ggpo::active()` versus `config::GGPOEnable`, 6 sites. `session::netplay()`
answers the configured half only. See §4 #9 for why this one bites.

**Q-g. "Is the recording latch currently on?"** — `recording_started`, a runtime
fact no cfg key implies. 11 sites.

**Q-h. "Is this a MACRO session, for file-format and metadata purposes?"** —
`macro()` exists with 0 callers while `MacroMode` is read raw at 6 sites.

**Q-i. "Does this session get the pause/step/overlay UI?"** — 4 sites, all
spelling `Training || play_match`, all therefore excluding Record Movie and
Record Macro. See §4 #4: a comment in `dojo.cpp` claims the opposite.

**Q-j. "Is the machine free to be stopped or started right now?"** — 3 sites.

**Q-k. "Is a savestate legal?"** — 3 sites, 3 different conjunctions. See §4 #6.

**Q-l. "Which player name goes in slot 1?"** — a pure labelling question spelled
as a kind test: `if (hosting || play_match || cfgLoadBool("dojo", "Replay", false))`.

**Q-m. "Is a headless harness driving this process?"** — `core/rend/mainui.cpp`,
2 sites. Automation, not session kind.

**Q-n. "Is the value I read the LIVE cfg or an Option cache?"** — not a session
question, but it decides the answer to every one of them. `dojo.cpp` states the
rule where it matters: *"the `config::` Options are caches only refreshed
mid-boot, and `settings.network.online` is not set until the handshake, so
neither can gate this reliably"*. `session::kind()` mixes both access paths in
one function — `cfgLoadBool` for the three dojo keys, `config::GGPOEnable` and
`settings.network.online` for netplay.

---

## 3. Where TAS and rollback are interleaved

Ordered by how hard they would be to separate.

**`Dojo::MapleApplyAction`, ~326 lines.** The worst by a wide margin. Seven of
the eight signal families and roughly 28 signal reads in one body: `play_match`
×10, `settings.network.online` ×3, `replay.ggpo_session` ×4, `config::GGPOEnable`,
`MacroMode`, `RecordMatches`, `PlayMacro`, `Training` ×2, `AutoCapture` ×2,
`HasAppendTarget()` ×2, `macro_armed`. Inside it, the rollback dedupe, the TAS
infinite-roll materialisation, the training input swap, the auto-fire live bake
and the `.flyr` append are sequential blocks in one straight-line body sharing
mutable locals — `current_inputs` above all, which every block writes in place.
**Splitting the function means splitting `current_inputs`.**

**`Dojo::PollRecordAction`, ~81 lines.** Four families. The re-record/divergence
bookkeeping (B) and the GGPO frame-dedupe (D) share the same
`session_inputs[frame_num]` write.

**`Dojo::MapleRecordAction`, ~108 lines.** Moderately tangled; the READ-WRITE /
WRITE mode axis and the GGPO trigger-fidelity branch interleave but touch
different fields.

**`gui_display_commands`, ~513 lines.** Not tangled in the desync sense — three
nested, non-aligned `if (!dojo.play_match)` regions interleaved with Training
regions, so any block move must re-derive which scope a widget was in.

**`gui_display_osd`, ~111 lines.** Both the spectate buffering test and the
second end-of-movie detector nested inside one `if (dojo.play_match)`.

**`Dojo::Reset`, ~92 lines.** Teardown for all four kinds in one body.

**`core/lua/lua.cpp`'s binding block.** Each binding is small; the tangle is
that five of them re-derive engine conditions rather than call one.

---

## 4. Latent disagreements — the same question, different conditions

These are bugs waiting, and several are live.

> **`[2026-09-10]` All eleven worked.** Six were fixed (#1, #4, #5, #6, #8, #9),
> two resolved as **two questions that were never one** (#2, #7), one
> **withdrawn** as a census error (#3), one **corrected** — its premise was
> wrong and the real finding is different (#10), and one was dead code deleted
> under compiler proof (#11).
>
> The pattern worth carrying: **four of the eleven dissolved or changed shape on
> investigation.** A census reads call sites; it cannot tell a duplicated rule
> from two rules that resemble each other, and it reads one call site where
> there are three. Every entry below now says which it turned out to be.

**#9 is live and it bit this repository today.** `core/dojo/replay.cpp` sets
`config::GGPOEnable = true` when it loads a clip that was recorded from a GGPO
match, and `session::kind()` reads that Option. So **replaying a GGPO clip
offline reports `Kind::Netplay`** — `session::replaying()` is false for it, and
a gate written as `!session::netplay()` disables every edit tool for a purely
local playback while saying "the tape is shared". No peer exists.
`ggpo::active()` has the same flaw from the other end: its body returns true for
`dojo.play_match && dojo.replay.ggpo_session`.

**#1 — "must this run be byte-reproducible?" `[FIXED 2026-09-10]`.** One owner:
`aica_if.cpp`'s `GetRTC_now()` calls `determinism::isDeterministicRun()`.

Neither list was right, and each had a piece the other needed. `aica` carried
**Receiving**, which `determinism` was missing and needed — a spectator replays a
stream of inputs and must produce the same frames as the sender, so it was not
being pinned. `determinism` was right to exclude **Transmitting**, which says
what to do with a result rather than that a session is happening — so a solo
player who had enabled uploads got a frozen 1/1/70 clock for no reason.

The earlier stale premise (that `Transmitting` defaults true) is corrected in
place rather than deleted, since a conclusion whose stated reason has rotted is
worth knowing about.
`core/determinism.cpp` omits `Receiving` and `Transmitting`;
`core/hw/aica/aica_if.cpp` includes them. `determinism.cpp` *names* the
disagreement and refuses to copy it — but its stated justification
("`Transmitting` defaults TRUE") is **stale on this base**: `core/cfg/option.cpp`
now reads `Option<bool> Transmitting("Transmitting", false, "dojo");`. The
premise is wrong; the conclusion may still be right for other reasons.

**#2 — "is a movie being written right now?", two conjunctions, neither a
superset of the other**, 240 lines apart in the same function. Different netplay
exclusion (`GGPOEnable` versus `network.online && !ggpo_session`), different
feature set (`Transmitting` versus `PlayMacro`), different access path (Option
`[RESOLVED 2026-09-10]` — as **two questions**, which is why neither was a
superset of the other:

  `writeGrow()` — MAY THE MOVIE GROW. A length question. `PlayMacro` is in it
  because a macro playback extends the roll.
  the other — SHOULD THIS FRAME BE APPENDED TO THE `.flyr`. A file question.
  `Transmitting` is in it because uploading needs a file to upload, and its
  third clause is documented at the site as "the replay-turned-write fix: a
  LOADED replay in write mode (R) must keep appending to its own file".

Merging them would change what lands on disk. `writeGrow()` got its first caller
(`MapleApplyAction`'s local, which had been the live path while the predicate
promoted out of it had none); the file question keeps its own conjunction, now
named as such rather than looking like a stray copy.

versus `cfgLoadBool`). `session::writeGrow()` copied the second; `lua.cpp`
copied the first, and its comment cites a line number that has since rotted.

**#3 — ~~the GGPO branch inverts the guard~~ WITHDRAWN `[2026-09-10]`.**
The finding said `ggpo.cpp` refuses to call `MapleRecordAction` under GGPO while
`PollRecordAction`, "reached from inside it", has a `config::GGPOEnable` arm — so
one path must be unreachable.

It is not. `PollRecordAction` has **three** call sites, and the census read one.
The GGPO arm is reached from a different entry point entirely:

    offline   ggpo.cpp:85  -> MapleRecordAction -> PollRecordAction   (!GGPOEnable arms)
    netplay   p2p.cpp:151  -> GGPORecordAction  -> PollRecordAction   (GGPOEnable arm)

`Dojo::GGPORecordAction` is called by the GGPO backend once inputs are
confirmed. The two guards are not an inverted pair; they are two entry points
for two session kinds, and both are live.

**The lesson is about the census method, not the code.** "Reached from inside it"
was an inference from one call site, and a call graph read at one site looks
exactly like a call graph read at all of them. Withdrawn rather than deleted, so
the next reader does not re-derive it.

**#4 — "does this session get pause and step?"** `[FIXED 2026-09-10]`
`session::steppable()` names the question and adds the third arm, `writeGrow()`,
which is the one that already excludes netplay. `gui_open_pause` and
`gui_open_step` use it. A self-test claim covers the Record Movie case and the
sabotage that restores the old pair fails exactly it. `tasWriteGrow` exists so that
pause/step survive a Record-Movie session, and a comment in `dojo.cpp` says so
explicitly. `gui_open_step` and `gui_open_pause` only resume for
`Training || play_match`. A Record Movie session is in neither set.

**#5 — "is the movie editable?" `[FIXED 2026-09-10]`.** There were three
answers and only one was wrong. `replay.cpp`'s `TextApply` / `ResizeProbe` have
no gate, which is correct where they sit — inside `Replay::Init`, before the
machine has started. `core/lua/lua.cpp`'s `movie.editable` answered
`!emu.running()` alone, so **a script could rewrite the tape during a paused
netplay session**, where the peer holds the same tape and desyncs on resume. It
is `!emu.running() && !session::netplay()` now, which is the scripted form of
the rule the piano roll's gate states in words.

**#6 — "is a savestate legal?" `[FIXED 2026-09-10]`.** Three conjunctions, now
one predicate: `session::rollbackLive()` — *does someone else's emulator depend
on the state of ours?*

Deliberately **not** `netplay()`: a receiver's machine mirrors a stream, so a
snapshot of it is of something nobody depends on. "Is there a peer" is the right
question for the tape and the wrong one for a savestate.

It fixed two things at once. A GGPO session before the handshake passed the UI
gate and failed the auto-save gate, because `network.online` is not set until
then. And `!config::GGPOEnable` was TRUE for a purely local replay of a
GGPO-recorded clip (§4 #9), so **auto-save was refused for a session with no peer
at all**. Loading stays unguarded, deliberately: `dc_loadstate(-1)` brings in the
synchronised net state, which is how a netplay session starts.

**#7 — "where does the movie end?", four spellings.** Two are reconciled by
comment; one defends `session_inputs.size()` as the right question for the
receive buffer.

**#8 — "is the input display meaningful with delay?" `[CORRECTED and FIXED
2026-09-10]`.** The finding said the rule is "applied at one site and omitted at
three". The three omissions are not omissions: they call
`show_last_inputs_overlay()`, which guards itself on its first line, so
repeating the rule there is what would be wrong.

What was real is a two-owner duplication between that guard and the **toggle
button** that turns the display on, in different files. Both call
`session::inputDisplayMeaningful()` now — training with delay shows the pad late,
which is worse than no display; a replay is unaffected, since what it shows is
recorded input rather than a live pad.

**#10 — `Replay::Init()` is triggered from two places `[CORRECTED 2026-09-10]`.**
Both sites do `if (cfgLoadBool("dojo", "Replay", false)) dojo.replay.Init();`
and `[MEASURED]` it runs twice per session — two `LOAD REPLAY FILE` lines.

**But they are not redundant, which is what "identical conditions" implies.**
`Dojo::Reset()` runs BETWEEN them and clears `session_inputs`, so the first
Init's work — parsing an 11,520-frame movie — is thrown away and the second is
the one that takes effect.

Established by trying to fix it. A guard keyed on the filename never fired,
because Reset cleared the flag; re-keyed on "the same clip AND the movie is
already loaded", it still never fired, because the movie is empty by then. A
guard that cannot fire is dead code, so it was reverted rather than shipped.

So the finding is not "one call is redundant" but **the first call is wasted**,
and the fix is a boot-ordering change rather than a guard — Init also sets
`savestateFolderOverride` and pins the savestate slot, which may be wanted
before the game loads. `[OPEN]`: whether the early call is needed for those
side effects, or can simply go.

**#11 — the same key read through two mechanisms.** `config::Training` is
registered and read by **nobody**; all 21 training sites use
`cfgLoadBool("dojo", "Training", false)`. `settings.dojo.Training` is written
once and read zero times. Three representations of one fact, one of which
carries all the traffic.

---

## 5. Training and rollback are already crossed

- The entire training block in `MapleApplyAction` sits **downstream of the TAS
  early return**: it is reached only because `MapleRecordAction` happens to have
  populated `session_inputs`. Nothing at either site says training depends on that.
- The delay-frame skip that makes training-with-delay work is spelled as an
  online test (`!settings.network.online && dojo.frame_number < config::Delay`)
  inside the TAS apply function.
- Training's savestate auto-load is expressed as a middle arm of three
  `AutoLoadNetState` clauses in `core/emulator.cpp`.
- A training-only rule (`Training && Delay > 0` → return) is the first line of
  `show_last_inputs_overlay()`, which replays also call.
- Insert/Eject Disc — a disc feature — has its availability decided by
  `!cfgLoadBool("dojo", "Training", false)`.

---

## 6. Open consequence of the 2026-09-09 gate correction

`roll_panel.cpp` now permits edits during a **paused replay**. Two guards inside
the funnel assume that never happens:

- the locked-range filter, `if (!history_replay && !play_match && ...)`
- the structural-edit refusal, `if (!history_replay && !play_match)`

with `play_match` true, both are skipped, so a paused-replay edit reaches
`ApplyEdit` / `ApplyEditResize` with locked slots unhonoured and structural
edits unrefused. **`[OPEN]`** — the correction was right in direction (the
funnel's own shipped customers already edit replays ungated) but it opened a
path those two guards were written against. Not yet resolved.

---

## 7. What this says about the fix

Not "adopt `session::` everywhere". The census says the predicates are unused
because they do not answer the questions being asked, so the order is:

1. **Name the questions** (§2), starting with Training as a `Kind` — 21 sites and
   no representation at all — and with Q-a and Q-f, the two that have already
   produced live bugs.
2. **Migrate against the denominator**, 170, so a missed site is loud rather
   than invisible. The failure this guards against is the familiar one: a
   partial migration reads exactly like a complete one.
3. **Work §4 as a bug list**, not as cleanup. Each entry is two sites that
   already disagree.

One predicate has been added under this reading so far — `session::livePeer()`,
for "is there a peer that would desync if this side rewrote the tape", which is
the question `netplay()` was standing in for and getting wrong per §4 #9.
