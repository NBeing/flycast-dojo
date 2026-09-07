# The unified build

Branch `dojo7`, based on **dojo-7-preview4**, carrying both forks' work in one
binary. It **builds and runs**.

`[CORRECTED 2026-09-07]` This file opened "this repo is a scratch clone". It is
not one any more: `dojo7` is a branch in `NBeing/flycast-dojo`, pushed, and it
is the checked-out branch of `~/dev/flycast-dojo` — home base.

## How to read the claims here

Borrowed from `~/dev/anita/nbneo-rr/CLAUDE.md`, because this document has
already had to retract several things:

- **`[MEASURED <date>]`** — a number this work produced, with the command or
  log line that produced it. Trust it to the extent you trust that command.
- **`[SOURCE]`** — read out of code in this checkout. **The quote is the
  citation and the line number is a hint**, because line numbers rot.
- **`[CORRECTED <date>]`** — this document previously claimed something else.
  The old claim is shown, not silently replaced.
- **Unmarked** — reasoning, not evidence. Treat accordingly.

Nothing here is `[TRACE]`-grade: there is no golden trace for this emulator,
and the determinism work is what would eventually produce one.

```sh
cd ~/dev/flycast-dojo7-draft
ninja -C build -j 10
./build/flycast ~/dev/davids_fly/NoBGM_VMU.cdi
```

`[MEASURED 2026-09-07]` Boot log from the built binary — not a compile check:

```
emulator.cpp:57        Game ID is [T1212N]
dojo/tas_ruler.cpp:45  TAS SKIPMAP: frame 0 rate 0 count 0
dojo/replay.cpp:409    TAS: savestate folder -> .../replays/NoBGM_VMU/
                       2026-09-07T02_23_46Z (new recording)
```

It created a clip folder.

## Branches

| branch | what |
|---|---|
| `sync/dojo7-lua` | your Lua/emuapi programme migrated onto dojo-7 |
| `sync/dojo7-rerecord` | + David's full re-record pipeline and TAS modules |

## The finding that made this work

**Choosing the base correctly turned the hardest remaining item into a file
copy.**

David's fork is dojo-7 based. So on this branch his re-record work is a
**same-lineage diff against dojo-7's own dojo layer** — no renames, no API
drift, no adapter:

`[MEASURED 2026-09-07]` — `diff <(tr -d '\r' < david/$f) <(tr -d '\r' < $f) | grep -c "^[<>]"`:

```
dojo.cpp     1195 -> 2896    (+1721 differing lines)
dojo.h        180 ->  389    (+209)
replay.cpp    395 ->  654    (+275)
replay.h       40 ->   55    (+15)
oslib.h/cpp                  (+286)
```

On the `video-recording` base the same work needed a `core/tas/tas_host.h`
adapter to bridge renamed members, and even then `dojo.cpp` could not come
across at all. Here it is `cp`.

Total friction for the entire re-record pipeline: **two undeclared symbols**
`[MEASURED 2026-09-07]`.

`[CORRECTED 2026-09-07]` That figure was true of the *build* and hid a
functional gap: `Dojo::SaveStateFrame` / `LoadStateFrame` came across in
`dojo.cpp` but their only callers live in `nullDC.cpp`, which was only partly
ported. They compiled, linked, and were **unreachable** — so savestates carried
no `.frame` sidecar and a seek had nothing to land on. A clean build is not
evidence that a feature is wired.

## What is stubbed, and why it is honest

`gui_locked_ranges()` returns an empty list. It is the piano roll's
locked-frame-ranges feature, and `dojo.cpp` consults it before applying an edit.
Empty means *nothing is locked*, which is the correct answer when there is no
piano roll to lock anything — the pre-editor behaviour, not a fudge.

`flycast.video.*` and `flycast.replay.*` are `#if 0`'d in `lua.cpp` with written
reasons. Video needs the render hooks whose files dojo-7 deleted
(`core/wsi/{wgl,xgl}.cpp`, `core/rend/gui_settings.*`); replay calls a
`DojoSession` API dojo-7 restructured into a `Replay` class. Guarded rather than
half-adapted, because a binding that misreports recording state is worse than
one that admits it is absent — emuapi's own failure tier 3.

