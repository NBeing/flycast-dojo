> ## SUPERSEDED — kept as the record, not as guidance
>
> `[CORRECTED 2026-09-07]` This was the opening analysis, written before any of
> the work. Its question — *what unifies, and at which layer* — has been
> answered by doing it. **Read `UNIFIED.md` for the current state.**
>
> It is kept rather than deleted because several of its conclusions were wrong
> in ways worth being able to look up:
>
> | this doc claimed | what actually happened |
> |---|---|
> | port *toward* David's tree, because `dojo_gui.cpp` is irreplaceable | reversed once you said you would reimplement the UI, then made moot: **both** forks were already dojo-7 descendants, so the answer was a third base neither of us was on |
> | the Lua/emuapi migration is "8 hunks / 227 lines" | measured **textual** conflicts only. Five further problems had no conflict marker, the worst being a file missing from the patch scope entirely (`core/hw/mem/mem_watch.h`) |
> | two capture stacks are "live and racing" | **wrong.** `avi_dump` could never start on this base — its start lives in unported UI code, so its two call sites were permanently-false guards. Unreachable code, not a race |
> | two frame counters are a collision | **wrong.** Two counters for two questions, and `spec.lua` already tagged `frame.count()` `(MAY drift)` |
> | FMA is unfixable, "a host CPU feature, no manifest fixes it" | **wrong.** Upstream already guarded it for GGPO (`rec_x64.cpp`); it needed one condition widened |
> | "~20 guards still carry inline conditions" | **wrong.** Triaged individually, almost all are netplay plumbing. **One** real conversion |
>
> The analysis that did hold: that the divergence was a *layer* choice rather
> than a feature list, that the `tas_*` modules were portable while the GUI was
> not, and that the two video recorders are different products rather than
> duplicates.

# UNIFICATION — the two forks, emuapi, and where the seam should fall

Written 2026-09-05 from a read of all three trees. Everything numeric here was
measured, not recalled; the commands are named so they can be re-run.

The question this answers: **two flycast-dojo forks have diverged, emuapi now
exists as an abstracted API — what unifies, and at which layer.**

---

## 0. The three things

| | path | what it is |
|---|---|---|
| **TAS fork** | `~/dev/davids_fly` | David's. PCSX2-rr-style TAS combo studio for MvC2. Netplay reduced. `core/dojo/` grew to ~40k lines. No git. |
| **Capture fork** | `~/dev/flycast-dojo` @ `video-recording` | Yours. Full dojo netplay intact, + WYSIWYG video capture, + a heavily expanded Lua surface. 46 commits past `origin/master`. |
| **emuapi** | `~/dev/emuapi` (submodule of the capture fork) | The neutral Lua interface: `spec.lua` (37 KB), `conformance.lua`, `adapters/{flycast,fbneo,mock}.lua`. Private repo. |

A nested copy of the TAS fork sits at `davids_fly/flycast-dojo-7/`; the root is
the working tree.

---

## 1. The bases diverged before either of you did

This is the headline, and it is not about your features.

```
merge-base  541544292  2023-05-13  "dx11 oit: resize to null width and height…"  (flyinghead)
   ├── 1480 commits ──▶ blueminder/master  d0e47e572  2025-06-30  ──▶ your video-recording (+46)
   └──  788 commits ──▶ dojo-7-preview4    f39061cfa  2025-06-28  ──▶ the TAS fork
```

`git merge-base dojo-7-preview4 HEAD` · `git rev-list --count 541544292..<each>`

The two lineages sit on **different flyinghead snapshots**, and the file layout
proves it:

| marker | TAS fork | capture fork |
|---|---|---|
| `core/audio/audiostream.cpp` | yes | — |
| `core/oslib/audiostream.cpp` | — | yes |
| `core/rend/gui_settings.cpp` | — | yes |
| `core/oslib/storage.cpp` | yes | — |
| `core/deps/imgui/backends/` | yes | — |
| ImGui version | **1.90.4** (docking, vendored swap) | **1.80 WIP** |

