--- =====================================================================
--- Emulator Lua API — a conformance stub
--- =====================================================================
---
--- STATUS: DRAFT. Not finished, and not yet safe to implement against.
---
--- Every question it set out to settle is settled: capability is queryable,
--- rollback safety is in the contract, unportable hooks are excluded with
--- reasons, buttons are named booleans, indices are 1-based, failure is loud,
--- only draw callbacks may draw, memory is addressed by named space, drawing
--- splits into ImGui widgets and game-pixel overlay, namespaces are bound
--- locally rather than installed, registration returns a handle that counts
--- its own deliveries, and a script declares what it is allowed to do.
--- What remains is porting, and a conformance suite to keep the two honest -
--- see LUA_TODO.md.
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
--- This interface is designed to the domain, NOT to the intersection of what
--- existing emulators happen to expose. Where an emulator's surface disagrees,
--- the emulator adapts. An adapter is a normal cost, not a spec failure.
---
--- The reason is concrete. fbneo-rr's `joypad.get(which)` ignores its argument
--- and returns one flat table of every game input, keyed by the driver's own
--- string ("P1 Fire 1"). flycast-dojo returned a raw active-low bitmask with no
--- constants exposed. Neither is a worse *spelling* of per-player buttons; they
--- are different data models. A spec assembled from the intersection would have
--- had to adopt one of them, or omit per-player input altogether. Designing the
--- sane thing and paying for an adapter is the only option that yields a
--- surface worth writing scripts against.
---
--- ONE BOUNDARY. Existing API *shape* is incidental and must not constrain the
--- design - adapt it. Existing *architecture* is essential and must - respect
--- it. That is why per-access memory hooks are excluded rather than specified:
--- they assume an interpreter core, and no adapter can paper over a
--- recompiler. The rule is: most logical, bounded by implementable on at least
--- two real emulators, even if expensively. An abstraction nothing can
--- implement is not logical, only tidy.
---
--- WHAT CONFORMANCE MEANS
---
--- 1. The conformance suite is the definition. If it passes, you conform.
---    Everything else is opinion. See LUA_TODO.md Part 3.
---
--- 2. Partial conformance is the normal case. Nothing will implement all of
---    this. Declare what you have:
---
---        if emu.supports("memory.registerwrite") then ... end
---
---    An absent capability MUST answer false and MUST raise if called anyway.
---    A function that silently does nothing is worse than one that admits it
---    does not exist. `emu.supports` should be derived from the bindings that
---    actually exist, never a hand-maintained list, so it cannot drift.
---
--- 3. Namespaces are NAMED neutrally - `emu`, `memory`, `input`, `joypad`,
---    `savestate`, `gui`, `ui`, `movie`, `sound`, `frame` - but they are NOT
---    installed as globals. The loader returns them and a script binds what it
---    wants:
---
---        local api = dofile("adapters/emuapi.lua").load()
---        local emu, joypad, memory, gui = api.emu, api.joypad, api.memory, api.gui
---
---    A file-scope local shadows a global for that file alone, so nothing else
---    in the Lua state is disturbed and the shape is identical on every host.
---
---    Installing globals cannot be the contract, because the names are not
---    free everywhere: fbneo-rr already owns emu, memory, input, joypad,
---    savestate, movie and gui. Overwriting them would break both its existing
---    scripts and the adapter itself, which reaches the host through those
---    very tables. A host with room may still opt in with api.install(), which
---    REFUSES rather than clobbers - loud, per failure tier 1.
---
--- 4. Anything not marked [core] is optional.
---
--- WHERE THE ADAPTER LIVES
---
--- Three options, and the first is underrated:
---
---   a. A Lua shim over the emulator's native API. Requires no changes to the
---      emulator at all, which means an emulator you do not control - or one
---      whose licence forbids sharing code with yours - can still present this
---      surface. Getting the design wrong costs a text edit, not a release.
---      See docs/adapters/.
---
---   b. Native implementation in the emulator. Best performance; needs upstream
---      buy-in. Worth it for hot paths (per-frame memory reads, drawing).
---
---   c. Hybrid: native where it is hot, shim for the rest. Expected end state.
---
--- Start at (a). Move a function to (b) when measurement says to.
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
--- FAILURE SEMANTICS — resolved
---
--- Three tiers, because "it failed" covers three different situations that
--- deserve different answers.
---
---   1. A programmer mistake RAISES. Wrong type, index out of range, unknown
---      button name, malformed argument. These are bugs in the script and must
---      be loud at the call site.
---
---   2. Genuinely absent data returns `nil`. No game loaded, no session, an
---      address that is not mapped. Never a silent zero: zero is a legitimate
---      value and indistinguishable from failure, which turns a missing
---      feature into wrong data.
---
---   3. A missing capability answers false from `emu.supports()`, and RAISES
---      if called anyway. Never a function that silently does nothing.
---
--- The rule of thumb: if a correct script could hit it at runtime, return nil;
--- if only a wrong script can hit it, raise.
---
---
--- INDEXING — resolved
---
--- Every INDEX a script sees is 1-BASED: players, axes, slots, ports. Lua is a
--- 1-based language and a mixed convention is a permanent source of off-by-one
--- bugs. Out of range raises, per tier 1 above.
---
--- Note for implementers: an emulator's internal base is its own business, but
--- it MUST NOT leak. flycast-dojo is 1-based for players and axes, and 0-based
--- for savestate slots internally; its neutral `savestate.*` alias is
--- responsible for the shift, not the script.
---
--- Note on players: this interface is per-player, and `joypad.get(player)` is
--- expected to return only that player's buttons. fbneo-rr's native model is
--- different - one flat table of every game input, keyed by the driver's own
--- name ("P1 Fire 1"), with the player encoded in the string and the argument
--- ignored. Conforming there means splitting that table by player and mapping
--- driver names onto button names. That is real work, not a rename, and it is
--- the largest single conformance cost identified so far.
---
---
--- THE UI SURFACE — ImGui is the interface, not an implementation detail
---
--- There are two different drawing problems and they want different answers.
---
---   WIDGET UI - windows, buttons, sliders, text fields, plots. Panels a
---   person interacts with. Abstracting this to text/box/line primitives means
---   reimplementing layout, hit-testing and focus in Lua, badly.
---
---   CONTENT OVERLAY - marks locked to the game image: hitboxes, positions,
---   trajectories. These must be in game pixels or they do not line up.
---
--- For widget UI the interface IS ImGui. Every C++ emulator in scope already
--- embeds it, so the integration contract becomes small: give the script an
--- ImGui context and call its draw callback inside a live frame. Add that
--- canvas to an emulator and the whole UI surface arrives with it.
---
--- VERSION SKEW is the obvious objection and it measures smaller than it
--- looks. flycast-dojo ships ImGui 1.80 WIP, fbneo-rr ships 1.92.9 WIP - five
--- years apart - yet all 19 ImGui functions fbneo-rr actually exposes to Lua
--- exist in 1.80. A conservative subset is what emulators bind in practice, so
--- that subset is the baseline:
---
---   Begin End Text TextColored Button Checkbox Selectable
---   SliderFloat SliderInt InputText SameLine Separator Spacing
---   SetNextWindowSize SetNextWindowPos
---   GetMousePos IsMouseClicked IsMouseDown IsMouseReleased Image
---
--- A conforming host MUST provide all of the baseline under `ui.*`. Anything
--- beyond it - tables, plots, docking - is capability-gated like everything
--- else, so a script degrades instead of crashing:
---
---     if emu.supports("ui.BeginTable") then ... else ... end
---
--- Names match ImGui's own, deliberately. A script author already knows this
--- API, and renaming it to look neutral would only add a translation layer
--- between the documentation and the binding.
---
--- `gui.*` remains, narrowed to its real job: content overlay in game pixels.
--- ImGui does not solve that, because a widget positioned in window space does
--- not track the game image across resizes, letterboxing or scaling.
---
---
--- ADDRESS SPACES — resolved
---
--- Systems have more than one. fbneo-rr exposes a second CPU through suffixed
--- accessors (readbyte_audio, writeword_audio, ...), which does not scale: the
--- set of spaces is per-system, so a fixed suffix per space means a new
--- function for every CPU any supported system might have.
---
--- Spaces are named and queried instead, the same shape as joypad.buttons():
---
---     memory.spaces()               -- {"main", "sound"}
---     memory.space("sound").readbyte(0x100)
---     memory.readbyte(0x8C010000)   -- shorthand for the main space
---
---   * "main" always exists. It is the primary CPU's space.
---   * Every other name is per-system; a script asks rather than assumes.
---   * An unknown space name raises, per failure tier 1.
---   * A space object is a plain table of the same read/write functions, so it
---     can be hoisted out of a loop: local snd = memory.space("sound").
---
--- ADDRESSES ARE NOT NORMALISED. They are whatever that CPU uses, in its own
--- convention - SH4 virtual addresses on a Dreamcast, 68K addresses on a CPS2.
--- This is deliberate. A script reading a game's health value is bound to that
--- game's memory map by its nature, and a "portable address" would be a
--- translation layer that lies about the hardware while breaking the moment a
--- script author cross-references a disassembly or a cheat file.
---
--- Whether a space is a separate CPU or a window into the main one is an
--- implementation detail the script must not have to care about. fbneo-rr's
--- audio space is a genuinely separate accessor; flycast-dojo's sound RAM is a
--- window in the SH4 space at 0x00800000, presented as a space with its own
--- 0-based addresses by the adapter. Same interface, different mechanism.
---
---
--- CALLBACK THREADING — resolved. A correctness rule, not a style note.
---
--- Callbacks do not all run on the same thread, and an emulator is not
--- required to make them. flycast-dojo dispatches its draw callback from the
--- render thread inside an active drawing frame, and its frame callback from a
--- separate emulation thread (ThreadedRendering, on by default). A UI toolkit
--- keeps global per-frame state, so a script that draws from a frame callback
--- races the renderer and corrupts it - silently, and only under load.
---
--- The rule:
---
---   * `gui.*` may be called ONLY from a `gui.register` callback. Everywhere
---     else it MUST raise, per failure tier 1 - this is a script bug and has
---     to be loud, because the alternative is an intermittent crash that looks
---     like an emulator fault.
---   * Everything else - memory, input, frame counters, savestates - is
---     callable from any callback.
---   * An observer that wants to draw BUFFERS on the frame callback and emits
---     from the draw callback. This is the portable shape:
---
---         local pending = {}
---         emu.registerafter(function()
---             pending.hp = memory.readbyte(0x8C0100)   -- emulation thread
---         end)
---         gui.register(function()
---             gui.text(8, 8, "HP " .. (pending.hp or "?"))  -- render thread
---         end)
---
---   * A single-threaded emulator MUST still enforce this. Scripts written
---     against a permissive host break on a threaded one, which is exactly the
---     portability failure this interface exists to prevent.
---
--- Implementers: a thread-local "inside the draw callback" flag is enough, and
--- is cheaper and more precise than comparing thread ids. It also catches
--- drawing from outside any callback, where there is no frame to draw into.
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
---
--- A BYTE OFFSET INTO AN OPAQUE BLOB IS NOT AN INDEX, and is 0-based. An offset
--- into a savestate string or a memory region is an address, and addresses are
--- reported in the machine's own terms everywhere else in this interface; a
--- host that added one to them would be inventing a convention no disassembly
--- or cheat file shares. A host publishing such an offset must say so at the
--- call site and give the Lua slice form, as nbneo-rr does:
---     blob:sub(r.offset + 1, r.offset + r.bytes)
--- Stated because the rule above says "every index" without qualification, and
--- getting this backwards reads a region one byte out - which is plausible
--- data, not an error.

