#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <algorithm>
#include <utility>

#include <array>
#include <bitset>
#include <functional>
#include <queue>

#include "deps/filesystem.hpp"

#include "cfg/option.h"
#include "emulator.h"
#include "hw/sh4/sh4_mem.h"
#include "network/ggpo.h"
#include "rend/gui.h"

#include "message_writer.h"
#include "message_reader.h"

#include "net_beacon.h"
#include "replay.h"
#include "training.h"
#include "relay_client.h"
#include "dojo_file.h"
#include "tcp_client.h"

#define MAPLE_FRAME_SIZE 28
#define FRAME_BATCH 120

#include "input/gamepad_device.h"

constexpr int MAX_PLAYERS = 2;

constexpr u32 BTN_TRIGGER_LEFT = DC_BTN_BITMAPPED_LAST << 1;
constexpr u32 BTN_TRIGGER_RIGHT = DC_BTN_BITMAPPED_LAST << 2;

#pragma pack(push, 1)
struct FrameInputs
{
	u32 kcode : 20;
	u32 mouseButtons : 4;
	u32 kbModifiers : 8;

	union
	{
		struct
		{
			u8 x;
			u8 y;
		} analog;
		struct
		{
			s16 x;
			s16 y;
		} absPos;
		struct
		{
			s16 x;
			s16 y;
			s16 wheel;
		} relPos;
		u8 keys[6];
	} u;
	struct
	{
		u8 l;
		u8 r;
	} triggers;
};
#pragma pack(pop)

// THE ONE backup-folder predicate (guardrail, 2026-09-03): <clip>_gen_NN, <clip>_setup_NN (macro clips) or a bare
// gen_NN from older builds. kind / num come back. Every scan in dojo.cpp and dojo_gui.cpp goes through this - three
// private copies used to disagree (two of them never saw setups).
bool tasGenFolderKind(const std::string& name, const std::string& base, std::string& kind, int& num);

class Dojo
{
public:
	NetBeacon presence;
	Replay replay;
	Training training;

	void AssignPlayerNames();

	bool hosting;
	std::string player_1;
	std::string player_2;

	std::atomic<u32> frame_number = {0};
	std::map<uint32_t, std::vector<uint8_t>> session_inputs;
	std::map<uint32_t, std::vector<uint8_t>> rec_inputs;

	// Wait-for-Frameskip (Input Sender): a live Send the user held for the next MvC2 skip/reset frame. Set by
	// the Send button when dojo:WaitForFrameskip is on; MapleApplyAction releases it (playLive at skip+1) when
	// the skip toggle reads 255, or at the frame deadline if no skip is seen (so it can never hang).
	bool frameskip_send_pending = false;
	u32 frameskip_send_deadline = 0;
	std::vector<u16> frameskip_send_p1, frameskip_send_p2;