On top of that the TAS fork **renamed every dojo file**: `DojoSession.cpp` →
`dojo.cpp`, `DojoGui.cpp` → `dojo_gui.cpp`, `ReplayManager.cpp` → `replay.cpp`,
`DojoLobby` → `net_beacon` (`class DojoLobby` → `class NetBeacon`),
`DojoFile.cpp` → `dojo_file.cpp`.

**Consequence.** `git merge`, `cherry-pick` and `rebase` are all off the table —
there is no shared history *and* no shared filenames. Every unified feature
moves as a **port**: read the source file, write it into the other tree, retest.
Plan the work in units of "one feature ported and re-verified", never "one merge".

The one thing that *is* cheap to move is a self-contained file with narrow
includes. Section 4 identifies which those are.

---

## 2. The real divergence is a layer choice

Both forks solved overlapping problems. They put them at **different levels of
the stack**, and that — not the feature list — is what unification has to resolve.

**The capture fork built a seam.** `core/lua/lua.cpp` grew 822 → **1,616 lines**,
38 → **107 bound functions**, plus `lua_console.cpp` (269) and a `flycast-lua.log`.
Namespaces added: `video`, `replay`, `memory` watches + registers, `input`
button *names* and tables, `state` rollback/confirmed-frame/viewport, `ui` raw
ImGui forwarding. emuapi's `adapters/flycast.lua` binds against exactly this.

**The TAS fork built natively.** `core/lua/lua.cpp` is **byte-identical to stock
dojo-7** (verified: `diff <(tr -d '\r' < dojo7_lua.cpp) <(tr -d '\r' < core/lua/lua.cpp)`
→ identical). Its TAS surface is instead:

```
core/dojo/dojo_gui.cpp   21,877 lines   the whole studio UI
core/dojo/dojo.cpp        2,896         input pipeline, re-record, sidecars
core/dojo/replay.cpp        654         clip-folder playback init
core/dojo/avi_dump.cpp      944         capture stack
core/dojo/tas_*.cpp       3,673         the eight TAS libraries
```

**So today emuapi cannot load on the TAS fork at all.** The adapter reaches for
~45 host entry points; roughly 30 exist only in the capture fork —
`input.buttonNames / getButtonTable / setButtonTable / setButton`,
`state.isRollback / getConfirmedFrameNumber / getResimSteps / getGameViewport`,
`emulator.isOnline / isReplay / setSpeedMode / saveStateString / hashState`,
all of `memory.watch*` and `getRegister/setRegister`, all of `replay.*`, and
the whole `ui.*` ImGui forwarding.

Porting `lua.cpp` is therefore the **prerequisite for every other unification
option**, and it is the cheapest large-leverage move available.

---

## 3. Feature matrix

Where both columns are filled, the capability was **arrived at twice** — which is
precisely the condition emuapi was written to stop.

### Video capture — two different products, both wanted

| | capture fork | TAS fork |
|---|---|---|
| file | `core/rend/video_recorder.{h,cpp}` (631) | `core/dojo/avi_dump.{h,cpp}` (1,048) |
| hook point | presented back buffer, **after ImGui, before swap** | renderer FBO, **before OSD** |
| what it records | **what the user sees** — game + OSD + Lua overlays | **clean game image only** |
| backends | GL, Vulkan, DX11, DX9 | DX9, DX11 |
| readback | **async** (PBO ring / fence / staging pair); DX9 sync only | sync `GetRenderTargetData` (~27 fps at 4K) |
| codecs | mjpeg / libx264 via `ffmpeg` on PATH | ProRes LT / CineForm / VfW, **bundled ffmpeg.exe** |
| queue-full | drop *contents*, keep the **slot** (CFR); `blockonfull=yes` opt-in | **always blocks** — never drops |
| audio | tap in `core/oslib/audiostream.cpp`, mux at stop, pads gaps >250 ms | tap per emulated sample, WAV stream-copy mux |
| verified | A/V drift 0.000 s over 41 s; 1:1 frame correspondence | A/B/C/D codec test vs a 617 Mbps reference |

