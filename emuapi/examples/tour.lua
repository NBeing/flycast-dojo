--- A guided tour of the cross-emulator Lua surface.
--- @runnable -- a launcher may offer this as a script to run.
---
---   RUN:   put the emuapi package in your config dir and make flycast.lua say
---            require("emuapi.examples.tour")
---          Or: flycast-rofi -> "Play + Lua script" -> tour.lua
---   PASS:  a panel appears listing live values from every namespace, a green
---          box hugs the game image, the "delivered" counts climb every frame,
---          and the "retires itself" count stops at 120 and stays there.
---
--- This is not the conformance suite. conformance.lua ASSERTS the rules and
--- prints a verdict; this EXERCISES the surface so you can watch it work, and
--- doubles as a worked example of every call.
---
--- WHAT IT DELIBERATELY DOES NOT DO, because "every part" is not the same as
--- "every part unprompted":
---
---   emu.exit()             closes the emulator. Never automatic.
---   savestate.save/load    overwrite a slot. Behind a button.
---   savestate.save_mem     serialises the whole machine, ~28 MB. On demand.
---   emu.pause/unpause      behind a button; a demo that pauses itself looks
---                          broken.
---   joypad.set             fights whoever is holding the pad, so it is off
---                          until you tick "drive input".
---   emu.speedmode          behind a checkbox for the same reason.
---
--- The script also demonstrates the rule it is bound by: everything that reads
--- the machine happens on the frame callback, everything that draws happens on
--- the draw callback, and the two communicate through a table. Drawing from a
--- frame callback raises - see CALLBACK THREADING in lua_api_spec.lua.

-- A package, so require() finds it: no path arithmetic and no dependence on
-- the working directory, which is what broke this script when it was loaded
-- from a desktop menu instead of a terminal. The dofile fallback covers a host
-- that runs one script file and never touches package.path; it needs the host
-- to expose its script directory, which is what SCRIPT_DIR is.
local ok_, mod = pcall(require, "emuapi")
if not ok_ then
    local here = rawget(_G, "SCRIPT_DIR")
    mod = dofile((here and (here .. "/") or "") .. "emuapi/init.lua")
end
local api = mod.load(rawget(_G, "EMUAPI_HOST"))
local emu, frame, joypad, memory  = api.emu, api.frame, api.joypad, api.memory
local savestate, gui, ui          = api.savestate, api.gui, api.ui
local movie, sound                = api.movie, api.sound

--- DECLARE FIRST, before anything is called. This tour drives input, takes
--- savestates and pauses the emulator, so it needs the top tier and says so.
--- A read-only overlay would ask for "observer" instead and would then be safe
--- to run in a session that refuses anything more.
---
--- Note what the granted tier does NOT depend on: the locals bound above. The
--- enforcement wrapper rawsets into the same namespace tables these locals
--- point at, so declaring after binding still governs every call made through
--- them. Binding early is not a way to escape a tier.
local tier = emu.declare({ tier = "full" })
if tier ~= "full" then
    print(("[tour] asked for full, granted '%s' - the on-demand buttons will refuse")
            :format(tier))
end

--- Anything the panel shows is captured here and drawn later.
local live = {
    buttons = {}, downs = {}, axes = {},
    pc = 0, r15 = 0, ram = 0, snd = 0,
    watchHits = 0, watchPolls = 0,
    steps = 0,
    patternLabel = "(not driving)",
}

--- Options the panel owns.
local opt = { drive = false, fast = false, showOverlay = true, pattern = 1 }

--- Results of the expensive or destructive calls, run only on request.
local onDemand = { hash = nil, stateBytes = nil, note = "" }
local request = nil          -- set by a button, consumed on the frame callback

local buttonNames = nil
local watch = nil

--------------------------------------------------------------------------
-- Frame callback: everything that READS the machine.
--------------------------------------------------------------------------
--- HANDLES. Registration returns one, and the counts it carries are the
--- reason: a callback that is registered but never delivered looks exactly
--- like a callback whose condition never came true. The panel shows these
--- climbing, so "the tour is running" is observable rather than assumed.
local hBefore, hAfter, hDraw, hRetiring

