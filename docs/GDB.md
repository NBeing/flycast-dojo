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

### The guest is in a normal loop, not waiting on hardware

`[MEASURED 2026-09-11]` with the SH4 stub up (see below), the guest PC sampled
eight times while the movie is stalled at frame 9949:

    8c191c90  8c1921c4  8c191cc0  8c191c92
    8c191cca  8c18f7fa  8c18f7fa  8c191cc0

A ~2.5 KB range, so it is looping in real game code rather than off in the
weeds. Disassembling it:

    8c191c90:  mov.l 0x8c191cfc,r1   ! 8c32ea9c
    8c191c92:  mov.l @r1,r2
    8c191c94:  tst   r2,r2
    8c191c96:  bt    0x8c191cac
    ...
    8c18f7f6:  jsr   @r1
    8c18f7fe:  rts

**Every address it reads is main RAM** (`0x8c32…`), not a hardware register
(`0xa05f…`, `0xff…`). So it is not polling maple, or any device, for a flag that
never arrives. It is walking a structure in its own memory through an indirect
call and never finishing.

That points at the restored state being INTERNALLY INCONSISTENT rather than
mis-restored: `STATE VERIFY: idempotent OK` proves save->load->save is
byte-stable, which is a claim about the serialiser and says nothing about
whether the machine that was saved made sense. A task list or dispatch chain
that is walked forever fits the evidence.

Identifying the structure needs either game knowledge or a RAM diff against a
state that loads cleanly - not another log line.

### Turning the SH4 debugger on: it was never the option

`[CORRECTED 2026-09-11]` an earlier note here said `config::GDB` was not picking
up `-config config:Debug.GDBEnabled=yes`. That was wrong, and gdb settled it in
one run by printing the option object itself:

    (gdb) print config::GDB
    $1 = {... section = "config", name = "Debug.GDBEnabled",
          value = true, defaultValue = false, overridden = true ...}

The plumbing was fine all along. **The feature was not compiled in:**

    ENABLE_GDB_SERVER:BOOL=OFF          # in build-dojo7/CMakeCache.txt

    cmake -S . -B build-dojo7 -DENABLE_GDB_SERVER=ON

That is now ON. It adds `core/debug/gdb_server.cpp` and a `GDB_SERVER` define,
and costs nothing at runtime while `Debug.GDBEnabled` is off - which for a tree
whose whole subject is re-recording is worth having permanently.

**And the instrument that produced the wrong answer is worth naming**, because
it is a shape this project keeps hitting: `info functions debugger::init |
grep -c "init"` returned 1 and was read as "present". The 1 was gdb's own header
line, `All functions matching regular expression "debugger::init":`, which
contains "init". The right test is the one that failed loudly later -
`break debugger::init` answering **`Function "debugger::init" not defined.`**

### Talking to the guest

    (Xvfb :209 &) ; sleep 2
    DISPLAY=:209 … flycast … -config config:Debug.GDBEnabled=yes \
                             -config config:Debug.GDBWaitForConnection=no &

    gdb-multiarch -batch -nx \
      -ex "set architecture sh4" \
      -ex "target remote localhost:3263" \
      -ex "printf \"pc = %08x\\n\", \$pc" -ex "stepi" … \
      -ex "detach"

`gdb-multiarch` is required - the stock `gdb` here answers *Undefined item:
"sh4"*. `WaitForConnection=no` matters: otherwise the emulator blocks at boot
waiting for a debugger, which is the opposite of what a headless reproduction
wants. Port **3263** (`gdb_server.h: DEFAULT_PORT`).

### What was already known before the stub worked

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

### Interrupting a wedge, when there is nothing to break ON

`[MEASURED 2026-09-11]` The recipe above needs a `break <where>`, which assumes
you know where. For "it is stuck and I do not know where", send `SIGINT` to
**gdb** after a delay and let the remaining `-ex` commands run:

    gdb -batch -nx -ex "set pagination off" -ex "set confirm off" \
      -ex "handle SIGSEGV nostop noprint pass" \
      -ex "handle SIGBUS  nostop noprint pass" \
      -ex "run" -ex "thread apply all bt 14" --args ./build-dojo7/flycast ... &
    GP=$!; sleep 14; kill -INT $GP