These are **not duplicates to collapse**. WYSIWYG-with-overlays and
clean-plate-for-editing are different deliverables. Unify by making them two
modes of one recorder, not by picking one.

The genuinely portable half is the **backend hooks**: the capture fork's GL and
Vulkan paths are the thing the TAS fork lacks and cannot easily rebuild, and
`video_recorder.cpp` is explicitly renderer-agnostic by design.

### Movies and re-recording

| | capture fork | TAS fork |
|---|---|---|
| | `ReplayManager.cpp` (389) — upstream dojo replay | `replay.cpp` + `dojo.cpp` (3,550) |
| model | play a recorded match | **PCSX2-rr re-record**: `session_inputs[frame]`, last-write-wins |
| editing | — | `Dojo::ApplyEdit(firstChangedFrame)` — one choke point, BizHawk's discipline |
| timeline | — | events deferred to *true divergence*; savestates flagged **stale** by generation |
| storage | flat replay files | clip folders + `.frame` sidecars + `clip.json` (schema 5) + `_gen_NN` backups |
| text form | — | `tastext` — byte-identical round trip, proven on 3,606 frames |

This is the TAS fork's crown jewel and the capture fork has no equivalent.

### Netplay

| | capture fork | TAS fork |
|---|---|---|
| lobby | `DojoLobby` + `LobbyClient` (844) | `net_beacon` (279) — the LAN beacon, reduced |
| transport | `UDPClient` (573) + `AsyncTcpServer` (104) + `RelayClient` | `tcp_client` (213) + `relay_client` (661) |
| hooks | `EmulatorHooks.cpp` (780) | **absent** |
| ggpo | compiled | compiled (but netplay deliberately neutered in `emu.cfg`) |

Unification direction is one-way: netplay flows **into** the TAS fork if wanted
at all, and the TAS fork's CLAUDE.md is explicit that it was neutered on purpose
(shadow recordings stealing the savestate folder, blocked headless boots).
Leaving it stripped is a legitimate outcome.

### Scripting

| | capture fork | TAS fork |
|---|---|---|
| `lua.cpp` | 1,616 / 107 fns | 822 / 38 fns — **stock** |
| console | `lua_console.cpp` + `flycast-lua.log` | — |
| frame counter | counts **delivered** callbacks, monotonic, rollback-gated | dojo's own (stalls and jumps offline) |
| draw-thread guard | raises outside a draw callback | — |
| emuapi | 83 pass / 0 fail / 2 skip; 35/51 surface implemented | **cannot load** |

### Determinism

Both took it seriously, differently, and **both sets are worth having**:

- TAS fork: RTC pinned, SH4 clock pinned to 200 MHz under record/replay, every
  savestate load byte-verified (`VerifyState`, on by default and deliberately
  *not* exposed in the UI), `SERMAP` per-subsystem offsets. Two real desync bugs
  found this way.
- Capture fork: frame callbacks never delivered for re-simulated frames,
  `isrollback()` in the contract, confirmed-frame counter that cannot go
  backwards — enforced by the conformance suite's rollback group.

### Testing culture — the quiet incompatibility

- TAS fork: `test.ps1`, `guardtest.ps1`, `textguard.ps1`, `chordtest.ps1`,
  `scrubstuck.ps1`, `crashrepro.ps1`, `resizeguard.ps1` — PowerShell harnesses
  that **drive the real emulator** via `keybd_event` and judge from log markers.
  Slow, high fidelity, Windows-only.
- Capture fork: `lua run-conformance.lua` — **~1 second, no emulator**, plus
  in-emulator runs, plus integration tests.

Any Lua migration only pays if the fast headless loop comes with it. Note the
version trap: you link `find_package(Lua)`, the TAS fork pins
`find_package(Lua 5.2)`, and this machine's `lua` is **5.1** — so the headless
conformance run is not exercising the same interpreter as the in-emulator run.
Pin it deliberately before building on it.

