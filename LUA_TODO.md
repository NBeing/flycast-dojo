# Lua API — porting and specification work

Working list for `docs/lua_api_spec.lua`, the cross-emulator Lua interface, and
for porting fbneo-rr's remaining script surface into flycast-dojo.

**Is the interface done? No.** It is a first draft that names the right things
and settles three real questions (capability is queryable, rollback safety is
in the contract, unportable hooks are excluded with reasons). What it does not
yet do is pin down the details that make two implementations actually
interchangeable. Those are Part 1, and they matter more than the port itself:
a spec with unresolved semantics produces two conforming emulators that still
disagree.

Legend: **[S]** spec gap · **[P]** port work · **[T]** tooling · **[?]** needs a decision

---

## Part 1 — Spec gaps, in the order they will bite

### [S][?] 1. Button representation — the biggest divergence
flycast-dojo's `input.getButtons(player)` returns a raw `u32` (`kcode`), and
**no button constants are exposed to Lua at all** (`core/lua/lua.cpp`), so a
script hardcodes Dreamcast bit values. fbneo-rr's `joypad.get()` returns a
table of named buttons, FCEUX-lineage.

A shared interface needs one of:
- a canonical name set (`up`, `down`, `a`, `b`, `start`, …) that each emulator
  maps onto, with an escape hatch for hardware-specific buttons, or
- named-table in, named-table out, with the vocabulary declared per system via
  something like `joypad.buttons()`.

Bitmasks cannot be the interface: the bit layout is per-system by definition.
**Decide this first** — everything input-related depends on it.

### [S][?] 2. Player indexing
flycast is 1-based (`checkPlayerNum`, then `player - 1` internally). Confirm
fbneo's convention and state it. Lua convention argues for 1-based. Cheap to
settle, expensive to get wrong silently.

### [S][?] 3. Address spaces
flycast's `memory.read*` take SH4 virtual addresses. fbneo has per-CPU address
spaces. The interface currently pretends there is one flat space. Options: an
implicit "main CPU, main space" default plus an explicit
`memory.space("sh4")` / `memory.cpu(n)` selector, or require the selector.

### [S] 4. Failure semantics
Unspecified today: what an out-of-range read does. Error, `nil`, or zero? What
a write to ROM does. What happens when a capability is missing but called
anyway. Pick one rule and state it — silent zeros and thrown errors are very
different to write against.

### [S] 5. Drawing coordinate system
`gui.*` says nothing about origin, units, or scaling. flycast draws through
ImGui with a `uiScale` factor and window-relative coordinates; fbneo draws
game-pixel primitives. Specify: game pixels, origin top-left, with the
emulator responsible for scaling. Anything else and overlays land in different
places on each emulator, which defeats the point.

### [S] 6. Callback threading — a correctness rule, not a style note
flycast runs `overlay` on the render thread and `vblank` on the emulation
thread, serialised by a mutex. A script that draws from a frame callback is
touching the UI toolkit from the wrong thread. The spec must state which
callbacks may draw and which may not, and require observers to buffer rather
than draw directly.

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
