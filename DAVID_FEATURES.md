# Flycast Dojo — Added Features

Reference for features added on the `video-recording` branch, why they are
built the way they are, and exactly what has and has not been verified.

Branch: `video-recording` · Fork: `NBeing/flycast-dojo` · Base: `d0e47e572`

---

## 1. Video capture (AVI export)

Records **the frame the user actually sees** — game image, OSD, and anything
Lua draws through its `overlay` callback — by reading the presented
framebuffer immediately before the buffer swap and piping raw frames to
`ffmpeg`.

### Using it

Three interchangeable controls; all drive the same state.

**Toolbar** (fastest). A camera icon sits between the Replays and Settings
buttons in the top bar. Click to start; the icon becomes a red stop button
while running, and its tooltip shows live frame and drop counts.

**Settings.** Settings → Video → *Video Recording*, with start/stop, counters,
and the output path.

**Lua.**

```lua
flycast.video.startRecording("")            -- "" = timestamped file in the data folder
flycast.video.startRecording("/tmp/x.avi")  -- explicit path
flycast.video.stopRecording()
flycast.video.toggleRecording()
flycast.video.isRecording()                 -- bool
flycast.video.recordingPath()               -- string
```

Output defaults to `<data folder>/flycast-YYYYMMDD-HHMMSS.avi`.

**Requires `ffmpeg` on `PATH`.** If it is missing, recording fails with a
notification instead of crashing.

### Two behaviours that surprise people

- `isRecording()` returns **false** for one frame after `startRecording()`.
  The request is deferred to the renderer, which is the only place that knows
  the framebuffer dimensions. It flips true on the next frame.
- **Resizing the window stops the recording.** The encoder is opened at a
  fixed frame size; a resize would desynchronise every subsequent frame, so it
  stops with a log line rather than emitting garbage.

### Configuration

`emu.cfg`, section `[record]`. No rebuild needed.

| Key | Default | Notes |
|---|---|---|
| `ffmpeg` | `ffmpeg` | Binary name or full path |
| `codec` | `mjpeg` | `libx264` gives far smaller files for more CPU |
| `quality` | `3` | Passed as `-q:v`; lower is better |
| `fps` | `60` | Encoder frame rate |

### Why it hooks where it does

Lua does **not** draw into the game's framebuffer. `lua::overlay()`
(`core/lua/lua.cpp:119`) fires an event callback, and the exposed API is ImGui
wrappers (`lua.cpp:394-475`). All of that lands in ImGui's draw data, which
renders into the **default framebuffer / back buffer** — not the offscreen
game FBO.

So capture must read the default framebuffer, after ImGui renders and before
the swap. Every backend hooks that exact window:

| Backend | Hook | Reads |
|---|---|---|
| OpenGL | `do_swap_capture()`, top of each WSI `swap()` | framebuffer 0 |
| Vulkan | `DoSwapCapture()`, before `vkQueuePresentKHR` | current swapchain image |
| DX11 | `DoSwapCapture()`, before `IDXGISwapChain::Present` | back buffer |
| DX9 | `DoSwapCapture()`, before `IDirect3DDevice9::Present` | back buffer |

There was already a raw-frame dump at `core/rend/gles/gles.cpp` inside
`#ifdef TEST_AUTOMATION`, but it read the offscreen game FBO (`gl.ofbo`), so
it would never have contained overlays.

### Design notes

**Readback is asynchronous everywhere it can be.** A synchronous readback
before present stalls the pipeline, and on a rollback netplay build perturbing
frame pacing is worse than an imperfect capture. Each backend defers by one
frame:

- OpenGL — ring of 3 pixel buffer objects; sync fallback on GLES2.
- Vulkan — blit into a linear host-visible image, collected next frame on a fence.
- DX11 — two staging textures; copy into one, map the other.
- **DX9 — no async path exists.** `GetRenderTargetData` is synchronous and
  `LockRect` blocks on it, so DX9 costs a stall per frame. Prefer DX11 on
  Windows while recording.

**Frames are dropped, never blocked on.** A bounded queue (6 frames) feeds a
writer thread. If the encoder falls behind, the newest frame is dropped and
counted rather than stalling the render thread.

**Each backend submits its native pixel layout** and ffmpeg converts —
`rgb24` for GL, `rgba`/`bgra` for Vulkan and D3D depending on the swapchain
format. No CPU swizzling.

**Rows are copied individually** because row pitch routinely exceeds the
visible width, and `GL_PACK_ALIGNMENT` is forced to 1 — without it, any width
that is not a multiple of 4 shears the image diagonally.

**Staging is allocated lazily, not at start.** Swapchains get rebuilt
routinely (the vsync mode settles on the first present; D3D9 loses resources
on device reset), and each rebuild tears staging down. Allocating only at
start produced a recording that died after one frame.

### Files

| File | Role |
|---|---|
| `core/rend/video_recorder.{h,cpp}` | Renderer-agnostic core: encoder pipe, writer thread, queue. No GL/D3D/VK. |
| `core/rend/gles/gles.cpp` | OpenGL capture (`do_swap_capture`) |
| `core/wsi/gl_context.h` + `core/wsi/{sdl,xgl,wgl,egl,osx}.cpp` | OpenGL hook declaration and call sites |
| `core/rend/vulkan/vulkan_context.{h,cpp}` | Vulkan capture |
| `core/rend/dx11/dx11context.{h,cpp}` | DX11 capture |
| `core/rend/dx9/dxcontext.{h,cpp}` | DX9 capture |
| `core/rend/gui.cpp` | Toolbar toggle |
| `core/rend/gui_settings.cpp` | Settings panel |
| `core/lua/lua.cpp` | `flycast.video.*` bindings |
| `core/rend/mainui.cpp` | Stops capture at shutdown so quitting leaves a playable file |