--- The four input patterns the "pattern" slider chooses between. Each returns
--- the table joypad.set is given, so each is also a worked example of the
--- setter's contract: PRESENT-AND-TRUE presses, PRESENT-AND-FALSE releases,
--- and ABSENT leaves a button alone. Pattern 4 relies on that last rule - it
--- names one button per step and never mentions the others, so it does not
--- fight anything else holding the pad.
local patterns = {
    --- 1: alternate the first two buttons every half second.
    function(f, names)
        local a, b = names[1], names[2] or names[1]
        local phase = (f % 60) < 30
        return { [a] = phase, [b] = not phase }, "alternate " .. a .. "/" .. b
    end,
    --- 2: mash the first button - four frames on, four off.
    function(f, names)
        return { [names[1]] = (f % 8) < 4 }, "mash " .. names[1]
    end,
    --- 3: hold the first button down. The dull case, and worth having: a
    --- pattern that never changes is how you tell a stuck reader from a
    --- working one.
    function(_, names)
        return { [names[1]] = true }, "hold " .. names[1]
    end,
    --- 4: walk every button in turn, a quarter second each. Sweeps the whole
    --- named set, so a button missing from joypad.buttons() shows up as a gap.
    ---
    --- IT NAMES TWO BUTTONS, NOT ONE, AND THAT IS THE LESSON. "Absent leaves
    --- that button alone" is what keeps a script from fighting whoever else is
    --- holding the pad - but it applies just as much to the button this script
    --- pressed a moment ago. Naming only the new one held every previous one
    --- down too: after fifteen steps the whole pad was pressed, which on a
    --- Dreamcast is A+B+X+Y+Start, which is the soft reset. The tour rebooted
    --- the machine to BIOS and looked like a broken emulator.
    ---
    --- So a script must release what it pressed. It still names nothing it does
    --- not own, so it still does not fight anything else.
    function(f, names)
        local step = math.floor(f / 15)
        local i    = (step % #names) + 1
        local prev = ((step - 1) % #names) + 1
        local set  = { [names[i]] = true }
        if prev ~= i then set[names[prev]] = false end
        return set, "walk " .. names[i]
    end,
}

hBefore = emu.registerbefore(function()
    -- registerbefore runs ahead of registerafter, so input driven here is in
    -- place before anything below reads it back.
    if opt.drive and buttonNames then
        local fn = patterns[opt.pattern] or patterns[1]
        local set, label = fn(frame.confirmed(), buttonNames)
        joypad.set(1, set)
        live.patternLabel = label
    else
        live.patternLabel = "(not driving)"
    end
end)

hAfter = emu.registerafter(function()
    buttonNames = buttonNames or joypad.buttons()

    live.buttons = joypad.get(1)
    live.downs   = joypad.getdown(1)
    live.axes    = { joypad.getaxis(1, 1), joypad.getaxis(1, 2) }

    live.pc  = memory.getregister("pc")
    live.r15 = memory.getregister("r15")
    live.ram = memory.readdword(0x8C010000)
    live.snd = memory.space("sound").readbyte(0x100)

    -- Near the stack pointer, because it changes constantly. A demo whose
    -- watch always reads zero cannot be told apart from a broken one.
    watch = watch or memory.watch(0x8C00F000, 64)
    live.watchPolls = live.watchPolls + 1
    if watch:changed() then live.watchHits = live.watchHits + 1 end

    -- The on-demand calls run here rather than in the draw callback, because
    -- they touch the machine and the draw callback is on another thread.
    if request == "hash" then
        onDemand.hash = savestate.hash()
        onDemand.note = "hashed the live machine"
    elseif request == "state" then
        local s = savestate.save_mem()
        onDemand.stateBytes = #s
        onDemand.hash = savestate.hash(s)
        onDemand.note = "serialised a state and hashed it"
    elseif request == "pause" then
        emu.pause()
        onDemand.note = "paused - use the emulator menu to resume"
    elseif request == "speed" then
        opt.fast = not opt.fast
        emu.speedmode(opt.fast and "turbo" or "normal")
        onDemand.note = opt.fast and "speedmode turbo" or "speedmode normal"
    end
    request = nil
end)

--- A subscriber that retires itself, so unregister() can be watched working
--- rather than taken on trust. It unregisters from INSIDE a callback, which is
--- the case that breaks a naive dispatcher walking its own list - the panel's
--- count freezing at 120 while the others keep climbing is the proof it did
--- not.
hRetiring = emu.registerafter(function()
    if hRetiring:hits() >= 120 then hRetiring:unregister() end
end)

--- Frame stepping, running alongside the callbacks above.
emu.run(function()
    while true do
        live.steps = live.steps + 1
        emu.frameadvance()
    end
end)

--------------------------------------------------------------------------
-- Draw callback: everything that DRAWS. No machine reads here.
--------------------------------------------------------------------------
local function heldList()
    local out, n = {}, 0
    for name, held in pairs(live.buttons) do
        if held then n = n + 1; out[#out + 1] = name end
    end
    table.sort(out)
    return (n == 0) and "(none)" or table.concat(out, " ")
end

hDraw = gui.register(function()
    -- Content overlay, in GAME pixels: it tracks the picture, not the window.
    if opt.showOverlay then
        local w, h = 640, 480
        -- Colours through gui.rgba, never a packed literal. The packing
        -- differs between conforming hosts, and the two conventions agree on
        -- exactly white, the greys and opaque red - so a hardcoded number
        -- passes its first test everywhere and renders wrong on one host the
        -- moment it is green.
        gui.box(0, 0, w - 1, h - 1, gui.rgba(0, 255, 0))              -- game bounds
        gui.line(w / 2 - 12, h / 2, w / 2 + 12, h / 2, gui.rgba(255, 255, 255))
        gui.line(w / 2, h / 2 - 12, w / 2, h / 2 + 12, gui.rgba(255, 255, 255))
        -- a marker that sweeps, so a frozen overlay is obvious at a glance
        local x = (frame.confirmed() * 2) % w
        gui.box(x, h - 16, x + 8, h - 8, gui.rgba(0, 255, 255))
    end

    ui.SetNextWindowPos(8, 8)
    ui.SetNextWindowSize(360, 560)
    if ui.Begin("emuapi tour") then
        ui.Text(string.format("%s  api %d  %s   tier %s",
            api.host, emu.apiversion(), emu.romname() or "?", emu.tier()))
        ui.Text(string.format("%dx%d  online=%s replay=%s rollback=%s",
            emu.screenwidth(), emu.screenheight(),
            tostring(emu.isonline()), tostring(emu.isreplay()),
            tostring(emu.isrollback())))
        ui.Separator()

        ui.Text(string.format("frame  %d   confirmed %d   resim %d",
            frame.count(), frame.confirmed(), frame.resimsteps()))
        ui.Text(string.format("steps  %d  (emu.run + frameadvance)", live.steps))
        ui.Separator()

        --- DELIVERY COUNTS. These climbing is what tells you the tour is
        --- running rather than merely loaded - the distinction a registered
        --- callback cannot otherwise report about itself.
        ui.Text(string.format("delivered  before %d   after %d   draw %d",
            hBefore:hits(), hAfter:hits(), hDraw:hits()))
        ui.Text(string.format("faults     before %d   after %d   draw %d",
            hBefore:faults(), hAfter:faults(), hDraw:faults()))
        ui.Text(string.format("retires itself: %d  (stops at 120, then frozen)",
            hRetiring:hits()))
        ui.Separator()

        ui.Text("held:  " .. heldList())
        local downs = {}
        for name, hit in pairs(live.downs) do if hit then downs[#downs + 1] = name end end
        ui.Text("newly: " .. ((#downs == 0) and "(none)" or table.concat(downs, " ")))
        ui.Text(string.format("axes   x=%d y=%d", live.axes[1] or 0, live.axes[2] or 0))
        ui.Separator()

        ui.Text("spaces: " .. table.concat(memory.spaces(), ", "))
        ui.Text(string.format("main 0x8C010000 = 0x%08X", live.ram))
        ui.Text(string.format("sound 0x100     = %d", live.snd))
        ui.Text(string.format("pc=0x%08X r15=0x%08X", live.pc, live.r15))
        ui.Text(string.format("watch 0x8C00F000: %d changes / %d polls",
            live.watchHits, live.watchPolls))
        ui.Separator()

        ui.Text(string.format("movie %s  frames %d",
            tostring(movie.mode()), movie.framecount()))
        ui.Text(string.format("sound %d voices @ %d Hz",
            sound.voicecount(), sound.outputrate()))
        ui.Separator()

        -- Widgets, exercised as themselves.
        opt.drive       = ui.Checkbox("drive input", opt.drive)
        opt.showOverlay = ui.Checkbox("game-pixel overlay", opt.showOverlay)
        opt.pattern     = ui.SliderInt("pattern", opt.pattern, 1, 4)
        --- What the slider is actually doing. It used to select nothing: the
        --- value was read and stored and no code consumed it, so the control
        --- looked functional and was not - the exact defect this tour exists
        --- to make visible elsewhere.
        ui.Text("  pattern: " .. tostring(live.patternLabel))
        local scale     = ui.SliderFloat("ui scale (read-only)", ui.GetScale(), 0, 4)
        local mx, my    = ui.GetMousePos()
        ui.Text(string.format("mouse %d,%d  buttons %s%s",
            mx, my,
            ui.IsMouseDown(1) and "L" or "-",
            ui.IsMouseDown(2) and "R" or "-"))
        ui.Spacing()

        -- The expensive and destructive calls, on request only.
        if ui.Button("hash live state") then request = "hash" end
        ui.SameLine()
        if ui.Button("serialise + hash") then request = "state" end
        if ui.Button("toggle speedmode") then request = "speed" end
        ui.SameLine()
        if ui.Button("pause") then request = "pause" end

        if onDemand.hash then
            ui.Text(string.format("hash 0x%08X%s", onDemand.hash,
                onDemand.stateBytes and string.format("  (%d bytes)", onDemand.stateBytes) or ""))
        end
        if onDemand.note ~= "" then ui.TextColored(onDemand.note, 0.6, 0.9, 0.6, 1.0) end

        ui.Spacing()
        ui.Selectable("(Selectable, exercised)", false)
    end
    ui.End()
end)

print("[tour] loaded on " .. api.host)
