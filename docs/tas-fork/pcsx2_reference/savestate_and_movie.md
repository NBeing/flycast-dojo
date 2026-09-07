# Savestate ↔ movie in PCSX2-RR (1.4.0-rr)

Source: `C:\_mvc2\other\pcsx2-1.4.0-rr`. All paths below are relative to `pcsx2/`.

## 1. A savestate embeds ONLY the frame counter — not the movie

`TAS/KeyMovie.cpp:19-27`:
```cpp
void SaveStateBase::keymovieFreeze()
{
	FreezeTag("keymovie");
	Freeze(g_FrameCount);
	if (IsLoading()) {
		g_KeyMovieData.addUndoCount();
	}
}
```
Called as the LAST step of `SaveStateBase::FreezeInternals()` (`SaveState.cpp:236-239`), which runs on both
save (`gui/SysState.cpp:328-333`) and load (`gui/SysState.cpp:623`). Those 4 bytes land inside the `.p2s` zip's
"internal structures" entry. **No input log, no movie filename, no hash, no sidecar.** Upstream's `rcntFreeze()`
(`Counters.cpp:933-939`) does not freeze `g_FrameCount`; the fork added this block. Side effect: a vanilla 1.4.0
state lacks the `keymovie` tag and fails to load in this build (`SaveState.cpp:98-113`).

## 2. Load in RECORD mode — no truncation, no restore

- The load restores `g_FrameCount`, then `addUndoCount()` fires on EVERY `IsLoading()` pass, in every mode — it is
  not tied to divergence (`TAS/KeyMovie.cpp:24-26`; `TAS/KeyMovieOnFile.cpp:208-215` writes the new count to the
  file immediately at `SEEKPOINT_UNDOCOUNT`).
- Subsequent frames in RECORD go through `TAS/KeyMovie.cpp:71-75`:
  ```cpp
  if (state == RECORD) {
      keyMovieData.updateFrameMax(g_FrameCount);
      keyMovieData.writeKeyBuf(g_FrameCount, port, bufIndex, nowBuf);
  }
  ```
  and `updateFrameMax` **only raises** (`TAS/KeyMovieOnFile.cpp:198-207`: `if (MaxFrame >= frame) return;`).
- So: load at frame 100 of a 500-frame movie, record 50 frames, stop → blocks 100-149 are new, 150-500 are the
  old take, `MaxFrame` is still 500. **The stale future stays on disk.**
- There is no `OnStateLoad`-style hook in the movie code; the two lines above are the whole load-side behavior.

### Truncation history — this semantic changed three times
| Version / commit | Behavior |
|---|---|
| v1.0 `f339e62` | `Freeze(g_FrameCount); if(IsLoading()) Rerecs++` — count in memory only |
| v1.1 `bead1cd` (2017-01-08) | rerecs persisted on load; the R toggle REPLAY→RECORD did `FrameMax = g_FrameCount` = **truncate at the switch point** (v1.1, v1.1a, v1.2) |
| `544e582` (pre-v2.0) | toggle truncation removed; RECORD set `FrameMax = g_FrameCount` on **every** recorded frame (write-head tracking) |
| `dd08214` (v2.0, 2017-01-22) | replaced with only-raise `updateFrameMax` — **v2.0/v3.0 dropped truncation**, presumably to make the KeyEditor's in-place insert/update/delete workable |

## 3. Load in READ-ONLY (playback) — seek only; the log is untouched

`TAS/KeyMovie.cpp:76-89`:
```cpp
else if (state == REPLAY) {
    if (keyMovieData.getMaxFrame() < g_FrameCount) { g_MovieControle.Pause(); Stop(); return; }
    u8 tmp = 0;
    if (keyMovieData.readKeyBuf(tmp, g_FrameCount, port, bufIndex)) buf[BufCount] = tmp;
}
```
After a load `g_FrameCount` is whatever the state stored; the next poll reads that block and the movie plays on
from there. A state past `MaxFrame` ends the movie on the first poll. **No identity check ties a state to the
movie** — a state from a different movie or a stale take plays the file's bytes from that index (the README's
own "desync" note, lines 13-14).

Caveats on "read-only": `UndoCount` is still written to disk on every load; "Play" opens the file `rb+`
(`TAS/KeyMovieOnFile.cpp:24`), so it is writable — that is what lets the `R` toggle flip to RECORD without
reopening.

Where the toggle lives: menu `gui/MainMenuClicks.cpp:615-631` — `Menu_KeyMovie_Record` → `Start(path,false)`
(opens `wb+`, i.e. **truncates to an empty movie**, after copying `<file>_backup`, `TAS/KeyMovie.cpp:134-151`);
`Menu_KeyMovie_Play` → `Start(path,true)`. Hotkey `R` → `KeyMovie::RecordModeToggle()` (`TAS/KeyMovie.cpp:160-170`)
flips `state` and nothing else. `Space` = FrameAdvance, `P` = TogglePause (`gui/FrameForGS.cpp:82-84`).

## 4. No special "slot 0" / base state — all 10 slots are identical

The fork's only slot code is `gui/GlobalCommands.cpp:466-475`:
```cpp
void States_SaveSlot(int slot) { States_SetCurrentSlot(slot); States_FreezeCurrentSlot(); }
void States_LoadSlot(int slot) { States_SetCurrentSlot(slot); States_DefrostCurrentSlot(); }
```
plus the Lua twins `savestate.saveslot/loadslot` (`lua/LuaFunctions.cpp:260-281`, range 0-9). Underneath is stock
1.4.0 (`gui/Saveslots.cpp:29-30`, `gui/SysState.cpp:654-687`). No base-state, no anchor, no slot-aware movie logic.

## 5. Frame counter & seeking

`g_FrameCount` (`Counters.cpp:46`) increments once per VSync end (`:433`), is zeroed on `rcntInit` (`:145`) and
core reset (`gui/SysCoreThread.cpp:137`), and — only because of the fork — is saved/restored by `keymovieFreeze`.
The movie header holds only `MaxFrame` and `UndoCount`; a block's frame is purely its offset. This is the analog
of our `.frame` sidecar, but embedded in the state, with no `seq` and no prefix-hash equivalent.
