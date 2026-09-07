# Sync settings — the `sync{}` block

Draft, 2026-09-06. Written in a scratch clone (`flycast-sync-draft`) so nothing
touches the working tree. Nothing here is compiled or tested; the classification
is the part worth arguing about, the code is comparatively mechanical.

---

## The idea in one line

**A movie must carry everything that changes what the game does.** Anything that
affects emulation and lives outside the movie is a silent desync waiting to
happen.

This is BizHawk's *SyncSettings* distinction. Their cores split every option in
two: **Settings** (cosmetic — free to change any time, never recorded) and
**SyncSettings** (affects emulation — stored inside the movie; changing them
invalidates it). It is the single thing that makes their movies portable between
machines and across time. `BIZHAWK_NOTES.md` covers TAStudio, bk2 and greenzone
but not this, so it is the one large idea from that codebase still unsurveyed.

The cheats question is the clearest case. A cheat is a memory write at a defined
point — perfectly deterministic, perfectly recordable. Upstream disables cheats
for netplay because synchronising cheat state across two peers is more work than
forbidding it. That is a **netplay convenience, not a law of determinism**. The
fix is not to ban cheats; it is to record that they were on.

---

## Why this codebase makes it cheap

Two pieces already exist and neither was built for this.

**1. There is already a registry.** Every option self-registers in its
constructor (`core/cfg/option.h`):

```cpp
Option(const std::string& name, T defaultValue = T(), const std::string& section = "config")
    : section(section), name(name), value(defaultValue), ...
{
    settings.options.push_back(this);        // <- every option, automatically
}
```

So the manifest can be **derived by walking `settings.options`**, never
hand-maintained. That matters for the same reason `emu.supports()` is derived in
emuapi: a hand-kept list drifts from reality the moment someone adds an option,
and a *silently incomplete* sync manifest is worse than none — it makes a movie
look verified when it is not.

**2. `override()` is already the apply mechanism.** It is exactly what upstream
uses for its own determinism guards:

```cpp
// flyinghead f8d5517b8 — "Disable overclocking for ggpo and online games"
config::Sh4Clock.override(200);
```

So *applying* a manifest is a loop of `override()` calls against machinery that
already exists and is already proven by upstream's netplay pins.

---

## Patch shape

Four virtuals on `BaseOption`, one flag on `Option`, one accessor on `Settings`.

```cpp
class BaseOption {
public:
    virtual ~BaseOption() = default;
    virtual void save() const = 0;
    virtual void load() = 0;
    virtual void reset() = 0;

    // --- sync manifest ---
    virtual bool        isSync()    const { return false; }
    virtual std::string syncKey()   const = 0;   // "section.name"
    virtual std::string syncValue() const = 0;   // current value, as text
    virtual void        syncApply(const std::string& v) = 0;  // -> override()
};
```

`Option` gains a third template parameter (it already has two —
`Option<int, false> SavestateSlot` uses the second for per-game config):

```cpp
template<typename T, bool PerGameOption = true, bool SyncCritical = false>
class Option : public BaseOption {
    bool isSync() const override { return SyncCritical; }
    ...
};
```

Declaration sites then carry their own classification, so it cannot drift from
the option it describes:

```cpp
Option<int,  true, true>  Broadcast("Dreamcast.Broadcast", 0);   // sync-critical
Option<bool, true, false> ShowFPS  ("rend.ShowFPS");             // cosmetic
```

`Settings::options` is currently private; the manifest needs a const accessor.

**Writing** the manifest: walk the registry, emit `{syncKey: syncValue}` for
every option where `isSync()`. **Applying** it: walk the manifest, look up by
key, `syncApply()`. An unknown key means the movie was made by a newer build —
that is a **refusal**, not a warning (see failure policy below).

---

## Classification

190 options. The overwhelming majority are cosmetic. What follows is the
sync-critical set, grouped by how confident I am.

### Certain — guest-visible machine configuration

| Option | Why |
|---|---|
| `Dreamcast.Broadcast` | NTSC/PAL. **50 Hz vs 60 Hz** — the entire frame timeline differs. The single most destructive one. |
| `Dreamcast.Region` | Games branch on it; BIOS differs. |
| `Dreamcast.Language` | Games read it. |
| `Dreamcast.Cable` | VGA vs composite selects a video mode; games branch on it. |
| `UseReios` | HLE BIOS vs real BIOS — different code executes at boot. |
| `dojo.ForceRealBios` | Same axis, opposite lever. |
| `Dreamcast.FullMMU` | Changes address translation. |
| `Dreamcast.ForceWindowsCE` | Changes MMU and timing setup. |
| `Dynarec.Enabled` | Interpreter and recompiler are not cycle-identical in edge cases. |
| `Dynarec.idleskip` | Skips idle loops — changes cycle accounting. |
| `ForceFreePlay` | Patches NAOMI settings. |
| `aica.DSPEnabled` | AICA DSP state is serialized; toggling changes the audio state machine. |

### Certain — upstream already guards these for GGPO

If flyinghead disabled it for rollback, it is nondeterministic, and the local
record/replay case is the same problem with a different name.

