# Lua API — porting and specification work

Working list for `docs/lua_api_spec.lua`, the cross-emulator Lua interface, and
for porting fbneo-rr's remaining script surface into flycast-dojo.

**Is the interface done? The design questions are.** It settles ten
(capability is queryable, rollback safety is in the contract, unportable hooks
are excluded with reasons, buttons are named booleans, indices are 1-based,
failure is loud, only draw callbacks may draw, and memory is addressed by named
space, drawing splits into ImGui widgets and game-pixel overlay, and namespaces
are bound locally rather than installed). What remains is porting and a
conformance suite, not design. Versioning is stubbed as `emu.apiversion()`
returning 0 and should be bumped on the first breaking change. What it does not
yet do is pin down the details that make two implementations actually
interchangeable. Those are Part 1, and they matter more than the port itself:
a spec with unresolved semantics produces two conforming emulators that still
disagree.

Legend: **[S]** spec gap · **[P]** port work · **[T]** tooling · **[?]** needs a decision

---

## Part 1 — Spec gaps, in the order they will bite

### [x] 1. Button representation — RESOLVED, and implemented
**Buttons are named booleans, never a bitmask.**

A bitmask could not be the interface: its layout is per-system by definition,
and polarity leaks. flycast-dojo's `kcode` is active-low, so a *cleared* bit
means held, and no constants were exposed to Lua at all — every script was
hardcoding inverted tests against Dreamcast bit values.

Contract (now in `docs/lua_api_spec.lua`):
- a button reads `true` when held, whatever the hardware does
- `joypad.buttons()` enumerates this system's names, so scripts adapt
- on set: present-and-true presses, present-and-false releases, **absent leaves
  that button alone** — so one script can drive one button without fighting
  another script or the player
- an unknown name is an error, not a silent no-op

Implemented in flycast-dojo as `input.buttonNames`, `input.getButtonTable`,
`input.setButtonTable` and `input.setButton`. The raw `input.getButtons`
bitmask is kept for existing scripts but is explicitly not part of the
interface.

Verified in-game: 17 names; absent keys preserved across successive sets;
explicit false releases; unknown name rejected; and the raw mask cross-checked
against the table to confirm the active-low inversion is hidden correctly
(`0xFFFFFFF5` ⇒ `b` and `start` held).

### [x] 2. Player indexing — RESOLVED, and validated
**Every index a script sees is 1-based**: players, axes, slots, ports. Out of
range raises. An emulator's internal base is its own business but must not
leak — flycast-dojo is 1-based for players and axes and 0-based for savestate
slots internally, and the neutral `savestate.*` alias owns that shift, not the
script.

The survey turned up something larger than a base-off-by-one. fbneo-rr's
`joypad.get(which)` **ignores its argument entirely** and returns one flat
table of every game input, keyed by the driver's own name (`"P1 Fire 1"`), with
the player encoded in the string. Conforming means splitting that table by
player and mapping driver names onto button names — real work, not a rename,
and the largest single conformance cost found so far. Recorded in the spec.

Validation in flycast-dojo was already consistent for players (1–4) and axes
(1–6). Savestate slots were **not** range-checked at all; they now raise
outside 0–9.

### [x] 3. Address spaces — RESOLVED, and implemented
**Spaces are named and queried**, not encoded in function names:

```lua
memory.spaces()                        -- {"main", "sound"}
memory.space("sound").readbyte(0x100)
memory.readbyte(0x8C010000)            -- shorthand for main
```

fbneo-rr's suffixed accessors (`readbyte_audio`, …) do not scale: the set of
spaces is per-system, so a suffix per space means a new function for every CPU
any supported system might have. `"main"` always exists; everything else is
queried; an unknown name raises.

**Addresses are not normalised** — they are whatever that CPU uses, in its own
convention. A script reading a game's health value is bound to that game's
memory map by nature, and a "portable address" would be a translation layer
that lies about the hardware and breaks the moment an author cross-references a
disassembly or a cheat file.

The two emulators reach the same interface by different mechanisms, which is
the adapter philosophy working: fbneo's audio space is a genuinely separate
accessor, while flycast's sound RAM is a **window in the SH4 space at
`0x00800000`** (`_vmem.cpp:478`), presented with its own 0-based addresses by
the adapter. Verified empirically before relying on it — a byte written through
`memory.space("sound")` at `0x400` reads back identically through the main
space at `0x00800400`.

