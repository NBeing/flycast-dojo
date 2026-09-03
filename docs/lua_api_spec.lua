--- =====================================================================
--- Emulator Lua API — a conformance stub
--- =====================================================================
---
--- STATUS: DRAFT. Not finished, and not yet safe to implement against.
---
--- It names the right things and settles four questions (capability is
--- queryable, rollback safety is in the contract, unportable hooks are
--- excluded with reasons, and buttons are named booleans). It does NOT yet pin
--- down player indexing, address spaces, failure semantics, drawing
--- coordinates, callback threading, or interface versioning - and two
--- emulators can both conform to what is written here and still disagree in
--- every one of those.
--- See LUA_TODO.md, Part 1, before building on this.
---
--- WHAT THIS IS
---
--- A stub, not an implementation. Nothing here executes. It declares an
--- emulator-agnostic Lua surface that more than one emulator can conform to,
--- so that a training tool, an overlay or a TAS script can move between them
--- without being rewritten.
---
--- It exists because flycast-dojo and fbneo-rr already expose overlapping but
--- differently-shaped versions of the same ideas, and the overlap was arrived
--- at twice rather than agreed once. The immediate purpose is to record the
--- intent to port fbneo-rr's remaining surface into flycast-dojo; the longer
--- purpose is that the target of the port is a shared interface rather than
--- one emulator copying another.
---
--- It doubles as an editor completion stub: point your LSP at this file and
--- script authors get the contract with documentation.
---
---
--- CONFORMANCE
---
--- 1. Namespaces are ROOT-LEVEL and neutral: `emu`, `memory`, `input`,
---    `joypad`, `savestate`, `gui`, `movie`, `sound`, `frame`.
---
---    Neither emulator does this today. flycast-dojo nests everything under
---    `flycast.*` (`flycast.emulator.startGame`), fbneo-rr exposes `emu.*` and
---    an `fba.*` alias. Conforming means publishing the neutral names as
---    aliases of whatever the emulator already has. Existing names stay: this
---    interface adds, it does not rename.
---
--- 2. An emulator declares what it implements. A script asks rather than
---    assumes, because no emulator will implement all of this:
---
---        if emu.supports("memory.registerwrite") then ... end
---
---    Absent capability MUST be a missing/false answer from `emu.supports`,
---    never a silently-does-nothing function. A stub that pretends to work is
---    worse than one that admits it does not.
---
--- 3. Anything not marked [core] is optional.
---
---
--- STATUS TAGS
---
---   [ok]    implemented in flycast-dojo today
---   [port]  exists in fbneo-rr, intended for flycast-dojo
---   [spec]  in neither yet; proposed for the common interface
---   [no]    deliberately excluded, with the reason given
---
--- Tags describe flycast-dojo. fbneo-rr's own conformance is a separate pass.
---
---
--- THE ROLLBACK CONTRACT — read before writing an observer
---
--- Any emulator with rollback netplay re-runs frames it has already run. A
--- callback that accumulates - a counter, a tally, a log line, a file write -
--- will otherwise count the same logical frame once per rollback pass. This is
--- not hypothetical: flycast-dojo shipped exactly that bug in its `vblank`
--- callback until 2026-09.
---
--- The interface therefore requires:
---
---   * Frame callbacks are NOT delivered for re-simulated frames by default.
---   * `emu.isrollback()` exists for scripts that deliberately want them.
---   * `frame.confirmed()` is monotonic and does not move during rollback.
---     A plain frame counter usually does move, because it is incremented per
---     pass and rarely restored by the state loader - assume it drifts.
---   * A conforming emulator without rollback answers false and 0 rather than
---     omitting these, so one script works on both.
---
--- =====================================================================

local spec = {}

--- ---------------------------------------------------------------------
--- emu — lifecycle, identity, capability
--- ---------------------------------------------------------------------
emu = {}