local spec = {}

--- ---------------------------------------------------------------------
--- emu — lifecycle, identity, capability
--- ---------------------------------------------------------------------
emu = {}

function emu.supports(name) end          --- [spec] [core] string -> bool

--- ---------------------------------------------------------------------
--- DECLARATION - what this script is ALLOWED to do
--- ---------------------------------------------------------------------
--- DECLARATION AND DISCOVERY ARE ORTHOGONAL, NOT ALTERNATIVES. Conflating
--- them is how a host ends up with neither, and it is worth stating because a
--- surveyed host argues at length that declaration REPLACES discovery.
---
---     emu.supports(name)   PORTABILITY:    does this host have the function
---     emu.declare{...}     AUTHORISATION:  may this script call it, here, now
---
--- A host can answer yes to the first and no to the second - the function
--- exists, and this session will not let you have it - and a script needs
--- both answers to do anything sensible about either.
---
--- Three tiers, ordered:
---     observer   reads and draws. Cannot touch the machine.
---     mutator    drives input and writes memory.
---     full       savestates, recording, emulator control.
---
--- The granted tier MAY BE NARROWER than the one requested, and that is not an
--- error. A script that can degrade should be able to:
---
---     local tier = emu.declare{ tier = "mutator" }
---     if tier ~= "mutator" then  -- observe only; do not drive input
---
--- WHY A SCRIPT WOULD LIMIT ITSELF: because that is what makes it portable to
--- a session that limits it involuntarily. The motivating case is netplay.
--- flycast-dojo today refuses Lua ENTIRELY when online, which is the bluntest
--- possible version of this - an overlay that could not desync anything is
--- refused alongside a script that could. A tier is what lets the first one
--- run, and a script that already declares observer needs no change when the
--- host starts enforcing.
---
--- DECLARING IS IRREVERSIBLE AND HAPPENS ONCE. A script that could widen its
--- own tier later would not be declaring anything. Declaring twice raises;
--- it is a bug, not a widening.
---
--- UNDECLARED MEANS FULL, so every script written before this existed keeps
--- working. Declaring is opting IN to being restricted.
---
--- A host with no authorisation model of its own grants what was asked for.
--- That is the honest answer; a pretend restriction is worse than none.
function emu.declare(req) end            --- [ok]   {tier=} -> granted tier
function emu.tier() end                  --- [ok]   the tier in force; never nil
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
--- ---------------------------------------------------------------------
--- REGISTRATION RETURNS A HANDLE, AND THE HANDLE COUNTS ITS OWN DELIVERIES
--- ---------------------------------------------------------------------
--- Every registration function returns a handle:
---
---     h:hits()        deliveries so far
---     h:faults()      deliveries that threw
---     h:unregister()  stop delivering
---
--- THE COUNT IS PART OF THE INTERFACE, NOT A DIAGNOSTIC EXTRA, and it is here
--- for one reason: an action that produces nothing observable is
--- indistinguishable from one that worked. A callback that is registered but
--- never delivered looks exactly like a callback whose condition never came
--- true, and no amount of care in the script can tell those apart from the
--- inside. hits() is the difference.
---
--- This promotes a testing rule into the surface deliberately. The same shape
--- has produced every expensive defect on the hosts surveyed: a well-formed
--- video file that was entirely black, a memory watch reporting zero changes,
--- a coverage report reading 54/54 with a fifth of the surface unwired, a
--- script that loaded nothing and said nothing, and - on fbneo-rr - a probe
--- that pressed a button for 180 frames, passed its neutral control, and had
--- pressed nothing at all while every downstream check agreed with it.
---
--- The rule generalises past callbacks: ANY primitive that can silently do
--- nothing must expose how often it did something. A watch reports polls and
--- changes; a bounded queue reports drops; an input write reports refusals.
---
--- MULTIPLE SUBSCRIBERS ARE NORMAL. Registering a second handler must not
--- replace the first. Unregistration takes a handle, not a function, so two
--- subscribers sharing one closure remain distinguishable.
---
--- A THROWING SUBSCRIBER IS CONTAINED, NOT EVICTED. One bad frame in one
--- observer must not remove it for the session: that turns a transient into a
--- feature that appears never to have been enabled. It is counted and called
--- again. (fbneo-rr evicts, and both successors independently rejected that.)
---
--- UNREGISTERING FROM INSIDE A CALLBACK IS NORMAL, not an edge case, and a
--- host must survive it - by copying the list before the walk, or by marking
--- and sweeping after it.
function emu.registerbefore(fn) end      --- [ok]   before the frame is simulated
function emu.registerafter(fn) end       --- [ok]   after, confirmed frames only
function emu.registerexit(fn) end        --- [ok]

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