`run` returns when the interrupt stops the inferior, and the batch continues to
the backtrace. Nothing is typed and no display is touched.

**Attaching to a running flycast does not work on this machine.** `[MEASURED
2026-09-11]` `/proc/sys/kernel/yama/ptrace_scope` is **1**, so `gdb -p <pid>`
against a process gdb did not start produces no stacks and no obvious error -
it looks exactly like "gdb found nothing to say". Launch under gdb instead; do
not conclude anything from an empty `thread apply all bt`.

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

Four things, all invisible to the tests that were passing:

1. **The savestate wedge, root-caused.** The `[OPEN]` that had blocked every
   re-record test across several sessions. Four gdb readings closed it, each
   cheap and each impossible to get from source:

   - `thread apply all bt` after an interrupt: **Thread 1 is the SH4**
     (`Emulator::render -> recSh4_Run`), which is why the first watchdog was
     asleep - see item 1 below.
   - `p sch_list` stopped just after a load: `sch_list[vblank_schid]` has
     **`end = -1`** - disabled - while `start` holds a real value. `[SOURCE]`
     `sh4_sched_remaining` returns `end - now` as a u32, so a disabled event is
     never selected, `spg_line_sched` never runs, and only `spg_line_sched` can
     re-arm `spg_line_sched`.
   - `break rend_vblank` then `p sch_list[vblank_schid].end`: **-1 while inside
     the callback**, with AICA's entry live at the same instant. The stack -
     `rend_vblank <- spg_line_sched <- handle_cb <- sh4_sched_tick` - is the
     whole explanation: `handle_cb` clears the deadline BEFORE invoking the
     callback and re-arms only after, so anything that saves a state from a
     vblank hook records a machine with its raster switched off.
   - `dojo:SchedTrace` (added because of this) then settled the *fix*: the
     candidate save-side repair round-tripped the field **identically**, which
     ruled it out and pointed at `sh4_sched_next` instead. Three wrong guesses
     preceded that one number.

   Why nothing else could see it: the bytes are perfect. `STATE VERIFY:
   idempotent OK (27793147 bytes round-trip)` on a state that cannot run. Every
   fidelity check in the tree is a claim about the serializer, and the
   serializer was right.


2. **A watchdog asleep on the thread it was watching.** `core/liveness.cpp` was
   written to report a savestate that restores bytes but leaves a machine that
   does not run. Its eight unit claims were green, it compiled, it linked, and
   against the exact defect it was built for it emitted **no verdict at all** -
   not Alive, not Dead, nothing. The stack answered it in one interrupt:

       Emulator::render -> Emulator::run -> recSh4_Run -> X64Dynarec::mainloop

   on **Thread 1**. With single-threaded rendering the UI loop runs the guest
   inline until it yields a frame, so `mainui_rend_frame()` - where the check
   had been put - never returns once the guest stops finishing frames. The
   check was arming on one thread and being polled from the thread the failure
   suspends. It now has its own thread (`liveness::startWatchdog`).

   Two further things only the stack could have told you: `ps -L` showed the
   main thread at 98% CPU, so the usual "blocked thread sits at 0%" reasoning
   said it was *working*; and with `ThreadedRendering=yes` the UI thread is
   separate and the check fires correctly, so the bug was invisible in half the
   configurations.

3. **A template that was never a template.** `dojocfg::writeThrough`'s `const T& v`
   was never read, so every instantiation was byte-identical, the linker folded
   them, and gdb reported a `std::string` call arriving in `writeThrough<bool>`.
   No compiler warning (unused *template* parameters draw none) and no test
   failure (behaviour identical either way). Fixed in `d3c372bf0`.
4. **A `.flyr` is not a header followed by frames at all.** gdb read 11520 movie
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

The last two are the same lesson from opposite directions: source-reading tells
you intent, the built artifact tells you truth. The first is a third direction -
a unit test tells you a rule is right, and says nothing about whether the rule
is ever consulted.
