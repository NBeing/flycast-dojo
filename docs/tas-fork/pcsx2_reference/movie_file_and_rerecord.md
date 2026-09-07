# The `.p2m2` movie file and the re-record model (1.4.0-rr)

Source: `C:\_mvc2\other\pcsx2-1.4.0-rr`. Paths relative to `pcsx2/`. Recording subsystem = `TAS/` (not
`Recording/`): `KeyMovie.{h,cpp}` (modes + SIO hook + savestate hook), `KeyMovieOnFile.{h,cpp}` (file layout +
all I/O), `MovieControle.{h,cpp}` (pause/frame-advance), `PadData.{h,cpp}` (12-byte record + button bits),
`KeyEditor.{h,cpp}` (frame editor GUI). Extension `.p2m2`; the legacy google-code format is `.p2m`.

## 1. File format — fixed 160-byte header + fixed 12-byte per-frame blocks, seekable by frame

Header struct `TAS/KeyMovieOnFile.h:12-25`:
```cpp
struct KeyMovieHeader {
	u8 version = 1;
	u8 ID = 0xCC;
	char emu[50] = "PCSX2-1.4.0-rr";
	char author[50] = "";
	char cdrom[50] = "";
```
All byte-aligned → `sizeof == 152`, no padding. The two counters live OUTSIDE the struct at fixed offsets
(`TAS/KeyMovieOnFile.cpp:5-11`):
- `HEADER_SIZE = sizeof(KeyMovieHeader)+4+4` = **160**
- `SEEKPOINT_FRAMEMAX = 152` (u32 `MaxFrame` — a max frame **index**, not a count)
- `SEEKPOINT_UNDOCOUNT = 156` (u32 `UndoCount` — the re-record counter)
- `BLOCK_DATA_SIZE = 6*2` = **12 bytes/frame** (2 ports × 6 bytes; no block header)
- Seek (`:13-16`): `HEADER_SIZE + frame*BLOCK_SIZE`, per byte `+ 6*port + bufIndex` (`:56`)

Per-frame record = the six PS2 pad-response bytes (protocol `BufCount` 3..8 → `bufIndex 0..5`,
`TAS/KeyMovie.cpp:63`): `[0][1]` button bits (active-low, `FF FF` = nothing pressed), `[2][3]` right stick X/Y,
`[4][5]` left stick X/Y (`TAS/PadData.cpp:9-14, 175-178`). Two ports only; multitap is not hooked.
`readHeaderAndCheck` (`:166-183`) checks `ID==0xCC` and `version==1`; `writeHeader` writes only the 152-byte
struct (`:184-190`); `writeMaxFrame` / `updateFrameMax` / `addUndoCount` write their u32 in place (`:191-215`).

## 2. How recording writes — rewrite in place, one byte per SIO protocol byte

`TAS/KeyMovieOnFile.cpp:52-65`:
```cpp
long seek = _getBlockSeekPoint(frame) + BLOCK_HEADER_SIZE + 6 * port + bufIndex;
if (fseek(fp, seek, SEEK_SET) != 0){ return false; }
if (fwrite(&buf, 1, 1, fp) != 1) { return false; }
fflush(fp);
```
Called from the SIO hook `KeyMovie::ControllerInterrupt` (`TAS/KeyMovie.cpp:71-75`) after every controller byte
(`Sio.cpp:179`, right after `PADpoll`), gated on the `0x42` READ_DATA poll with `0x5A` ack (`:47-60`). Indexed by
the VSync counter `g_FrameCount` (`Counters.cpp:433`) — if a game polls twice in one frame the second poll
overwrites the same 12 bytes. **No append, no buffering.**

## 3. Truncation / the "future" after a rewind — NONE in v2.0/v3.0

`TAS/KeyMovieOnFile.cpp:198-207`:
```cpp
void KeyMovieOnFile::updateFrameMax(unsigned long frame)
{
	if (MaxFrame >= frame) { return; }
	MaxFrame = frame;
	... fwrite(&MaxFrame, 4, 1, fp);
```
No `_chsize` / `SetEndOfFile` / `truncate` exists anywhere in `TAS/`. `MaxFrame` only goes down via the editor's
`DeletePadData` (`MaxFrame--`, `:114`). There is no separate "frames written" high-water vs "current frame" —
`MaxFrame` IS the high-water mark, and playback runs to it (§5), so **after a rewind + shorter re-record, replay
runs past your new ending straight into the stale old tail.**

History: v1.1–v1.2 truncated logically on the R toggle (`bead1cd`); `544e582` truncated on every recorded frame
(`FrameMax = g_FrameCount; writeFrameMax();`); `dd08214` (v2.0) removed both in favor of the monotonic
`updateFrameMax`. In every version the stale bytes stay on disk; only the logical end moved.

## 4. The re-record counter — bumps on EVERY savestate load, unconditionally

The single increment is `keymovieFreeze` (`TAS/KeyMovie.cpp:19-27`): `if (IsLoading()) g_KeyMovieData.addUndoCount();`.
`addUndoCount` (`TAS/KeyMovieOnFile.cpp:208-215`): `UndoCount++;` then, if a file is open, `fseek(SEEKPOINT_UNDOCOUNT)`
+ `fwrite`. Every `IsLoading()` path through it:
1. User savestate load (F3 / Shift-F3 backup / `States_LoadSlotN` / menu): `gui/SysState.cpp:623` ← `:644-648`
   ← `:673-687` ← `gui/Saveslots.cpp:104`.
