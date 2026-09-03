# Lua API — porting and specification work

Working list for `docs/lua_api_spec.lua`, the cross-emulator Lua interface, and
for porting fbneo-rr's remaining script surface into flycast-dojo.

**Is the interface done? No, but it is three questions closer.** It names the
right things and settles six (capability is queryable, rollback safety is in
the contract, unportable hooks are excluded with reasons, buttons are named
booleans, indices are 1-based, and failure is loud — the last three resolved
and implemented). What it does not
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

### [S][?] 3. Address spaces
flycast's `memory.read*` take SH4 virtual addresses. fbneo has per-CPU address
spaces. The interface currently pretends there is one flat space. Options: an
implicit "main CPU, main space" default plus an explicit
`memory.space("sh4")` / `memory.cpu(n)` selector, or require the selector.

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
