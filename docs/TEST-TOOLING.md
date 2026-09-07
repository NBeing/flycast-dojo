# Driving flycast-dojo for automated tests

Investigation, 2026-09-07. Read-only survey of this repo plus `~/dev/fbneo-rr`
and its sibling harness `~/dev/anita/lemalta/`. Nothing here was changed by the
investigation; every claim cites a file.

## Three corrections that change the plan

**1. Lua `print()` already writes to a per-line-flushed text file.**
`luaPrint` (`core/lua/lua.cpp:84-99`) calls `luaconsole::add()` *before*
`printf`, and `luaconsole::add` (`core/lua/lua_console.cpp:81-95`) appends to
`get_writable_config_path("flycast-lua.log")` and **fflushes every line**:

> "Flushed per line rather than buffered: the lines that matter most are the
> ones written immediately before whatever went wrong, and a buffered tail is
> exactly what a crash discards."

`~/.config/flycast-dojo/flycast-lua.log` exists right now. Prefixes: `"  "`
output, `"! "` error, `"# "` info, `"> "` input, with `=== session <ts> ===`
separators. **The assertion channel already exists** — the marker-file idiom
every harness hand-rolled was reinventing it. `[VERIFIED]` the file is present
and current.

**2. stdout is real but lossy.** `printf` at `lua.cpp:97` with no `fflush`, and
nothing calls `setvbuf` except under `TEST_AUTOMATION` (`core/nullDC.cpp:27-31`).
Redirected, stdout is 4 KB-buffered, and harnesses `kill` with SIGTERM, which
terminates without flushing. `core/linux/common.cpp:189` sets
`signal(SIGINT, exit)`, so **`kill -INT` recovers the output today**, no source
change.

**3. The NUL bytes have one source, ~20 lines of dead debug code.** Raw
`std::cout` of NUL-padded fixed-width strings from `MessageReader::ReadString`
(`core/dojo/dojo.cpp:2464-2468`), written at `dojo.cpp:2523,2525,2526,2536,2537`.
The ENGINE log is unaffected — `ConsoleListenerNix::Log` goes to stderr
(`core/log/ConsoleListenerNix.cpp:58`) and `FileLogListener` flushes per line
(`core/log/LogManager.cpp:46-52`). So the `grep -a` trap is deletable.

Also: **`dojo:UiIni=no` is a no-op here.** `core/rend/gui.cpp:140` sets
`io.IniFilename = NULL` unconditionally; the flag only exists in the TAS fork.

## Zero-source-change wins, do these first

- **B1. Sandbox the config dir.** `find_user_config_dir()` honours
  `XDG_CONFIG_HOME` (`core/linux-dist/main.cpp:121-123`), so
  `export XDG_CONFIG_HOME=$T/config` gives each run its own `emu.cfg`,
  mappings, `flycast.lua` and `flycast-lua.log`. The backup/restore dance
  disappears and input bindings become deterministic. Keep `XDG_DATA_HOME` real
  (BIOS/nvmem/replays live there, `main.cpp:263-292`).
- **B2. `-config config:LuaFileName=<name>` already works.**
  `core/cfg/option.cpp:263`; `-config` is applied before `cfgOpen()` and
  `Settings::load` (`nullDC.cpp:37-56`). It is a NAME under the config dir
  (`core/stdclass.cpp:58-72`), which B1 makes a non-issue.
- **B3. Take the engine log from a file:** `-config log:LogToFile=yes` writes
  `flycast.log` in the CWD, flushed per line, NUL-free
  (`LogManager.cpp:133-139`); `log:LogToConsole=no` silences stderr.
- **B4. Quit from the script** via `flycast.emulator.exit` (`lua.cpp`), but
  NEVER from `vblank` — `dc_exit` → `emu.stop()` joins the emulation thread
  (`core/emulator.cpp:706-712`). `[MEASURED]` the same hazard applies to pause.
- **B5. Poll for markers with a deadline**, as `scripts/tas-fork/test.ps1:102-130`
  already does. Never sleep-and-hope.
- **B6. Boot flags that must always be set:** `dojo:AutoLoadNetState=no
  Transmitting=no Receiving=no` (else boot blocks on a savestate download,
  `gui.cpp:571-580`) and `dojo:AutoSeekState=0` to unfreeze a replay
  (`core/dojo/replay.cpp:157-163`, `core/rend/mainui.cpp:123-129`).

## Source changes, ranked

**Tier 1 (~1 h each)**
- **A1** `fflush(stdout)` in `luaPrint`, or line-buffer stdout at startup.
- **A2** Delete/downgrade the ~20 raw `std::cout` sites — the entire cause of
  the binary-log trap. Highest value per line in the list.
- **A3** Accept a `.lua` positional argument; `core/cfg/cl.cpp:113-133` already
  sniffs extensions. This is what fbneo does
  (`src/burner/win32/main.cpp:973-978`).
- **A4** A `dojo:TestMode=yes` umbrella setting the safety flags together, so a
  new harness cannot forget one. Precedent: the `TEST_AUTOMATION` block at
  `nullDC.cpp:27-31`.

**Tier 2 (half a day each)**
- **A5** `flycast.test.quit(code)/fail(msg)/runFrames(n)` + a real exit code
  from `main()` (`core/linux-dist/main.cpp:381` returns a bare 0). `quit()` must
  defer to the main thread — see the deferred hook, which exists now.
