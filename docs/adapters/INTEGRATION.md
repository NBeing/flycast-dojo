# Integrating a new emulator

What it takes to put an emulator behind `docs/lua_api_spec.lua`, and the traps
that cost time the first time round. Every item here is something that actually
went wrong while building the flycast-dojo adapter, not a precaution.

The short version: write a Lua adapter first, run the conformance suite, and
only move something to a native binding when measurement says to.

---

## The minimum a host must provide

1. **A Lua interpreter with a script entry point.** Anything that can `dofile`
   a script and expose C++ functions to it.
2. **A per-frame callback**, dispatched once per emulated frame.
3. **A draw callback**, dispatched inside a live drawing frame.
4. **Memory read/write** at the CPU's own addresses.
5. **Input read/write.**

Everything else is optional and capability-gated. An emulator that provides
only these already conforms usefully.

If the host embeds ImGui, most of the widget surface arrives by forwarding
names — see THE UI SURFACE in the spec. The baseline profile was chosen so that
a five-year-old ImGui satisfies it.

---

## Rules that are correctness, not style

### Adapters must be idempotent to load

An adapter owns a **singleton**: the host's one callback registry. Loading it
twice builds two adapters, the second silently orphaning the first's handlers,
so two scripts using the interface cannot coexist. Cache the instance somewhere
that survives repeated `dofile` — the global registry, not the file's locals.

*Found when a conformance run stopped printing its report.*

### Drawing is legal only from the draw callback

Callbacks do not all run on the same thread. In flycast-dojo the draw callback
runs on the render thread inside a live frame and the frame callback runs on a
separate emulation thread. A UI toolkit keeps global per-frame state, so a
script drawing from a frame callback races the renderer — silently, and only
under load.

Enforce it and **raise**. A `thread_local` "inside the draw callback" flag is
enough, and is cheaper and more precise than comparing thread ids; it also
catches drawing from outside any callback, where there is no frame at all.

A single-threaded host must enforce this too. A script written against a
permissive host and shipped will break on a threaded one, which is the exact
portability failure the interface exists to prevent.

### Frame callbacks are not delivered for re-simulated frames

Any host with rollback re-runs frames it has already run. A callback that
accumulates counts the same logical frame several times. Gate the dispatch, and
expose `isrollback()` for scripts that deliberately want to observe it.

The conformance suite checks this directly: a frame callback must never see
`isrollback()` true.

### The frame counter must count delivered frames

Do not derive it from whatever counter the host already has. flycast-dojo's own
advances on its netplay session's schedule rather than per frame, so offline it
both stalled and jumped — a script asking "what frame is it" got neither a
monotonic count nor a 1:1 one. Count once per *delivered* callback, which
excludes re-simulated frames by construction and is monotonic for free.

*Two independent tests reported this as a defect before it was believed.*

---

## Traps

### Error propagation differs between binding styles

If the host uses a binding generator, it probably wraps calls and converts C++
exceptions into Lua errors. A binding registered as a **raw `lua_CFunction`**
is called straight from Lua with no wrapper, so a thrown exception unwinds past
the interpreter and reaches `terminate` — a hard abort instead of a catchable
error. Use the interpreter's own error mechanism there, and call it before any
local with a destructor exists so the longjmp is safe.

*Found by an immediate core dump on the first run of a guard that worked
everywhere else.*

### Do not install globals

The neutral names are not free everywhere: fbneo-rr already owns seven of the
ten. Return the namespaces and let scripts bind locally. Offer `install()` as
an opt-in that **refuses** rather than clobbers.

Note the asymmetry — flycast-dojo namespaces everything under `flycast.*`, so
all ten names are free there. A global-install design looks correct on the host
you can test and breaks on the one you cannot.

### Colour packing is rarely what you assume

flycast-dojo's are `0xAABBGGRR` (ImGui's `IM_COL32`), not `0xRRGGBBAA`. Greys
are identical under both readings, so the mistake survives casual testing.
Check with a saturated colour.

*Found by drawing a "blue" line that came out red.*

### Test overlays at a non-matching aspect ratio

Game-pixel drawing is only interesting when the game image does not fill the
window. At a matching aspect ratio a broken mapping looks identical to a
correct one. Use a deliberately wide window and check that a box at the game's
own bounds hugs the picture rather than the window.

### Do not copy the host's suffix conventions

fbneo-rr exposes its second CPU as `readbyte_audio`, `writeword_audio`. That
encodes the count and identity of address spaces into function names, which is
per-system information — a three-CPU board needs three more families. Spaces
are named and queried instead.

---

## Order of work

1. Write the adapter in Lua. Nothing in the emulator needs to change yet.
2. Run `conformance.lua`. Fix what it reports.
3. Declare what is genuinely absent in the adapter's `unsupported` table, so
   `emu.supports()` answers false rather than a name simply being missing.
4. Move a function to a native binding only when profiling says the shim costs
   too much — per-frame memory reads and drawing are the usual candidates.

## What "done" means

`conformance.lua` passing. Presence is reported separately by
`emuapi.report()` and is not the same thing: a host may implement half the
surface and conform completely, because a missing capability is a legitimate
answer and a *wrong* one is not.

Bear in mind what a green result does not prove. flycast-dojo scores 84/84 with
the suite and the adapter written by the same author against the same host —
the suite earns its keep the first time it runs somewhere else. And it reports
honestly when a rule was never exercised, such as the rollback gate in a
session where no rollback occurred.
