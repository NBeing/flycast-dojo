# Seams — how to make this codebase reasonable before ~20,000 lines of studio UI land

`[WRITTEN 2026-09-08]` A ranked list of places where two concepts are wearing one
name, what the seam between them would be, what it costs, and what it unblocks.
Ordered by **how much cheaper it makes the studio port**, which is the near-term
goal — not by how satisfying the abstraction is.

Marks follow `CLAUDE.md`:

- **`[MEASURED <date>]`** — a number this pass produced, with the command.
- **`[SOURCE]`** — read out of code in this checkout or in `~/dev/davids_fly`.
  **The quote is the citation; the line number is a hint**, because line numbers rot.
- **`[REASONED]`** — derived from source, not run.
- **`[OPEN]`** — a question this pass could not settle.

`TODOS.md` ("A movie that does not start at power-on cannot be replayed") points
here for the disposition of the six `session_inputs.size()` sites. **§2 answers
that, and corrects it** — two of the five said to be "correct-for-netplay" are not.

## The ranking, in one table

| | seam | the two concepts wearing one name | cost | why it is here |
|---|---|---|---|---|
| **S1** | no panel/window registry | *a studio tool* vs *eight separate facts about one* | ~200 lines, then one row per panel | the port's per-panel cost; ~730 lines of `strcmp` chain and four draw lists disappear |
| **S2** | `session_inputs` has eight answers to "is it over" | *how many frames were authored* vs *where the timeline ends* vs *is frame N authored* | ~60-line header, ~12 call sites | correctness; the piano roll is the biggest panel and asks this per row |
| **S3** | `dojo:` cfg read three ways, no owner | *the stored value* vs *the shadowed value* vs *the cached value* | ~150 lines + a sweep of 56 keys | the port adds 76 keys and ~200 call sites |
| **S4** | four idioms for "stop the machine" | *a pause reason* vs *a hand-rolled stop/start* | ~40 lines + 3 rewrites + a marker | the port adds ~14 render-thread panels that mutate movie state |
| **S5** | session mode as a cfg conjunction | *what kind of session* vs *which flags happen to be set* | low (enum) / moderate (boot handoff) | S1's `enabled()` predicates land here or become 14 more conjunctions |
| **S6** | the `Dojo` god object | five concerns in one class | — | **do not attack directly**; it shrinks as a by-product of S1/S2/S5 |

**The single highest-value move: land S1 before the first panel arrives.** Every
other seam can be retrofitted; that one cannot, because retrofitting it means
editing 161 file-statics inside a 21,877-line file.

---

## 0. The decision this document does not make

Two strategies for the studio are live in the tree's own documents, and the
ranking below depends on which one is taken:

- `UNIFIED.md`, *Suggested next steps* 4: *"look at `dojo_gui.cpp` — with the
  engine already running, the GUI can be rebuilt panel by panel on emuapi `ui.*`
  rather than ported wholesale."*
- The brief for this pass: *"we are about to port ~20,000 lines of TAS studio UI."*

**This document assumes panel-by-panel**, because that is what makes the seams
below nearly free: each panel arrives as one registry row plus one draw function
rather than as edits to four dispatch lists. If the plan is instead a wholesale
`cp` of `davids_fly/core/dojo/dojo_gui.cpp`, then S1 and S3 stop being cheap
scaffolding and become a 21,877-line refactor, and the honest advice changes to
"copy it, then do not touch it for a while". **Pick one before starting**; the
expensive outcome is a `cp` followed by an attempt to retrofit a registry.

`docs/STUDIO-IN-EMUAPI.md` is the companion to this file and does not overlap
with it. That document asks *which studio features are portable interface ideas*
and answers with a Lua/emuapi surface. This one asks *what shape the C++ should
be in for the port to land*, and every seam below is host furniture by that
document's own classification — furniture is where the port's cost actually is.

### What the port is, measured

`[MEASURED 2026-09-08]` `wc -l`, and
`grep -cE '^static [A-Za-z_].*=' / '^\s+static '`:

| | this tree (`dojo7`) | `davids_fly` (the source) | delta |
|---|---|---|---|
| `core/dojo/dojo_gui.cpp` | 2,544 lines | 21,877 | **+19,333** |
| `core/rend/gui.cpp` | 5,019 | 6,078 | +1,059 |
| file-static mutable state in `dojo_gui.cpp` | **0** | 161 | +161 |
| function-local `static`s in `dojo_gui.cpp` | 7 | 187 | +180 |
| `ImGui::Begin` calls in `dojo_gui.cpp` | 14 | 27 | +13 |
| distinct `dojo:` cfg keys, whole tree | 56 | 132 | **+76** |
| `cfgLoad*("dojo",…)` call sites | — | 301 over 128 keys | 2.35× per key |

The port roughly **doubles the config surface and adds ~340 pieces of hidden
mutable UI state into one translation unit that currently has seven.** That last
number is the reason a registry has to exist before the panels arrive: 161
file-statics in one file is precisely what makes a file un-splittable afterwards.

---

## S1 — The panel layer has no registry. **Highest value; do it first.**

### The concept being conflated

"A studio tool" is eight facts: a stable id, a menu label, an open flag, whether
that flag persists, a draw function, which of the two ImGui frame streams it
draws in, a gating predicate, and a focus/zoom key. Today those eight are spread
across four different mechanisms and four hand-maintained lists, and **no single
place knows that a panel exists.**

### Evidence

`[SOURCE]` The Windows menu in the source fork uses **four different mechanisms
for one concept** in fourteen lines — `davids_fly/core/dojo/dojo_gui.cpp:18798`:

```cpp
cfgItem("Piano Roll", "PianoRoll", false);          // cfg key, no bool
ImGui::MenuItem("Notepad", nullptr, &notepadOpen);   // file-static bool
ImGui::MenuItem("Input Sender", nullptr, &inputSenderOpen);
cfgItem("Input Viz", "InputViz", false);
ImGui::MenuItem("Timeline", nullptr, &timelineOpen);
...
ImGui::MenuItem("Hotkeys", nullptr, &dojo.hotkey_overlay);   // a member of the Dojo god object
{   const bool st = gui_states_is_open();                     // an accessor pair in another file
    if (ImGui::MenuItem("States", "F4", st)) gui_states_set_open(!st); }
```

`[SOURCE]` The seven file-static open flags are declared 12,000 lines apart —
`dojo_gui.cpp:3934, 4609, 9078, 10028, 10029, 15849, 16573`.

`[SOURCE]` Their persistence is a hand-written load-once block and a hand-written
dirty-check block wrapped around a hardcoded list of six draw calls —
`dojo_gui.cpp:16795` (`DojoGui::show_tas_tool_windows`), which even names the
gap the registry closes: *"imgui.ini saves positions/docking but NOT whether a
window is shown, so load the open flags once … and save whenever one toggles."*

`[SOURCE]` The focus-mode menu bar dispatches by string key through a **ten-branch
`strcmp` chain spanning ~730 lines** — `dojo_gui.cpp:18048` through `:18683`,
each branch re-asking "is it open?" through whichever of the four mechanisms that
panel happens to use:

```cpp
if      (!strcmp(sel, "RollGridScale")     && cfgLoadBool("dojo","PianoRoll",false))
else if (!strcmp(sel, "NotepadUiScale")    && notepadOpen)
else if (!strcmp(sel, "MacrosUiScale")     && macrosBrowserOpen)
else if (!strcmp(sel, "StatesUiScale")     && gui_states_is_open())
else if (!strcmp(sel, "HotkeysUiScale")    && dojo.hotkey_overlay)
...
```

`[SOURCE]` The draw list is written out **four times**, and the four are not the
same list — `davids_fly/core/rend/gui.cpp:4082-4085` (the settings scale-preview
case), `:4164-4170` (Paused), `:4328-4333` (the OSD stream), plus
`dojo_gui.cpp:21793-21795` inside `show_pause()`. The Paused case carries the
scar: *"show_pause() renders the whole overlay suite itself … calling them again
here was double-rendering everything (user saw the Timeline content twice)."*
And a second scar at `dojo_gui.cpp:15781`: *"They used to be drawn at the tail of
`show_piano_roll`, so toggling the roll off made them [vanish] … Called from both
frame streams (running + paused) right after `show_piano_roll`."* Both defects
are "a panel drew in the wrong set of streams", which is exactly the fact a
descriptor holds and a hand-maintained list cannot.

`[MEASURED 2026-09-08]` Across this tree (`gui.cpp`, `dojo_gui.cpp`, `gui_cheats`,
`gui_util`, `gui_android`, `lua.cpp`) there are **33 top-level `ImGui::Begin`
calls** today and **exactly one is dockable** (`"Game"`, `core/rend/gui.cpp:559`). None of the
33 has a persisted open flag: visibility is either an arm of the single
mutually-exclusive `GuiState` switch or an ad-hoc bool checked at its own call
site. The port adds ~14 windows of a **different kind** — independent, dockable,
coexisting studio panels — into a tree that has no machinery for that kind at all.

### The abstraction

The shape is already proven twice, once in each project.

In this fork, by `TasHotkeyDef` / `TAS_HOTKEYS[]` — `[SOURCE]`
`davids_fly/core/dojo/dojo_gui.cpp:1080`, whose own comment states the property:
*"The KEY shown in the panel is looked up LIVE from the keyboard device's input
mapping (single source of truth), so this display can never drift from reality."*
Three consumers already loop that one array (the settings table, the cheat-sheet
overlay, the copy-to-other-pad action). **A panel registry is that same pattern
applied to a second axis, not a new idea.**

In `nbneo-rr`, by `PanelDesc` / `kPanels[]` / three loops — `[SOURCE]`
`/home/nbee/dev/anita/nbneo-rr/shell/gui/main_gui.cpp:1041`:

```cpp
struct PanelDesc {
    const char* id;          // STABLE key, used on disk: "dag". Never the label.
    const char* label;       // menu text
    const char* shortcut;
    bool*       open;        // THE one owner of "is it open"
    void      (*draw)(bool* open);
    bool        persist;     // no default - a transient panel must say so
};
```

with registration/persistence at `main_gui.cpp:1143`, the View menu at `:1622`,
and the draw loop at `:3602`. Its post-mortem
(`nbneo-rr/docs/panel-scaffolding.md:5-30`) is the same defect list this fork is
about to import, measured a month earlier: *"three different mechanisms for the
one concept 'is this panel open'"*, three panels that *"did not survive a restart
at all"*, an *"Audio panel's toggle in the Video menu — nobody decided that; it
is where it landed"*, and a flag that *"shipped missing the sixth [touch point]
and was silently lost on every round trip for a day."*

For this tree the descriptor needs two fields `nbneo` does not, both forced by
facts documented in `core/rend/gui.cpp:437-446`:

```cpp
struct TasPanel {
    const char *id;          // "pianoroll" - the cfg key and the focus key. Never the label.
    const char *label;       // "Piano Roll"
    bool       *open;        // one owner; may point into another TU behind an accessor
    bool        persist;     // no default
    u32         streams;     // OSD | PAUSED - which ImGui frame streams draw it
    bool      (*enabled)();  // studio mode, not capturing, ... ; nullptr = always
    void      (*draw)(float scaling);
};
```

`streams` is the field that makes the two shipped double-render / vanish defects
unrepresentable: today "which streams does this panel draw in" is a fact stored
only in the shape of four call lists.

### Cost

