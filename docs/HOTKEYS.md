# Hotkeys — five files that have to agree

`[2026-09-10]`

## The shape of the problem

A hotkey in this tree is not one declaration. It is five, in four files:

| file | what it decides | missing it means |
|---|---|---|
| `core/input/gamepad.h` | the id exists | it does not compile |
| `core/input/mapping.cpp` | it **persists** | the binding is forgotten on restart |
| `core/rend/gui.cpp` → `dcButtons[]` | bindable on a Dreamcast layout | invisible to DC users |
| `core/rend/gui.cpp` → `arcadeButtons[]` | bindable on an arcade layout | invisible to arcade users |
| `core/input/gamepad_device.cpp` | it **does** something | the key is bindable and dead |

Each of those files is self-consistent and complete-looking on its own. That is
exactly why a hotkey with four of the five is invisible: nothing in the tree
held the *relationship*, so there was nothing for a reader to notice.

This is the same shape `core/rend/panel.h` was built to fix one layer up — "one
descriptor, one array, three loops", written after `docs/MODULARIZATION.md` §1
counted a panel costing 6–13 edits across four mechanisms.

## What the first audit found

`scripts/hotkeyaudit.py`, run for the first time on 2026-09-10 against 48
actions.

### 1. Six training hotkeys were bindable and dead `[FIXED]`

`EMU_BTN_RECORD_3/4/5` and `EMU_BTN_PLAY_3/4/5` were in the enum and in **both**
UI tables, and in neither `mapping.cpp` nor the dispatch switch.

`Training::record_slot` is `[6]` `[SOURCE]`, so slots 4, 5 and 6 are real and
work through the panel's own controls. The Controller Mapping window has always
offered "Record Slot 4/5/6" and "Play Slot 4/5/6"; binding one did nothing, and
the binding did not survive the process.

Fixed by adding the six persistence rows and the six dispatch cases. The option
names continue the existing `btn_record_N_` scheme rather than starting a tidier
one — a rename would silently drop every binding already on disk.

### 2. Two combos are the same combo twice `[RESOLVED]`

`[MEASURED]` comparing what each combo **dispatches to** rather than what it is
called:

```
A + X    <- EMU_CMB_X_A, EMU_CMB_1_4
B + Y    <- EMU_CMB_Y_B, EMU_CMB_2_5
```

Both pairs call `comboAssign` with the same two buttons in the opposite order.
One combo, declared twice — once with Dreamcast naming, once with arcade naming.

That is why `dcButtons` showed **two rows both labelled "X+A"** with no way to
tell them apart. It was not a typo in a label; the two rows genuinely did the
same thing.

**The ids and their persistence rows are kept.** Deleting a redundant id is the
tidy fix and the wrong one: a user may have `cmb_1_4_` bound in `emu.cfg`, and
dropping the id discards that binding silently. The alias still fires. It is
simply no longer *offered* for a new binding, so the duplicate row is gone.

### 3. `EMU_CMB_A_START` is absent from the arcade layout — correctly

An arcade cabinet has no A button in that sense. Its absence is the right
answer, not a gap, so it is an exemption with a stated reason rather than
something to "fix".

## The audit

```
scripts/hotkeyaudit.py              PASS: 48 actions, all consistent
scripts/hotkeyaudit.py --self-test  PASS: the audit can say no, and can say yes
```

Both are ctest entries (`flycast.hotkeyaudit`, `flycast.hotkeyaudit_can_fail`).
No emulator, no display, well under a second.

**It is a source-level check on purpose.** The property is that five tables in
four files agree; that property lives in the source, and the tables are
file-static. A runtime check would need surface added for the sole benefit of
the test. Reading the source is the honest instrument here, not a workaround.

**Exemptions are named, never inferred.** An audit that silently skips whatever
does not fit has no failure mode. Every entry in `EXEMPT` carries the reason it
is exempt, and **an exemption for an action that has since been wired is itself
reported** — otherwise the exemption list becomes the one place a real gap can
hide permanently. That claim has its own arm in `--self-test`, because
"we would notice" has to be demonstrated rather than asserted.

**Non-vacuity.** The parser refuses to report success if it finds fewer than 40
actions in the enum. An enum that stopped parsing would otherwise report
"all consistent" over nothing at all.

**Each sabotage asserts it applied.** `re.subn(..., count=1)` must return 1, or
the arm fails as "the sabotage patched nothing" rather than passing over
unmodified source — `CLAUDE.md`'s control-that-fails-to-apply.

## Not done, and why

## The five TAS actions that landed

`[2026-09-10]` Five of the fork's eighteen, and the cut is the point: **an id
must have something to dispatch to today.**

| action | does | via |
|---|---|---|
| `EMU_BTN_PIANO_ROLL` | toggle the piano roll | `panels::toggle("pianoroll")` |
| `EMU_BTN_SLOT_PICKER` | toggle the States wall | `panels::toggle("states")` |
| `EMU_BTN_SAVESTATE_SLOT_NEXT` | next slot, wrapping | `hostfs::currentSavestateSlot()` |
| `EMU_BTN_SAVESTATE_SLOT_PREV` | previous slot, wrapping | as above |
| `EMU_BTN_GEN_ARCHIVE` | archive the clip into `gen_NN` | `Dojo::ArchiveGeneration()` |

The remaining thirteen are held back because their **features** are not in this
tree: an input visualizer, a frame-skip test, an AVI toggle behind permanently
false guards. A bindable key that silently does nothing is the exact defect the
audit was written to catch, and shipping thirteen more of them to look complete
would be the worst possible use of it.