### Verification status — read this before trusting it

| Item | Status | Evidence |
|---|---|---|
| OpenGL capture | **Verified** | 734-frame 640×480 AVI; overlay content confirmed appearing/disappearing frame-by-frame (4559 px delta on hover) |
| Vulkan capture | **Verified** | 772-frame AVI on lavapipe; correct channel order and orientation; same hover delta |
| DX11 capture | **Not run** | Type-checked only, cross-compiled with `x86_64-w64-mingw32-g++` against real `d3d11.h` |
| DX9 capture | **Not run** | Type-checked only, same method against real `d3d9.h` |
| Toolbar button, both states | **Verified** | Screenshot, idle and recording |
| Shutdown flush | **Verified** | SIGTERM mid-capture leaves a playable file |
| Lua bindings start capture | **Verified** | Driven from `flycast.lua` |
| **Lua `overlay` content in a recording** | **NOT verified** | `lua::overlay()` only fires in-game and no ROM was available. Proven only for ImGui content on the identical draw path into the identical buffer — one inference short of a direct test. |

For the D3D backends, "type-checked" means the code was confirmed to be
*inside* the compiled region (by injecting a deliberate error and seeing it
surface), not merely that a guarded block was skipped. Every API call and type
is validated. Runtime behaviour is not.

### Limitations

- **No audio.** Video only — the encoder is fed raw video frames and nothing
  else. The AVI is silent.
- **Lua overlays do not draw during online netplay** at all, so they cannot be
  recorded there. `core/rend/gui.cpp` gates `lua::overlay()` on
  `!settings.network.online`. This is a pre-existing behaviour, not something
  capture introduced.
- Window resize stops the recording.
- Constant frame rate is assumed. Fast-forward, pausing and rollback all break
  that assumption, so long recordings can drift.
- DX9 stalls once per frame while recording.

---

## 2. Windows build scripts

`shell/windows/setup-and-build.ps1` — run in plain PowerShell on a machine
with nothing installed. Installs MSYS2 unattended, updates packages, clones
with submodules, builds, and stages a double-clickable folder.

```powershell
.\shell\windows\setup-and-build.ps1                              # fresh clone
.\shell\windows\setup-and-build.ps1 -SourceDir C:\dev\flycast-dojo
.\shell\windows\setup-and-build.ps1 -SkipBuild                   # toolchain only
```

`shell/windows/build-mingw64.sh` — the day-to-day half, run inside the MSYS2
**MINGW64** shell.

```bash
./shell/windows/build-mingw64.sh            # build the repo it lives in
./shell/windows/build-mingw64.sh --clone    # clone fresh first
./shell/windows/build-mingw64.sh --clean    # wipe build dir
./shell/windows/build-mingw64.sh --debug
```

Both are idempotent and safe to re-run to repair a half-finished setup.

### What they guard against

- **Wrong shell.** MSYS2 ships MSYS, MINGW64, UCRT64 and CLANG64 terminals;
  only MINGW64 has the toolchain that builds a native 64-bit Windows exe. The
  script refuses to run elsewhere with a fix-it message rather than failing
  later with a confusing toolchain error. This is the most common failure.
- **Missing or emptied submodules.** It checks
  `core/deps/breakpad/CMakeLists.txt` directly, because an
  emptied-but-*initialised* submodule reports clean in `git submodule status`
  yet fails configure.
- **The pacman self-update.** A core update can replace pacman and
  `msys2-runtime` and kill the shell mid-run, so the update runs twice: once
  tolerantly, once strictly.
- **Missing DLLs.** The exe finds its DLLs on `PATH` inside MINGW64 but not
  from Explorer, so runtime DLLs are staged beside the binary, with `ldd` used
  to catch anything the hardcoded list misses.

### Notes

- **Do not use WSL.** It produces Linux ELF binaries, not a Windows `.exe`,
  and has no USB HID passthrough — controllers would not work. Cross-compiling
  from WSL does yield a real exe but requires hand-building SDL2, Lua, asio,
  breakpad and OpenSSL for the mingw target, which is what MSYS2 exists to
  avoid.
- **No DirectX SDK is needed.** `CMakeLists.txt` wraps the DXSDK include and
  library paths in `if(NOT MINGW)`; under MinGW it links mingw-w64's own
  bundled `d3d9`/`d3dx9`. CI still has a "Download DX2010" step but never sets
  `DXSDK_DIR`, so it is dead weight for the MinGW job. Ignore any guide that
  says otherwise — those describe the MSVC path.

### Verification status

| Item | Status |
|---|---|
| `build-mingw64.sh` syntax, arg parsing, wrong-shell guard | Verified |
| `build-mingw64.sh` end-to-end on Windows | **Not run** |
| `setup-and-build.ps1` | **Not run** — no Windows host or PowerShell interpreter available |

---

## Commits

```
63c0b876d  feat(gui): record toggle in the main toolbar
60ddc6ad6  build(windows): scripted MSYS2 setup and MinGW64 build
14fc7b2f5  feat(rend): video capture for DirectX 11 and DirectX 9
63a6f0b2d  feat(rend): video capture for the Vulkan backend
9092ba7a1  feat(rend): capture presented frames to video via ffmpeg (OpenGL)
```