### [x] 4. Failure semantics — RESOLVED, and verified
Three tiers, because "it failed" covers three situations that deserve
different answers:

1. **A programmer mistake raises** — wrong type, index out of range, unknown
   button name. Bugs in the script must be loud at the call site.
2. **Genuinely absent data returns `nil`** — no game loaded, no session, an
   unmapped address. Never a silent zero: zero is a legitimate value and
   indistinguishable from failure, which turns a missing feature into wrong
   data.
3. **A missing capability** answers false from `emu.supports()` and raises if
   called anyway. Never a silent no-op.

Rule of thumb: if a correct script can hit it at runtime, return `nil`; if only
a wrong script can hit it, raise.

Verified in-game across 11 cases — player 0/5, axis 0/7, unknown button name,
non-table argument, savestate slot -1/10, maple bus 9 all raise with
descriptive messages; the valid calls do not.

**Known deviation, follow-up:** flycast-dojo's `memory.read*` return 0 for an
unmapped address rather than `nil`, because detecting mapped-ness is not cheap
in `_vmem`. Tier 2 says this should be `nil`. Left as-is and recorded here
rather than quietly ignored.

### [x] 5. Drawing — RESOLVED as two layers, not one
The original question ("what coordinate system does `gui.*` use?") was the
wrong question. There are two drawing problems:

**Widget UI** — panels, buttons, sliders. **The interface is ImGui itself,
under `ui.*`, using ImGui's own names.** Every C++ emulator in scope already
embeds it, so the integration contract shrinks to "give the script an ImGui
context and call its draw callback inside a live frame" — add that canvas and
the UI surface arrives with it. Abstracting this to primitives would mean
reimplementing layout, hit-testing and focus in Lua, badly.

**Content overlay** — hitboxes, position markers. Stays `gui.*`, and is defined
in **game pixels, origin at the top-left of the game image**, with the emulator
owning the mapping including scaling and letterboxing. ImGui does not solve
this: a widget positioned in window space does not track the game image across
a resize.

**Version skew measures smaller than it looks.** flycast ships ImGui 1.80 WIP,
fbneo ships 1.92.9 WIP — five years apart — yet **all 19 ImGui functions
fbneo actually exposes to Lua exist in 1.80** (verified against
`core/deps/imgui/imgui.h`). Emulators bind a conservative subset in practice,
so that subset is the baseline profile every conforming host must provide.
Beyond it (`BeginTable`, plots, docking) is capability-gated:
`emu.supports("ui.BeginTable")`.

*Reverses an earlier exclusion.* The spec had ruled out `imgui.*` as "leaking
the host toolkit into scripts". That was the right call for an unbounded target
set and the wrong one for this one — C++ emulators that all embed ImGui.

**Follow-up work, now split:**

- [x] **Baseline `ui.*` implemented** — 19 of 20 under ImGui's own names, plus
  `GetScale()`. Verified in-game with a live panel: `Text`, `TextColored`,
  `Checkbox`, `SliderFloat`, `SliderInt`, `InputText`, `Selectable`, `Button`,
  `Separator`, `Spacing`, `SetNextWindowPos`/`Size`, `GetMousePos`,
  `IsMouseClicked`/`Down`/`Released`, `Begin`/`End`. Value-owning widgets
  return `(value, changed)` rather than the C++ pointer dance. Mouse buttons
  are 1-based per the indexing rule; ImGui's own are 0-based.
- [ ] `ui.Image` is the one baseline member not implemented: it needs host
  texture management, which is genuinely emulator-specific. Capability-gated
  rather than faked — `emu.supports("ui.Image")` answers false.
- [x] **New `ui.*` applies no implicit scaling.** `GetScale()` hands the factor
  to the script instead. The old `beginWindow` still scales its size but not
  its position — incoherent at any scale but 1 — and is left alone rather than
  silently changed under the scripts that depend on it. Prefer
  `SetNextWindowPos`/`Size`.
