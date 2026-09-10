# The States window — the lift, and what our tree can and cannot answer

`[MEASURED 2026-09-09]` Two surveys: the fork's snapshot pane, and this tree's
own savestate machinery. Read `docs/PIANO-ROLL-LIFT.md` first for the method.

---

## 0. A correction before anything else

**`show_states_snapshots` is not "the States window".** It is ten lines that
delegate:

    void DojoGui::show_states_snapshots(float height)
    {
        ...
        if (!dir.empty() && height > 0.f)
            tasClipSnapshotsPane(clipUiStates, dir, ..., "StatesSnapshotsPaneH", ...);
        tasClipPopups(clipUiStates, scaling);
    }

The States window proper is the **100-slot thumbnail wall**, and it lives in
`core/rend/gui.cpp` (`gui_draw_slot_picker` / `drawSlotBrowser`), not in
`dojo_gui.cpp`. What follows is the **GENERATIONS pane** docked underneath it —
a snapshot-library browser. `[OPEN]` the wall has not been lifted.

That correction is itself the first finding: the pane is **already a widget with
a host binding**, shared by four hosts (`replays`, `macros-preboot`,
`macros-studio`, `states`), and the binding is five string arguments. Only three
`DojoGui` methods exist solely for this window.

## 1. The lift

| | count |
|---|---|
| distinct identifiers | 365 |
| ambient (already in our tree) | 142 |
| **real dependencies** | **131** |
| cascade noise (locals) | 90 |

| cluster | count |
|---|---|
| the window itself | 60 |
| savestate / host | 27 |
| record model (the `clip.json` schema) | 15 |
| movie / clip library — **all dead in this window** | 4 |
| chrome | 20 |
| platform (config, notification, clock) | 5 |
| **game-specific** | **0** |

**Zero game-specific symbols.** `grep -n "mvc2\|Marvel\|MVC2"` over the subject
returns nothing — no character table, no combo meter, no memory address. Where
the piano roll had a 4-symbol column-model cluster to divorce, here that cluster
is empty. This window is already game-agnostic; the port's cost is entirely the
host interface.

The couplings that matter reduce to seven names: which clip folder is bound to
the running session, whether a game is running at all, which snapshot the live
files came from and whether they have diverged since, a just-captured snapshot
wanting focus, a cache-invalidation epoch, three archive/restore verbs, and a
library version ticket.

## 2. The host interface — ours covers about one and a half of seventeen

`roll_host.h` asks four questions about **individual live slots relative to the
movie**. This window asks about **whole snapshot sets on disk**. The overlap is
nearly nil.

- `slotFrame` — partially. `Generation::atFrame` / `movieFrames` are the
  archived equivalents, and `Generation::slotFrames` is literally the same fact
  per slot, asked of a folder instead of the live set.
- `framesToSlots` — no.
- `slotStale` — no. Its analogue is `liveEdited`, which is coarser: "the live
  set changed since the restore", not per-slot.
- `staleNoticePhase` / `staleNoticeWasDeletion` — no backing, but structurally
  identical to `restoreDoneAt` + `restoreDoneGen` + `restoreDoneDir`: a
  transient timed notice with a variant flag. **Generalise the notice mechanism
  rather than writing a second one.**

Seventeen questions the window needs, grouped: session binding (2), snapshot
library read (4), snapshot library write (4), live-set provenance (2), capture
and restore (3), a capture-focus event (1), invalidation (1).

Two of those deserve naming now because they delete a leak each:

- **`takeCaptureFocusRequest()`** — a consume-once queue replacing
  `dojo.snapshot_reveal` + `snapshot_prompt_name` + `snapshot_prompt_pending`
  **and** the `strcmp(host, "states") == 0` inside an otherwise host-agnostic
  function. Which view gets the reveal is a policy the host should express, not
  a string compare.
- **`canRestore(setPath) -> { allowed, reasonText }`** — replaces three inlined
  live-set computations and lets the refusal text come from the host instead of
  being hardcoded in the widget.

## 3. What our tree can already answer

Better than expected, and two of my own prior claims were stale.

**`[CORRECTED 2026-09-09]` The 0..9 slot cap is already fixed.** `docs/`'s plan
and this session both repeated it. `luaSavestateSlot` now bounds on
`hostfs::MAX_SAVESTATE_SLOTS`, `savestateAnchor` is 0-based, and `slotCount()`
derives from the same constant. `scripts/tests/slots.lua` pins it by asking the
host rather than a literal.

**The clocks agree — this was the biggest unknown and it is good news.** The
`.frame` sidecar stores `dojo.frame_number`, which `docs/FRAME-CLOCKS.md` calls
the **movie index**, and that is exactly the key space of `session_inputs` —
the roll's rows. `LoadStateFrame` assigns straight across with no conversion.

**The staleness rule exists and is exact:**

    bool Dojo::IsStateStale(u32 stateFrame, u32 stateSeq) const
    {
        for (const auto& r : rewind_log)
            if (r.first > stateSeq && r.second < stateFrame)
                return true;
        return false;
    }