`[REASONED]` ~200 lines of new scaffolding (`core/rend/tas_panels.{h,cpp}`), plus
one row and one `enabled()` per panel as it lands. It **deletes** the ~730-line
`strcmp` chain (each branch becomes a `PanelMenus` callback hung off the row),
the load-once/save-on-change block, the Windows menu body, and three of the four
draw lists. Net line count during the port is roughly neutral; net *touch points
per new panel* goes from six-to-thirteen to one.

The cost is only low **if the registry exists before the first panel lands.**
Retrofitting it into 161 file-statics afterwards is the expensive version, and it
is the version `nbneo-rr` paid for.

### Unblocks

Uniform persistence (which no panel in this tree has today); the two-frame-stream
rule enforced in one loop instead of four lists; per-panel UI zoom and the
focus-menu-bar without a string chain; a `--panel=x` style test hook, since a
capture test can name a panel by its stable id; and the ability to split
`dojo_gui.cpp` at all, because a panel that owns its state behind an accessor can
move to its own file.

### What it does **not** buy, and should not try to

Do not fold the existing `GuiState` screens (Settings, Commands, Main,
ReplayEnd, …) into the registry. They are mutually exclusive by construction —
one arm of one `switch` — and the registry's whole premise is independent
coexisting bools. Converting them is a real UX change wearing a refactor's
clothes. **The registry covers the TAS panel class only**; the GuiState screens
stay as they are.

---

## S2 — "How long is the movie / is it over" has eight answers. **Highest correctness value.**

### The concept being conflated

`session_inputs` is a **sparse, frame-keyed, last-write-wins map** — `[SOURCE]`
`core/dojo/dojo.h:105`, `std::map<uint32_t, std::vector<uint8_t>> session_inputs;`
— and the `.flyr` body agrees: each record is `[u32 frame][bytes]` and the parser
does `session_inputs[frame_num] = inputs;` (`core/dojo/dojo.cpp:2626`). **The
storage layer has always been sparse.** What is dense is a *netplay match*, and
the consumers were written for that: they read `.size()` as "the last frame".

Three questions are wearing one name:

1. **How many frames were authored** — `.size()`. A stats number. Correct.
2. **Where does the timeline end** — `rbegin()->first + 1`. A frontier. Different.
3. **Is frame N authored** — `find(N) != end()`. Different again: a hole mid-movie
   is not the end.

They coincide exactly when the movie is dense from frame 0, which every studio
movie is today.

### Evidence

`[SOURCE]` The right answer already exists, with the argument attached —
`core/dojo/dojo.h:148`:

> *"The movie END as 'one past the last authored key' — NOT `session_inputs.size()`:
> a Play-Macro-Full roll is keyed from State 0's frame (A..A+len-1), so `size()`
> undercounts and every frontier test (R -> READ seeks BASE, the banner READ
> click, ReplayEnd, the step-hold) fired early (macro-parity audit). Dense
> movies: identical value."*
> ```cpp
> u32 MovieEnd() const { return session_inputs.empty() ? 0 : session_inputs.rbegin()->first + 1; }
> ```

`MovieEnd()` is under-used. `[MEASURED 2026-09-08]` **eight distinct
implementations** of "the last frame / is it over" exist in this tree:

| # | site | metric | scope |
|---|---|---|---|
| 1 | `dojo.h:151` `MovieEnd()` | `rbegin()->first + 1` | canonical |
| 2 | `dojo.cpp:1985` `frame_number == MovieEnd() - 1` → `ReplayEnd` | uses #1 | ordinary replay |
| 3 | `dojo.cpp:2011` `session_inputs.find(fn) == end()` → `ReplayEnd` | any unauthored frame | fires on an interior gap too |
| 4 | `dojo.cpp:1974` macro-READ end, `fn >= rbegin()->first` | no `±1` shared with #2 | macro mode |
| 5 | `dojo.cpp:790,811` `LoadStateFrame`, `fn >= rbegin()->first` | seek-target validity | *was* the `size()-1` bug; fixed |
| 6 | **`gui.cpp:4576`** `frame_number == session_inputs.size()` → `ReplayEnd` | `.size()` | **all replay, incl. TAS** |
| 7 | `tas_clip.cpp:381` `flyrFrameCount()` | `maxFrame+1`, parsed from raw `.flyr` bytes | stats fallback |
| 8 | `lua.cpp:1783` `getReplayFrameCount()` vs `lua.cpp:582` `getMovieLength()` | `.size()` vs `MovieEnd()` | two Lua answers, one wrong |

`[SOURCE]` The comment that names the assumption is still there —
`core/rend/gui.cpp:769`: *"buffering carries the netplay
`frame_number == session_inputs.size()` test."*

`[SOURCE]` The `.flyr` **header** is a GGPO match record and has no TAS field at
all — `core/dojo/replay.cpp:463` (`GenHeader`) writes, in order: `version`,
`rom_name`, `PlayerName`, `OpponentName`, `Quark`, `RelayServer#RelayKey`,
`analogAxes`, `precise_triggers`, `ggpo`, `P1CountryCode`, `P2CountryCode`;
parsed symmetrically at `core/dojo/dojo.cpp:2500`. No frame count, no start
frame, no timeline id, no sidecar reference. Every TAS fact lives in a sidecar
beside it (`<state>.frame` v2/v3, `clip.json`, `<clip>_macro.txt`,
`audio.env`, `skip.map`).

### Correcting `TODOS.md`

`TODOS.md` says of the six `.size()` sites: *"One of those six was a real bug and
is fixed … The other five are correct-for-netplay and should not be touched
piecemeal."* `[MEASURED 2026-09-08]` Reading the guards: **three of the five are
netplay-only. Two are not.**

Netplay-only, and correct as they stand:

- `gui.cpp:4568` — inside `if (cfgLoadBool("dojo","Receiving"))`. A netplay
  stream is dense by construction: frames arrive in order over TCP. **Correct.**