- [x] **`gui.*` game-pixel mapping done.** No renderer hook was needed after
  all: `getDCFramebufferAspectRatio()` reads only config (`Rotate90`,
  `ScreenStretching`), so the letterbox rectangle can be computed exactly as
  `renderLastFrame()` does it, from a neutral place. Exposed as
  `state.getGameViewport()` → x,y,w,h and `state.getGameResolution()` → w,h;
  the adapter maps game pixels through it.

  Verified visually at a deliberately non-4:3 window (960×480, so the 4:3 game
  is pillarboxed with 160px bars): a box drawn at game (0,0)–(639,479) hugs the
  game image exactly, diagonals run corner-to-corner of the picture rather than
  the window, and the reported viewport is `160,0 640×480`.
- [x] **Colour format documented.** `gui.*` colours are `0xAABBGGRR` — ImGui's
  own `IM_COL32` packing, not the `0xRRGGBBAA` a reader assumes. Found by
  drawing a "blue" line that came out red. Greys are identical under both
  readings, so the mistake survives casual testing.
- [?] `emu.screenwidth()`/`screenheight()` still return the *window* size
  (`settings.display.width`). Game-pixel drawing now has
  `state.getGameResolution()`, so the remaining decision is only whether the
  neutral accessors should report the game and gain separate window accessors.

### [x] 6. Callback threading — RESOLVED, and enforced
**`gui.*` is legal only inside a `gui.register` callback. Everywhere else it
raises.** Everything else — memory, input, frame counters, savestates — is
callable from any callback. An observer that wants to draw buffers on the frame
callback and emits from the draw callback.

This was a live hazard, not a theoretical one. `ThreadedRendering` defaults to
**true**, emulation runs on a separate `std::async` thread, `overlay` is
dispatched from the render thread and `vblank` from the emulation thread — and
the `ui.*` bindings called ImGui with **no thread guard and no frame guard**.
A script drawing from `vblank` raced ImGui's global per-frame state. Silent,
and only under load.

Enforced with a `thread_local` "inside the draw callback" flag checked by all
14 `ui.*` entry points — cheaper and more precise than comparing thread ids,
and it also catches drawing from outside any callback, where there is no frame
to draw into.

The spec requires single-threaded emulators to enforce it too: a script written
against a permissive host that breaks on a threaded one is exactly the
portability failure this interface exists to prevent.

Verified in-game: drawing from `vblank` raises with a message naming the fix,
memory reads from `vblank` still work, and drawing from `overlay` renders.

### [S][?] 7. Callback registration model
Two shapes in play: flycast's `flycast_callbacks` table with well-known keys,
and fbneo's `emu.registerbefore(fn)`. The spec lists the latter without
reconciling them. Registration functions are the better contract (multiple
subscribers, unregistration), but the table form is what flycast scripts use
today, so both need to work.

### [S] 8. Interface versioning — STUBBED, not settled
`emu.apiversion()` exists (`emuapi.lua:88`) and returns `0`. What is not
settled is the policy: nothing bumps it, and there is no rule saying what
constitutes a breaking change. Until something does, it cannot distinguish a
partial implementation from an old one, which was the point of having it.

### [x] 9. Namespace collision — RESOLVED
**Namespaces are named neutrally but not installed as globals.** The loader
returns them; a script binds what it wants:

```lua
local api = dofile("adapters/emuapi.lua").load()
local emu, joypad, memory, gui = api.emu, api.joypad, api.memory, api.gui
```

A file-scope `local` shadows a global for that file alone, so nothing else in
the Lua state is disturbed and the shape is identical on every host.

Installing globals could not be the contract: fbneo-rr already owns `emu`,
`memory`, `input`, `joypad`, `savestate`, `movie` and `gui` — **seven of the
ten names**. Overwriting them would break both its existing scripts and the
adapter itself, which reaches the host through those same tables. Note the
asymmetry: flycast namespaces everything under `flycast.*` so all ten are free
there, meaning a global-install design would have looked correct on the host
that can be tested and broken on the one that cannot.

`api.install()` remains for hosts with room, and **refuses rather than
clobbers**, naming the conflicts — loud, per failure tier 1.

Verified: local binding gives the full API with no globals touched;
`install()` succeeds on flycast; a second `install()` is refused.

### [x] 10. Third surface surveyed — nbneo-rr
`docs/adapters/NBNEO_SURVEY.md` `[SURVEYED 2026-09-04]`, read-only.