function emu.supports(name) end          --- [spec] [core] string -> bool
function emu.capability() end            --- [port] tier: "off"|"observer"|"mutator"|"full"
function emu.can(what) end               --- [port] may this script do `what` right now
function emu.pause() end                 --- [ok]   flycast.emulator.pause
function emu.unpause() end               --- [ok]   flycast.emulator.resume
function emu.exit() end                  --- [ok]   flycast.emulator.exit
function emu.message(text) end           --- [ok]   flycast.emulator.displayNotification
function emu.romname() end               --- [ok]   flycast.state.gameId (rename to conform)
function emu.gamename() end              --- [port]
function emu.screenwidth() end           --- [ok]   flycast.state.display.width
function emu.screenheight() end          --- [ok]   flycast.state.display.height
function emu.isonline() end              --- [port] in a netplay session
function emu.isreplay() end              --- [port] playing a recorded session back
function emu.isrollback() end            --- [ok]   flycast.state.isRollback
function emu.frameadvance() end          --- [port] yield one frame; the TAS primitive
function emu.speedmode(mode) end         --- [port] "normal"|"turbo"|"maximum"
function emu.registerbefore(fn) end      --- [port] before the frame is simulated
function emu.registerafter(fn) end       --- [port] after, confirmed frames only
function emu.registerexit(fn) end        --- [port]

--- ---------------------------------------------------------------------
--- frame — the rollback-safe clock
--- ---------------------------------------------------------------------
frame = {}

function frame.count() end               --- [ok]   flycast.state.getFrameNumber (MAY drift)
function frame.confirmed() end           --- [ok]   flycast.state.getConfirmedFrameNumber
function frame.resimsteps() end          --- [port] frames re-simulated so far

--- ---------------------------------------------------------------------
--- memory — reads and writes are portable; hooks mostly are not
--- ---------------------------------------------------------------------
memory = {}

function memory.readbyte(addr) end       --- [ok]   memory.read8
function memory.readword(addr) end       --- [ok]   memory.read16
function memory.readdword(addr) end      --- [ok]   memory.read32
function memory.readbytesigned(addr) end --- [ok]   memory.read8s
function memory.readrange(addr, len) end --- [ok]   memory.readTable8/16/32
function memory.writebyte(addr, v) end   --- [ok]   memory.write8
function memory.writeword(addr, v) end   --- [ok]   memory.write16
function memory.writedword(addr, v) end  --- [ok]   memory.write32
function memory.getregister(name) end    --- [port] CPU register by name
function memory.setregister(name, v) end --- [port]

--- Change detection. Two tiers, because the strong one is not portable.
function memory.watch(addr, len) end     --- [spec] poll-based: did this range change
function memory.unwatch(id) end          --- [spec]
function memory.registerwrite(a, l, fn) end
--- [no] Per-access write hooks. fbneo-rr injects these into its M68K
---      accessors, which works because that core is an interpreter. An
---      emulator with a recompiler inlines memory access into generated code,
---      so a per-access hook means either recompiler surgery or forcing the
---      interpreter. Where dirty-page tracking already exists for rollback,
---      `memory.watch` can be built on it at page granularity, once per
---      confirmed frame, with no PC context. That is a different capability
---      and is spelled differently on purpose.
function memory.registerexec(a, fn) end
--- [no] Execution hooks. Typically implemented by patching a trap opcode into
---      emulated memory. Under rollback that patched memory is snapshotted and
---      restored, and differs from the peer's - a desync. Excluded until some
---      emulator has hardware breakpoints that survive a state load.

--- ---------------------------------------------------------------------
--- input / joypad
--- ---------------------------------------------------------------------
--- RESOLVED: buttons are named booleans, never a bitmask.
---
--- A bitmask cannot be the interface. Its layout is per-system by definition,
--- and polarity is an implementation detail that leaks: flycast-dojo's kcode
--- is active-low, so a *cleared* bit means held, and it exposed no constants
--- at all - scripts were hardcoding inverted tests against Dreamcast bit
--- values. An emulator MAY keep a raw accessor for its own scripts, but it is
--- not part of this interface.
---
--- Contract:
---   * A button reads `true` when it is held, whatever the hardware does.
---   * `joypad.buttons()` enumerates the names this system understands, so a
---     script adapts instead of assuming. Names are lowercase strings.
---   * On set: present-and-true presses, present-and-false releases, and an
---     absent key leaves that button ALONE. This is what lets one script drive
---     one button without fighting another script, or the player.
---   * An unknown button name is an error, not a silent no-op.
---
--- Common names, where the system has them: up down left right, start, coin,
--- a b c d x y z, l r. Systems add their own; `joypad.buttons()` is the truth.
joypad = {}

