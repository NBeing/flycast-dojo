# Debugging flycast-dojo with gdb

Everything here was run against this tree on 2026-09-08 and is marked
`[MEASURED]` where it is a number rather than a description. Nothing in this
file is generic gdb advice: if an idiom is written down, it was watched working
on this emulator, and the limits are the ones actually hit rather than the ones
usually warned about.

The house method is debug-by-instrumentation (CLAUDE.md), and that does not
change. gdb is for the questions instrumentation answers badly:

- **A crash.** A stack trace names the caller; a `NOTICE_LOG` requires already
  guessing where to put it.
- **"What did the linker actually build?"** Source tells you intent. gdb tells
  you what exists. It found dead code here within one run (see *What it has
  found*, below).
- **A one-off runtime question** where adding a trace, rebuilding and re-running
  costs more than just looking.

It is **not** for sabotage. A sabotage arm must go through the compiler, because
the claim is about the shipped artifact; mutating a live process proves something
weaker.

## It works today, with no rebuild

    gdb 15.1 (Ubuntu 24.04)
    build-dojo7: CMAKE_BUILD_TYPE=RelWithDebInfo, "with debug_info", not stripped

So `break`, `bt`, `print` and pretty-printers all work against `build-dojo7/flycast`
as it stands. No debug build is needed for the idioms below.

## The one line you cannot omit

**`handle SIGSEGV nostop noprint pass`** — and the same for `SIGBUS`.

flycast installs its own `fault_handler` for both (`core/linux/common.cpp:91-98`);
faulting on memory is part of how the emulator *works*, not a bug. gdb stops on
those signals by default, so without this line the debugger halts inside normal
operation and never reaches anything you asked for.

`[MEASURED]` Same conditional breakpoint, same clip, only this line differing:

| | result |
|---|---|
| with `handle SIGSEGV …` | stopped at the requested frame in **5 s** |
| without | `received signal SIGSEGV` at **frame 0**, breakpoint never reached |

That is the whole difference between "gdb is unusable on this project" and a
five-second answer. It is written first because two sessions were lost to it.

## The headless recipe

    (Xvfb :83 -screen 0 1280x1024x24 &) ; sleep 2
    DISPLAY=:83 XDG_CONFIG_HOME=$WORK/config \
    gdb -batch -nx \
      -ex "set pagination off" -ex "set confirm off" \
      -ex "handle SIGSEGV nostop noprint pass" \
      -ex "handle SIGBUS  nostop noprint pass" \
      -ex "break <where>" \
      -ex "run" \
      -ex "<inspect>" \
      -ex "kill" \
      --args ./build-dojo7/flycast \
        -config dojo:Replay=yes -config "dojo:ReplayFilename=$WORK/clip/clip.flyr" \
        -config dojo:AutoSeekState=0 \
        -config dojo:AutoLoadNetState=no -config dojo:Transmitting=no \
        -config dojo:Receiving=no \
        -config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
        "$ROM"

`-batch` needs no interaction, so this steals no display, keyboard or mouse and
fits the existing harness discipline. `-nx` ignores any `~/.gdbinit`, so the run
is the same for everyone.

Two flags are load-bearing and are the same ones `scripts/testrun.sh` passes:

- **`AutoLoadNetState=no`, `Transmitting=no`, `Receiving=no`.** Without them the
  boot tries to *download* a netplay savestate
  (`dojo_file.cpp:76`, "save url: …/NoBGM_VMU.state.net") and blocks there.
  `[MEASURED]` A run missing these reached nothing in 60 s **with gdb entirely
  absent** — the log's last line was `Remote file not found`. This is the
  netplay leftover CLAUDE.md warns "blocked headless boots"; it is still live.
- **`UiIni=no`**, so the run cannot overwrite your dock layout. Any new harness
  that launches flycast.exe must pass it.

> **When gdb looks broken, run the identical config without gdb first.**
> Both "gdb is slow here" hypotheses formed while chasing the above were wrong,
> and the un-debugged control found the real cause in one minute.

## What it found on 2026-09-11: the guest stops polling maple

The `[OPEN]` that blocks every re-record test (`docs/TEST-PLAN.md`) was carried
for two days as "a state load wedges the emulator". gdb narrowed it to something
much more specific, and every step is an idiom from this file.

**A conditional breakpoint on the movie clock, keyed on a counter the load
bumps.** `Dojo::LoadStateFrame` does `load_seq++`, so a breakpoint that is only
live *after* a load needs no timing at all:

    -ex "break Dojo::MapleApplyAction if dojo.load_seq > 0"

`[MEASURED]` It **never fires**. The load completes at 2.6 s, the movie reaches
frame 9949, and in the remaining ~117 s the maple poll that advances the movie
is not reached once. Combined with `ps` showing **197% CPU, main thread 99.4% in
`R`**, the guest is executing flat out and never performing another maple DMA.

### Three fixtures that measured nothing first

Worth recording, because each read plausibly:

- **`interrupt` does not work under `-batch`.** `run &` then `interrupt` answers
  *"Selected thread is running."* and every subsequent command fails. Nor does
  wrapping it in Python: `time.sleep()` blocks gdb's event loop, so the stop
  event is never processed. **A breakpoint stops synchronously; use one.**
- **A conditional breakpoint on a per-BLOCK function is the performance trap
  this file says a per-FRAME one is not.** `break bm_GetCodeByVAddr if …` with an
  ignore count of 400 000 never arrived in 220 s. `Dojo::MapleApplyAction` runs
  once per frame and is fine; `bm_GetCodeByVAddr` runs thousands of times per
  frame and gdb round-trips on each.
