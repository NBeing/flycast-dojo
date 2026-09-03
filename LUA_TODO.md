# Lua API — porting and specification work

Working list for `docs/lua_api_spec.lua`, the cross-emulator Lua interface, and
for porting fbneo-rr's remaining script surface into flycast-dojo.

**Is the interface done? No, but only two questions remain.** It settles eight
(capability is queryable, rollback safety is in the contract, unportable hooks
are excluded with reasons, buttons are named booleans, indices are 1-based,
failure is loud, only draw callbacks may draw, and memory is addressed by named
space — the last five resolved and implemented). Still open: drawing
coordinates, and namespace collision. Versioning is stubbed as
`emu.apiversion()` returning 0. What it does not
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
- [S] `gui.*` game-pixel mapping needs the game viewport rect, which is
  currently renderer-local (`TransformMatrix` is constructed inside the
  renderer; `getPvrFramebufferSize` needs a `rend_context`). Exposing it means
  caching the viewport where the Lua layer can reach it. Until then the adapter
  draws in window pixels — **a known deviation**, and the reason a hitbox
  overlay would not yet track a resized window.
- [?] `emu.screenwidth()`/`screenheight()` currently return the *window* size
  (`settings.display.width`). For game-pixel drawing a script needs the *game*
  resolution. Decide whether these report the game and add separate window
  accessors, or vice versa.

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

### [S] 8. Interface versioning
There is no `emu.apiversion()`. Without it a script cannot tell a partial
implementation from an old one, and `emu.supports()` alone cannot express
"this function exists but changed shape". Add a single integer, bumped on
breaking change.

### [S] 9. Namespace collision policy
The spec asks for root-level `emu`, `memory`, `gui`. Those are plausible names
for a script's own globals, and `gui`/`input` are generic enough to collide.
State whether the neutral names are always published, opt-in via
`require`, or namespaced behind a single root.

---

## Part 2 — Port work, in dependency order

### [P] Tier 1 — unlocks the most script functionality
- `emu.frameadvance()` — the TAS primitive. Everything step-based needs it.
  Interacts with rollback: define it as advancing one *confirmed* frame.
- `emu.registerbefore` / `registerafter` / `registerexit` — the observation
  points. `registerafter` must be confirmed-frames-only per the rollback
  contract already implemented for `vblank`.
- `joypad.getdown` / `getup` — edge detection. Trivial once (1) is settled;
  currently every script reimplements it and gets rollback wrong.

### [P] Tier 2 — cheap, self-contained
- `savestate.save_mem` / `load_mem` / `hash` — states as strings. `hash` is
  the desync-hunting tool and is worth having regardless of the port.
- `emu.gamename`, `emu.isonline`, `emu.isreplay` — flycast has all the
  underlying state; these are accessors.
- `memory.getregister` / `setregister` — CPU registers by name.
- `emu.speedmode` — flycast has fast-forward internally.

### [P] Tier 3 — larger, still portable
- `movie.*` — mode, framecount, rerecord counting, read-only flag. Overlaps
  the replay library work already done; reuse `ReplayManager` rather than
  adding a second concept of a recording.
- `sound.voicecount` / `outputrate` / `voice(n)` — per-channel AICA state.
  Drives audio-reactive overlays.
- `memory.watch` / `unwatch` — page-granularity change detection built on the
  existing `memwatch` dirty-page tracking, evaluated once per confirmed frame.
  Deliberately weaker than fbneo's per-access hooks, and named differently for
  that reason. See the exclusions in the spec.

### [P] Tier 4 — conformance surface
- Publish neutral root-level aliases (`emu`, `memory`, `joypad`, …) over the
  existing `flycast.*` tables. Makes flycast-dojo the first conforming
  implementation and costs almost nothing.
- `emu.supports()` backed by a real registry rather than a hardcoded list, so
  it cannot drift from what is actually bound.

---

## Part 3 — Tooling

### [T] Conformance test suite — the thing that keeps this honest
A spec with no executable check drifts into fiction. A Lua script that any
emulator can run, which walks `emu.supports()`, calls everything claimed,
checks shapes and failure modes, and prints a conformance report. Should run
headless. This is worth more than any single Tier 2 item.

### [T] Rollback conformance case
Specifically: a script that fails on an emulator which delivers frame
callbacks during re-simulation. flycast-dojo would have failed it before
`cf47f4745`. Ship it with the fix it validates.

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
- **`imgui.*`** — leaks the host toolkit into scripts; `gui.*` is the portable
  subset.
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
