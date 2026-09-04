--- A guided tour of the cross-emulator Lua surface.
--- @runnable -- a launcher may offer this as a script to run.
---
---   RUN:   copy into your config dir as flycast.lua, with adapters/ beside it,
---          and start a game. Or: flycast-rofi -> "Play + Lua script" -> tour.lua
---   PASS:  a panel appears listing live values from every namespace, and a
---          green box hugs the game image.
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

-- Resolve the adapter relative to the CONFIG DIRECTORY, not the working one.
-- dofile("adapters/...") resolves against the cwd, which is the config dir when
-- flycast is started from a terminal there and something arbitrary when it is
-- started from a desktop menu - so the same script worked in testing and failed
-- from the launcher. SCRIPT_DIR is set by the host; the fallback keeps this
-- working on a host that does not set it.
local here = rawget(_G, "SCRIPT_DIR")
local api = dofile((here and (here .. "/") or "") .. "adapters/emuapi.lua").load()
local emu, frame, joypad, memory  = api.emu, api.frame, api.joypad, api.memory
local savestate, gui, ui          = api.savestate, api.gui, api.ui
local movie, sound                = api.movie, api.sound

--- Anything the panel shows is captured here and drawn later.
local live = {
    buttons = {}, downs = {}, axes = {},
    pc = 0, r15 = 0, ram = 0, snd = 0,
    watchHits = 0, watchPolls = 0,
    steps = 0,
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
emu.registerbefore(function()
    -- registerbefore runs ahead of registerafter, so input driven here is in
    -- place before anything below reads it back.
    if opt.drive and buttonNames then
        -- A visible pattern rather than a constant hold: alternate two buttons
        -- every 30 frames so you can see it in the button list.
        local a = buttonNames[1]
        local b = buttonNames[2] or a
        local phase = (frame.confirmed() % 60) < 30
        joypad.set(1, { [a] = phase, [b] = not phase })
    end
end)

emu.registerafter(function()
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

gui.register(function()
    -- Content overlay, in GAME pixels: it tracks the picture, not the window.
    if opt.showOverlay then
        local w, h = 640, 480
        gui.box(0, 0, w - 1, h - 1, 0xFF00FF00)                  -- game bounds
        gui.line(w / 2 - 12, h / 2, w / 2 + 12, h / 2, 0xFFFFFFFF)  -- crosshair
        gui.line(w / 2, h / 2 - 12, w / 2, h / 2 + 12, 0xFFFFFFFF)
        -- a marker that sweeps, so a frozen overlay is obvious at a glance
        local x = (frame.confirmed() * 2) % w
        gui.box(x, h - 16, x + 8, h - 8, 0xFF00FFFF)
    end

    ui.SetNextWindowPos(8, 8)
    ui.SetNextWindowSize(360, 470)
    if ui.Begin("emuapi tour") then
        ui.Text(string.format("%s  api %d  %s",
            api.host, emu.apiversion(), emu.romname() or "?"))
        ui.Text(string.format("%dx%d  online=%s replay=%s rollback=%s",
            emu.screenwidth(), emu.screenheight(),
            tostring(emu.isonline()), tostring(emu.isreplay()),
            tostring(emu.isrollback())))
        ui.Separator()

        ui.Text(string.format("frame  %d   confirmed %d   resim %d",
            frame.count(), frame.confirmed(), frame.resimsteps()))
        ui.Text(string.format("steps  %d  (emu.run + frameadvance)", live.steps))
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
