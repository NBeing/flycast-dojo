# The unified build

This repo is a scratch clone starting at **dojo-7-preview4**, carrying both
forks' work in one binary. It **builds and runs**.

```sh
cd ~/dev/flycast-dojo7-draft
ninja -C build -j 10
./build/flycast ~/dev/davids_fly/NoBGM_VMU.cdi
```

Boot log from the built binary — not a compile check:

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

Total friction for the entire re-record pipeline: **two undeclared symbols.**

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

## Caveats

Built with `-DUSE_VULKAN=OFF`. dojo-7 pins a newer VulkanMemoryAllocator that
exports a CMake target; the submodules here were copied from the older base
rather than checked out at dojo-7's pinned commits. **Fix the submodules before
trusting this build for anything but compilation** — they are the older fork's
versions throughout, which is fine for a syntax and link check and not fine for
a release.

Submodule `.git` pointers were removed, so `git submodule` does not work here.
They are listed in `.git/info/exclude`.

## Suggested next steps

1. Check out dojo-7's real submodule commits, rebuild with Vulkan on.
2. Re-apply the determinism branch here, adding the `Sh4Clock` pin.
3. Re-place the video capture hooks against dojo-7's WSI layer.
4. Then, and only then, look at `dojo_gui.cpp` — with the engine already
   running, the GUI can be rebuilt panel by panel on emuapi `ui.*` rather than
   ported wholesale.