- `gui.cpp:4950`, `gui.cpp:4999` — both guarded on `dojo.buffering`, which is set
  true only in that same Receiving branch. **Netplay-only. Correct.** (So is
  `dojo_gui.cpp:2439`, which `TODOS.md` does not list — same `buffering` guard.)

Not netplay-only:

- **`dojo_gui.cpp:1033`** — the position-overlay gate,
  `if (dojo.frame_number < dojo.session_inputs.size() || Training)`, with
  `:1039` and `:1052` printing `"%u / %u"` against `.size()` as the total. It is
  reached whenever `dojo.play_match`, i.e. on every TAS replay. Cosmetic, but
  wrong in two ways at once on a sparse movie: the overlay disappears partway
  through, and the total it prints is the count, not the end.
- **`gui.cpp:4576` — the `else` of the Receiving test, still inside
  `if (dojo.play_match)`. It runs on every TAS replay** and sets
  `GuiState::ReplayEnd`. `[SOURCE]`:
  ```cpp
  else {
      if (dojo.frame_number == dojo.session_inputs.size()) {
          settings.input.fastForwardMode = false;
          gui_state = GuiState::ReplayEnd;
      }
  }
  ```
  It is a **second, live end-of-movie detector** competing with `dojo.cpp:1985`'s
  `MovieEnd()`-based one, on a different metric. Today the two agree, because a
  studio movie is dense from frame 0.

`[SOURCE]` The source fork has already converted exactly this site and left the
Receiving one alone — `davids_fly/core/rend/gui.cpp:4349`:
`if (dojo.frame_number == dojo.MovieEnd() && !cfgLoadBool("dojo","MacroMode",false))`,
with the same conversion at its `:5998`, `:6028`, `:6069` and the annotation at
`:5981`: *"true end - size() undercounts an offset Play-Macro roll (every READ
click seeked BASE)."*

**So the port converts this from latent to live.** The macro roll is the feature
that produces a movie starting above frame 0 — `[SOURCE]`
`core/dojo/dojo.cpp:1913` `InjectPendingMacroAt`:
`for (u32 i = 0; i < macro_pending.size(); i++) session_inputs[startFrame + i] = macro_pending[i];`
— and it lands with the studio. `gui.cpp:4576` will then end the movie at frame
`len` instead of `start+len`, which is the exact defect `dojo.h:148` says the
macro-parity audit found.

`[SOURCE]` A second producer of a non-zero start already exists, and already
says so in its own log line — `core/lua/lua.cpp:1888` `replay.startRecording`:
*"A movie started from Lua mid-session does NOT begin at power-on … this clip
needs a savestate to be replayable."* `[REASONED]` Interior gaps are *permitted*
by the map and by `ApplyEditResize`'s `session_inputs.erase(rf)`
(`dojo.cpp:1595`), but whether one is ever produced depends on what the piano
roll hands in — a row-delete that renumbers stays dense. **`[OPEN]`** Nothing in
this tree demonstrates an interior gap; leading gaps are demonstrated twice.

### The abstraction

A `MovieTimeline` view over the map, in the house style of `game_viewport.h` —
derived, never stored, one owner:

```cpp
// core/dojo/movie_timeline.h
namespace movie {
    bool authored();          // any frames at all
    u32  first();             // first authored frame (0 for a power-on movie)
    u32  end();               // one past the last authored frame  == today's MovieEnd()
    u32  count();             // how many frames are authored - a STATS number, never a frontier
    bool has(u32 frame);      // is this exact frame authored
    bool atEnd(u32 frame);    // the ONE frontier test every consumer calls
    bool dense();             // count() == end() - first(); the netplay/power-on shape
}
```

The value is not the six functions; it is that **`count()` and `end()` have
different names**, so a consumer has to choose, and the choice is reviewable. The
`atEnd()` entry collapses answers #2, #4 and #6 into one; #3 becomes
`!has(frame)` and keeps its distinct meaning; #7 and #8 become callers.

Netplay keeps `dense()` — the four legitimate `.size()` sites become
`movie::count()` with the Receiving guard unchanged, which documents *why* the
dense metric is right there instead of leaving it to be re-discovered.

### Cost

`[REASONED]` ~60 lines of header plus a thin `.cpp`, and about a dozen call-site
edits, four of which are the netplay sites and are renames only. The dominant
cost is deciding, per site, which of the three questions it meant — which is
work that has to happen anyway when the macro roll lands, and is cheaper as one
reviewed pass than as five separate bug reports.

### Unblocks

The piano roll. It is the single largest panel in the port —
`docs/STUDIO-IN-EMUAPI.md` §2.9 puts it at *"~3,400 lines inside
`dojo_gui.cpp`"* — and every row it draws asks "does this frame exist, and where does
the movie end". `docs/STUDIO-IN-EMUAPI.md` §5 also makes this the C++ side of its
payload item: `movie.prefixhash` / `savestate.anchor` are stated over exactly
this timeline, and `movie.framecount()`'s documented defect ("`session_inputs.size()`
during playback and `frame_number` during recording — the length in one mode and
the playhead in the other") is one of these eight answers leaking into the Lua
surface.

### Do **not** change the `.flyr` header