function memory.spaces() end             --- [ok]   adapter; see ADDRESS SPACES
function memory.space(name) end          --- [ok]   adapter; returns an accessor table
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

--- Change detection, poll-based.
---
---     local hp = memory.watch(0x8C123456, 4)
---     if hp:changed() then ... end        -- since the last call
---     hp:release()
---
--- changed() compares CONTENT, not writes: rewriting the same bytes reports no
--- change, which is what a script watching a value wants and what a write hook
--- could not give it. The range is exact - a write one byte past it does not
--- register.
---
--- Snapshot-compare and dirty-page tracking answer different questions, and
--- this is the former. Comparing a copy is O(watched bytes) per poll, paid
--- only by whoever watches - a 4-byte watch at 60Hz is 240 bytes a second -
--- and it is byte-exact and content-based. Page tracking is O(dirtied pages)
--- globally, paid whether or not anyone is watching, write-based, and reports
--- 4KB granules that must then be compared anyway to find what moved. Scripts
--- watch tens of bytes, so the crossover is nowhere near them.
---
--- An implementation with tracking already armed may use it as a pre-filter,
--- but must still answer for the exact bytes. The question page tracking is
--- actually good at - "what changed anywhere" - is a different capability and
--- belongs under a different name.
function memory.watch(addr, len) end     --- [ok]
function memory.unwatch(w) end           --- [ok]
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
--- [capability] Execution hooks. RE-OPENED 2026-09-04: the exclusion stood on a
---      rationale that does not hold.
---
---      It read: "typically implemented by patching a trap opcode into emulated
---      memory; under rollback that patched memory is snapshotted and restored,
---      and differs from the peer's - a desync." That is an argument against
---      OPCODE PATCHING, not against exec hooks, and it silently assumed the
---      one implementation.
---
---      nbneo-rr does it a different way and none of the objection survives:
---      `core/compat/exec_hook.cpp` rides the m68k PC-changed callback the core
---      already has, dispatching through a 1 MiB bitmap over the 24-bit address
---      space. Nothing is written into emulated memory, so nothing is
---      snapshotted, nothing is restored and nothing can differ from a peer.
---      Cost with no hooks registered is one load and one branch.
---
---      Nor was it a fringe request: in the phobos corpus `registerexec` has 29
---      call sites - busier than the whole of `joypad.*`, which this spec calls
---      its largest conformance cost - and nbneo's entire tick model is built
---      on it.
---
---      So it is CAPABILITY-GATED, not excluded. A host with a recompiler that
---      inlines memory access answers false from emu.supports("memory.registerexec")
---      and raises if called, which is exactly what the gate is for. A host with
---      a per-instruction seam may implement it. The blocker is the mechanism,
---      and the spec had promoted one mechanism's limitation into a rule.

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
--- ui — widget UI. ImGui, under its own names. See THE UI SURFACE.
--- ---------------------------------------------------------------------
--- Baseline profile; a conforming host provides all of it. Legal only inside a
--- gui.register callback, per CALLBACK THREADING.
ui = {}