---

## 4. Should `tas_*.cpp` become part of emuapi?

**Mostly no — and the parts that should be are not the C++.**

`spec.lua`'s own boundary rule is *most logical, bounded by implementable on at
least two real emulators*. Three modules fail that on sight:

- `mvc2.cpp` — MvC2-on-Dreamcast RAM offsets from one trainer script
- `tasva2.cpp` — a personal 2009 combo-transcript dialect
- `tasmacro.cpp` — one specific CE trainer's per-player keyboard letters

These are **content**, not interface. Admitting them would do the thing the
suite already caught once, when it was poking `0x8C010000`: make the neutral
layer flycast-shaped.

What *does* belong in emuapi is the **contract they imply**, which the spec
currently only stubs:

1. **`movie.*` is a playback model and should be a re-record model.**
   Today: `framecount / mode / rerecordcounting / get+setreadonly / stop / play
   / record`. Missing entirely: an edit funnel, a notion that inputs are a
   last-write-wins map rather than a stream, and a timeline identity. The TAS
   fork has all three, working and guarded. This is the single strongest piece
   of spec work available.
2. **`savestate.*` has no timeline identity.** Nothing expresses "this state
   belongs to a generation of the movie that has since been edited". The
   `.frame` sidecar + staleness rule is the missing concept, and it is the
   dead-timeline foot-gun the TAS fork's CLAUDE.md documents as its one
   remaining re-record hazard.
3. **`sound.*` has no per-frame envelope hook.** `tas_wave` needs one;
   `voicecount / outputrate / voice` do not provide it.
4. **There is no file namespace at all.** `tas_ruler` persists `skip.map`,
   `tas_clip` owns `clip.json`. Any Lua port of either needs somewhere to write.

---

## 5. Is Lua better? Per module

The load is not the deciding axis — you are right that parsing is cheap. The
axis is **how often does this change, and who changes it.**

| module | LOC | verdict | reasoning |
|---|---|---|---|
| `tasva2` `tasmacro` `tastext` | 1,829 | **Lua, strongly** | Pure text↔frames, runs once per import/export. The grammars are still being *discovered* — VA2 carries typo-tolerance rules, macro letter maps are per-trainer. Today a grammar tweak costs a rebuild, and the linker lock means **closing the emulator first**. In Lua it is a text edit and a reload. That is an iteration-rate change, which for a language still being designed is the whole argument. |
| `mvc2.cpp` | 242 | **Lua/JSON data** | A table of addresses currently shaped as code. It is the single thing standing between this tool and a second game. |
| `tas_ruler` | 198 | **Lua, probably** | Four byte reads per frame. Blocked only on the missing file namespace. |
| `tas_auto` | 123 | **Native, or spec work first** | Trivially cheap, but **sync-critical ordering**: it must bake into the recording immediately before the `.flyr` append. emuapi's `emu.registerbefore` is listed missing in the capture fork, and the spec never pins *when* it fires relative to the movie write. Getting that wrong desyncs — the one thing the northstar forbids. |
| `tas_wave` | 256 | **Native, always** | Tapped inside `WriteSample` at 44.1 kHz on the audio thread, before host volume. Not a Lua place at any speed. The *read* side (`sound.envelope(frame)`, once per drawn row) could be. |
| `tas_clip` | 1,025 | **Native; share the schema instead** | Called pre-boot from the browser, before a Lua state need exist. Make `CLIP_SCHEMA.md` the shared artifact. |

### Costs of the Lua move, priced honestly

- **The proof is the asset, not the parser.** T5's value is a byte-identical
  round trip on 3,606 frames with `tastext.ps1` running it headlessly. A rewrite
  must re-earn that. The harness drives the emulator rather than the C++, so it
  survives — but this is real work against something currently *proven*.
- **Byte-exactness needs the right interpreter.** `@raw`'s exact-bytes escape
  wants `string.pack` (5.3+). Lua 5.1 has no integer subtype and no bit ops
  without a library. See the version trap in §3.