`Player` / `Opponent` / `Quark` / `Relay Key` are dead weight for a TAS clip and
they should stay. They are a wire format with files on disk in the user's
`data/replays/`, the parser is version-gated, and none of them costs anything at
runtime. The TAS facts already live in sidecars and that layering is right —
`STUDIO-IN-EMUAPI.md` §2.1 makes the same call from the other direction ("the
anchor is neutral; the sidecar is not"). Adding a `startFrame` field would be
tempting and is not needed: `first()` is derivable from the records that are
already there.

---

## S3 — `dojo:` config has three read paths and no owner. **Cheap, and the port doubles the surface.**

### The concept being conflated

"The current value of a setting" is answered three ways, and they do not agree:

- **(a)** `config::X` Option objects, which **cache** at `Settings::load()`.
- **(b)** `cfgLoadBool/Int/Str`, a live read of the store.
- **(c)** `cfgSetVirtual`, which writes a **shadow section that wins every read
  and is never persisted.**

### Evidence

`[SOURCE]` Virtual wins outright, with no merge — `core/cfg/ini.cpp:126`
(`ConfigFile::get_entry`) checks `get_section(name, /*virtual*/true)` first and
returns immediately if the key is there. `cfgSetVirtual` (`core/cfg/cfg.cpp:102`)
writes only into `virtual_sections`; `ConfigFile::save()` (`ini.cpp:261`)
iterates only `sections`, so a virtual entry is never written and never cleared.

`[SOURCE]` **A `-config` flag therefore shadows every later save for the life of
the process.** `core/cfg/cl.cpp:53` parses `section:key=value` straight into
`cfgSetVirtual`, and the tool's own help text at `cl.cpp:80` claims *"virtual
config values won't be saved to the .cfg file unless a different value is written
to them"* — which the code does not do. `cfgSaveBool` writes the regular section,
`get_entry` never looks at it again, and the UI checkbox looks broken.

`[MEASURED 2026-09-08]` **Fifteen `dojo:` keys are read through more than one
mechanism** — `Training`, `Replay`, `Receiving`, `Transmitting`, `RecordMatches`,
`ReplayFilename`, `RelayKey`, `RelayServer`, `RelayAddressHistory`, `Relay`,
`TestGame`, `SpectateKey`, `Delay`, `AutoLoadNetState`, `Quark`. `Training` has a
`config::Training` Option (`core/cfg/option.cpp:175`) that is effectively
**decorative**: `[MEASURED 2026-09-08]` **33** sites call
`cfgLoadBool("dojo", "Training", …)` directly instead, and three sites write the
key only virtually. `RelayAddressHistory`
is worse in kind — `core/dojo/relay_client.cpp:393` writes the store with
`cfgSaveStr` *from* `config::RelayAddressHistory.get()`, so the cached Option is
stale until the next `Settings::load()`.

`[SOURCE]` The cost of the (a)/(b) split is written into this tree already —
`core/dojo/replay.cpp:10`:

> *"Read the LIVE cfg value, not the `config::ReplayFilename` Option: the Option
> caches the value loaded at startup and only refreshes on `Settings::load()`,
> which runs AFTER this. The replay browser sets `ReplayFilename` via
> `cfgSetVirtual` just before boot, so the cached Option still held the PREVIOUS
> session's persisted path — picking any clip silently played the most recently
> recorded one instead (and pointed F3 at the wrong folder)."*

`[SOURCE]` And the one place that gets the (a)/(c) interaction right shows the
shape and why — `core/dojo/replay.cpp:23`:

```cpp
config::SavestateSlot.set(0);
// The .set alone is NOT enough: Emulator::loadGame re-runs Settings::load(true)
// mid-boot, which re-reads the Option from cfg and stomped this back ... (the
// "replay opened on slot 1" regression). Virtual entries win cfg reads, so the
// reload now re-reads 0.
cfgSetVirtual("config", "Dreamcast.SavestateSlot", "0");
```

The mid-boot reload it names is real — `core/emulator.cpp:492`:
`config::Settings::instance().reset(); config::Settings::instance().load(false);`.

`[MEASURED 2026-09-08]` **Correcting a piece of received wisdom:** the "write
BOTH `cfgSetVirtual` and `cfgSave*`" pattern that `davids_fly/CLAUDE.md`
prescribes **does not exist in this tree at all** — zero call sites pair them on
the same key. It exists in the source fork (e.g. its Windows-menu `cfgItem`
lambda, `davids_fly/core/dojo/dojo_gui.cpp:18788`, and its View-menu
`viewToggle`, `:18689`) and arrives with the port, hand-written per call site.

`[MEASURED 2026-09-08]` The source fork reads `dojo:` keys at **301 call sites
over 128 distinct keys** — `MacroMode` 14 times, `Training` 13, `TasUi` 11,
`RelayKey` 10. Every one repeats the default literal. To this fork's credit, the
defaults have **not** drifted: only two keys are read with differing default
spellings and both are benign (`SlotCycleCount`, `Relay`). The problem is not
drift today; it is that the port adds 76 keys and ~200 call sites to a system
where nothing prevents it.

`[SOURCE]` There is **no owner**: no `dojo::settings` namespace, no struct, no
accessor. Every file calls `cfgLoad*` or reads `config::X` ad hoc.

### The abstraction

One table, typed accessors, and a `set()` that does the right thing once:

```cpp
// core/dojo/dojo_settings.h
namespace dojocfg {
    // One row per key: name, type, default, and whether a launch flag may set it.
    // Reads are LIVE (the store, virtual-aware). There is no cached copy: a cached
    // copy is what config::X already is, and it is what silently played the wrong
    // clip (replay.cpp:10).
    bool  getBool(Key k);
    int   getInt (Key k);
    const std::string& getStr(Key k);
    // THE writer. Writes the virtual entry AND the persisted one, in that order,
    // because a -config launch flag shadows the persisted value for the life of the
    // process and a UI toggle that only persists looks broken.
    void  set(Key k, bool v);   // and int / string overloads
}
```

`Key` is an enum generated from the same table that holds names and defaults, so
a typo is a compile error rather than a silent default, and the default is stated
once. `[REASONED]` This also makes the TAS settings panel generable the way the
hotkeys panel already is.

Leave `config::X` Options alone for the flycast-wide settings they were built
for. The rule to state and enforce is narrower: **a `dojo:` key has exactly one
mechanism, and it is this one.**

### Cost

`[REASONED]` ~150 lines plus a mechanical sweep of the existing 56 keys.
Per-key cost during the port drops from "a literal default at every call site
plus remembering the virtual/persist dance" to one table row.

### Unblocks

76 new keys landing as 76 rows. The `-config` shadow becoming a documented
property of `set()` instead of a trap re-learned per checkbox. And a generated
settings panel — `[REASONED]` on the order of a few hundred more lines of the
port's hand-written UI that never has to be written.

---

## S4 — Four idioms for "stop the machine safely". **The port adds ~14 GUI-thread panels that mutate movie state.**

### The concept being conflated

"The machine must be still while I touch it" is implemented four ways, two of
them the new abstractions and two of them older hand-rolled rituals that the new
abstractions did not reach.

### Evidence

`[SOURCE]` The two good seams, both recent, both with their defect written into
the header:

- `core/pause.h` — a reason bitmask (`USER | STEP | LUA | MODAL`) where *"each
  owner sets and clears only ITS OWN reason, so no owner can cancel another's
  pause"*, and `core/pause.cpp:8` *"THE ARBITER ONLY RESTARTS WHAT IT STOPPED."*
  It replaced an OR-of-reasons in `gui.cpp` and four copies of a
  save-and-restore ritual in `lua.cpp`.
- `core/deferred.h` — *"Run something OUTSIDE both the ImGui frame and the
  emulation loop"*, drained once per frame from `mainui_rend_frame`
  (`core/rend/mainui.cpp:97`).

`[SOURCE]` `deferred.h` is also an unusually honest artifact: the thing it was
built for — a deferred savestate *restore* — **did not work and was removed**
rather than shipped. `core/lua/lua.cpp:2247` records the elimination table
(`dc_loadstate` + `pausing::Scoped(MODAL)` → wedges; + explicit stop/start →
wedges; with `rend.ThreadedRendering=no` → wedges; with no stop → wedges).
`[OPEN]` Why the identical call is fine from a `vblank` callback and wedges from
the drain point is still unidentified.

`[SOURCE]` The other two idioms, doing the same job by hand:

- Raw `emu.stop(); dc_loadstate(); emu.start();` in `gui_loadState` /
  `gui_saveState` (`core/rend/gui.cpp:4697`), plus a bare `emu.start()` in
  `gui_open_step` (`:4967`). `deferred.h:18` cites this as *"the supported
  shape"* — and it is, but it is a parallel mechanism, not one built on
  `pausing::`.
- `aroundStopped()` (`core/lua/lua.cpp:1214`), a second hand-written stop/start
  pair introduced **one day after** the arbiter, in the same file, for the
  `deferred::post` bindings.

`[SOURCE]` The riskiest shape found is an **unstated precondition**:
`gui_display_commands()` calls `dc_loadstate` / `dc_savestate` at
`core/rend/gui.cpp:919`, `:945`, `:1011` with **no stop of any kind at the call
site**. It is safe only because `GuiState::Commands` is reachable only through
`gui_open_settings()`, which stopped the emulator at `:741`. Nothing at the call
site says so. `[REASONED]` A future path into `Commands` — a Lua state
transition, or the `pausing::MODAL` route — makes three savestate calls race the
emulation thread, silently.

`[SOURCE]` `session_inputs` has **no lock**. Its safety is a stated convention:
*"The emu thread owns session_inputs whenever it runs (dojo.h), so an edit from
the Lua thread mid-frame is a data race on the movie"* (`core/lua/lua.cpp:613`).
`Dojo::ApplyEdit` — the documented single edit funnel (`dojo.h:228`) — takes no
lock itself and trusts its callers to have stopped the machine.

`[SOURCE]` And one open admission: `core/lua/lua.cpp:1694`
`.addFunction("startGame", gui_start_game)	// FIXME threading!`

`[SOURCE]` The bespoke locks are individually good and correctly argued —
`clipMutex` (`tas_clip.cpp:495`), `locked_ranges_mtx` with a published
emu-thread-readable snapshot (`dojo.h:349`, `dojo.cpp:1399`), `lua::mutex` with
the read-under-the-lock rule stated twice (`lua.cpp:117`, `:166`), the
`avi_dump` bounded queue (`avi_dump.h:34`). None of these should be merged into
anything.

### The abstraction

Not a new mechanism — a **rule plus a marker**, because the mechanisms already
exist and are good:

1. **`pausing::Scoped` is the only way to stop the machine for an operation.**
   Rewrite `gui_loadState`/`gui_saveState`/`aroundStopped` on top of it. That is
   three call sites; the arbiter's *"only restarts what it stopped"* rule already
   makes it safe to nest inside a `gui_state`-driven stop.
2. **A thread-affinity marker on every function that touches machine state**, so
   the precondition is at the call site rather than three files away:
   ```cpp
   #define TAS_REQUIRES_STOPPED   // documentation + a debug-build assert
   void gui_saveState() TAS_REQUIRES_STOPPED;
   s64  Dojo::ApplyEdit(...) TAS_REQUIRES_STOPPED;
   ```
   In a debug build it asserts `!emu.running() || pausing::mask() != 0`. In
   release it is a comment that cannot drift, because it lives on the declaration.
   `[REASONED]` This would have caught `gui_display_commands` on the day the
   assert landed, and it is the cheapest available answer to *"a clean build is
   not evidence a feature is wired"* applied to preconditions.

### Cost

`[REASONED]` ~40 lines, three rewrites, and one marker per machine-touching
function (about 15 today). The rewrite of `gui_loadState` needs care — it is the
one path `deferred.h` names as proven-safe, so change it and re-run
`scripts/docktest.sh` and the conformance suite rather than reasoning about it.

### Unblocks

The port adds ~14 panels that all run on the render thread and all mutate movie
state through `ApplyEdit` (paint, paste, insert/delete, macro place, the Input
Sender, the notepad). Today the rule that makes that safe is a sentence in
`dojo.h`. After the port there will be dozens of call sites relying on it, and
the marker is what stops the twentieth one from being written by someone who did
not read the sentence.

### Do **not** fold `buffering` and `stepping` into `pausing::`

The code already argues this and is right — `[SOURCE]` `core/rend/gui.cpp:761`:
*"buffering and stepping stay as themselves ON PURPOSE. They are not pause
REASONS, they are state machines that imply a stop: stepping carries
target_step_frame and drives the scrub … Folding those into bits would throw the
information away."*

---

## S5 — "What kind of session is this" is an ad-hoc conjunction of cfg keys

`[MEASURED 2026-09-08]` In the source fork, `MacroMode` is read 14 times,
`PlayMacro` 8, `RecordMatches` 5, `Replay` 5, `Training` 13, alongside
`dojo.play_match` (16 reads in `gui.cpp`, 6 in `lua.cpp`). The status pill in the
menu bar computes the session's identity inline — `[SOURCE]`
`davids_fly/core/dojo/dojo_gui.cpp:17948`:

```cpp
const char *mode = tas_clip::labIsActive(hostfs::savestateFolderOverride) ? "TEST LAB"
        : macroMode ? (cfgLoadBool("dojo","PlayMacro",false) ? "PLAY MACRO" : "RECORD MACRO")
        : dojo.play_match ? "REPLAY"
        : cfgLoadBool("dojo","RecordMatches",false) ? "RECORD MOVIE" : "";
```

The fork has started the fix on its own — `tasMacroMode()` and `tasStudioMode()`
(`dojo_gui.cpp:16921`, `:16915`) — and 25 raw call sites remain.

Adjacent and worse: the **boot handoff** is a five-flag hand-rolled state machine
(`boot_ready_arm`, `replay_bootload`, `macro_fullload`, `onenter_ff`,
`clip_ready_pending` — `core/dojo/dojo.h:145,336-338,146-147`) set in
`dojo.cpp` / `replay.cpp` and consumed across `gui.cpp:4364-4473`, `:5627`,
`:5995`, `:6025`, `:6049`, `gamepad_device.cpp:119`. `[REASONED]` Five booleans
that must be cleared in the right combinations on the right frame is a state
machine written as flags; `dojo.cpp:2852-2863` (`Reset`) is the evidence — six
lines of clearing with a comment explaining which leak each one prevents.

**Abstraction:** `enum class Session { JustPlay, RecordMovie, Replay, RecordMacro,
PlayMacro, TestLab }` plus `Session current()`, derived once per frame from the
cfg keys, and a small `BootHandoff` enum replacing the five flags. **Cost:** low
for the session enum (it is a switch over facts that already exist); moderate for
the boot handoff, which needs the transitions written down before they can be
enumerated. **Unblocks:** every panel's `enabled()` predicate in S1 becomes a
`Session` test instead of a cfg conjunction, which is where the 25 raw reads go.

**Rank note:** this is worth doing *after* S1, because S1 is what creates the
demand for a clean `enabled()` and would otherwise create 14 more conjunctions.

---

## S6 — The `Dojo` god object. **Real, and the wrong thing to attack directly.**

`[MEASURED 2026-09-08]` `core/dojo/dojo.h:91-386` — one class, **98 data members
and 55 methods**, spanning five unrelated concerns:

| cluster | members (examples) | belongs to |
|---|---|---|
| netplay / match | `hosting`, `player_1/2`, `presence`, `relay_client`, `tcp_client`, `p1_wins`, `current_p1_wins`, `last_score_frame`, `FirstToPoll` | the netplay fork |
| movie / timeline | `session_inputs`, `frame_number`, `MovieEnd`, `ApplyEdit`, `ApplyEditResize`, `undo_stack`, `rewind_log`, `IsStateStale`, `MoviePrefixHash`, `divergence_open`, `stale_tail_from` | **S2** |
| clip / generations | `ArchiveGeneration`, `RestoreClipDir`, `RecordGeneration`, `ReconcileGenerations`, `BeginClipStats`, `live_from_gen`, `clip_sessions`, `edit_base` | mostly `tas_clip` already |
| UI / gesture state | `hotkey_overlay`, `shift_held_since`, `save_hold_since`, `save_flash_at`, `slot_held*`, `step_held*`, `next_step_time`, `snapshot_prompt_*`, `clip_ready_text` | **S1** (panel state) |
| session / boot | `play_match`, `macro_armed`, `boot_ready_arm`, `macro_fullload`, `onenter_ff`, `macro_pending`, `loaded_macro_path` | **S5** |

`[MEASURED 2026-09-08]` It leaks widely: 32 files outside `core/deps` reference the
`dojo` global; `gui.cpp` touches 19 distinct members, `lua.cpp` 11. Both reach
`dojo.session_inputs` **directly** (4 and 8 times) rather than through any
accessor.

**Do not attack this as a project.** A "split the god object" commit is a large
diff with no observable, which is the shape this repository's own testing
doctrine says is worth least. Every cluster above is *already assigned* to one of
S1, S2, S5 or `tas_clip`, and each of those has an observable. `[REASONED]` If
S1, S2 and S5 land, the UI/gesture, movie and session clusters leave `Dojo` as a
by-product, and what remains is a netplay session object with a movie reference —
which is a defensible thing for a class called `Dojo` to be.

The one thing worth doing early and alone: **stop `gui.cpp` and `lua.cpp`
touching `session_inputs` directly.** That is 12 call sites and it is S2's
interface anyway.

---

## What is NOT worth abstracting

Stated plainly, because premature abstraction has a cost and several of the
shapes below are deliberate and documented.

**A CMake library boundary for `core/dojo`.** `[SOURCE]` `CMakeLists.txt:1006-1041`
lists every dojo file into the single `${PROJECT_NAME}` target. Splitting it into
a static library would express a dependency direction that is not true —
`dojo.cpp` calls `gui_locked_ranges` and `gui_display_notification`; `gui.cpp`
calls into `Dojo`. The cycle is the honest state of affairs and a build-system
boundary would only force `extern` shims around it. Fix the cycle by moving code
(S1, S2) if it bothers you; do not fix it in CMake.

**Converting the `GuiState` screens to the panel registry.** See S1's closing
note. They are mutually exclusive by design.

**The `.flyr` netplay header.** See S2's closing note. Wire format, on disk,
version-gated, costs nothing.

**A generic "derived per-frame lane" framework** for `tas_wave` / `tas_ruler`.
`docs/STUDIO-IN-EMUAPI.md` §7 already declines this with the right reason — two
lanes, both game- or host-specific at the tap, and *"wait until two components
want one."* Agreed. What they share is invalidation, and invalidation comes free
from S2's prefix hash.

**The `tas_*` modules.** `[SOURCE]` They are already the best-factored code in
the tree: a namespace, no ImGui, stated rules, and a stated defect history.
`tas_clip.h:10` — *"everything that touches a CLIP FOLDER and its clip.json, with
no ImGui and no session state … Before this module the same clip.json was
read-modify-written from nine places with two path spellings and two dump
formats, and three private 'is this a backup folder' predicates disagreed."*
`tas_ruler.h` states its thread preconditions on each function. **Leave them
alone and copy their style.**

**`core/rend/game_viewport.{h,cpp}`, `core/deferred.h`, `core/pause.h`.** These
are the model. Each is under 110 lines, states its contract, names the defect it
prevents, and says what it deliberately does *not* do. Every seam proposed above
should look like these and none of them should be reopened.

**Merging the bespoke mutexes** (`clipMutex`, `locked_ranges_mtx`, `lua::mutex`,
the `avi_dump` queue). Each guards a different piece of data with a different
access pattern, and each carries a written argument for its shape. A shared lock
would be a coarser one.

**`pausing::` absorbing `buffering` / `stepping`.** The code argues it; see S4.

**Rewriting the studio on emuapi `ui.*` instead of porting the C++.** This is not
a "not worth it" so much as a **decision that is not this document's to make**
(see §0). What can be said: `STUDIO-IN-EMUAPI.md` classifies the great majority
of the studio as HOST furniture, and furniture does not get cheaper by being
written in Lua. The two items it classifies as the cheapest genuine wins — the
Input Visualizer (~371 lines of C++ → ~40 of Lua) and autofire (~30 lines) —
are both small and both independent of everything above.

---

## Sequencing

Each step is shippable alone and each produces evidence, in the order that makes
the next one cheaper.

**0. `core/dojo/movie_timeline.h` and the twelve call sites (S2).** Small,
self-contained, and it converts a latent bug into a fixed one before the feature
that triggers it arrives.

The observable, and it must be able to fail: a diagnostic flag that asserts, on
every replayed frame, that **the two live end-of-movie detectors agree** —
`frame_number == session_inputs.size()` (`gui.cpp:4576`) and
`frame_number == MovieEnd() - 1` (`dojo.cpp:1985`). On a power-on movie they
agree and the check is silent, which is the control. Then drive it with a movie
whose first frame is not 0, which this tree can already produce two ways
(`replay.startRecording` from Lua, `core/lua/lua.cpp:1888`; or
`InjectPendingMacroAt` with a non-zero `startFrame`, `core/dojo/dojo.cpp:1913`),
and require it to fire. Both halves are needed: without the sparse case the
check is decoration, and without the dense control a check that fires
unconditionally would look like the same result.

`[REASONED]` Note the honest limit — `TODOS.md` records that a Lua-started movie
"cannot be replayed" for reasons *upstream* of this, so the sparse half may have
to be driven by constructing the map directly rather than by replaying a file.
Say which was done; a check that quietly never ran is the failure this project
keeps paying for.

**1. `core/rend/tas_panels.{h,cpp}` (S1), with the existing overlays as its first
rows.** Not the ported panels — the ones already here (`#one`, `#two`, `#pos`,
`#one_input`, `#two_input`). `[SOURCE]` They already have persisted flags
(`config::PlayerNameOverlay`, `ReplayPositionOverlay`, `ShowTrainingInputDisplay`,
`ShowReplayInputDisplay` — `core/cfg/option.cpp:167,221,228,229`), so the
registry is not adding persistence here; it is proving the *other* four fields on
windows you can afford to break. The observable is `streams`: give each row its
stream mask, then check that every overlay draws exactly once per frame in both
the OSD and Paused paths — the double-render and the vanish-with-the-roll are the
two defects the source fork shipped, and both are a wrong stream set. Sabotage a
row's mask and require the check to notice; a check that passes on any mask is
not checking the field.

**2. `dojocfg` (S3), swept over the existing 56 keys.** Do it before the port so
the 76 new keys arrive as table rows. The observable is a test that sets a key
via a simulated `-config` flag, toggles it through `set()`, and requires the read
to change — which fails against every UI toggle in the tree today.

**3. The `TAS_REQUIRES_STOPPED` marker and the three stop/start rewrites (S4).**
After step 1, because the panels are the population that needs it.

**4. `Session` and `BootHandoff` (S5).** After step 1, because that is what
creates the demand.

**5. Then port panels, one per commit, each one a `TAS_PANELS[]` row.** Start
with the Piano Roll: it is the largest, it is the heaviest consumer of step 0,
and if the registry and the timeline do not fit it, they are wrong and it is
cheap to find out on the first one rather than the fourteenth.

`Dojo` is never a step. It shrinks as a by-product of 0, 1 and 4, and what is
left of it is a netplay session object, which is what it was called.