	// WRITE stale-tail marker (PCSX2-RR v2.0+ parity, David): a state load in WRITE does NOT truncate; the frames
	// from the loaded frame onward are the un-reached OLD take until re-recorded. This is the first still-old frame
	// (~0u = none): set = fn on a WRITE load, bumped to frame+1 by each re-recorded frame, cleared on Reset. The
	// roll greys [stale_tail_from, movieLen) so the old tail is visibly old (PCSX2 has no roll - it is invisible there).
	u32 stale_tail_from = ~0u;
	// Bumped on every sidecar-bearing state load (LoadStateFrame). The Notepad's "Auto re-send on reload" polls
	// it to re-fire Send -> Live after each F3 (David: hands-free send -> reload -> bump skip+N loops).
	u32 load_seq = 0;
	// F8 snapshot prompt (David, 2026-09-04): after every snapshot the GUI shows an inescapable tag / notes prompt for
	// the generation just made (drawn by DojoGui::show_snapshot_prompt on the OSD and Paused paths). Facts are captured
	// here at F8 time so the prompt never reads live session data.
	bool snapshot_prompt_pending = false;
	// F8 REVEAL (David, 2026-09-04): set with the prompt above - the F8 hotkey opens + focuses the States window and its
	// Generations pane drops the cursor in the new generation's Tags cell. A one-shot, consumed once that row is loaded.
	bool snapshot_reveal = false;
	std::string snapshot_prompt_name;
	int snapshot_prompt_num = 0, snapshot_prompt_files = 0;
	u64 snapshot_prompt_bytes = 0;
	u32 snapshot_prompt_frame = 0, snapshot_prompt_movie = 0;
	// LIVE FROM (David, 2026-09-04): after a pre-boot restore the live files came from a snapshot; clip.json restoredFrom
	// says which. Read at session open, shown top right and in F4, so a session on restored files knows it.
	std::string live_from_gen, live_from_local;
	// EDITED SINCE (David, 2026-09-04): restoredFrom.editedSince - the live files changed after the restore (a rewind / edit,
	// a state written, the movie's length changed), so "Loaded - live = gen NN" stops claiming identity. WriteClipStats
	// stamps it (sticky until the next restore); the two counters feed the verdict and BeginClipStats resets them.
	bool live_from_edited = false;
	u32 live_state_writes = 0;		// states written to the live folder this session (gui_saveState)
	u32 movie_len_at_begin = 0;	// session_inputs.size() when the clip opened (0 = the roll arrives later, e.g. a macro Full boot)
	// BOOT READY (David, 2026-09-04: pick -> launch -> freeze -> show F4). LoadMacroFull / SeedOnEnter arm it; the boot
	// handoff pause in gui_display_osd consumes it (composes the banner, opens the F4 window); the first resume clears it.
	bool boot_ready_arm = false;
	bool clip_ready_pending = false;
	std::string clip_ready_text;
	// The movie END as "one past the last authored key" - NOT session_inputs.size(): a Play-Macro-Full roll is keyed
	// from State 0's frame (A..A+len-1), so size() undercounts and every frontier test (R -> READ seeks BASE, the
	// banner READ click, ReplayEnd, the step-hold) fired early (macro-parity audit). Dense movies: identical value.
	u32 MovieEnd() const { return session_inputs.empty() ? 0 : session_inputs.rbegin()->first + 1; }

	u32 last_applied_frame = 0;

	bool play_match = false;
	bool precise_triggers = true;

	void InitScore();
	void RegisterPlayerWin(int player);
	bool ScoreAvailable();
	void UpdateScore();
	void FirstToPoll();

	uint32_t p1_wins = 0;
	uint32_t p2_wins = 0;

	uint32_t current_p1_wins = 0;
	uint32_t current_p2_wins = 0;
	uint32_t last_score_frame = 0;

	void WriteStringToOut(std::string name, std::string contents);

	std::string GetTrainingLua();

	std::string game_name;
	bool commandLineStart;
	bool disconnect_toggle = false;

	std::string GetEntryPath(std::string entry);

	void PollRecordAction(int frame, int size, unsigned char *bits);
	u32 WriteMacroFile();	// TAS: write session_inputs -> <folder>_macro.txt (teardown + live macro-record auto-save)
	void MacroFlush();		// TAS: rewrite it NOW if this session owns a macro and it is stale (Record Macro: always) - ESC / pause / teardown / unloadGame
	u32 MacroAnchorFrame(bool& hasState0);	// TAS: State 0's frame (macro is relative to it) - else first input frame
	void RecRecordAction(int frame, int size, unsigned char *bits);
	void GGPORecordAction(int frame, int size, unsigned char *bits);

	bool recording_started = false;

	void FillDelayFrames();
	void MapleRecordAction(MapleInputState inputState[4]);
	void MapleApplyAction(MapleInputState inputState[4]);
	void SaveStateFrame(const std::string& stateFile);	// TAS: persist the movie frame alongside a savestate
	void LoadStateFrame(const std::string& stateFile);	// TAS: seek the movie to a savestate's frame (read-only)
	void ArchiveGeneration();							// TAS: F8 - snapshot the ACTIVE clip into gen_NN
	void BeginClipStats();								// TAS: start tracking stats for the active clip
	void WriteClipStats();								// TAS: merge session stats into the clip's clip.json
	int ArchiveClipDir(const std::string& clipDir, int *filesCopied = nullptr,
			u64 *bytesCopied = nullptr, const char *tag = "gen");	// returns gen/backup number or -1; tag: gen | setup
	// Copy a generation's files back over the live clip. Gens themselves are never touched -
	// they are constants; the live folder is the scratchpad. Returns the number of files copied.
	int RestoreClipDir(const std::string& clipDir, const std::string& genName);
	void RecordGeneration(int gen, int files, u64 bytes, const char *kind = "gen");	// append a clip.json entry (gen / setup) with the live facts at backup time (schema 6)
	void ReconcileGenerations(const std::string& clipDir);	// make clip.json generations[] match the folder (pre-boot safe, schema 6)