function ui.Begin(name, open, flags) end      --- [ok]
function ui.GetScale() end                   --- [ok] UI scale factor, so a
--- script can scale deliberately rather than have it applied behind its back
function ui.End() end                         --- [ok] endWindow
function ui.Text(s) end                       --- [ok]
function ui.TextColored(r,g,b,a,s) end        --- [ok] textColor
function ui.Button(label, w, h) end           --- [ok]
function ui.SameLine(offset) end              --- [ok]
function ui.Checkbox(label, value) end        --- [ok]
function ui.Selectable(label, selected) end   --- [ok]  
function ui.SliderFloat(label, v, lo, hi) end --- [ok]  
function ui.SliderInt(label, v, lo, hi) end   --- [ok]  
function ui.InputText(label, text) end        --- [ok]
function ui.Separator() end                   --- [ok]  
function ui.Spacing() end                     --- [ok]  
function ui.SetNextWindowSize(w, h) end       --- [ok]
function ui.SetNextWindowPos(x, y) end        --- [ok]
function ui.GetMousePos() end                 --- [ok]  
function ui.IsMouseClicked(button) end        --- [ok]
function ui.IsMouseDown(button) end           --- [ok]  
function ui.IsMouseReleased(button) end       --- [ok]  
function ui.Image(id, w, h) end               --- [no] needs host texture
--- management, which is emulator-specific. Capability-gated rather than faked:
--- emu.supports("ui.Image") answers false where it is absent.