2. The plugin apply/reload round-trip (Config → Plugins → Apply with a live VM): `gui/Panels/PluginSelectorPanel.cpp:273,283`
   → `SysCoreThread::UploadStateCopy` → `FreezeAll` (`gui/SysCoreThread.cpp:162-163`) → `FreezeInternals`.

Consequences: it bumps in **REPLAY** too, and since Play opens `rb+` the increment hits disk — "read-only" is not
read-only on disk. No input-difference test, no mode test. Exposed to Lua as `movie.rerecordcount`
(`lua/LuaFunctions.cpp:335-343`). The author's own name is `UndoCount` (commit `bead1cd` calls it the "append count").

## 5. Modes and controls

- Modes: `enum KEY_MOVIE_MODE { NONE, RECORD, REPLAY }` (`TAS/KeyMovie.h:28-32`).
- Menu (`gui/MainFrame.cpp:547-554`, handlers `gui/MainMenuClicks.cpp:615-658`): *New Record* → `Start(path,false)`
  (opens `wb+` = **truncates the file to empty** after one `<file>_backup` copy, `TAS/KeyMovie.cpp:136-139`);
  *Play* → `Start(path,true)` (`rb+`); *Stop*; *Convert(p2m→p2m2)*; *Convert(v1.0~1.2→v2.0)*; *Open KeyEditor*.
  Both Record and Play begin **paused** (`TAS/KeyMovie.cpp:108`).
- Hotkeys (`bin/inis_1.4.0/PCSX2-rr_keys.ini:85-87`, dispatched `gui/GlobalCommands.cpp:453-465,640-642`):
  `Space` = FrameAdvance, `P` = TogglePause, `R` = KeyMovieModeToggle.
- `RecordModeToggle` (`TAS/KeyMovie.cpp:160-170`) flips REPLAY↔RECORD with no reopen; because Play is `rb+`,
  toggling to RECORD immediately overwrites in place from the current `g_FrameCount`. **This is the only way to
  extend or re-record an existing movie** — New Record always starts empty.
- Frame advance: `MovieControle::FrameAdvance` sets `stopFrameCount = g_FrameCount` (`TAS/MovieControle.cpp:54-60`);
  `StopCheck` at each VSync (`Counters.cpp:521`) pauses once `g_FrameCount > stopFrameCount` (`:34-47`).
- Stop: `state = NONE; Close()` → `writeHeader()` + `fclose` (`TAS/KeyMovieOnFile.cpp:39-47`). No finalization of
  the counters is needed — they were written in place as they changed.
- Movie END: in REPLAY, `if (getMaxFrame() < g_FrameCount) { Pause(); Stop(); }` (`TAS/KeyMovie.cpp:78-84`) — the
  movie ends at `MaxFrame` **inclusive**, the highest frame **ever** recorded, not the last frame of the most
  recent session.
- Editor `Update/Insert/Delete` rewrite blocks in place; Insert/Delete shift later blocks one by one and adjust
  `MaxFrame` (`TAS/KeyMovieOnFile.cpp:99-146`). (`InsertPadData`'s loop underflows when `frame == 0`.)

## 6. Movie start point — no anchor of any kind

`KeyMovie::Start` (`TAS/KeyMovie.cpp:106-154`) only opens the file and sets `state`; it never resets the VM or
touches `g_FrameCount`. `g_FrameCount` is zeroed only at `rcntInit` (`Counters.cpp:145`) and `SysCoreThread::Reset`
(`gui/SysCoreThread.cpp:137`), increments at VSync end (`Counters.cpp:433`), and is **restored from a savestate**
by the `Freeze(g_FrameCount)` in `keymovieFreeze` — that single line is what makes a state-anchored workflow
work at all. The header records only `cdrom` (ISO filename; a mismatch on Play is a console warning,
`TAS/KeyMovie.cpp:122-127`) and `emu`.

Hazard: New Record at frame N>0 never writes frames 0..N-1; `fseek` past EOF + `fwrite` zero-fills the gap, and
`0x00` in the active-low pad encoding means **every button pressed** and sticks at 0 (`TAS/PadData.cpp:9-14,116-118`).
Such a movie is silently non-replayable from power-on; the tool relies on the user to remember the anchor by
convention.

## The 0.9.6-era `.p2m` (David's binary)

The converter comment (`TAS/KeyMovieOnFile.cpp:261-264`) quotes pcsx2-rr's own reader: `fread(&g_Movie.FrameMax,4)`,
`fread(&g_Movie.Rerecs,4)`, then `fread(g_PadData[0]+2, 6, 1, file)` per frame — an **8-byte `{FrameMax, Rerecs}`
header + 6 bytes/frame, pad 1 only** (seek `8 + frame*6`). pocokhc's v1.0 copied that header struct verbatim
(`git show bead1cd:pcsx2/TAS/TAS.cpp`) together with the `Rerecs++`-on-load rule and the monotonic `FrameMax`, so
0.9.6-rr very likely had the same table / rewrite-in-place model and the same "any load bumps" counter
(inference — the 0.9.6 write path is not in this tree). 1.4.0-rr adds: second pad, the 152-byte text header,
`_backup`, the R toggle, pause/frame-advance, KeyEditor, Lua. Savestate majors are incompatible (`0x8b40` vs
`0x9A0B`, `gui/SysState.cpp:287-290`).
