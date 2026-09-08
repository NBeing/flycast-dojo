# A TAS studio, sorted into emuapi

`[WRITTEN 2026-09-08]` Which parts of David's TAS studio are a **neutral interface idea**, which
are a **reusable Lua component**, and which are **host furniture** that should stay in flycast
forever.

Sources read for this: `emuapi/{README,ARCHITECTURE,FEATURE_MAP}.md`, `emuapi/spec.lua`,
`emuapi/init.lua`, `emuapi/conformance.lua`, `emuapi/adapters/{flycast,mock,agnes}.lua`,
`emuapi/components/pianoroll.lua` (branch `display-viewport`); and, on the fork side,
`davids_fly/CLAUDE.md` + `CLIP_SCHEMA.md` and the engine our own tree already carries at parity —
`core/dojo/dojo.{h,cpp}`, `core/dojo/tas_{clip,wave,ruler}.{h,cpp}`, `core/dojo/tas{text,macro,va2}.cpp`,
`core/oslib/oslib.h`, `core/lua/lua.cpp`.

One fact frames everything below. `[MEASURED 2026-09-08]` **David's fork cannot reach its own movie
from Lua at all** — `davids_fly/core/lua/lua.cpp` binds no `session_inputs`, no `ApplyEdit`, no
sidecar, nothing. Our `core/lua/lua.cpp` (2,321 lines vs his 822) already binds
`flycast.movie.{length,has,editable,getButtons,setButtons}`. So on the one axis this document is
about — *is the studio's data model reachable by a portable script* — the port is ahead of the fork
it is porting from, and the question is not "how do we catch up" but "what do we expose next, and
what do we refuse to."

---

## 1. THE TEST I APPLIED

Three questions, in order. A feature is **NEUTRAL** only if it passes all three.

**Q1 — Can it be stated with the four verbs plus a capability?**
step, save/restore, press, look. If stating the feature requires naming a file layout, a window, a
thread, a codec, a device, or a Dreamcast, it is not neutral. (`ARCHITECTURE.md`, "The four verbs".)

**Q2 — Could a foreign host implement it meaningfully, and would a script mean the same thing
there?**
The two foreign hosts I actually tested each idea against are the ones in the package:

- **agnes** (`emuapi/hosts/agnes/`) — a 2,583-line NES core. Frame-boundary *and* instruction
  granularity, 84,560-byte states, a 16-bit bus with `& 0x7ff` mirroring, input latched by the game
  writing `$4016`, **eight digital buttons, one player, no ImGui, no window, no audio, no movie.**
- **a CPS2-class core** (nbneo-rr, surveyed in `FEATURE_MAP.md`) — an interpreter, **1–2 logic
  ticks per drawn frame** (mean 1.2308 measured), a user-level DAG instead of slots, macros as
  documents with anchors.

"Meaningfully" excludes a stub. agnes implementing `savestate.info` by returning a fixed record is
not an implementation.

**Q3 — Name the observable that witnesses it, and the wrong implementation it catches.**
This is the one with teeth, and it is the house rule verbatim: *"for every capability the interface
exposes, name the observable that witnesses it. If none exists, that is the work, and it comes
first. A capability whose only proof is its own getter is not a capability, it is a variable."*

The operational form I used: **write down the plausible wrong implementation, then write the check
that fails on it.** If the only check I can write is `set(x); assert(get() == x)`, the verdict is
not NEUTRAL — because that check passes against a binding that stores into a struct nothing reads,
and every defect the ten nbneo passes found had exactly that shape.

**COMPONENT** is the residual class with a positive test of its own: *would this same Lua file, with
no edits, draw something correct and useful on agnes?* `components/pianoroll.lua` passes because it
asks `joypad.buttons()` for its columns. David's roll fails because its columns are
`{"^","v","<",">","LP","HP","LK","HK","A1","A2","ST"}` — a literal MvC2 pad, in code
(`dojo_gui.cpp:2455`). Same feature, and the difference between the two is the entire thesis.

**HOST** is everything whose *meaning* is a property of one emulator's build: files, windows,
threads, codecs, devices, its config store, its menu bar. Not a lesser class — the studio is mostly
this, and correctly so.

### The falsifier for this document

If someone implements one of my NEUTRAL verdicts on agnes and finds it either (a) cannot be
implemented without inventing a fact, or (b) can be implemented as a constant that passes every
check I propose, **that verdict is wrong and this section is how you prove it.** Section 6 exists
so that (b) is answerable without arguing.

---

## 2. COMPONENT BY COMPONENT