Everything emuapi actually binds is intact: memory, input, savestates, frame
counters, `ui.*`.

## Video capture — ported and verified

`flycast.video.*` is live. Driven from Lua during MvC2 attract mode:

```
mjpeg      1679 frames        -> 27.983 s
pcm_s16le  1,233,920 samples  -> 27.983 s     exact, to the sample
mean_volume -32.9 dB   max_volume -12.0 dB    real dynamic range
```

An extracted mid-capture frame shows a hyper combo with correct colours and
orientation - no channel swizzle, no vertical flip.

An earlier boot-window capture recorded digital silence (-91 dB), which was the
ROM being a NoBGM build during REIOS boot rather than a broken tap. The
gameplay capture settles it: **the audio tap works**, relocated unchanged from
`core/oslib/audiostream.cpp` to dojo-7's `core/audio/audiostream.cpp`.

Not ported: the **toolbar camera button**. It is entangled with dojo-7's
different toolbar layout maths and capture is fully reachable without it.

## What is NOT here

- **The determinism work.** It lives on `~/dev/flycast-sync-draft`
  (`sync/03-david-port`) against the `video-recording` base. It should be
  re-applied here — and one item changes: dojo-7 **has** `config::Sh4Clock`, so
  the overclock pin is now needed, and upstream's guard (`f8d5517b8`) covers
  GGPO only. See `DETERMINISM.md` there.
- **`dojo_gui.cpp`** (21,877 lines) — the whole studio UI, including the piano
  roll. Not attempted.
- **The video capture stack.** `avi_dump` compiles because `dojo.cpp` calls
  `avi_toggle_recording`, but its DX9/DX11 readback is inert on Linux. Your
  `video_recorder` (GL/Vulkan/DX11/DX9, async) is the better one and its hooks
  need re-placing against dojo-7's restructured WSI layer.

## Submodules and Vulkan — FIXED

Originally built `-DUSE_VULKAN=OFF` against submodules copied from the older
base. Both are now correct.

Every submodule is checked out at **dojo-7's own pinned commit**:

```
f461d91cd  core/deps/SDL
85c2334e9  core/deps/Vulkan-Headers
6eb62e151  core/deps/VulkanMemoryAllocator
1ab24bcc8  core/deps/breakpad
7239eab39  core/deps/libchdr
c19931b48  core/deps/luabridge
```

Spout (Windows), Syphon (macOS), oboe (Android) and libzip (optional) are left
uninitialised; CMake configures without them on Linux.

**A real bug was fixed doing this.** dojo-7 does **not** have glslang as a
submodule — it *vendors* it as 2,796 regular tracked files, which is what its
HEAD commit ("add cstdint for gcc 15 support, glslang") is about. Copying the
older fork's glslang over it had clobbered vendored source. Restored with
`git checkout -- core/deps/glslang`; the tree is now pristine there.

Rebuilt with `-DUSE_VULKAN=ON`: **ninja exit 0, zero errors**, 12 Vulkan
objects linked. Verified selecting Vulkan at runtime, not merely linking:

```
$ ./build/flycast -config config:pvr.rend=4 NoBGM_VMU.cdi
rend/vulkan/vulkan_context.cpp:237  Vulkan API 1.1. Device NVIDIA GeForce RTX 2060
rend/vulkan/vulkan_renderer.cpp:30  VulkanRenderer::Init
emulator.cpp:57                     Game ID is [T1212N]
```

Note the `-config` syntax is `section:key=value`; `pvr.rend=4` without the
`config:` prefix is silently ignored and you get OpenGL.

Submodule `.git` pointers are real now, so `git submodule status` works.

## Suggested next steps

1. ~~Check out dojo-7's real submodule commits, rebuild with Vulkan on.~~ **Done.**
2. Re-apply the determinism branch here, adding the `Sh4Clock` pin — dojo-7
   inherits flyinghead's overclock slider and its GGPO-only guard
   (`f8d5517b8`), so the pin that was unnecessary on the older base is
   necessary on this one.
3. Re-place the video capture hooks against dojo-7's WSI layer, then unguard
   `flycast.video.*`.
4. Then, and only then, look at `dojo_gui.cpp` — with the engine already
   running, the GUI can be rebuilt panel by panel on emuapi `ui.*` rather than
   ported wholesale.