	// Dead-timeline guard. Every re-record rewind is logged as (rerecord seq, rewound-to frame);
	// a savestate is STALE iff a rewind LATER than its save went BELOW its frame - the machine in
	// the state then belongs to an abandoned branch of the movie, and loading it desyncs even
	// though the state itself verifies byte-perfect. Exact rule, so it never cries wolf: states
	// below every subsequent rewind point stay clean through any number of re-records.
	std::vector<std::pair<u32, u32>> rewind_log;	// (seq, frame), append-only, persisted in clip.json
	bool IsStateStale(u32 stateFrame, u32 stateSeq) const;
	// With a sidecar-v3 prefix hash: the seq rule only ARMS suspicion; identical bytes
	// below the anchor EXONERATE (undo with auto-purge off revalidates kept states).
	bool IsStateStale(u32 stateFrame, u32 stateSeq, u64 prefixHash) const;
	u64 MoviePrefixHash(u32 frame) const;
	// Guard refinement: a rewind alone is NOT a timeline event. The event fires at the FIRST
	// movie write whose bytes actually differ from what was there - so seeking back to look
	// (then R) costs nothing, replaying an identical stretch costs nothing, and the event frame
	// is the true divergence point, not the seek target. divergence_open collapses a run of
	// consecutive differing frames into the single event it is; it re-arms on any time jump
	// (state load, R toggle, edit).
	bool divergence_open = false;
	void ReleaseTasHolds();
	void VerifyInputsFrame(const std::vector<u8>& frameData);	// T3 gate G1: SENT vs game READ
	void VerifyInputsReport();

	// T6: THE edit funnel - the single choke point through which every non-recording write to
	// session_inputs must pass (text import today, the piano-roll editor later). Diffs, applies,
	// persists (append to the .flyr - records carry frame numbers and the parser is last-write-
	// wins), and logs the timeline event that makes the dead-timeline guard see the edit.
	// Returns the first changed frame, or -1 for a no-op (no event logged - the no-op guard).
	s64 ApplyEdit(const std::map<u32, std::vector<u8>>& edited, const char *source);
	// PR3: the one path that may SHRINK the movie (row delete). Frames present now but
	// absent from `edited` are REMOVED; the .flyr is rewritten from scratch (appends cannot
	// express shorter). Same guard event, history and stats as ApplyEdit.
	s64 ApplyEditResize(const std::map<u32, std::vector<u8>>& edited, const char *source);
	// TASEditor-style history: every ApplyEdit captures the OLD bytes of the frames it
	// touched; Ctrl+Z / Ctrl+Shift+Z walk the stacks. An undo/redo is itself an edit -
	// it fires a guard event like any other change (the timeline moved again).
	struct EditPatch
	{
		std::vector<std::pair<u32, std::vector<u8>>> frames;	// old bytes; empty = frame absent
		std::string source;
		std::string gui_meta;	// opaque GUI-side state (bookmarks) captured pre-edit
	};
	std::vector<EditPatch> undo_stack;
	std::vector<EditPatch> redo_stack;
	bool history_replay = false;	// suppresses capture while replaying a patch
	// GUI metadata rides the history: the piano roll registers these so undo/redo
	// restore bookmarks alongside the frames (the dojo core stays blob-agnostic).
	std::function<std::string()> edit_meta_capture;
	std::function<void(const std::string&)> edit_meta_apply;
	bool ApplyUndo();
	bool ApplyRedo();	// drop every held-key latch (called when a replay ends)
	void PrintInputs(int player, FrameInputs inputs);
	void PrintMapleInputState(MapleInputState inputState[4]);

	std::array<std::map<u32, std::bitset<18>>, 2> displayed_inputs;
	std::array<std::map<u32, std::string>, 2> displayed_inputs_str;
	std::map<u32, std::string> last_displayed_inputs_str;
	std::array<std::map<u32, std::string>, 2> displayed_dirs_str;
	std::array<std::map<u32, u32>, 2> displayed_inputs_duration;
	std::array<std::bitset<18>, 2> last_held_input;
	std::array<std::map<u32, std::vector<bool>>, 2> displayed_dirs;
	std::array<std::map<u32, int>, 2> displayed_num_dirs;

