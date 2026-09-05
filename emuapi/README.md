# emuapi

A Lua interface more than one emulator can conform to, so an overlay, a
training tool or a TAS script moves between them unchanged.

```
emuapi/
  init.lua          the loader - detects the host, returns the namespaces
  spec.lua          the interface itself, with the reasoning
  conformance.lua   its executable definition
  adapters/
    flycast.lua     flycast-dojo
    fbneo.lua       fbneo-rr
    mock.lua        a fake emulator, for running the suite with no emulator
  examples/
    tour.lua        every call, exercised live
  run-conformance.lua
```

## Running the suite

Two runs, and both should pass. When they disagree, the disagreement is the
finding.

```sh
lua emuapi/run-conformance.lua      # no emulator, ~1 second
```

...and inside the emulator, with a game loaded, by making the host's script say
`require("emuapi.conformance")`.

The headless run is not a convenience. Before it existed the suite had only
ever run on one emulator, against an adapter by the same author as the
interface - so a passing result measured agreement between two halves of one
head, and anything flycast-shaped that leaked into the neutral layer was
invisible. It caught one immediately: the suite was poking `0x8C010000`, an
SH4 address. Adapters now declare where the suite may poke.

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
local api = require("emuapi").load():install()
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

Presence is not conformance, which is what `conformance.lua` is for.

## The conformance suite

```lua
require("emuapi.conformance")   -- run with a game loaded
```

It checks shapes, failure modes and the contract rules, not just presence. A
missing capability is reported as SKIP, not a failure — `emu.supports()`
answering false is a legitimate answer. A **failure** means the interface was
claimed and then behaved differently to the specification.

flycast-dojo today: **83 pass, 0 fail, 2 skip.**

What it enforces, by spec section:

| Group | Checks |
|---|---|
| capability | `supports()` agrees with reality both ways; malformed names are false |
| indexing | player 0 and 99 raise, player 1 works; axis 0 raises; savestate slot 0 raises |
| buttons | names are strings, values are booleans, **absent keys are left alone**, unknown name raises |
| memory | `spaces()` non-empty and contains `main`; unknown space raises |
| frames | `confirmed()` and `count()` are numbers; `isrollback()` is a boolean |
| threading | `gui.*` and `ui.*` raise outside a draw callback, and work inside one |
| rollback | a frame callback must **never** observe `isrollback()` true; the confirmed counter must never go backwards |

The rollback group is the one flycast-dojo would have failed before
`cf47f4745`. It reports honestly when no rollback occurred during the session,
because a rule that was never exercised has not been tested.

## Poking at it live

Press `` ` `` in-game to open the Lua console. It evaluates in the global
environment, so the locals a script binds (`emu`, `joypad`, ...) are not in
scope there - reach the adapter through the instance it memoises instead:

```lua
api = _G["emuapi.instance"]
return api.emu.gamename(), api.frame.count()
return api.emu.supports("memory.registerwrite")   --> false
```

A line that throws prints red and changes nothing else; the interpreter and
every registered callback survive it. Everything the console shows is also
appended to `flycast-lua.log` beside the config, flushed per line.