function joypad.buttons() end            --- [ok]   flycast.input.buttonNames
function joypad.get(player) end          --- [ok]   flycast.input.getButtonTable
function joypad.set(player, buttons) end --- [ok]   flycast.input.setButtonTable
function joypad.setbutton(player, name, held) end --- [ok] flycast.input.setButton
function joypad.getdown(player) end      --- [port] newly pressed this frame
function joypad.getup(player) end        --- [port] newly released this frame
function joypad.getaxis(player, axis) end     --- [ok] flycast.input.getAxis
function joypad.setaxis(player, axis, v) end  --- [ok] flycast.input.setAxis

input = {}
function input.get() end                 --- [port] host keyboard/mouse state
function input.registerhotkey(n, fn) end --- [port]

--- ---------------------------------------------------------------------
--- savestate
--- ---------------------------------------------------------------------
savestate = {}

function savestate.save(slot) end        --- [ok]   flycast.emulator.saveState
function savestate.load(slot) end        --- [ok]   flycast.emulator.loadState
function savestate.create(slot) end      --- [port] an object rather than a slot
function savestate.save_mem() end        --- [port] to a string, no file
function savestate.load_mem(s) end       --- [port]
function savestate.hash(s) end           --- [port] desync hunting
function savestate.registersave(fn) end  --- [port] attach script data to a state
function savestate.registerload(fn) end  --- [port]

--- ---------------------------------------------------------------------
--- gui — drawing
--- ---------------------------------------------------------------------
--- flycast-dojo draws through ImGui inside an overlay callback and needs a
--- window open first; fbneo-rr draws immediate primitives. The neutral surface
--- is the primitives, which the ImGui side can implement on a draw list.
gui = {}

function gui.text(x, y, s) end           --- [ok]   flycast.ui.text (needs beginWindow)
function gui.box(x, y, x2, y2, c) end    --- [ok]   flycast.ui.rect
function gui.line(x, y, x2, y2, c) end   --- [ok]   flycast.ui.line
function gui.pixel(x, y, c) end          --- [spec]
function gui.register(fn) end            --- [ok]   flycast_callbacks.overlay

--- ---------------------------------------------------------------------
--- movie — recording and rerecording
--- ---------------------------------------------------------------------
movie = {}

function movie.framecount() end          --- [port]
function movie.mode() end                --- [port] "record"|"playback"|nil
function movie.rerecordcounting(on) end  --- [port]
function movie.getreadonly() end         --- [port]
function movie.setreadonly(v) end        --- [port]
function movie.stop() end                --- [ok]   flycast.replay.stopRecording
function movie.play(path) end            --- [spec] flycast launches replays from the UI only
function movie.record(path) end          --- [ok]   flycast.replay.startRecording

--- ---------------------------------------------------------------------
--- sound
--- ---------------------------------------------------------------------
sound = {}

function sound.voicecount() end          --- [port]
function sound.outputrate() end          --- [port]
function sound.voice(n) end              --- [port] per-channel state; drives audio overlays

--- ---------------------------------------------------------------------
--- Out of scope for the common interface
--- ---------------------------------------------------------------------
--- rewind.*   fbneo-rr's snapshot ring. Depends on cheap delta snapshots.
---            Revisit per-emulator; not a portable assumption.
--- spec.*     Counterfactual rollout (save, step, inspect, restore). Needs
---            cheap speculative stepping. Assess against whatever dominates
---            the emulator's snapshot cost, not against RAM size.
--- quark.*    Fightcade replay format. Emulator-specific by definition.
--- macro.*    fbneo-rr's macro editor timeline. A tool, not an emulator API.
--- vsav.*     Per-game recorder. Belongs in a script, not the interface.
--- imgui.*    Direct ImGui bindings leak the host toolkit into scripts. The
---            neutral `gui.*` primitives are the portable subset.
--- socket.*   Scripts opening sockets is a real security question. If it is
---            ever specified it needs a capability gate, not a default.
---
--- ---------------------------------------------------------------------
--- Licensing note for anyone doing the port
--- ---------------------------------------------------------------------
--- flycast-dojo is GPL-2.0-or-later. FBNeo's licence carries field-of-use
--- restrictions, which GPLv2 section 6 forbids adding, so FBNeo-derived code
--- cannot be combined into flycast-dojo and distributed. Port the ideas and
--- your own independently-authored code. Do not transcribe FBNeo sources, and
--- note that some fbneo-rr Lua files are upstream FCEUX/FBA lineage rather
--- than that fork's own work. This file is a specification, written from
--- observed API surface, and carries no upstream code.

return spec