| Option | Upstream commit |
|---|---|
| `rend.ThreadedRendering` | guarded at `emulator.cpp:951` |
| `rend.EmulateFramebuffer` | `51758b965` "ggpo: disable full framebuffer emulation" |
| `Sh4Clock` | `f8d5517b8` — **absent from this base**; add when rebasing onto dojo-7 |
| *(elan / Naomi 2)* | `0362fd107` "disable elan when net rollbacking" — not an option here, a code path |

### Certain — input transformed before the guest sees it

These change the actual button state delivered to the machine, so a movie
recorded under one setting is simply a different movie under another.

| Option | Why |
|---|---|
| `SOCDResolution` | Resolves simultaneous opposing directions. Left+Right becomes *something*, and which something is a setting. |
| `input.EnableDiagonalCorrection` | Rewrites analog values. |
| `input.MouseSensitivity` | Scales mouse deltas, where a mouse device is bound. |

### High confidence

| Option | Why |
|---|---|
| `pvr.AutoSkipFrame` | Skips frames based on **measured performance** — a wall clock in the loop. |
| `ta.skip` | Skips TA processing. |
| `rend.RenderToTextureBuffer` | RTT results land in guest-visible VRAM. |
| `rend.WidescreenGameHacks` | Patches guest memory for some titles. |
| `FastGDRomLoad` | Changes GD-ROM timing; games can and do race it. |
| `Debug.SerialConsoleEnabled` | Touches SCIF — and **a SCIF timer reschedule was one of the two real desync bugs David found**. |
| `network.EmulateBBA` | Adds a device to the machine. |

### Needs measurement before deciding

Flagged rather than guessed. Each is plausibly host-side pacing, and plausibly
not.

`rend.DelayFrameSwapping` · `rend.FixedFrequency` ·
`rend.FixedFrequencyThreadSleep` · `pvr.MaxThreads` · `aica.LimitFPS` ·
`rend.DupeFrames`

The test for each is mechanical once the anchor assertion exists: record a clip,
replay it with the option flipped, compare state hashes. That is a better
answer than reasoning about it.

### Sync-critical but *not* in `option.cpp`

The dangerous ones, because a registry walk will not find them.

- **Cheats.** A separate subsystem. Needs its own manifest entry: which cheats,
  and their enabled state.
- **Maple device configuration** — device type and subtype per port. Lives in
  `settings.input`, not in options. Changes what the guest polls. Absolutely
  sync-critical.
- **VMU / flash contents.** Not configuration at all. This is the *initial
  conditions* bucket, and it is what the savestate anchor exists to pin.
- **Per-game config.** `Settings::load(true)` applies a per-game section that can
  silently override any option above. Two people with identical global settings
  and different per-game sections diverge. This codebase already has a scar from
  it: `Emulator::loadGame` re-runs `Settings::load(true)` mid-boot, which stomped
  a `SavestateSlot` set and produced the "replay opened on slot 1" regression.
  **The manifest must be captured after per-game config is applied**, or it
  records a lie.

---

## Failure policy

Borrowed from emuapi's failure tiers, and from the distinction that a silent
degrade is the worst outcome:

- **Mismatch on replay → force the recorded value.** That is the whole point;
  `override()` already does it and upstream already relies on it.
- **Unknown key in the manifest → refuse to play, loudly.** The movie was made by
  a build that knew about something this one does not. Playing anyway produces a
  desync with no explanation attached.
- **Missing key that this build considers sync-critical → refuse.** Same
  reasoning in the other direction: an older movie predates a classification, so
  its value is unknown rather than default.
- **Never warn-and-continue.** A determinism failure surfaces thousands of frames
  later, in front of whoever is watching the combo, not in front of the author.

Cosmetic settings are untouched in every case. Change the renderer, the
resolution, the filtering mid-replay — none of it is recorded and none of it
matters.

---

## Relationship to the anchor assertion

Two halves of one guarantee.

- The **`sync{}` manifest** pins *configuration* — everything the emulator was
  told to do.
- The **savestate anchor** pins *initial conditions* — BIOS, flash, VMU, boot
  RNG, everything the machine had already become before frame 1.

Together they close the "same world" question. What remains after both is the
genuinely hard residue that neither can fix: host CPU differences (**FMA**
rounding, `879372cb7`), uninitialised memory, and thread races. Those need real
fixes, not bookkeeping.

The anchor also *tests* the manifest. Replay from power-on to frame N, hash,
compare against the anchor's recorded hash. If a sync-critical option is missing
from the classification, that comparison is what catches it — and because David's
clips carry up to 100 savestates each, the existing library is already a corpus
of assertion points, and the first failing anchor bisects the movie for you.

---

## Suggested order

1. Add the four virtuals + the template parameter. Classify nothing yet; the
   manifest comes out empty and everything still works.
2. Write and read the manifest, empty. Prove the plumbing round-trips.
3. Classify the **Certain** tiers only. That is ~22 options and most of the value.
4. Build the anchor assertion (needs `savestate.hash`, and needs
   `verifyLoadedStateIdempotent` first — a hash of an unstable serialization is
   noise).
5. Use it to settle the **Needs measurement** list empirically.
6. Cheats, maple config, per-game capture — the three that the registry walk
   cannot see.

Step 3 before step 4 is deliberate: the assertion is much less useful while
configuration can still drift underneath it.