The most useful surface so far, because it is the same author's deliberate
successor to fbneo-rr: where it agrees with this spec independently that is
corroboration, and where it disagrees one of the two is wrong on purpose. It
also separates **positions it argues for** from **positions it implements** —
several of its strongest README claims have no implementation, and one is
retracted by its own `docs/FACTS.md`.

Three of its sixteen recommendations are already applied (see the status table
at the end of that file):

- **`gui.rgba(r, g, b, a)`**, because nbneo packs `0xRRGGBBAA` where this spec
  packs `0xAABBGGRR` and **the two agree on every grey and on opaque red** —
  the colours anyone tests with first. A control run against a deliberately
  wrong packing was caught by 2 of 3 colour assertions; red did not catch it.
  The conformance suite now asserts with green and blue.
- **`memory.registerexec` re-opened**, capability-gated rather than excluded.
  The old rationale argued against trap-opcode patching and then banned the
  capability; nbneo rides a PC-changed callback with a bitmap and patches
  nothing, so none of the objection survives. It is also 29 call sites in the
  corpus against `joypad.*`'s 29 for the whole namespace.
- **Indices vs byte offsets** stated separately: indices are 1-based, a byte
  offset into an opaque blob is 0-based and must be documented at the call site.

Thirteen remain open, ranked in that file. The two largest are passing the draw
surface into the draw callback (a capability object rather than a thread-local
flag) and tier 2 returning `nil, reason`.

**Declined:** "capabilities are declared, not discovered" — it has zero
implementation sites, and declaration (authorisation) answers a different
question from `emu.supports()` (portability). Also declined: derives,
transcripts and the timeline as spec surface.

---

## Part 2 — Port work, in dependency order

### [P] Tier 1 — unlocks the most script functionality
- [x] **`emu.frameadvance()` + `emu.run()` — DONE.** Frame stepping via a
  coroutine the adapter resumes once per delivered frame, so a script reads as
  a straight line while advancing exactly one frame at a time. Rollback-safe by
  construction: the resume rides the frame callback, which is not delivered for
  re-simulated frames. Verified: 1,489 advances, **zero** that were not exactly
  one frame; calling it outside `emu.run()` raises.
- [x] **`emu.registerbefore` — DONE.** Shares the frame-boundary hook with
  `registerafter`, which is correct rather than a shortcut on this host: at a
  boundary "after frame N" and "before frame N+1" are the same instant, with no
  input latch between, so input set from either lands on the coming frame. What
  the two names buy here is *order* — before-callbacks run first, so a script
  injecting input runs ahead of one reading state. Verified `B-A-B-A` with
  equal counts.
- [x] **`joypad.getdown` / `getup` — DONE.** Synthesised in the adapter
  (`flycast.lua:85-86`) rather than bound in the host, because edge detection is
  a diff of two frames' button tables and needs no host support. Held stable for
  the whole frame however many callbacks read it.

### [x] Tier 2 — DONE
- `savestate.save_mem` / `load_mem` / `hash` — states as strings, sized by a
  dry run so the buffer is exact. Verified: 27.8 MB in 0.09 s, the live hash
  equals the hash of the serialised string, the hash **changes** after a memory
  write, and loading restores it. That combination is what makes it useful for
  hunting desyncs — two peers compare per frame and the first divergence is
  where to look.
- `emu.isonline`, `emu.isreplay`, `emu.speedmode`, `emu.gamename` — accessors.
  `speedmode` maps to a fast-forward boolean here rather than named tiers;
  `gamename` reports the game id, which is all the host knows.
- `memory.getregister` / `setregister` — `r0`–`r15`, `pc`, `pr`, `gbr`, `vbr`,
  `ssr`, `spc`, `sgr`, `dbr`, `fpul`. Unknown names raise. Coherent from the
  frame callback, which runs on the emulation thread; from a draw callback they
  race the running CPU exactly as the memory accessors do.

**Coverage: 75/76, 108 conformance checks, 0 failures, 0 skips**
`[MEASURED 2026-09-04]`. The one remaining unimplemented member is `ui.Image`,
which is capability-gated rather than faked.

### [x] Tier 3 — DONE
- [x] `movie.*` — `mode`, `framecount`, `record`, `stop`
  (`flycast.lua:274-282`), built on `ReplayManager` rather than a second
  concept of a recording. `mode()` returns `"record"`, `"playback"` or `nil`.
