# CANON — Driver Model (input-priority redesign)

Ground-truth spec for the input-mode redesign. Agents build from THIS. Open questions are `[OPEN]`.

## The core idea
The guest **always** plays the movie (`session_inputs`, via `MapleApplyAction`) — every frame, every
mode. A "mode" never changed who *drives* the guest; it only decided **who may AUTHOR the movie this
frame.** One thing the user holds in their head: **the DRIVER**.

## Acceptance flow (David's hands-on test — the north star for "done")
1. Boot + start a replay (the Shuma-Mag clip) → lands in **WATCH** (read-only).
2. Go to **Save State 2**, reload it — still WATCH.
3. Press **R** → land in **CONTROLLER** → add inputs with the pad from there.
4. Switch to **PIANO ROLL** → modify individual frames with the brush, etc.
5. While in PIANO ROLL (write), pick **LIVE** or **PROTECTED** somewhere.
Then (later) tweak how the Timeline interacts with both.

## The three drivers
Exactly the three reachable `(play_match, macro_armed)` states — no new top-level state, no fourth mode.

### 1. WATCH  — read-only
- Neither pad nor piano roll can modify the movie. Only navigation: reload, frame-step/scrub, slot switch.
- Entered by the **R key** (the read-only *leader*) or by booting **Play Movie**.
- `play_match = true`. The word is **WATCH** (retire "READ" as a user-facing mode word).

### 2. CONTROLLER  (aka DX / the pad)
- The **pad authors** — records every frame into the movie.
- The **piano roll is greyed / WATCH-ONLY**: it still displays incoming frames, but its edit tools
  (paste / brush / transform / place / erasers) are **disabled**.
- The pad has **no window of its own** — it lives only in the **Input Viz + hotkeys** — so its "I am the
  driver" signal must be loud in the Timeline, Piano Roll, and Input Sender.
- Input Sender piggybacks **→ Live** (inject alongside the pad; bakes into the movie).
- `play_match = false, macro_armed = false` (today's Recording).

### 3. PIANO ROLL  (aka PR)
- The **cells author** — place/edit via the roll + Input Sender → Movie.
- The **controller is fully out — NO gap-fill.** The pad is **shown-but-ignored**: the Input Viz still
  lights up (you SEE it's received) but nothing records. A loud "the pad can't author here" signal.
- Input Sender piggybacks **→ Movie** (Replace / Insert / Append — kept as-is).
- Per-range locks + the State-0/BASE lock + row locks live **only here**.
- Has a sub-toggle **LIVE ⇄ PROTECTED** (see below).
- `play_match = false, macro_armed = true` (today's Temp-Playback), **minus gap-fill**.

### PIANO ROLL sub-state: LIVE / PROTECTED
- **PROTECTED** (default) = your piano-roll cells are safe; a state reload cannot clobber them.
- **LIVE** = cells are not protected (normal live editing).
- Picked "somewhere" inside PR while in write. `[OPEN: exact mechanic — map onto the existing lock/
  protect machinery (locked_slots / base_prelock / the reload-clobber guard); the planning pass pins this]`

## Color language  (red/green family, tone = scope — NO blue)
The relationship the player should feel: same hues everywhere; **HUE = read-vs-write / safe-vs-hot**,
**TONE = which level you're at**.
- **R-key / session level = BRIGHT red/green** (a bit brighter than today's TAS_READ / TAS_WRITE):
  - **WATCH = bright green**  ·  **CONTROLLER = bright red** (the pad is hot / recording).
- **PIANO ROLL sub-state = DARKER / DESATURATED red/green** (same hues, muted, so the nesting reads):
  - **PROTECTED = dark green**  ·  **LIVE = dark red**.
- The **PIANO ROLL driver banner** wears its sub-state tone (dark green when PROTECTED, dark red when
  LIVE) — so green always means "safe/read-ish", red always means "writing/hot", and bright-vs-dark tells
  you session-vs-PR. `[OPEN: exact hex shifts — derive from TAS_READ/TAS_WRITE; confirm on test]`

## The DRIVER banner  (the UX-confusion fix)
- A **whole-width row** sitting **between the Timeline and the Piano Roll**, **tall enough to demand
  attention**. Also mirrored (same content + color + font) in the **Piano Roll** and **Input Sender** so
  it's seen with any panel open/closed.
- **One unified style**, canonized with the palette + vocabulary: the driver's color (per above), **bold,
  larger font**.
- Content: the DRIVER name + a one-line meaning, e.g. `CONTROLLER — the pad is authoring · piano roll is
  WATCH-ONLY`.
- The dimmed non-author window stamps **WATCH-ONLY** on its greyed face (the portable word) + a
  disabled-edit ✎ cue.

## Controls
- **R key** = WATCH ⇄ WRITE leader. Leaving WATCH lands in **CONTROLLER**.
- **DX / PR selector** = the author choice inside WRITE. **Prominent** — the first mandatory decision out
  of WATCH ("drive with the pad, or the piano roll?"). Switchable mid-session. `[OPEN: header segment vs
  part of the banner]`
- **LIVE / PROTECTED toggle** = inside PR, in write. `[OPEN: where it sits]`
- **Start screen** = a light teach: "Use Controllers / Use Piano Roll." User still gets a replay + states.
  The deep "author an entire clip via macro + piano roll, zero controller" workflow (its save-state +
  replay-exactness concerns differ) is a **separate future expansion** — deferred.

## Input Sender
- Keep Replace / Insert / Append exactly (overwrite / shift-in / tail). Do NOT rename or drop them.
- ADD a prominent **"sending now" active-feed indicator** — the module announcing *"feeding these queued
  frames into the movie now"* while a send is live. This is the missing piece.
- Send target **auto-follows the driver**: → Live in CONTROLLER, → Movie in PIANO ROLL.

## Feasibility & the sync northstar
- Most of this is **relabel + new banner/indicator UI** over the existing `(play_match, macro_armed)`
  machinery + a LIVE/PROTECTED sub-toggle that reuses the lock/protect machinery.
- The ONLY frame-byte change is **removing gap-fill**: `MapleRecordAction` (~dojo.cpp:487) holds the pad
  out **unconditionally** in PR. It only *removes* a write source → replay stays byte-identical.
- Enforcing separation = **gating** (disable the roll's edit tools in CONTROLLER; hold the pad out in PR;
  force the Sender's target by driver). No serialize path is touched.
- `test.ps1` must stay **1 tested, 0 failed** at every commit. Never rename an `ImGui::Begin` title.

## Vocabulary (canon)
- **DRIVER** — who authors the movie right now.
- **WATCH** — read-only (the R-key mode). **WATCH-ONLY** — the portable stamp a dimmed non-author window
  wears. Retire "READ".
- **CONTROLLER** (DX / the pad) — the pad authors.  **PIANO ROLL** (PR) — the cells author.
- **LIVE / PROTECTED** — the PR sub-state (cells clobberable vs safe-from-reload).
- **Sending…** — the Input Sender's live active-feed state.