- **An `ignore` count has to suit the clip.** `ignore 1 3000` on
  `Emulator::vblank` never fired — on a 60-frame clip, boot is ~1200 vblanks and
  the movie adds 60. The control failed identically, which is how it was caught:
  a control that fails the same way as the experiment is measuring neither.

### The next step, and what is already known about it

`p_sh4rcb->cntx.pc` is the guest PC (`Sh4cntx` is a macro for `sh4rcb.cntx`,
`core/hw/sh4/sh4_if.h`), but reading it from host gdb requires a stop, and the
question is *which loop* rather than one address.

flycast ships its own SH4 debugger - `core/debug/gdb_server.cpp`, default port
**3263**, enabled by `config::GDB` at `nullDC.cpp:110`. `[MEASURED 2026-09-11]`
it IS compiled into `build-dojo7/flycast` (`info functions debugger::init` finds
it), and `gdb-multiarch` on this machine does speak `sh4`. What did not work is
turning it on from the command line: `-config config:Debug.GDBEnabled=yes`
leaves port 3263 refusing connections, while the same `-config` syntax
demonstrably works for other options in the same section. So the remaining work
is *how that option is plumbed*, not whether the tool exists.

## Idioms that work

### Stop at a specific movie frame

The one that matters for a re-record tool. `Dojo::MapleApplyAction` runs once per
maple DMA per emulated frame, so it is the movie clock's own tick:

    -ex "break Dojo::MapleApplyAction if dojo.frame_number._M_i == 9940"

`[MEASURED]` Stops at exactly frame 9940 in **5 s** on an ~11.5k-frame clip,
including boot and the seek. A conditional breakpoint on a per-frame function is
often a performance trap; here it is not, and that is measured rather than
assumed.

`._M_i` is required: `frame_number` is `std::atomic<u32>` (`core/dojo/dojo.h:104`),
and `printf "%d", dojo.frame_number` fails with *"Value can't be converted to
integer"*.

### Read the movie

Pretty-printers **do** load — but only once the process is running, because they
come from the objfile. `info pretty-printer` against the binary alone lists none,
which is misleading rather than informative.

    (gdb) print dojo.session_inputs
    $1 = std::map with 11520 elements = {[0] = std::vector of length 24, ...}

    (gdb) print dojo.session_inputs._M_t._M_impl._M_node_count
    $2 = 11520

The raw member is worth knowing anyway: it is cheap, needs no printer, and works
in `printf`. Length 24 is the two ports' 12-byte `FrameInputs` — the frame record
on disk is 28 bytes because it also carries the 4-byte frame number.

### Ask the binary questions without running it

Fast, and needs no display or ROM:

    gdb -batch -ex "info functions Maple.*Action" build-dojo7/flycast
    gdb -batch -ex "info variables dojo"          build-dojo7/flycast
    gdb -batch -ex "ptype /o DojoSession"         build-dojo7/flycast

Note `info functions movie::` returns **nothing** — those helpers are header-only
inline (`core/dojo/movie.h`), so there is no symbol to break on. Break on the
caller instead.

## Limits, all of them hit rather than anticipated

- **The dynarec truncates backtraces.** From the emulation thread a stack reads
  `MapleApplyAction ← maple_DoDma ← SH4_TCB ← ?? ()` and stops. JIT-generated
  code carries no unwind tables, so gdb cannot see past it. Permanent, not a
  misconfiguration. Frames *above* the JIT are fine, which is usually where the
  answer is.
- **`RelWithDebInfo` optimises arguments away.** A real argument printed as
  `v=<optimized out>` in the first session. If a specific value matters, either
  read it from a caller frame or build that TU unoptimised.
- **`rr` is not installed**, so no reverse execution today. Worth knowing before
  wanting it: `rr` and gdb's own `record` mode both handle JIT/self-modifying
  code poorly, and this is a dynarec emulator. Treat "record and run backwards
  through a desync" as **untested**, not as available.
- **You cannot attach to a flycast you launched yourself.** `ptrace_scope` is `1`
  on this machine, so `gdb -p <pid>` only works on a descendant of the gdb
  process. Launch under gdb, or lower the sysctl deliberately.
- **Software watchpoints are not a region tool.** x86 hardware watchpoints cover
  a handful of bytes; watching a memory *region* means breaking on the write
  function instead. Relevant to the savestate-coverage work
  (`docs/STATE-COVERAGE.md`), where the question is "who wrote this".

## What it has found here

Two things in its first session, both invisible to the tests that were passing:

1. **A template that was never a template.** `dojocfg::writeThrough`'s `const T& v`
   was never read, so every instantiation was byte-identical, the linker folded
   them, and gdb reported a `std::string` call arriving in `writeThrough<bool>`.
   No compiler warning (unused *template* parameters draw none) and no test
   failure (behaviour identical either way). Fixed in `d3c372bf0`.
2. **A `.flyr` is not a header followed by frames at all.** gdb read 11520 movie
   keys from a 324,175-byte clip, which did not divide the way
   `scripts/testrun.sh` assumed. Chasing that discrepancy is what turned up the
   real layout: a stream of messages, each 12 bytes of header plus a body, with
   frames arriving in 120-frame batches. This clip is one 79-byte
   SPECTATE_START message plus 96 batches of 3376, summing to exactly 324,175.
   `testrun.sh` computed `frames = (size - 93) / 28` and reported 11574.

   My own first pass was wrong the same way: dividing the leftover by 28 gave a
   "1615-byte header", which is what you get by assuming flat records when the
   bytes are actually framing. The fix is `scripts/flyrframes.sh`, which parses
   the stream and counts DISTINCT frame numbers - a re-record appends override
   batches, so counting records inflates exactly the clips edited most.

Both are the same lesson from opposite directions: source-reading tells you
intent, the built artifact tells you truth.