- [x] `sound.voicecount` / `outputrate` (`flycast.lua:285-286`). `voice(n)` —
  per-channel AICA state — is **not** implemented; it is the one part of this
  tier still open.
- [x] **`memory.watch` — DONE**, and better than the page-granularity design
  originally sketched. Snapshot-and-compare rather than `memwatch` dirty pages:
  that tracking exists for rollback, is only armed under GGPO, reports whole
  pages rather than the bytes asked for, and arming it outside netplay would
  add a page fault to every write in the emulated machine. Comparing a copy
  costs the caller proportionally to what they watch and answers exactly the
  question asked.

  `changed()` compares **content, not writes** — rewriting the same bytes
  reports nothing, which is what a script watching a value wants and what a
  write hook could not give it. Verified: fresh watch quiet, change seen,
  repeat poll quiet, same-value rewrite quiet, write one byte past the range
  ignored, write inside seen, use-after-release raises, zero and oversize
  lengths rejected. Watches are cleared on teardown so a new game cannot
  inherit them.

### [P] Tier 4 — conformance surface
- Publish neutral root-level aliases (`emu`, `memory`, `joypad`, …) over the
  existing `flycast.*` tables. Makes flycast-dojo the first conforming
  implementation and costs almost nothing.
- `emu.supports()` backed by a real registry rather than a hardcoded list, so
  it cannot drift from what is actually bound.

---

## Part 3 — Tooling

### [x] Conformance suite — DONE (`docs/adapters/conformance.lua`)
Checks shapes, failure modes and contract rules, not just presence. A missing
capability is SKIP, not a failure; a failure means the interface was claimed
and then behaved differently to the spec. Runs headless.

**flycast-dojo: 83 pass, 0 fail, 2 skip.**

Includes the rollback case — a frame callback must never observe
`isrollback()` true, which is the rule flycast would have failed before
`cf47f4745`.

**Caveat worth keeping in view:** 83/83 on the host the adapter was written
against is a weaker signal than it looks. The suite has only ever been run on
one emulator by the person who wrote both it and the adapter. Its real value
arrives the first time it runs somewhere else.

**A false failure it produced on its first run, kept as a warning:** the
rollback check originally asserted that `frame.confirmed()` advances by one per
frame callback. That premise only holds while a session is running — offline
the counter tracks emulated game frames rather than display vblanks, so it
legitimately repeats, and the suite reported ~15% "stalls" as
NON-CONFORMING. The rule is directly observable instead (a frame callback must
never see `isrollback()` true), and stalls are now informational. A conformance
suite asserting a proxy rather than the rule is worse than no suite, because it
manufactures distrust in a correct implementation.

---

## Part 4 — Excluded, with reasons

Recorded in `docs/lua_api_spec.lua`; summarised here so this file stands alone.

- **`memory.registerwrite` / `registerexec`** — per-access hooks assume an
  interpreter core. A recompiler inlines memory access into generated code.
  Exec hooks are typically trap-opcode patches into emulated memory, which
  rollback snapshots, restores, and desyncs against the peer.
- **`rewind.*`** — depends on cheap delta snapshots; not a portable assumption.
- **`spec.*` (counterfactual rollout)** — needs cheap speculative stepping.
  Assess against whatever dominates snapshot cost, not against RAM size.
- **`quark.*`, `macro.*`, `vsav.*`** — emulator- or tool-specific.
- ~~**`imgui.*`** — leaks the host toolkit into scripts~~ **REVERSED by
  question 5.** ImGui *is* the widget interface, under `ui.*` and ImGui's own
  names. The exclusion was right for an unbounded set of hosts and wrong for
  this one, where every C++ emulator in scope already embeds it.
- **`socket.*`** — needs a capability gate, not a default.

---

## Licensing

flycast-dojo is GPL-2.0-or-later. FBNeo's licence carries field-of-use
restrictions, which GPLv2 section 6 forbids adding, so FBNeo-derived code
cannot be combined into flycast-dojo and distributed. Port ideas and
independently-authored code; do not transcribe FBNeo sources. Some fbneo-rr
Lua files are upstream FCEUX/FBA lineage rather than that fork's own work.
`docs/lua_api_spec.lua` is written from observed API surface and carries no
upstream code.