--- ---------------------------------------------------------------------
--- gui — content overlay, in GAME pixels
--- ---------------------------------------------------------------------
--- Not a lesser ImGui. This is the layer that tracks the game image: origin at
--- its top-left corner, one unit per game pixel, and the emulator owns the
--- mapping to screen including scaling and letterboxing. A mark drawn at a
--- character's position must stay on that character when the window resizes,
--- which window-space widgets cannot do.
--- COLOURS: build them with gui.rgba(r, g, b, a) and never write a packed
--- literal. The packing is 0xAABBGGRR here - ImGui's own IM_COL32, not the
--- 0xRRGGBBAA a reader tends to assume - but a portable script has no business
--- knowing that, because conforming hosts disagree: nbneo-rr packs 0xRRGGBBAA
--- and keeps that convention deliberately, so its existing corpus of colour
--- constants keeps meaning what it meant.
---
--- THE TWO CONVENTIONS AGREE ON EXACTLY THE COLOURS A PERSON TESTS WITH FIRST.
--- Every grey is byte-identical under both. So is opaque red: 0xFF0000FF is
--- opaque red under 0xAABBGGRR AND under 0xRRGGBBAA. They disagree on green
--- (0xFF00FF00 vs 0x00FF00FF) and blue (0xFFFF0000 vs 0x0000FFFF). A script
--- that hardcodes numbers therefore passes its first test on either host and
--- renders wrong on one of them the moment it draws something that is not
--- white, grey or red.
---
--- Corollary for anyone writing checks: assert colour with GREEN or BLUE. A
--- test written with white or red cannot fail on a host that packs the other
--- way round, which is the only thing such a test is for.
function gui.rgba(r, g, b, a) end        --- [ok]   channels in, packing hidden; a defaults to 255
gui = {}

function gui.text(x, y, s) end           --- [ok]   flycast.ui.text (needs beginWindow)
function gui.box(x, y, x2, y2, c) end    --- [ok]   outline
function gui.boxfill(x, y, x2, y2, fill, border) end --- [ok]
function gui.scale() end                 --- [ok]   screen px per game px
function gui.line(x, y, x2, y2, c) end   --- [ok]   flycast.ui.line
function gui.pixel(x, y, c) end          --- [spec]
function gui.register(fn) end            --- [ok]   flycast_callbacks.overlay
--- Reminder: every gui.* above is legal ONLY inside a gui.register callback.
--- See CALLBACK THREADING at the top of this file.

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
--- (imgui.* was previously excluded here as "leaking the host toolkit into
---  scripts". That was wrong for this target set - see THE UI SURFACE above.
---  It is now the primary UI surface.)
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