A rewind LATER than the save that went BELOW the state's frame. Plus a v3
overload that exonerates on a matching movie-prefix hash, so edit-then-undo
nets to clean. The sidecar is **v3**, not v2: `frame | rerecordSeq | movieLen |
prefixHash`, 20 bytes, each version a strict prefix extension so old readers
keep working.

**In-memory restore is 7.7 ms** against ~500 ms for the disk path — 65×, and the
single most important number for any live preview.

## 4. Four second-order clock hazards

The clocks agree; these do not follow from that.

1. **The record head leads the playhead by `config::Delay`.** Rows are applied
   at their index but written `+ Delay` early, and the timeline-event detector
   runs on the offset value.
2. **A state is captured BEFORE its frame is applied.** `MoviePrefixHash`
   excludes `frame` itself for this reason. `movie.h` exists because this ±1 had
   four spellings; a States window drawn on the render thread is in the AFTER
   phase while the sidecar was written in the BEFORE phase.
3. **`frame == 0` is overloaded to mean "no anchor".** A state legitimately
   saved at movie frame 0 — which is exactly what a power-on BASE state is — is
   indistinguishable from a state with no sidecar. `haveSeq` exists for the
   adjacent field; there is no `haveFrame`.
4. **A resize renumbers rows and never rewrites sidecars.** The anchored frame
   becomes **wrong**, not merely suspect, and nothing distinguishes those. See
   `docs/ROLL-EDIT-MODEL.md` §3 — the remap is the fix.

## 5. Gaps — what a States window would ask and this tree cannot answer

- **G1 — "is slot N stale?"** The rule exists; the accessor does not. Needs the
  memoised per-slot verdict plus its invalidation triple. Unmemoised,
  `IsStateStale` re-hashes the movie prefix per call, and the fork measured its
  window asking ~200×/frame.
- **G2 — "what just changed?"** There is no staleness *event*: no
  `staleEventAt`, no clean→stale flip scan. And `dojo.savestate_epoch` is **not
  bumped on `dc_savestate`**, despite its own comment claiming "bumped on every
  savestate write".
- **G3 — "which slots exist right now?"** `scanSavestateInfo()` does a directory
  walk plus up to 300 file opens, with **no cache and no throttle here**; the
  fork's 0.5 s tick was not ported.
- **G4 — "was this saved at movie frame 0?"** Unanswerable (see §4.3).
- **G5 — "does this slot actually load?"** Unanswerable. `dc_loadstate`
  swallows every failure and returns `void`, so **`savestate.load(n)` on an
  empty in-range slot returns `true`**. That also falsifies a comment in
  `emuapi/adapters/flycast.lua` which asserts "the host raises when the slot
  holds no state" — it does not, so the adapter's nil-plus-reason path is
  unreachable for the case it was written for.
- **G6 — thumbnails.** `hasPng` is read, `clip.json` records `thumb`, deletion
  removes `.png` — and **nothing in this tree ever writes one**.
  `docs/STUDIO-IN-EMUAPI.md` already refuses this: `GetLastFrameRGB()` is
  DX9/DX11 only and does not exist on our GL build.
- **G7 / G8 — rename and delete.** `saveSavestateLabel` and `deleteSavestate`
  exist with **zero callers**.
- **G10 — "is this slot locked?"** `gui_locked_ranges` is a stub returning
  empty, so the lock UI has state to toggle and no effect to produce.
- **G11 — "how many slots?"** Four owners, and one is worse than a count
  mismatch: flycast's own pause menu offers `for (int i = 0; i < 10; i++)` and
  labels them **1-based**, so slot 0 displays as "1". A States window showing
  "slot 0" beside a pause menu showing "1" for the same file reads as two
  different states. **Pick a side loudly.**
- **G12 — "is an in-memory state stale?"** Structurally unanswerable: the
  `DOJOFRM1` trailer carries `frame` only, so memory-resident states cannot
  carry a verdict at all.
- **G14 — "is saving even legal?"** Three different conjunctions; see
  `docs/SESSION-KINDS.md` §4 #6.

## 6. The finding that changes what to do first

**`roll::Host` has no production implementation.** `roll::setHost()` is called
exactly twice, both inside `roll_profile.cpp`'s own self-test, against a `Probe`
that answers `slotFrame() = 7`. There is no real host anywhere, so the piano
roll's savestate gutter has **never displayed a real slot** — the panel is
written against a possibly-null host and prints "No host installed".

That is this session's own trap, one more time: a seam with a self-test and no
customer. And it is cheap to close, because everything the four accessors are
built on — `hostfs::scanSavestateInfo`, `Dojo::IsStateStale`, `dojo.rewind_log`,
`dojo.savestate_epoch` — is already ported. Only the accessors and the
memoisation are missing.

So the order is: **implement the production host first**, because it makes an
already-shipped panel honest, it is the smallest piece of the States work, and
it forces the memoisation (G1) and the scan cache (G3) that the wall will need
anyway.