- **It leaves the type checker.** A C++ typo fails the build; a Lua typo fails
  whenever someone next runs it. Mitigated only by the headless suite.
- **Error surfacing.** `tastext` is strict by design — a malformed line is an
  error naming its line number, shown in an ImGui preview. In Lua that preview
  goes through emuapi `ui.*`, which exists in the capture fork and not the
  TAS fork. Another dependency on §6 step 1.

---

## 6. Proposed sequence

Ordered so each step is independently useful and unblocks the next.

**1. Port `lua.cpp` into the TAS fork.** ~800 lines, self-contained, no dojo
coupling. Bring `lua_console.cpp` and the log with it. Unblocks everything else
and immediately lets `conformance.lua` run against the TAS fork — which is the
first time the suite meets a host it was not written against, and therefore the
first time it is worth anything (`INTEGRATION.md` says exactly this).
*Watch:* the ImGui 1.90.4 vs 1.80 gap lands on the `ui.*` forwarding.
*Done when:* `emuapi.report()` prints on the TAS fork.

**2. Publish `mvc2.cpp` as a game-profile table.** Smallest real win, and it is
what makes anything downstream game-portable.

**3. Grow `movie.*` in the spec from the TAS fork's re-record model.** The edit
funnel, last-write-wins inputs, timeline events on true divergence, savestate
staleness. Spec first, conformance checks second, implementations third.
This is the piece that most deserves to be designed **once** rather than twice.

**4. Port the GL and Vulkan capture hooks into the TAS fork**, and add a
clean-plate mode (pre-OSD readback) to `video_recorder.cpp`. Converges the two
recorders into one with two modes rather than picking a winner.
*Watch:* the audio tap moved between `core/oslib/` and `core/audio/`.

**5. Rewrite one notation codec in Lua as the proof.** `tastext` is the right
one — it already has a headless harness and a byte-exact acceptance test, so the
rewrite is falsifiable in a way the others are not. If it survives
`tastext.ps1`, the case for `tasva2` and `tasmacro` is made empirically instead
of by argument. If it does not, you learned it cheaply and on the module you
could most afford to lose.

**6. Decide netplay deliberately.** Currently one-way and easy to leave alone.

---

## 7. Open questions — for you and David

1. **Is there one binary at the end, or two?** A shared core plus two builds
   (combo studio / netplay) is a legitimate answer and changes everything below
   it. Nothing in either tree commits to this yet.
2. **Which upstream base wins?** Someone eventually rebases onto the other, or
   both onto current flyinghead. The ImGui docking swap and `IMGUI_UPGRADE.md`
   argue the TAS fork's base is the more expensive one to abandon.
3. **Does emuapi's `movie` namespace get to be opinionated about re-recording?**
   The spec says "designed to the domain, not the intersection" — which points
   at yes. Worth stating explicitly, because it makes fbneo-rr's adapter harder.
4. **Who owns `CLIP_SCHEMA.md`?** It is currently a TAS-fork document describing
   a TAS-fork format. If the capture fork ever writes clips it becomes shared,
   and shared formats want a home outside both.
5. **Does the piano roll ever become scriptable?** 21,877 lines of `dojo_gui.cpp`
   is not a rewrite candidate. But new panels could be built on emuapi `ui.*`
   from the start, which is the only version of this that ever happens.

---

## Method

Read: both trees' `core/dojo`, `core/lua`, `core/rend`, `CMakeLists.txt`;
`emuapi/{README,INTEGRATION,spec.lua,conformance.lua,adapters/flycast.lua}`;
`DAVID_FEATURES.md`, `LUA_TODO.md`, `TODOS.md`, `ROADMAP.md`, the `CANON_*.md`
set, `WRITE_MODE_RESTRUCTURE.md`, `david_work_tracker.md`.

**Not read** (and so not assessed): the body of `dojo_gui.cpp` (21,877 lines)
and `dojo.cpp` (2,896). Claims about them come from their headers, their call
sites, and the fork's own documentation. Neither tree was built or run.