**`EMU_BTN_TOGGLE_READONLY` is held back for a different reason** — it is
serviceable but not *established*. `session::readOnly()` derives from
`dojo.play_match` `[SOURCE]`, and flipping that mid-session changes whether the
movie may grow past its last authored frame (`session::writeGrow()`), which the
edit funnel and the playhead both depend on. That is a claim about behaviour
nobody has measured, and a hotkey is a bad place to find out.

**No default keys.** `[SOURCE]` the fork defaults these to F2, F4, F5, F8 and
F9, and in this tree every one of those is already bound: `EMU_BTN_RECORD_1`,
`EMU_BTN_PLAY`, `EMU_BTN_PLAY_1`, `EMU_BTN_SAVESTATE`, `EMU_BTN_LOADSTATE`. That
fork repurposed flycast-dojo's training hotkeys for TAS; this one keeps training.
Adopting its defaults would silently take five keys off every existing user —
which is the divergence this branch exists to resolve, not to import. They ship
bindable and unbound, and both panels remain reachable from the View menu, so
the hotkey is a convenience rather than the only door.

**`panels::toggle()` is new**, and is separate from `panels::open()` on purpose:
a feature starting a session wants the window *shown*, a hotkey wants it
*flipped*, and one function taking a bool would push the caller into tracking a
state the registry already owns.

**His per-device hotkey editor is not being ported at all.** `[SOURCE]` his own
comment: *"The new-style TAS buttons aren't in mapping.cpp's persistence table
yet; wiring that is part of the planned Edit support."* He wrote ~640 lines of
bespoke rebind UI — a per-device editor pane, pad-to-pad binding copy, a
persisted drag-to-reorder — beside flycast's existing Controller Mapping window,
and his bindings still do not survive a restart. Flycast's own mapping UI is
driven by the `Mapping[]` tables; a row there gets rebinding *and* persistence
for free. The 640 lines are work the host already does.

## Pressing a key and proving the action ran

`scripts/hotkeytest.sh`, a ctest entry. It writes its own mapping file, presses
keys through XTest on a private Xvfb, and judges from the emulator's own traces.

It took **nine runs to get right**, and every one of the eight failures was the
harness or the guard rather than the binding. Worth listing, because each is a
different way for "the hotkey did not work" to be a wrong diagnosis:

1. **The mapping file was for a device that does not exist.** It wrote
   `SDL_Keyboard.cfg` — the name in the developer's own config directory — and
   the emulator loaded it, for a phantom. The keyboard SDL actually enumerates
   here is `Kinesis Freestyle2 PC - KB800`. Hardcoding *that* would pass on one
   laptop and fail everywhere else, so the harness reads the filename out of the
   emulator's log instead. `find_mapping` prints the name it wanted, because
   reconstructing it outside the process means reimplementing
   `make_mapping_filename`'s nine character substitutions.
2. **A ROM-less probe enumerated nothing** — mappings load from `Event::Start`.
3. **The clip belonged to a different game**, and another run's clip was 120
   frames. Both left the emulator in `GuiState::ReplayEnd` — the movie exhausted
   — long before a key was sent, and every TAS hotkey is correctly silent there.
4. **`xdotool windowactivate --sync` aborted**: the minimal i3 this harness
   starts does not advertise `_NET_ACTIVE_WINDOW`.
5. **`grep -E` does not expand `\t`**, so reading `GuiState::Paused`'s ordinal
   out of the header matched nothing and the run skipped.
6. **SDL eats F11.** `core/sdl/sdl.cpp` intercepts `SDLK_F11` for *"Alt-Return
   and F11 toggle full screen"* and consumes the **key down**, so the mapping
   only ever sees the key up — and every hotkey is guarded on `pressed`. **F11
   is not bindable in this emulator.** The trace showed it plainly:
   `HOTKEY: id=0x3000031 up` with no matching `down`, three runs running.

**The instrument had to exist before any of that was visible.**
`dojo:HotkeyTrace` logs every action reaching the dispatch, with the GuiState.
Before it, three completely different faults — the key never arrived, the key
arrived bound to nothing, the key arrived and the guard refused it — were the
same silence, and the harness printed the same wrong sentence for all three. It
now says which, and prints the trace.

Two values it derives rather than hardcodes, for the same reason the audit
exists: `GuiState::Paused`'s ordinal and `EMU_BTN_PIANO_ROLL`'s id are both
counted out of the headers. A literal would rot the first time an entry is
inserted above them.

### The design defect the test found

**`!gui_is_open()` is the wrong guard for a TAS hotkey**, and every other hotkey
in this tree uses it. `gui_is_open()` is true for every `GuiState` except
`Closed` `[SOURCE]` — including **`Paused`**, which is precisely when a TAS user
edits. A piano-roll hotkey guarded that way cannot be pressed at the only moment
it is wanted.

`gui_is_closed_or_paused()` is the rule, spelled as the two states it allows
rather than the fifteen it does not, so a state added later is refused by
default — the safe direction for something taking keys away from a menu.

The sabotage arm is the old guard: reverting it makes the test fail with *"the
key REACHED the dispatch and the action did not run; the guard refused it"*,
which is the bug, correctly named.

**The existing hotkeys are not migrated to a registry.** A `hotkeys::add()`
mirroring `panels::add()` would make the four sites loops over one descriptor
and is clearly where this ends up. `[UPDATED 2026-09-10]` the stated blocker —
*"nothing in the tree drives a hotkey end to end"* — **is gone**:
`scripts/hotkeytest.sh` does. The migration is now a refactor with a test under
it rather than one without, and it is the next piece of this work rather than an
open question.

What it still wants first is breadth. The audit covers the **shape** of all 53
actions; the test covers the **behaviour** of three. A migration that silently
broke the fourth would pass both.