	void AddToInputDisplay(MapleInputState inputState[4]);
	void ResetInputDisplay();

	std::set<int> button_check_pressed[2];

	std::string current_gamepad;

	void ProcessBody(unsigned int cmd, unsigned int body_size, const char *buffer, int *offset);

	void SaveRecordSlotsFile();
	void LoadRecordSlotsFile();
	void LoadRecordSlotsFile(std::string filename);

	void Split(std::string const &str, const char delim, std::vector<std::string> &out);
	void Replace(std::string &subject, const std::string &search, const std::string &replace);

	uint64_t UnixTimestamp();

	RelayClient relay_client;
	TcpClient tcp_client;

	bool stepping = false;
	bool buffering = false;
	bool manual_pause = false;
	u32 target_step_frame = 0;
	bool step_held = false;			// STEP key currently held (hold-to-frame-advance)
	double next_step_time = 0;		// os_GetSeconds() deadline of the next held-scrub step (accumulator pacing)
	double step_held_since = 0;		// os_GetSeconds() when STEP was pressed (hold-repeat debounce)
	// TAS clip stats (persisted into clip.json; *_base are the values already on disk so counters
	// accumulate across sessions rather than resetting each time the clip is opened)
	// The input MODE's cell-write axis (CANON_readwrite_model.md), under !play_match:
	//   macro_armed  = READ-WRITE: a live SIGNAL (pad / -> Live / auto-fire) stomps the active
	//                  frame; frames with no signal are PRESERVED (overdub).
	//   !macro_armed = WRITE: advancing overwrites every frame it passes (neutral when no signal).
	// (play_match = READ, above, overrides both - playback only.)
	bool macro_armed = false;
	// MERGE sends (David, "OR-style input merging"): ONE switch for additive authoring. ON = the GUI's bake verbs ADD
	// to the cell instead of replacing it, the pad in READ-WRITE adds to the cell (buttons OR, a non-neutral direction
	// replaces, a silent player's half untouched), and a live send in READ-WRITE ORs onto the cell instead of
	// clearing it first. OFF (default) = every path byte-identical to before. Mirrors dojo:SendMerge; the emu thread
	// reads it relaxed, the GUI writes it.
	std::atomic<bool> send_merge = {false};
	// Macro AUTOSAVE (David, after the 2026-09-03 loss - two force-killed sessions, and Play Macro had NO write path at all):
	// every movie write marks the clip's macro .txt stale; the GUI tick (gui.cpp) rewrites it ~0.35 s after the last edit
	// whenever the emulator is PAUSED (the emu thread owns session_inputs while it runs), and MacroFlush() runs at every
	// stop point. The status atomics feed the title-bar stamp so a failed write can never hide again.
	std::atomic<bool> macro_save_pending = {false};	// set by ApplyEdit / ApplyEditResize / a changed cell under the record head; cleared by a successful WriteMacroFile
	std::atomic<int> macro_save_result = {0};		// last WriteMacroFile: 0 never ran, 1 wrote, -1 failed (cannot open the file)
	std::atomic<u32> macro_save_frames = {0};		// frames in the last successful write
	std::atomic<double> macro_save_time = {0.0};	// wall clock (time()) of the last successful write - the title bar prints it
	// Per-frame handoff (emu thread): MapleRecordAction sets this true when it records the pad into the
	// cell this frame, false when READ-WRITE preserves it. The overlay bake reads it so a re-authored
	// signal REPLACES the prior cell (pad released -> clear then write) vs COMBINES (pad pressed -> OR).
	bool tas_rw_pad_wrote = false;
	// OnEnter boot seed: dojo:OnEnterFile (staged by the launch menu, or -config on the CLI) names a
	// Snippets-library .txt whose frames are injected at frame 0 of a fresh Record session. The session
	// starts in READ-WRITE so the seeded frames PLAY (they navigate the game's boot menus) and the
	// .flyr records them from frame 0 - a replay of the movie is input-aligned from power-on, no
	// savestate involved. Runs in gui_start_game after Reset(), before the emulator starts.
	void SeedOnEnter();
	// Play Macro FULL load (CANON_macro_mode.md §12): write the picked clip's macro into the roll (dense
	// 0..N-1) + arm a deferred State-0 load so the boot pauses AT the macro anchor (EXACT frame sync). Runs in
	// gui_start_game (emulator not up yet); the State load itself happens post-boot, WHILE PAUSED, in the
	// gui.cpp OSD handoff. Returns false if the macro file won't load (caller falls back to a seed boot).
	bool LoadMacroFull(const std::string& clipDir, const std::string& macroFile);
	bool LoadClipState0Boot(const std::string& clipDir);	// Play Macro STAGE with a State 0: the same deferred State-0 boot, EMPTY roll (the macro sits in the stage buffer)
	void InjectPendingMacroAt(u32 startFrame);		// lay macro_pending into the roll at startFrame.. (relative)
	std::vector<std::vector<u8>> macro_pending;		// Play Macro Full: macro rows awaiting injection at State 0's
													// frame (a macro is relative; the boot handoff places it there)
	bool onenter_ff = false;	// fast-forwarding through the seeded boot; cleared at the handoff pause (gui.cpp)
	bool replay_bootload = false;	// Replay boot (David, 2026-09-04): the boot handoff seeks State 0 (if the clip has one) while paused, then stays frozen
	bool macro_fullload = false;	// Play Macro Full load: the boot handoff loads State 0 while paused (gui.cpp); cleared there and in Reset()
	// Save-to-loaded-macro: the absolute macro .txt this movie was loaded from (set by macroLoadFull / LoadMacroFull).
	// Empty = no macro loaded this session. loaded_macro_rr = rerecord_count snapshot at load; a differing current
	// count means the movie was edited since -> the Save-to-loaded button/menu enable on that. Cleared in Reset().
	std::string loaded_macro_path;
	u32 loaded_macro_rr = 0;
	// TAS timeline-lock: the set of savestate SLOTS whose input range is locked (protected).
	// Replaces the old single states_locked flag: each locked slot protects [its movie frame,
	// the next state's frame) from EVERY writer (pad record, live/SEND, paint, edit verbs) while
	// still driving the guest. Read by gui_locked_ranges/gui_frame_locked; toggled in the Timeline.
	std::set<int> locked_slots;
	bool base_prelock = true;	// slot 0 (BASE): auto-lock [0, frame(BASE)) - the pre-combo run-up is protected by default
	// A2: emu-thread-readable snapshot of the locked frame ranges. The GUI thread publishes it each
	// frame (gui_locked_ranges); MapleApplyAction / MapleRecordAction read it via FrameLockedEmu under
	// the mutex, so the emu thread never calls the GUI-thread slotScan.
	std::vector<std::pair<u32, u32>> locked_ranges_cache;
	std::mutex locked_ranges_mtx;
	bool FrameLockedEmu(u32 frame);		// is this movie frame in a locked range? (emu-thread safe)
	// F3 on an empty slot: the Timeline shows a red warning for a few seconds (the console
	// line was the only witness before, and the native console is usually closed).
	double load_fail_at = -100.0;
	int load_fail_slot = -1;
	u32 rerecord_count = 0;			// state loads during recording THIS session (PCSX2-rr "re-records")
	u32 rerecord_base = 0;
	double edit_base = 0;			// seconds previously spent on this clip
	double clip_start_time = 0;		// os_GetSeconds() when this clip became active
	u32 clip_sessions = 1;			// how many times this clip has been opened, including now

	// F2 hold-to-repeat (accelerating), so reaching slot 43 is not 43 keypresses
	bool slot_held = false;
	bool slot_held_prev = false;	// direction latched at press time (Shift = previous)
	double slot_held_since = 0;
	double slot_next_repeat = 0;

	double save_hold_since = 0;		// os_GetSeconds() when F1 was pressed on an existing BASE slot
	bool save_hold_done = false;	// that hold matured and the BASE overwrite already fired
	// On-screen hotkey cheat sheet: toggled by its hotkey, or peeked at by holding Shift.
	bool hotkey_overlay = false;
	double shift_held_since = 0;
	double save_blocked_at = 0;		// os_GetSeconds() of the last REJECTED BASE tap (HUD red flash)
	double save_flash_at = 0;		// os_GetSeconds() of the last savestate write (HUD green flash)
	int save_flash_slot = -1;		// which slot that write went to, so the flash cannot follow F2
	// Bumped on every savestate write. The HUD and the States window cache their directory scan; without
	// this an overwrite would not show up until the next periodic rescan.
	std::atomic<u32> savestate_epoch{0};

	void ResetPause();
	void Reset();
};

extern Dojo dojo;
