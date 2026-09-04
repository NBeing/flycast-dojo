# Adapters

A Lua shim layer that presents `docs/lua_api_spec.lua`'s surface on top of an
emulator's native API. A script written against the neutral interface runs
unchanged on any emulator that has an adapter here.

```lua
local api = dofile("adapters/emuapi.lua").load()
local emu, joypad, memory, gui, frame = api.emu, api.joypad, api.memory, api.gui, api.frame

emu.registerafter(function()
    if joypad.getdown(1).a then emu.message("A pressed") end
end)
gui.register(function() gui.text(8, 8, "frame " .. frame.confirmed()) end)
```

## Nothing is installed as a global

The loader returns the namespaces; a script binds what it wants. A file-scope
`local` shadows a global for that file alone, so nothing else in the Lua state
is disturbed and the shape is identical on every host.

Installing globals cannot be the contract because the names are not free
everywhere — fbneo-rr already owns `emu`, `memory`, `input`, `joypad`,
`savestate`, `movie` and `gui`. Overwriting them would break both its scripts
and the adapter, which reaches the host through those same tables.

A host with room may opt in:

```lua
local api = dofile("adapters/emuapi.lua").load():install()
emu.pause()   -- now a global; succeeds on flycast, raises on fbneo
```

`install()` refuses rather than clobbers, and names the conflicts.

## Why a shim rather than native bindings

An emulator needs no source changes to conform. That matters for one you do
not control, and for two projects whose licences forbid sharing code — a
neutral surface can exist across both without a line moving between them.

It is also cheap to be wrong. A bad design decision costs a text edit, not a
release. Native implementation is then an optimisation for hot paths (per-frame
memory reads, drawing), not a prerequisite.

## Layout

| File | Status |
|---|---|
| `emuapi.lua` | Entry point: detects the host, loads its adapter, installs namespaces, derives `emu.supports()` |
| `flycast.lua` | flycast-dojo. **Verified** end to end against branch `video-recording` |
| `fbneo.lua` | fbneo-rr. **Unverified sketch** — written from observed API surface, never run |

## Notable details

**Host detection runs before installation.** fbneo-rr already owns the global
`emu`, which the interface also wants, so detection keys on `fba` and the
adapter snapshots the native table before it is shadowed.

**`emu.supports()` is derived, not declared.** It inspects the namespaces that
were actually installed, so it cannot drift from reality the way a
hand-maintained list does. A name that exists in the interface but is
deliberately not implemented is listed in the adapter's `unsupported` table and
answers `false`.

**Adapters absorb mismatches; scripts never see them.** In `flycast.lua`:

- savestate slots are 0-based in the host, 1-based in the interface — the shift
  lives in the adapter
- `gui.*` is immediate x/y in the interface, ImGui flow layout in the host —
  each primitive opens a borderless window at the requested point. Workable,
  and the clearest candidate for a native binding later
- `joypad.getdown`/`getup` do not exist in the host at all, and are synthesised
  from two frame-boundary snapshots

That last one is rollback-safe for free, because flycast-dojo does not deliver
frame callbacks for re-simulated frames. An adapter over an emulator *without*
that guarantee must gate on `frame.confirmed()` itself.

## Conformance today

`emuapi.report()` prints presence against the full surface. flycast-dojo:

```
emuapi: host=flycast api=0  35/51 implemented
  missing: emu.gamename emu.isonline emu.isreplay emu.frameadvance
           emu.speedmode emu.registerbefore frame.resimsteps
           memory.getregister memory.setregister memory.watch
           savestate.save_mem savestate.load_mem savestate.hash
           movie.framecount sound.voicecount sound.outputrate
```

That missing list is exactly the `[port]` items in the spec, which is a useful
cross-check: the report and the specification agree about what is absent.

Presence is not conformance. The real suite (LUA_TODO.md Part 3) must also
check shapes and failure modes, and include a case that fails on an emulator
delivering frame callbacks during re-simulation.