- **A6** Formalise the Lua log as the results channel (below).
- **A7** `lua::overlay()` in `GuiState::Commands`, the one stream still blind.

**Tier 3** A control socket — note flycast already ships a GDB stub
(`core/debug/gdb_server.cpp`) so a blocking TCP client is precedent, but it is
instruction-level; don't drive tests through it. And do **not** revive
`TEST_AUTOMATION` (compile-time, GL-only, hardcoded paths, `die()` on timeout).

## Assertion channel: one recommendation

**Use `flycast-lua.log` with a whole-line marker and a SUMMARY contract.** Not
stdout (noisy, buffered, lost on the kill signal harnesses send), not exit codes
alone (a summary, not a channel), not ad-hoc marker files (one file per fact, no
ordering, no session boundary).

```lua
local pass, fail = 0, 0
local function check(name, cond, detail)
  if cond then pass = pass+1; print(("  PASS  %s %s"):format(name, detail or ""))
  else          fail = fail+1; print(("  FAIL  %s %s"):format(name, detail or "")) end
end
print(("SUMMARY: %d passed, %d failed"):format(pass, fail))
print("done")
```

Three rules that cost nothing and are load-bearing:
1. **Whole-line matching** for the completion marker, never substring
   (`lemalta.py:4295-4315` — a line reading "baseline done" truncated a run into
   a false pass).
2. **Exit 0 with no verdict is INCONCLUSIVE, not PASS**
   (`dev-scripts/spec_suite.sh:30-47`).
3. **Truncate the log before launch** — "a stale log masquerading as the current
   one is worse than no log, because it reads as evidence"
   (`lemalta.py:3917-3922`). B1 makes this automatic.

## Determinism, speed, hang detection

- **One test = one fresh process restored from a state blob.** Measured in
  `docs/SPIKE-machine-pool.md`: three processes agree exactly; in-process reuse
  drifts. Never run two tests in one process.
- **Snapshot once, fan out.** Deserialize is 7.7 ms against 79 ms to serialize,
  so booting per test is waste.
- **Warm up before measuring** — fbneo hardcodes `WARMUP = 400`
  (`dev-scripts/determinism_smoke.lua:41`); restoring from a state gives it free.
- **Deadline in the parent, frame budget in the script.** Collect evidence
  BEFORE killing (`lemalta.py:5180-5187`). Kill by process GROUP. Use SIGINT
  until A1 lands.
- **Refuse to run a test that cannot fail.** `lemalta.py:5014-5028`. Our own
  `scripts/replay-bindings-test.sh:24-27` discovered this independently: "no
  clip folder was created" is also what "the Lua never ran" looks like. Every
  test's first assertion is that it is running in the state it claims to test.

## Copy, don't reinvent

lemalta is a **sibling** repo (`~/dev/anita/lemalta/`), not part of fbneo-rr.
That split is itself worth copying: generic harness machinery in one repo, only
Lua test scripts in the emulator repo. `scripts/isotest.sh` currently mixes both.

| Copy | From | Why |
|---|---|---|
| `ResourceStack` — LIFO, signal-safe teardown, process-GROUP kill | `lemalta/runtime.py` | cleanup written at three exit paths ran only on the remembered ones |
| `Target`/`Host` policy-vs-mechanism split | `lemalta.py:1931-2266` | the input gate lives above the mechanism "so every caller gets it for free" |
| `config_override()` with restore-on-any-exit + flock on a sidecar | `lemalta.py:3549-3572` | "a flock held on an unlinked inode protects nothing" |
| `.lua` positional dispatch | `fbneo-rr/src/burner/win32/main.cpp:973-990` | the proven shape for A3 |
| `check()/SUMMARY/done` + the parent's regex scrape | `dev-scripts/spec_runahead.lua`, `lemalta.py:5200-5205` | ready-made |
| INCONCLUSIVE verdict | `dev-scripts/spec_suite.sh` | seven lines of bash |
| Script never exits; parent kills | `cpu_debug_test.lua:233-235` | removes "passed, or crashed?" ambiguity |
| `_wait_until()` returning a boolean | `lemalta.py:4317-4337` | "a wait that cannot report failure is a sleep with extra steps" |
| Torn-line-safe stream reader | `lemalta.py:4248-4279` | needed to tail the Lua log live |
| Identify a process by what it IS, not its command line | `lemalta.py:2128-2137` | "every wrong kill this tool has made came from reading the command line" |

Do **not** copy: lemalta has no results JSON (our `test.ps1` already writes
`test_results.json`, which is better for the VS Code consumer), and it has no
quit-from-script primitive — A5 is an improvement over the reference.

## Suggested order

1. `scripts/testrun.sh` = B1 + B3 + B6 + deadline poll + SIGINT + group kill.
   No C++. Half a day.
2. A2 then A1. One hour; retires the `grep -a` trap permanently.
3. A3 + A4. One hour.
4. The `check()/SUMMARY` contract + a suite runner. Half a day.
5. A5, now that the deferred hook exists to make `quit()` safe.
6. Fixture-state-per-test once boot time dominates.

## Not verified

The agent did not run the emulator. B2's relative-path resolution, the exact
`LuaFileName` override behaviour, and the `dc_exit`-from-`vblank` deadlock are
reasoned from code, not measured — though the deadlock has since been
`[MEASURED]` for `pause()`, which takes the same path. Whether the BIOS resolves
under a sandboxed `XDG_DATA_HOME` is untested.