| # | Studio feature | Verdict |
|---|---|---|
| 1 | The `.frame` sidecar — a state knowing its movie frame | **NEUTRAL** |
| 2 | The dead-timeline guard | **NEUTRAL** |
| 3 | Savestate slots: how many, occupied, size, when | **NEUTRAL** |
| 4 | Slot labels | **NEUTRAL (weak check — see §6)** |
| 5 | Frame advance / the movie playhead's clock | **NEUTRAL** (mostly exists; one gap) |
| 6 | Read-only vs read-write, re-record count | **NEUTRAL** (in spec, unimplemented) |
| 7 | The edit funnel: batched, atomic movie writes | **NEUTRAL** |
| 8 | Row insert / delete (structural edit) | **NEUTRAL, capability, deferred** |
| 9 | Piano roll | **COMPONENT** (exists; needs §3's additions) |
| 10 | Input Visualizer | **COMPONENT** — the cheapest proof of the whole thesis |
| 11 | Autofire / turbo (`tas_auto`) | **COMPONENT** |
| 12 | Notepad + text notation codecs (`tastext`, `tasva2`, `tasmacro`) | **COMPONENT** (notation is DATA) |
| 13 | Input Sender (staged queue) | **COMPONENT** |
| 14 | Bookmarks (named frame spans) | **COMPONENT** — with a stated hole |
| 15 | Derived per-frame lanes (`tas_wave`, `tas_ruler`) | **COMPONENT** over §5's contract; the taps are HOST/DATA |
| 16 | States window (F4 thumbnail wall) | **COMPONENT** over §4's metadata |
| 17 | Per-state thumbnails | **HOST** |
| 18 | Generations (F8 backup/restore) | **HOST** |
| 19 | Clip folders, `clip.json`, tags, notes, browsers | **HOST** |
| 20 | BASE (slot 0) hold-to-overwrite | **HOST** |
| 21 | Timeline locks (`locked_slots`) | **HOST** |
| 22 | Hotkey registry, rebinding, chords, per-pad mappings | **HOST** |
| 23 | F5 menu bar, docking, dockable modules | **HOST** — neutral residue already shipped |
| 24 | Capture pipeline (ProRes/CineForm/VfW) | **HOST** |
| 25 | `VerifyState` byte-idempotence tripwire | **NEUTRAL — as a conformance check, not an API** |
| 26 | MvC2 memory probe (`mvc2.cpp`) | **DATA** (a profile) — not emuapi, by definition |

Reasoning follows. API sketches are in `spec.lua`'s style: lowercase names, 1-based indices, absent
data is `nil` plus a non-empty reason, programmer error raises, missing capability answers false
from `emu.supports()` and raises if called.

---

### 1. The `.frame` sidecar — **NEUTRAL**. The single most important idea in the studio.

Full argument in §4/§5; the verdict here.

**Q1.** A savestate is a value produced by `save`; a movie frame is a position in a clock; the pair
is one `look`. Nothing about it names a Dreamcast.

**Q2.** agnes has states and could have a movie in ten lines of its Lua host. nbneo has this
concept already, spelled differently — its DAG node is `{parent, inputs applied, resulting state
identity}`, which is the same fact with the parent pointer kept. Two independent projects reached
it; `ARCHITECTURE.md` calls that convergence "evidence the contract is discovered, not invented".

**Q3.** Two observables, both strong. **(a)** save while recording, then read the anchor back: its
frame must equal the playhead. **(b)** load an anchored state during playback: the playhead must
*move*. (b) is not decorative — the fork shipped and fixed exactly its absence: *"the counter kept
running while the machine rewound, so EVERY mid-recording load shifted all later inputs by the
rewound amount"* (`dojo.cpp:750`). A host that restores the machine and forgets the movie clock
fails (b) and passes everything else.

```lua
--- Where this state sits in a movie. nil + reason when the slot is empty, or
--- when the state was taken outside a movie - both are absent data, not errors.
function savestate.anchor(slot) end   --- [spec] -> record, or nil + reason
```
```
{ movie   = string,   -- movie.id() at save time
  clock   = string,   -- MUST be a member of clock.list()
  frame   = number,   -- its position in that clock
  prefix  = string or nil,   -- movie.prefixhash(frame) at save time; nil = this
                             -- host cannot state one. NEVER a guess.
  verdict = "clean" | "suspect" | "stale" | "foreign" | "unknown" }
```

Two rules ride with it, and they are the part a host can get wrong silently:

* **Anchoring is not optional.** If a movie is open when `savestate.save(slot)` succeeds, the host
  MUST record an anchor. A host that anchors only when a user asks has made the guard advisory, and
  an advisory guard is the one that is off on the day it mattered.
* **Loading an anchored state seeks the movie**, in every mode the host has. What "seek" means
  differs (playback jumps the playhead; recording rewinds it and re-records in place) and the
  interface does not care — it cares that `clock.now(movie.clock())` equals `anchor.frame`
  afterwards.

### 2. The dead-timeline guard — **NEUTRAL**. Contract in §5.

**Q1.** "Has the movie below this state changed since the state was taken" is `look` over
`save/restore` and `press`. **Q2.** Any host with re-recording has this problem *by construction* —
that is what re-recording is. **Q3.** The wrong implementation is a global dirty flag, and the
check that catches it is the one that must NOT fire: edit a frame *above* the anchor, verdict stays
clean. See §6.

### 3. Savestate slots: how many, occupied, size, when — **NEUTRAL**

**Q1.** `look`, at the host's own store of saved values. **Q2.** agnes writes a file per slot under
`$AGNES_STATE_DIR`; nbneo has a DAG and would answer with its arena. **Q3.** The observable is
already sitting in the suite as a *hole*: `conformance.lua` skips its empty-slot check on flycast,
which declares no `probe.emptyslot`, because *"loading a guessed slot would rewind the machine out
from under the suite."* `savestate.info` answers that question from the interface instead of from an
adapter's hand-written probe, and **retires that skip** — the strongest form of evidence available
here, a proposal that pays a debt the suite has already written down.

`[MEASURED 2026-09-08]` The other two adapters do declare one, and both are hand-picked magic
numbers: `mock.lua` says `emptyslot = 9`, `agnes.lua` says `emptyslot = 9999`. Neither is *wrong* —
agnes's slots are files, so 9999 is genuinely empty — but both are facts about a host, written by
hand, in a place the host cannot keep in sync with itself. `info()` derives them instead.

```lua
function savestate.slots() end        --- [spec] how many slots; valid indices are 1..N.
                                      ---        nil + reason where slots are UNBOUNDED
function savestate.info(slot) end     --- [spec] a record for ANY valid slot; raises out of range
```

**`slots()` may answer nil, and agnes is why.** `[MEASURED 2026-09-08]` its slots are files named by
index with **no ceiling at all**, so there is no number it could return that would not be a lie.
Same rule as `memory.space().size`, for the same reason: a caller trusts a number and bounds itself
by it. A nil means "ask `info()` about whatever slot you care about"; it does not mean the namespace
is unimplemented.
```
{ occupied = boolean,           -- the only required field
  bytes    = number or nil,     -- nil is legal and is not a hole - see below
  when     = number or nil,     -- seconds since the host's epoch
  label    = string or nil }
```

`bytes` and `when` may be `nil` **for the same reason `memory.space().size` may be**: a host that
cannot state one must answer nil rather than a plausible number, because a caller trusts a number
and bounds itself by it. That precedent is already argued at length in `spec.lua` (ADDRESS SPACES),
and reusing it is cheaper than inventing a second rule.

`savestate.slots()` also fixes a live defect. `[MEASURED 2026-09-08]`
`emuapi/adapters/flycast.lua:520` reads `local SLOTS = 10` — a hardcoded guess against a host whose
`core/oslib/oslib.h:50` says `MAX_SAVESTATE_SLOTS = 100`. **Ninety slots are unreachable through the
interface today and nothing says so**, because the interface never gave the host a way to answer.

### 4. Slot labels — **NEUTRAL, with a check I cannot make non-degenerate**

A label is a string a person attached to a state. `look`; any host can store one; agnes could keep a
`.label` file exactly as flycast does (`oslib.cpp:250`). It rides free on `info()`.

But its only honest observable is **persistence across a session**, and the suite cannot restart a
host. Write-then-read-back is precisely the degenerate check the house rule rejects. So: `label` is
a **read-only field of `info()`** now, and `savestate.setlabel` waits until an adapter offers
`control.reopen` (the same shape as `control.reserve`, which is what let the display group prove the
picture actually shrinks). I am recording this as a doubt, not resolving it by fiat.

### 5. Frame advance and the movie's clock — **NEUTRAL, and one real gap**

The verbs exist: `emu.frameadvance` (yield), `emu.run`, `emu.speedmode`, `emu.pause`. Hold-Space
scrub pacing, the ramp, the debounce — all HOST; they are sleeps in the host's main loop and cannot
perturb guest state, which is exactly why they are furniture.

The gap is **which clock the movie is indexed in**, and flycast has three counters that a script
cannot tell apart:

`[MEASURED 2026-09-08]` `frame.count()` → `dojo.frame_number` (the movie playhead — resets to 0 on
open, sits at 0 for ~142 boot frames, and is *not* monotonic). `frame.confirmed()` → a different
count entirely. And `movie.framecount()` → `getReplayFrameCount()`
(`flycast-dojo/core/lua/lua.cpp:1658`), which returns **`session_inputs.size()` during playback and
`frame_number` during recording** — *the length in one mode and the playhead in the other*. That is
a defect, and it is invisible today because nothing in the interface says which of the two it is
supposed to be.

```lua
function movie.clock() end   --- [spec] the clock a movie frame index is in, or nil + reason
```

Deliberately **not** a new position accessor: the position is `clock.now(movie.clock())`, using
machinery `spec.lua` already built and the flycast adapter already publishes (`movie_frame`,
`confirmed_frame`). One name, and it makes `movie.framecount()` checkable for the first time — the
contract becomes *`movie.framecount()` equals `clock.now(movie.clock())`*, which flycast fails
today, in playback, measurably.

### 6. Read-only / read-write and the re-record count — **NEUTRAL, in the spec, unimplemented**

`movie.getreadonly` / `setreadonly` / `rerecordcounting` are `[port]` in `spec.lua` and bound by
nobody. The mode toggle is the studio's **R** key and is genuinely portable — PCSX2, FCEUX, BizHawk
and this fork all have it under the same name.

The missing half is a **getter for the count**. `movie.rerecordcounting(on)` sets a policy with no
way to read the result, which is the "action that produces nothing observable" shape this project
keeps paying for. Add `movie.rerecords()`.

### 7. The edit funnel: batched, atomic movie writes — **NEUTRAL**

`movie.setframe(f, p, btns)` exists and is right. What is missing is **one edit that spans many
frames**. Every real editing gesture is plural — paint a column, paste a selection, mash-fill, undo
— and N single-frame calls are N timeline events, N `.flyr` appends and N chances to be interrupted
half-applied.

```lua
--- edits = { {frame=, player=, buttons=}, ... }. ALL OR NOTHING: if any entry
--- is refused, NOTHING is applied. Absent keys still leave a button alone.
--- Returns the number of frames changed, or false + reason.
function movie.setframes(edits) end   --- [spec]
```

**Q3** is the reason this is worth a name rather than a loop: atomicity is observable. Hand it a
batch containing one illegal entry and require that `movie.prefixhash` is **unchanged** afterwards.
A host that applies until it trips fails that; a loop in Lua *cannot pass* it. That is a property
the interface can hold and a script cannot fake.

### 8. Row insert / delete — **NEUTRAL, capability-gated, deliberately later**

Real TAS work (TASEditor has it; our `Dojo::ApplyEditResize` has it, and it must rewrite the `.flyr`
from scratch because appends cannot express a shrink). Portable in shape: `movie.insert(frame, n)` /
`movie.remove(frame, n)`, capability-gated because a host whose movie is an append-only log may
honestly answer `cannot`. Falsifiable trivially (`movie.length()` moves by n; the frame that was at
F is at F+n). **Sequenced last anyway** — see §7.

### 9. Piano roll — **COMPONENT.** It already exists and is the model.

`components/pianoroll.lua` is 208 lines and grows its columns from `joypad.buttons()`. David's is
~3,400 lines inside `dojo_gui.cpp` with `LP/HP/A1/A2` in a `static constexpr` array. The gap between
them is not 3,200 lines of portable code — it is selection, painting, clipboard, gutter drag,
bookmarks and undo, each of which is component work sitting on §3's four additions.

What the component needs from the neutral layer, and nothing more: `movie.getframe/setframe`
(have), `movie.setframes` (§7), `movie.length/hasframe` (have), `joypad.buttons` (have),
`movie.clock` (§5), `savestate.anchor` (§1, to draw the slot markers and grey the stale rows), and
`ui.SameLine(offset)/Selectable(w,h)/CalcTextSize` (have — and they were added for exactly this).

Undo/redo stays **component-side**, built from `getframe` snapshots plus `setframes`. I considered
`movie.undo()` and rejected it: two owners of one history is a defect, the host's own live recording
writes frames the component never sees, and the component that owns the edits is the one that can
name them. The neutral prerequisite is only that a component can *detect* its history went stale —
which `movie.prefixhash` gives it for free.

### 10. Input Visualizer — **COMPONENT**, and the cheapest proof of the thesis

371 lines in the fork; ~40 in Lua. It draws `joypad.get(player)` against
`joypad.buttons()`. It has no host dependency at all beyond the surface, it renders correctly on
agnes with eight buttons and one player without an edit, and it is the smallest thing that
demonstrates the whole argument to someone who has not read any of this.

One half of it is *not* portable and the split is instructive: the fork's viz draws **SENT** (from
the movie) as a ring and **READ** (the game's own latched flags, out of MvC2 RAM via `tas_mvc2::read()`)
as a fill. SENT is neutral. READ is a **profile** — it is game knowledge, and it belongs in a
profile with tests, per `ARCHITECTURE.md`'s DATA class. The component asks the profile; it never
reads an address.

### 11. Autofire (`tas_auto`) — **COMPONENT**

`{[player][button] = hz}` plus a phase test per frame, driving `joypad.set` from `registerbefore`.
Pure `press`. Nothing in it is a Dreamcast. ~30 lines of Lua, and the input arbiter already handles
the interesting case (two owners of one button are refused, loudly).

### 12. Notepad and the notation codecs — **COMPONENT** (and the notation is DATA)

Three different things wear one name here, and they sort three different ways:

* **The editor** (`ImGuiColorTextEdit`, multi-cursor, syntax squiggles) — HOST. It is a vendored
  widget, and `ui.*` is a baseline profile, not a text-editor toolkit.
* **The text↔frames codec** — COMPONENT. `tastext.cpp`'s frame-per-line mnemonic table is column
  names + one character each, generated from the button list; in Lua it is a table comprehension
  over `joypad.buttons()`.
* **VA2 / V PRO notation** (`tasva2.cpp`, 1,115 lines: `236hk12`, `[lp/hp]*360`, assist slots that
  are *unknowable from the text*) — **DATA**. It is one game's transcription dialect from 2009, and
  putting a fighting-game motion grammar in a neutral emulator interface is precisely the mistake
  `ARCHITECTURE.md` names ("Game knowledge... that is content").

The important structural point: the notepad **never writes `session_inputs` directly** — it parses
to frames and calls `ApplyEdit`. That is `movie.setframes`. The fork already discovered that the
text layer wants exactly one neutral verb, which is good evidence the verb is the right size.

### 13. Input Sender — **COMPONENT.** A staged queue with its own undo, committed through the
funnel. Same shape as the notepad, one layer thinner.

### 14. Bookmarks (named frame spans) — **COMPONENT, with a hole I will not paper over**

`{name, lo, hi}` is portable and obviously useful. The problem is **where they live**: the fork
persists them in `clip.json`, and emuapi has, deliberately, **no persistence surface at all**
("Storage. See the DAG section: identity yes, mechanism no"). So a bookmark component today can hold
spans for a session and lose them at exit.

I am not proposing `movie.setmeta`/`getmeta` to fix it. A key-value blob attached to a movie is the
kind of addition that looks free and then becomes the place every tool stores everything, and its
conformance check is `set/get` — degenerate. If bookmarks need to persist, the honest first move is
for **one host** to offer it as an adapter extra and see what a second host does with it. Recorded
as an open question, not designed around.

### 15. Derived per-frame lanes (`tas_wave`, `tas_ruler`) — **COMPONENT over §5's contract**

This one changed my mind while writing, and it is the most valuable secondary finding here.

`tas_wave` stores a per-movie-frame audio envelope; `tas_ruler` stores a per-frame skip sample. Both
are indexed by movie frame, both persist beside the clip, and — the interesting part — **both had to
invent staleness independently**: `tas_wave::FrameEnv.flag` is `0 = never measured, 1 = live (this
take), 2 = stale (an old take: rewound or edited before it)`, and `tas_ruler::eraseFrom(frame)`
exists because "row surgery" invalidates samples the same way.

That is the dead-timeline problem again, wearing a third hat, in a subsystem that has nothing to do
with savestates. Which means: **once §5's contract is neutral, every derived lane a tool builds
inherits invalidation for free** — a lane records `movie.prefixhash(f)` alongside its samples and
knows exactly which of them belong to an abandoned take. No new interface, no new capability.

The lane *library* is a component. The **taps** are not: the audio envelope needs a per-sample hook
inside the mixer (HOST — and note the fork taps *before* host volume, so the OS mixer cannot
perturb it), and the skip map reads MvC2 addresses (DATA).

### 16. States window — **COMPONENT** over §4's metadata.

The 100-slot wall, sorting, filtering, the card size slider, arrow-key navigation over the
*displayed* order — all Lua over `savestate.slots()` + `savestate.info()` + `savestate.anchor()`.
What is not portable is the thumbnail image (next item), the docking, and the "mouse-only while a
recording is live" rule, which is a statement about the host's own input focus.

### 17. Per-state thumbnails — **HOST**

Tempting, and I am refusing it on my own Q3.

A thumbnail is produced by `renderer->GetLastFrameRGB()` — implemented **only for DX9 and DX11**, so
it does not exist on our own GL build, let alone on agnes, which has no renderer at all. Consuming
it needs `ui.Image`, which is declared unimplemented on *every host in the package* (`not_yet` on
flycast and mock, `cannot` on agnes). And its observable is a picture: the only non-degenerate check
is "the preview of state A differs from the preview of state B", which needs a framebuffer hash the
package does not have.

That last point is the precedent, not my opinion: the layer-mask capability in the nbneo passes
**could not be tested at all** until a pass that gave rendering a name was built first, and it was
displaced rather than shipped untestable. Thumbnails are in exactly that position. When a
framebuffer hash exists, revisit; until then, `info().preview` would be a variable, not a
capability.

### 18. Generations (F8) — **HOST**

`archive()` copies every top-level file with one of eleven extensions into `<clip>_gen_NN`;
`restore()` moves live-only states to `.trash/<utc>/`, overwrites, and **merges** `clip.json`
field-by-field (`rerecords` becomes `max(live, backup)` so the sequence clock never runs backward).
Every sentence of that is a file-system sentence. It is also *good* — the merge rule is subtle and
correct — and none of it is an interface idea.

The neutral residue is one word: **identity**. A restore is only meaningful if a movie and its
states can say which movie they belong to, which is `movie.id()` and `anchor.movie` (§1). That is
the whole of it, and `ARCHITECTURE.md` said so in advance: *"Specify identity; never specify
storage."*

### 19. Clip folders, `clip.json`, tags, notes, browsers — **HOST**

`clip.json` schema 6 is a genuinely well-designed document (additive versioning, `states[]` declared
a *cache* with the on-disk sidecars authoritative, `contents` recomputed per write). It is also a
**storage format**, and a storage format in a neutral interface is the thing that stops a second
host conforming — nbneo's equivalent is a DAG in an arena, and no amount of goodwill turns that into
a folder of `.state` files.

Where it is right, it is right about something emuapi should *state* rather than *store*: `rewinds`
is the timeline ledger (→ §5), `states[]` is slot metadata (→ §4), `stats.rerecords` is the
re-record count (→ §2.6).

### 20. BASE hold-to-overwrite — **HOST.** A one-second key hold with a filling green bar, driven off
`os_GetSeconds()` in the main loop. It is a *gesture*, and a gesture is furniture by definition. The
underlying want — "this state must not be lost" — is a slot lock, and see the next item for why I am
not proposing one.

### 21. Timeline locks (`locked_slots`) — **HOST**

I initially had this as neutral and it is not. `[MEASURED 2026-09-08]` `Dojo::locked_slots` is a
`std::set<int>` of **slots whose forward input range is protected**, held **in memory only** and
re-derived each session; `ApplyEdit`/`ApplyEditResize` drop writes landing inside a locked range.

It fails Q2 for a specific reason worth writing down: **a lock that only a cooperating tool respects
is not a lock.** For it to mean anything, the *host* must enforce it against its own live recording
— which requires the host to own the edit funnel, which is the host-local thing. A neutral
`movie.lock(lo, hi)` would be honoured by scripts and ignored by the emulator, and a lock that holds
for everyone except the emulator is worse than none.

### 22. Hotkeys — **HOST**, and the residue already exists

1,150 lines about the **host's** input devices: per-device `.cfg` mapping files, two identical pads
sharing one `InputMapping`, analog directions bound as buttons, chords in the high bits of a key
code, a detect mode that swallows the press before it reaches the emulator. Not one sentence of that
is about the guest.

The neutral residue is already in `spec.lua` as `input.registerhotkey(name, fn)` `[port]`: a script
asks to be called when the user triggers a *named action*, and where that name is bound is the
host's business. And note what the TAS hotkeys actually *are* underneath — frame advance is
`emu.step`, F1/F3 are `savestate.save/load`, R is `movie.setreadonly`. The verbs were already there;
the keys are furniture on top of them.

### 23. F5 menu bar, docking, dockable modules — **HOST**, and this is the model case

The neutral residue of the entire docking system is **one rectangle**, and it already shipped:
`display.viewport()` on branch `display-viewport`, with the argument written out in `spec.lua` —
the host publishes the area it left, the renderer publishes the aspect, the viewport is the product,
and *"a host that derives it from the window instead is correct until the day something docks, and
wrong silently after."*

That is the correct ratio for a studio feature: thousands of lines of furniture, four numbers of
interface. It is the ratio I have tried to hold everywhere else in this document.

### 24. Capture — **HOST.** Renderer readback, a bounded queue, a writer thread, ffmpeg, VfW codecs,
`.mov` muxing. Zero portable content, and I would refuse an `emu.capture()` even if asked: its
observable is a video file, which is the exact defect this project already paid for once ("a
well-formed video file that was entirely black").

### 25. `VerifyState` — **NEUTRAL, and it is not an API**

The fork's northstar tripwire is: after every load, re-serialise and byte-compare against the file
(`idempotent OK`, or a first-diff offset plus `SERMAP` per-subsystem offsets). It found two real
desync bugs (an SCIF timer reschedule and AICA envelope side-effects clobbering restored state).

It needs **no new interface** — `savestate.save_mem` and `savestate.hash` already express it:

```lua
local a = savestate.save_mem()
savestate.load_mem(a)
local b = savestate.save_mem()
-- a state that does not reload to itself is a desync waiting to happen
```

So it belongs in `conformance.lua`, as a group every host runs. `[REASONED]` This may be the
highest value-per-line item in the whole document: it is ~15 lines of suite, it needs nothing built,
and it is a check that has *already* caught two shipping bugs on one host and has never been run on
the other two. Note honestly what it cannot do: it proves a state is self-consistent, not that it is
complete — a subsystem missing from serialisation entirely is missing from both sides of the
comparison and the check passes. A second, harder check (save → run N frames → load → run N frames →
compare state hashes) closes that, and can also fail.

### 26. `mvc2.cpp` — **DATA.** A profile, by the book. Not emuapi, and the fact that it is only 242
lines is the argument for the whole layering.

---

## 3. WHAT THE EXISTING SPEC IS MISSING

Checked against `spec.lua` and `init.lua`'s `M.surface` first. **None of these duplicates an
existing name.** Every one names the verb it reduces to.

| Proposal | Verb | Why it is not a duplicate |
|---|---|---|
| `savestate.slots()` | look | nothing asks how many slots exist; the flycast adapter hardcodes 10 against a host with 100 |
| `savestate.info(slot)` | look | `savestate.load` answers *nil + reason* for an empty slot; nothing describes a slot without loading it |
| `savestate.anchor(slot)` | look over save/restore | no savestate name mentions a movie today |
| `movie.id()` | look | `emu.romname`/`gamename` identify the *game*; two clips of one game are different movies |
| `movie.clock()` | look | `clock.list()` names the host's clocks; nothing says which one the movie is in |
| `movie.prefixhash(frame)` | look | `savestate.hash` hashes the machine; this hashes the document |
| `movie.rerecords()` | look | `movie.rerecordcounting(on)` sets the policy and there is no reader |
| `movie.setframes(edits)` | press | `movie.setframe` is one frame; atomicity across many is a different property |
| `emu.step(n)` | step | `emu.frameadvance` is a *yield* — it needs the host to own the loop |
| `movie.insert/remove` | press | structural; `setframe` cannot change length |

Full sketch, in `spec.lua`'s idiom:

```lua
--- savestate ------------------------------------------------------------
function savestate.slots() end          --- [spec] N; valid slots are 1..N
function savestate.info(slot) end       --- [spec] {occupied, bytes?, when?, label?}
                                        ---        raises out of range (tier 1)
function savestate.anchor(slot) end     --- [spec] the record in section 2.1,
                                        ---        or nil + reason

--- movie ---------------------------------------------------------------
function movie.id() end                 --- [spec] opaque identity of the open
                                        ---        movie, or nil + reason
function movie.clock() end              --- [spec] a name from clock.list()
function movie.prefixhash(frame) end    --- [spec] a STRING - identity of every
                                        ---        authored frame BELOW `frame`
function movie.rerecords() end          --- [spec] monotonic; never decreases
function movie.setframes(edits) end     --- [spec] atomic; -> count, or false + reason
function movie.insert(frame, n) end     --- [spec] [capability]
function movie.remove(frame, n) end     --- [spec] [capability]

--- emu -----------------------------------------------------------------
function emu.step(n) end                --- [spec] [capability] advance n frames
                                        ---        IMPERATIVELY and return
```

Three notes that are part of the proposal, not commentary.

**`prefixhash` returns a STRING**, for the reason `savestate.hash` does and which is already argued
in `spec.lua`: a wide hash carried as a Lua number collides silently under 5.1/5.2 and prints
identically under 5.3. The contract spans 5.1 through 5.4. Do not repeat that mistake in a second
place.

**`emu.step` is a CAPABILITY, not a portable primitive**, and I am contradicting `ARCHITECTURE.md`'s
enthusiasm for it on purpose. `[MEASURED 2026-09-08]` flycast's `emu.run` is a coroutine resumed
from the host's frame callback — the script runs *inside* a frame, so re-entering the emulator loop
from a Lua call is not a binding, it is a rearchitecture. agnes can do it in one line
(`agnes_tick()`). So flycast very likely answers `{kind = "cannot", why = ...}` or `not_yet`, and
**that is the capability tier working as designed**: *"a capability that lives only in the host that
has it is a fact the other host cannot even decline."*

**Two places where I am bending the four-way classification, said out loud:**

1. `savestate.info` reports `bytes` and `when`, which smell like **storage**, and
   `ARCHITECTURE.md` says identity yes, mechanism no. My argument: these are observable facts about a
   saved value, not a mechanism — nothing here says where the bytes are, whether they are compressed,
   or whether the host re-derives them from an ancestor rather than storing them at all (which is
   exactly the freedom that section was protecting). And both fields may be `nil`, so a host that
   stores nothing is fully conforming. If that argument is wrong, the fix is to drop the two fields,
   not the namespace.
2. **The movie as an editable document** is arguably a layer-2 tool, not one of the four verbs. It is
   already in `spec.lua`, and the evidence says it belongs: the `port` branch classified 33 real
   `macro.*` names in advance and landed **25 portable, 5 host-local, 3 tool** — *"transport +
   document is portable, file I/O is the host's, naming is the tool's."* A movie is a deferred
   `press`, addressed by frame instead of by now.

---

## 4. THE SAVESTATE / SLOT MODEL

David's fork has: 100 slots; a write-protected BASE; per-slot PNG thumbnails; `.label` sidecars; a
`.frame` sidecar (v1 → v2 → v3); and generations. My split, with the reasoning:

| Fork feature | Verdict | Because |
|---|---|---|
| **100 slots** | NEUTRAL **as a count**, never as the number | `savestate.slots()`. 100 is a flycast fact; "how many do you have — or none" is a question every host can answer, and the interface currently makes the adapter guess (it guessed 10) |
| **`SlotCycleCount`** (how many F2 walks) | HOST | A UI convenience over a keyboard. All 100 remain reachable; the cycle is furniture |
| **BASE write-protection** | HOST | A one-second key hold. A gesture |
| **Thumbnails** | HOST | No observable without a framebuffer hash; `ui.Image` unimplemented everywhere. §2.17 |
| **Labels** | NEUTRAL (read), weak check | §2.4 |
| **`.frame` sidecar** | **NEUTRAL — the most important idea in the studio** | below |
| **Generations** | HOST | File copying. Residue = `movie.id()` |

### The sidecar is the load-bearing one, and here is the argument

**A savestate alone is not a TAS artefact.** It is a machine. What makes it usable in a TAS is that
it names a *position in a document* — and every re-recording emulator that has ever worked has
needed that fact, under a different name each time:

* flycast-dojo: a `.frame` sidecar file beside the state.
* PCSX2-rr / FCEUX / BizHawk: the frame counter serialised **into** the state.
* nbneo-rr: a DAG node, which is `{parent, inputs applied, resulting state identity}` — the same
  fact with the parent pointer kept and the frame index implied by depth.

Three independent projects, three storage mechanisms, one concept. That is the definition of
something that belongs in an interface: **the concept is shared and every implementation of it is
different**, so a script that wants the concept has to be rewritten per host for no reason.

And the counter-argument deserves a hearing, because it is the reason to hesitate: *a host that
serialises the frame counter into the state has no sidecar and no separate metadata — is
`savestate.anchor` asking it for something it does not have?* No, and this is the point of an
adapter. That host answers `anchor(slot)` by reading the counter out of its own state; flycast reads
a 20-byte file; nbneo reads a node. **The interface asks for the fact and never for the file.** If
the interface had specified a sidecar, only flycast could conform — which is exactly how the
"neutral layer with one host's facts inside it" failure mode looks, and this package has shipped it
once already (an SH4 address in a portable check).

Decision: **the anchor is neutral. The sidecar is not.** `savestate.anchor(slot)` returns the four
facts (movie, clock, frame, prefix) and says nothing about where they live.

One more property worth requiring, because losing it is silent: **an anchor must survive being
copied with the state.** Not testable by the suite in one session (same hole as labels) — recorded,
not asserted.

---

## 5. THE DEAD-TIMELINE PROBLEM

> After a re-record, savestates from the abandoned attempt no longer match the movie. They load
> fine. They verify byte-perfect. The movie desyncs from them anyway.

**This is neutral, and it is the second-most-valuable thing in the studio after the anchor —
because it is the failure mode that is silent by construction.** Every check the user has says the
state is fine. The state *is* fine. The state is from another timeline.

Any host with re-recording has this, because re-recording is *defined* as overwriting authored
frames in place. A host that thinks it does not have it has it and does not know.

### The contract

**Definition (this is the whole thing).** A state's anchor is **valid** iff the movie's authored
content **below** its anchor frame is identical to what it was when the state was taken.

Content, not history. That word choice is the entire design, and the fork arrived at it the
expensive way — v2 of the sidecar judged by history (a rewind sequence number) and cried wolf on
undo; v3 added a prefix hash so identical bytes *exonerate*, and the fork's own comment for it is
*"the seq rule only ARMS suspicion; identical bytes below the anchor EXONERATE."* Take the v3
answer, not the v2 one.

**The five verdicts.** `anchor.verdict` is one of:

| verdict | meaning | witnessed by |
|---|---|---|
| `clean` | proof the prefix is unchanged | `anchor.prefix == movie.prefixhash(anchor.frame)` |
| `stale` | proof it changed | the same comparison, unequal |
| `suspect` | something was re-recorded below it; the host cannot prove the bytes differ | a host with a rewind ledger and no hash |
| `foreign` | the state belongs to a different movie | `anchor.movie ~= movie.id()` |
| `unknown` | this state carries no anchor data | old sidecars; a host that anchors nothing |

Five feels like a lot and I kept it, because **the fork needed exactly these five and collapsing any
two of them is a bug it actually shipped.** Collapse `suspect` into `stale` and you get v2's crying
wolf, which trains the user to ignore the warning — the worst possible outcome for a guard.
Collapse `unknown` into `stale` and every pre-v2 state is condemned on sight. Collapse `foreign`
into `stale` and the message says "re-save this slot", which is wrong and destructive advice for a
state that belongs to another clip.

**The rules a host must satisfy.**

1. **Content decides.** If `prefix` is non-nil, `verdict` MUST be `clean` when
   `anchor.prefix == movie.prefixhash(anchor.frame)` and `stale` when it differs. The host is
   checkable **against itself**, which is the delivery cross-check's shape and the strongest form
   available without a second host.
2. **No crying wolf.** An edit at a frame **≥** the anchor frame MUST NOT change a `clean` verdict.
   This is the rule that fails on the obvious wrong implementation (a global "movie dirty" flag) and
   it is the reason the positive check alone is not evidence.
3. **Exoneration is mandatory.** If the prefix returns to a previous value — undo, or re-recording
   an identical stretch — the verdict MUST return to `clean`. A host may pass through `suspect` in
   between; it may not stay there.
4. **A host without a hash may answer `suspect`, never `stale`.** `stale` is a claim of proof.
   Saying `suspect` when you can prove nothing is honest; saying `stale` is the black-AVI-with-a-
   green-light shape one level up.
5. **Advisory, never enforcement.** `savestate.load` on a stale state MUST still load it, and MUST
   NOT silently repair anything. Loading a stale state and re-saving the slot **is the standard
   repair workflow**; a host that refuses has broken re-recording to protect the user from it.
6. **Seek and judge are separate.** Loading an anchored state seeks the movie (§1) *whatever the
   verdict is*. A host that skips the seek because the state is stale has invented a third behaviour
   nobody asked for.

**What is deliberately NOT in the interface: the rewind ledger.** The fork's
`rewind_log` — `(seq, frame)` pairs, `stale iff ∃ r: r.seq > stateSeq and r.frame < stateFrame` — is
a good, cheap, conservative approximation for a host that cannot hash a prefix. It is also
unverifiable from outside (nothing a script can observe distinguishes a correct ledger from a
plausible one) and it commits every host to one mechanism. **Specify the observable; let the ledger
be an optimisation.** A host that has only a ledger answers `suspect`, and rule 4 makes that a
legitimate, complete answer rather than a failure.

**One refinement the fork learned that the contract should carry.** `[MEASURED 2026-09-08]` A rewind
is **not itself** a timeline event in `dojo.cpp` — the event fires at the first *write whose bytes
actually differ*, with that divergence frame as the event frame (`divergence_open` collapses a run
into one event). So seeking back to *watch* your own work costs nothing, and replaying an identical
stretch costs nothing. Rules 1 and 3 give a conforming host that behaviour automatically, since a
prefix that never changed never differs — which is a good sign that the contract is stated at the
right level: **the fork's hard-won UX refinement falls out of the definition instead of needing a
rule of its own.**

---

## 6. A CONFORMANCE STRATEGY

House rule: *a test which cannot fail is not evidence.* For each proposal: the check, and **the
wrong implementation it catches**. Written in `conformance.lua`'s existing idiom (`needs()` gates on
capability and SKIPs with a reason; `ok()` passes or names the broken rule).

### Group `slots`

```lua
needs({"savestate.slots", "savestate.info"}, g, function()
    local n, why = savestate.slots()
    if n == nil then
        --- LEGAL, and it must be said out loud rather than passed over: agnes
        --- has no ceiling, so the bound check below cannot run there.
        ok(type(why) == "string" and #why > 0, g, "an unbounded host says WHY it has no count")
        skipped(g, "this host states no slot ceiling (" .. tostring(why)
                .. "), so the out-of-range check was not exercised")
    else
        ok(type(n) == "number" and n >= 1, g, "slots() is a positive count")
        ok(not pcall(savestate.info, n + 1), g, "a slot past the end raises")
    end
    ok(not pcall(savestate.info, 0), g, "slot 0 raises - this interface is 1-based")
    local rec = savestate.info(1)
    ok(type(rec) == "table" and type(rec.occupied) == "boolean",
       g, "info() answers a record with an occupied flag for a valid slot")
    --- THE CROSS-CHECK, and the only part of this group that can catch a
    --- constant: a slot info() calls unoccupied must answer nil + reason from
    --- load(), and a slot it calls occupied must not.
    ok(select(1, savestate.load(emptySlotFromInfo)) == nil,
       g, "a slot info() reports empty is empty to load() as well")
end)
```
**Fails on:** a 0-based host leaking its base; `info()` returning nil for an empty slot instead of
`occupied = false`; an `info()` that answers a constant record, caught by the load cross-check —
without that last line the group is satisfied by `return {occupied=false}`, which is exactly the
degenerate shape this document keeps refusing.
**Non-vacuity:** it **retires the empty-slot SKIP on flycast**, and on the two hosts that do declare
a `probe.emptyslot` it lets the suite derive the same slot instead of trusting a hand-written 9 or
9999.

### Group `anchor` — needs a movie, so it SKIPs loudly where there is none

```lua
-- (a) anchoring happens
movie.record(path); emu.step(10)          -- or ten frame callbacks
savestate.save(slot)
local a = savestate.anchor(slot)
ok(a ~= nil and a.frame == clock.now(movie.clock()),
   g, "a state saved during a movie anchors at the playhead")
ok(a ~= nil and clock.about(a.clock) ~= nil,
   g, "the anchor's clock is one the host publishes")     -- cross-check
-- (b) loading seeks
emu.step(30)
savestate.load(slot)
ok(clock.now(movie.clock()) == a.frame,
   g, "loading an anchored state moves the movie playhead to its frame")
```
**(a) fails on:** a host that returns a constant, nil, or 0; a host whose anchor clock is not in
`clock.list()` (the "one word, two meanings" defect the clock section exists for).
**(b) fails on:** the real bug this fork shipped — restoring the machine while the movie counter
keeps running. Nothing else in the suite can see that.

### Group `timeline` — the important one, and the negative direction is the evidence

```lua
local a = savestate.anchor(slot)          -- taken at frame F
ok(a.verdict == "clean", g, "a fresh anchor is clean")

-- 1. NO CRYING WOLF: edit ABOVE the anchor
local before = movie.prefixhash(a.frame)
movie.setframe(a.frame + 5, 1, { a = true })
ok(movie.prefixhash(a.frame) == before, g, "an edit above the anchor leaves its prefix alone")
ok(savestate.anchor(slot).verdict == "clean", g, "an edit above the anchor does NOT stale it")

-- 2. an edit BELOW the anchor
movie.setframe(a.frame - 5, 1, { a = true })
local v = savestate.anchor(slot).verdict
ok(v == "stale" or v == "suspect", g, "an edit below the anchor invalidates it")
ok(a.prefix == nil or v == "stale", g, "a host that states a prefix must PROVE, not suspect")

-- 3. EXONERATION: put it back
movie.setframe(a.frame - 5, 1, { a = false })
ok(movie.prefixhash(a.frame) == before, g, "identical content hashes identically")
ok(savestate.anchor(slot).verdict == "clean", g, "restoring the content clears the verdict")

-- 4. FOREIGN
movie.stop(); movie.record(other)
ok(savestate.anchor(slot).verdict == "foreign", g, "a state from another movie says so")
```

**What each one catches, which is the whole point:**

* Check 1 fails on a **global dirty flag** — the single most likely wrong implementation, and one
  that passes every positive check in this group. Without it, a host that returns `"stale"`
  unconditionally after any edit scores four out of four.
* Check 2 fails on a host that never invalidates anything (the other trivial wrong answer:
  `return "clean"`).
* Check 3 fails on a **history-only** host that claims `stale`. It is allowed to say `suspect` at
  step 2 — and then rule 3 still requires it to come back, so it cannot buy an exemption by being
  vague.
* Check 4 fails on a host that scopes anchors per-session and forgets which movie they came from.
* **The two together** are what make this suite evidence rather than decoration: `return "stale"`
  fails check 1, `return "clean"` fails check 2, and there is no constant that passes both.

`movie.prefixhash` gets its own non-vacuity control: it must be **stable** (two calls, same value —
catches a hash that mixes in a timestamp or an address) and **sensitive** (changes when a frame
below changes — catches a constant). Both are needed; either alone is passable by a stub.

### Group `atomicity`

```lua
local before = movie.prefixhash(movie.length())
local okc = movie.setframes({ {frame=10, player=1, buttons={a=true}},
                              {frame=11, player=1, buttons={nosuchbutton=true}} })
ok(okc == false, g, "a batch with an illegal entry is refused")
ok(movie.prefixhash(movie.length()) == before, g, "a refused batch changes NOTHING")
```
**Fails on:** a host that implements `setframes` as a `for` loop over `setframe` — which is the
implementation everyone writes first, and the one this name exists to forbid.

### Group `rerecords`

Monotonic across the session (sample it in the frame callback and assert it never decreases — that
can fail), and it must **move** across one real re-record cycle (save, step, load, diverge). A host
hardcoding 0 fails the second; a host resetting it on load fails the first.

### Group `stateidentity` (no new API — §2.25)

`save_mem` → `load_mem` → `save_mem` → compare. **Fails on** a state that does not reload to itself,
which is a shipped-bug class on one host and untested on the other two. Honest limitation, stated in
the group so a green line does not overclaim: it cannot see a subsystem missing from serialisation
entirely, since that subsystem is absent from both sides of the comparison.

### Where I cannot design a falsifiable check — and what I conclude

Per the brief, these are stated as doubts rather than smuggled through:

* **Labels.** Only witness is persistence across a session; the suite cannot restart a host.
  → ship as a read-only field of `info()`, no setter, until an adapter offers `control.reopen`.
* **Thumbnails / previews.** Only witness is a picture. → HOST until a framebuffer hash exists.
  This is precedent, not preference: the layer-mask capability had to be *displaced* by a pass that
  named an observable before it could be tested at all.
* **Bookmarks and any movie key-value metadata.** Only witness is `set/get`. → not proposed.
* **An anchor surviving a state being copied elsewhere.** Real property, no in-session witness.
  → written down, not asserted.

### One process rule, from a defect this package already has

`[MEASURED 2026-09-07, README]` five `movie.*` names went onto the surface and into the flycast
adapter **without ever reaching the mock**, and the capability check said CONFORMS anyway. So: every
name proposed here lands in **`mock.lua` and `agnes.lua` in the same commit** — implemented, or in
`unsupported` with a `kind` and a `why`. A name that exists on one adapter and is merely absent from
the others is invisible to both the report and the suite, which is the exact hole that let
`54/54` be printed over a fifth of an unwired surface.

---

## 7. RECOMMENDED SEQUENCE

Ordered by (evidence produced ÷ work), and each step is shippable alone.

**0. The `stateidentity` group, first, because it costs nothing.**
~15 lines of `conformance.lua` over two names that already exist on all three hosts. It is the
fork's northstar tripwire, generalised, and it has never been run on agnes or the mock. If it fails
anywhere, everything below is built on sand and we want to know before building it.

**1. `savestate.slots()` + `savestate.info()`.**
Smallest real addition. Retires the empty-slot SKIP on flycast, replaces two hand-written magic
probe numbers on the other adapters with a derived answer, and exposes the ninety slots the flycast
adapter currently cannot reach. Do it first because it pays a written-down debt and touches nothing
else.

**2. `movie.id()` + `movie.clock()`.**
Prerequisite for everything after — an anchor that cannot name its clock or its movie is a number
with no denominator, which is the defect the `clock` section of `spec.lua` exists to prevent. Also
makes `movie.framecount()` checkable for the first time, and it **fails on flycast today** during
playback. Finding a live defect on the reference host is the best possible outcome for step 2.

**3. `movie.prefixhash()` + `savestate.anchor()` + the timeline group.**
**The payload of this document.** Everything else in the studio — the roll's grey rows, the States
window's stale badges, the derived lanes' `flag = 2`, the generations restore's "which timeline is
this state from" — is a consumer of this one contract, and today each of them reimplements it.

**4. A movie in the agnes host.**
`[REASONED]` The cheapest way to make steps 1–3 mean anything. agnes already latches input at
`$4016` and steps one instruction at a time; a movie is a table of per-frame button rows and a
playhead, plus an anchor recorded on save. Perhaps a hundred lines of Lua in a host **neither of our
projects shaped** — which is the entire reason `hosts/agnes` was vendored: *"a contract derived from
hosts you wrote yourself may be invented rather than discovered."* Until the anchor contract runs on
agnes, §5 is one project's design with a spec file wrapped round it. Bluntly: **if step 4 does not
happen, do not trust step 3.**

**5. `movie.setframes()` + `movie.rerecords()`, then extend `components/pianoroll.lua`.**
Anchors as row markers, stale rows greyed, a real selection, batch paint. The component becomes the
demonstration that a studio *is* portable, on a host with eight buttons and one player.

**6. `components/inputviz.lua`.**
~40 lines, no new interface, and the best single artefact to show someone who has not read any of
this. Could honestly be done at step 0; it is here because it produces no *evidence*, only
persuasion.

### What I am deliberately NOT abstracting yet, and why

Premature abstraction is a real cost, and each of these is left concrete on purpose:

* **`emu.step(n)`.** `ARCHITECTURE.md` wants it and I want it, but flycast's Lua runs *inside* a
  frame, so it is a rearchitecture there rather than a binding. Adding it now buys one host's
  capability and a `cannot` on the reference host. Add it when the exploration loop it exists for is
  actually being written.
* **Row insert / delete.** Genuinely portable, genuinely wanted, and it changes movie *length* —
  which means every anchor above the edit point moves. Do not design that interaction until §5 has
  run on two hosts, or the first cross-host bug will be in the intersection of two new things.
* **Undo / redo as an interface.** Component-side. Two owners of one history is a defect, and the
  host's own live recording writes frames a component never sees.
* **Derived lanes as a framework.** Wait until two components want one. Right now the fork has two
  lanes and both are game- or host-specific at the tap; the *pattern* costs nothing to leave in
  userland.
* **Movie metadata (bookmarks, tags, notes).** A key-value blob is where every tool would then store
  everything, and its check is degenerate. Let one host offer it as an adapter extra first.
* **Anything about storage** — clips, generations, folders, `clip.json`. Identity yes, mechanism no,
  and the mechanism is where nbneo's DAG and flycast's folders can never agree.
* **Thumbnails, capture, hotkey binding, docking, the notepad editor, VA2 notation.** Furniture and
  content. They are the studio's *value* and none of them are its *interface*.

### The one-line version

**Build the anchor and its validity contract (steps 1–3), and prove it on agnes (step 4).** The
studio's ten thousand lines of UI are furniture over roughly a dozen neutral facts, and the fact
that a savestate knows which movie frame it belongs to — and whether that movie still exists — is
the one everything else stands on.
