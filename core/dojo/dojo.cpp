#include "dojo.h"
#include "pause.h"
#include "tasmacro.h"
#include "tas_auto.h"
#include "rend/video_recorder.h"
#include "oslib/oslib.h"	// hostfs::savestateFolderOverride (generation archive)
#include <ctime>			// macro autosave stamp (wall clock)
#include "json.hpp"
#include "tas_wave.h"
#include "tas_ruler.h"
#include "tas_clip.h"
#include <cctype>
#include "mvc2.h"			// T3 input-fidelity probe			// clip.json metadata (tags + session stats)

void Dojo::AssignPlayerNames()
{
	hosting = config::ActAsServer;

	if (hosting || play_match || cfgLoadBool("dojo", "Replay", false))
	{
		player_1 = settings.dojo.PlayerName;
		player_2 = settings.dojo.OpponentName;
	}
	else
	{
		player_1 = settings.dojo.OpponentName;
		player_2 = settings.dojo.PlayerName;
	}

	if (config::StreamTxtOutput)
	{
		WriteStringToOut("p1name", player_1);
		WriteStringToOut("p2name", player_2);
	}
}

void Dojo::InitScore()
{
	p1_wins = 0;
	p2_wins = 0;

	if (config::StreamTxtOutput)
	{
		WriteStringToOut("p1wins", std::to_string(p1_wins));
		WriteStringToOut("p2wins", std::to_string(p2_wins));
	}
}

void Dojo::RegisterPlayerWin(int player)
{
	if (player == 0)
	{
		NOTICE_LOG(NETWORK, "P1 WIN", p1_wins);
		p1_wins++;

		if (config::StreamTxtOutput)
			WriteStringToOut("p1wins", std::to_string(p1_wins));
	}
	else if (player == 1)
	{
		NOTICE_LOG(NETWORK, "P2 WIN", p2_wins);
		p2_wins++;

		if (config::StreamTxtOutput)
			WriteStringToOut("p2wins", std::to_string(p2_wins));
	}

	last_score_frame = (u32)frame_number;

	if (config::TransmitScore || config::SpectatorIP.get() == "match.dojo.ooo")
	{
		MessageWriter player_win;
		player_win.AppendHeader(1, PLAYER_WIN);
		player_win.AppendInt(player);

		std::vector<unsigned char> message = player_win.Msg();
		std::string msg((const char *)message.data(), message.size());

		tcp_client.outgoing_msgs.push(msg);
	}
}

bool Dojo::ScoreAvailable()
{
	bool score_available = false;

	std::string score_games[] =
		{
			"AKATSUKI BK AUSF. ACHSE",
			"CAPCOM VS SNK  JAPAN",
			"CAPCOM VS SNK 2  JAPAN",
			"HOKUTO NO KEN",
			"JINGI STORM THE ARCADE",
			"MARVEL VS CAPCOM2  JAPAN",
			"MOERO JUSTICE GAKUEN  JAPAN",
			" POWER SMASH 2 -----------",
			"SAMURAI SPIRITS 6",
			"THE KING OF FIGHTERS XI",
			"The Rumble Fish 2",
			"TOY FIGHTER",
			"VF4 FINAL TUNED JAPAN",
		};

	for (auto it = std::begin(score_games); it != std::end(score_games); ++it)
	{
		if (settings.content.gameId.find(*it) != std::string::npos)
		{
			score_available = true;
		}
	}

	return score_available;
}

void Dojo::FirstToPoll()
{
	if (config::FirstTo > 0 && (p1_wins == (unsigned int)config::FirstTo.get() || p2_wins == (unsigned int)config::FirstTo.get()))
	{
		u32 cooldown_frames = 1200;
		u32 frame_num = (u32)frame_number.load();

		if (frame_num > (last_score_frame + cooldown_frames))
		{
			if (ggpo::active())
				ggpo::stopSession();
			else if (config::GGPOEnable)
				gui_setState(GuiState::Disconnected);
			else
				gui_stop_game();
		}
	}
}

void Dojo::UpdateScore()
{
	uint32_t cooldown_frames = 600;

	u32 frame_num = frame_number.load();
	if (frame_num < (last_score_frame + cooldown_frames))
		return;

	if (ScoreAvailable())
	{
		uint32_t detected_p1_wins = 0;
		uint32_t detected_p2_wins = 0;

		if (settings.content.gameId == "MOERO JUSTICE GAKUEN  JAPAN" ||
			settings.content.gameId == "SAMURAI SPIRITS 6" ||
			settings.content.gameId == "VF4 FINAL TUNED JAPAN")
		{
			uint32_t winning_streak;
			uint32_t champion_player;

			if (settings.content.gameId == "MOERO JUSTICE GAKUEN  JAPAN")
			{
				champion_player = ReadMem8_nommu(0x8C2EED95);
				winning_streak = ReadMem8_nommu(0x8C2EED96);
				NOTICE_LOG(NETWORK, "C %u, S %u", champion_player, winning_streak);
			}
			else if (settings.content.gameId == "SAMURAI SPIRITS 6")
			{
				champion_player = ReadMem8_nommu(0x8C2F7CF0);
				winning_streak = ReadMem8_nommu(0x8C2F7D18);
				NOTICE_LOG(NETWORK, "C %u, S %u", champion_player, winning_streak);
			}
			else if (settings.content.gameId == "VF4 FINAL TUNED JAPAN")
			{
				champion_player = ReadMem8_nommu(0x8C2925D4);
				winning_streak = ReadMem8_nommu(0x8C2925DC);
				NOTICE_LOG(NETWORK, "C %u, S %u", champion_player, winning_streak);
			}

			if (champion_player == 0)
			{
				detected_p1_wins = winning_streak;
				detected_p2_wins = 0;
			}
			else if (champion_player == 1)
			{
				detected_p2_wins = winning_streak;
				detected_p1_wins = 0;
			}
			// double ko, samsptk
			else if (champion_player == 255)
			{
				champion_player = 0;
				winning_streak = 0;

				detected_p1_wins = 0;
				detected_p2_wins = 0;
			}

			if (champion_player != 255)
			{
				if (current_p1_wins + 1 == detected_p1_wins)
				{
					RegisterPlayerWin(0);
				}

				if (current_p2_wins + 1 == detected_p2_wins)
				{
					RegisterPlayerWin(1);
				}
			}

			current_p1_wins = detected_p1_wins;
			current_p2_wins = detected_p2_wins;

			FirstToPoll();

			return;
		}

		if (settings.content.gameId == "AKATSUKI BK AUSF. ACHSE")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C19609C);
			detected_p2_wins = ReadMem8_nommu(0x8C196388);
		}
		else if (settings.content.gameId == "CAPCOM VS SNK  JAPAN")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C2357A3);
			detected_p2_wins = ReadMem8_nommu(0x8C235DAF);
		}
		else if (settings.content.gameId == "CAPCOM VS SNK 2  JAPAN")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C241D3F);
			detected_p2_wins = ReadMem8_nommu(0x8C24230B);
		}
		else if (settings.content.gameId == "HOKUTO NO KEN")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C4939BE);
			detected_p2_wins = ReadMem8_nommu(0x8C4939CA);
		}
		else if (settings.content.gameId == "JINGI STORM THE ARCADE")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C1C996C);
			detected_p2_wins = ReadMem8_nommu(0x8C1C9970);
		}
		else if (settings.content.gameId == " POWER SMASH 2 -----------")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C23DE6C);
			detected_p2_wins = ReadMem8_nommu(0x8C23DEE4);
		}
		else if (settings.content.gameId == "THE KING OF FIGHTERS XI")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C27CBB8);
			detected_p2_wins = ReadMem8_nommu(0x8C27CDB0);
		}
		else if (settings.content.gameId == "The Rumble Fish 2")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C3A59B0);
			detected_p2_wins = ReadMem8_nommu(0x8C3A59B4);
		}
		else if (settings.content.gameId == "TOY FIGHTER")
		{
			detected_p1_wins = ReadMem8_nommu(0x8C0F8BCC);
			detected_p2_wins = ReadMem8_nommu(0x8C0F8BCD);
		}
		else if (settings.content.gameId == "MARVEL VS CAPCOM2  JAPAN")
		{
			uint32_t in_match = ReadMem8_nommu(0x8C2F836C);
			if (in_match == 0)
				return;
			detected_p1_wins = ReadMem8_nommu(0x8C2F83D4);
			detected_p2_wins = ReadMem8_nommu(0x8C2F83D5);
		}

		if (current_p1_wins + 1 == detected_p1_wins)
		{
			RegisterPlayerWin(0);
		}

		if (current_p2_wins + 1 == detected_p2_wins)
		{
			RegisterPlayerWin(1);
		}

		current_p1_wins = detected_p1_wins;
		current_p2_wins = detected_p2_wins;

		FirstToPoll();
	}
}

void Dojo::WriteStringToOut(std::string name, std::string contents)
{
#ifndef __ANDROID__
	auto dir_name = get_writable_config_path("out/");
	if (!ghc::filesystem::exists(dir_name))
		ghc::filesystem::create_directory(dir_name);

	std::string path = dir_name + name + ".txt";
	std::ofstream fout(path);
	fout << contents;
	fout.close();
#endif
}

std::string Dojo::GetTrainingLua()
{
	if (settings.content.gameId == "T1249M")
		return get_readonly_config_path("training/cvs2.lua");
	else if (settings.content.gameId == "T1212N")
		return get_readonly_config_path("training/mvsc2.lua");
	else
	{
		// look up by game file name in training folder
		std::string lua_file = settings.content.path;
		size_t lastindex = lua_file.find_last_of('/');
#ifdef _WIN32
		size_t lastindex2 = lua_file.find_last_of('\\');
		if (lastindex == std::string::npos)
			lastindex = lastindex2;
		else if (lastindex2 != std::string::npos)
			lastindex = std::max(lastindex, lastindex2);
#endif
		if (lastindex != std::string::npos)
			lua_file = lua_file.substr(lastindex + 1);
		lastindex = lua_file.find_last_of('.');
		if (lastindex != std::string::npos)
			lua_file = lua_file.substr(0, lastindex);

		lua_file = lua_file + ".lua";
		auto lua_path = get_readonly_data_path("training/" + lua_file);

		if (ghc::filesystem::exists(lua_path))
			return lua_path;
	}

	return "";
}

std::string Dojo::GetEntryPath(std::string entry)
{
	// default arcade rom name + .zip
	// fall back to dreamcast chd file
	std::string entry_name = std::string(entry.data());
	std::string zip_filename = entry_name + ".zip";
	std::string chd_filename = entry_name + ".chd";
	std::string gdi_filename = entry_name + ".gdi";
	std::string cdi_filename = entry_name + ".cdi";
	std::string dir_name = "ROMs";
	std::string nested_dir = "";

	auto rom_paths = config::ContentPath.get();

	if (std::find(rom_paths.begin(), rom_paths.end(), "ROMs") == rom_paths.end())
		rom_paths.push_back("ROMs");

	for (unsigned int i = 0; i < rom_paths.size(); i++)
	{
		// check if destination filename exists, return if so
		std::string target;
		std::string chd_target = "";
		std::string gdi_target = "";
		std::string cdi_target = "";
		if (nested_dir.empty())
		{
			target = rom_paths[i] + "/" + zip_filename;
			if (!chd_filename.empty())
				chd_target = rom_paths[i] + "/" + chd_filename;
			if (!gdi_filename.empty())
				gdi_target = rom_paths[i] + "/" + gdi_filename;
			if (!cdi_filename.empty())
				cdi_target = rom_paths[i] + "/" + cdi_filename;
		}
		else
		{
			target = rom_paths[i] + "/" + nested_dir + "/" + zip_filename;
			if (!chd_filename.empty())
				chd_target = rom_paths[i] + "/" + nested_dir + "/" + chd_filename;
			if (!gdi_filename.empty())
				gdi_target = rom_paths[i] + "/" + nested_dir + "/" + gdi_filename;
			if (!cdi_filename.empty())
				cdi_target = rom_paths[i] + "/" + nested_dir + "/" + cdi_filename;
		}

		if (!target.empty() && ghc::filesystem::exists(target))
			return target;
		if (!chd_target.empty() && ghc::filesystem::exists(chd_target))
			return chd_target;
		if (!gdi_target.empty() && ghc::filesystem::exists(gdi_target))
			return gdi_target;
		if (!cdi_target.empty() && ghc::filesystem::exists(cdi_target))
			return cdi_target;
	}

	return "";
}

void Dojo::PollRecordAction(int frame, int size, unsigned char *bits)
{
	u32 frame_num = (unsigned int)frame;
	std::vector<u8> m_inputs;
	std::vector<u8> to_record(size, 0);

	std::memcpy(to_record.data(), bits, size);

	for (int i = 0; i < size; i++)
	{
		m_inputs.push_back((unsigned char)(to_record[i]));
	}

	if (replay.ggpo_session)
	{
		if (session_inputs.count(frame_num))
			return;
	}

	// THE TIMELINE EVENT, refined: fires only when an overwrite actually CHANGES bytes. During
	// normal recording the frontier only ever appends, so a differing overwrite can only follow
	// a rewind or an R-toggle back to write - and a run of consecutive differing frames is one
	// re-record, not many (divergence_open collapses it).
	if (!play_match && recording_started)
	{
		auto prev = session_inputs.find(frame_num);
		if (prev != session_inputs.end() && prev->second != m_inputs)
		{
			if (!divergence_open)
			{
				divergence_open = true;
				rerecord_count++;
				rewind_log.emplace_back(rerecord_base + rerecord_count, frame_num);
				WriteClipStats();
				NOTICE_LOG(NETWORK, "TAS: re-record CONFIRMED - input diverged at frame %u"
						" (timeline event [%u, %u])", frame_num,
						rerecord_base + rerecord_count, frame_num);
			}
		}
	}

	{	// macro autosave: a cell that actually CHANGES under the record head / the pad marks the macro .txt stale
		auto pit = session_inputs.find(frame_num);
		if (pit == session_inputs.end() || pit->second != m_inputs)
			macro_save_pending.store(true, std::memory_order_relaxed);
	}
	session_inputs[frame_num] = m_inputs;
	if (stale_tail_from != ~0u && frame_num >= stale_tail_from)
		stale_tail_from = frame_num + 1;	// write head advanced: this old-take frame is now re-recorded

	// LIVE macro-mode auto-save (David): write <folder>_macro.txt as you record, not only at teardown.
	// Throttled (~every 30 recorded frames) so it stays cheap; Movie recording (MacroMode=no) is skipped.
	if (!play_match && recording_started)
	{
		static int macroSaveTick = 0;
		if (++macroSaveTick >= 30)
		{
			macroSaveTick = 0;
			if (cfgLoadBool("dojo", "MacroMode", false))
				WriteMacroFile();
		}
	}

	if (config::GGPOEnable && !play_match &&
		(config::RecordMatches || config::Transmitting))
	{
		if (!recording_started)
			replay.StartRecording();

		// create frame container for export
		unsigned char new_frame[MAPLE_FRAME_SIZE] = {0};
		memcpy(new_frame, (unsigned char *)&frame_num, sizeof(unsigned int));
		memcpy(new_frame + 4, (unsigned char *)m_inputs.data(), m_inputs.size());
		std::string frame_((const char *)new_frame, MAPLE_FRAME_SIZE);

		replay.AppendToReplay(frame_, 4);
	}

	// NOTICE_LOG(NETWORK, "FRAME %u SIZE %u", frame, m_inputs.size());
}

void Dojo::RecRecordAction(int frame, int size, unsigned char *bits)
{
	u32 frame_num = (unsigned int)frame;
	std::vector<u8> m_inputs;
	std::vector<u8> to_record(size, 0);

	std::memcpy(to_record.data(), bits, size);

	for (int i = 0; i < size; i++)
	{
		m_inputs.push_back((unsigned char)(to_record[i]));
	}
	rec_inputs[frame_num] = m_inputs;
}

void Dojo::FillDelayFrames()
{
	// fill initial frames for delay
	for (unsigned int i = 0; i < (unsigned int)config::Delay.get() + 1; i++)
	{
		for (int j = 0; j < MAX_PLAYERS; j++)
		{
			std::vector<u8> blank_inputs;
			blank_inputs.resize(sizeof(FrameInputs));
			std::fill(blank_inputs.begin(), blank_inputs.end(), 0);
			PollRecordAction(i, blank_inputs.size(), blank_inputs.data());
		}
	}
}

namespace { u16 canonFromPacket(const FrameInputs& fi); }	// defined in the anon namespace below (~1158)
static void tasMergeCanonIntoFrame(FrameInputs *fi, u16 add);	// defined with the canon helpers below (~1640)
void Dojo::MapleRecordAction(MapleInputState inputState[4])
{
	if (frame_number == 0 && config::Delay.get() > 0)
	{
		FillDelayFrames();
	}

	tas_rw_pad_wrote = false;	// set true iff we record the pad (stomp) this frame; read by the overlay bake
	// OnEnter seed window: the pad is OUT until the handoff - a bump would stomp a seed cell (BOTH
	// players' halves go with the row), desync the boot nav on-screen, and log a bogus re-record
	// event (audit). tasHotkeysBlocked() locks the TAS hotkeys for the same window.
	if (onenter_ff && stepping && frame_number.load() < target_step_frame)
		return;
	// READ-WRITE vs WRITE (CANON_readwrite_model.md). The pad is a SIGNAL in BOTH writable modes now; only the
	// no-signal default differs: READ-WRITE (macro_armed) PRESERVES the existing cell (overdub), WRITE clobbers
	// it (recorded below, neutral included). A LOCKED range is never overwritten in either mode. (READ =
	// play_match; Record is only ever called when !play_match, so READ needs no branch here.)
	if (!play_match && FrameLockedEmu((u32)(frame_number.load() + config::Delay)))
	{
		if (!macro_armed)	// WRITE warns; READ-WRITE silently preserves a locked cell, like any protected cell
		{
			static double lastLockWarn = 0;
			const double nowLw = os_GetSeconds();
			if (nowLw - lastLockWarn > 1.5)
			{
				gui_display_notification("Locked range - controller input not recorded (unlock in Timeline)", 1500);
				lastLockWarn = nowLw;
			}
		}
		return;
	}

	PrintMapleInputState(inputState);
	std::vector<FrameInputs> maple_in;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		// Zeroed, NOT default-constructed: the struct was recorded with uninitialized union bytes
		// (stack garbage - a constant 78 11 landed in bytes 8/9 of every packet of every movie),
		// which playback never reads but which made movie files nondeterministic in their dead
		// bytes and unrepresentable as clean text. Old movies keep their garbage (the text codec
		// @raw-escapes them); everything recorded from now on is exact.
		FrameInputs inputs;
		memset(&inputs, 0, sizeof(inputs));
		inputs.kcode = ~inputState[i].kcode;
		u32 analogAxes = replay.analog;
		if (analogAxes > 0)
		{
			inputs.u.analog.x = inputState[i].fullAxes[PJAI_X1] >> 8;
			if (analogAxes >= 2)
				inputs.u.analog.y = inputState[i].fullAxes[PJAI_Y1] >> 8;
		}

		// only record precise triggers offline
		if (config::GGPOEnable)
		{
			if (rt[i] >= 0x4000)
				inputs.kcode |= BTN_TRIGGER_RIGHT;
			else
				inputs.kcode &= ~BTN_TRIGGER_RIGHT;
			if (lt[i] >= 0x4000)
				inputs.kcode |= BTN_TRIGGER_LEFT;
			else
				inputs.kcode &= ~BTN_TRIGGER_LEFT;
		}
		else
		{
			inputs.triggers.l = inputState[i].halfAxes[PJTI_L];
			inputs.triggers.r = inputState[i].halfAxes[PJTI_R];
		}

		PrintInputs(i, inputs);
		maple_in.push_back(inputs);
	}

	// READ-WRITE (macro_armed) records ONLY when the pad pressed a game input this frame; a released pad
	// PRESERVES the existing cell (overdub). WRITE (!macro_armed) records every frame - neutral included (clobber).
	if (macro_armed)
	{
		bool signal = false;
		for (const FrameInputs &fi : maple_in)
			if (canonFromPacket(fi) != 0) { signal = true; break; }
		if (!signal)
			return;
	}
	std::vector<u8> m_inputs(sizeof(FrameInputs) * MAX_PLAYERS);
	if (macro_armed && send_merge.load(std::memory_order_relaxed))
	{	// MERGE (David): the pad ADDS to the cell - buttons OR, a non-neutral pad direction replaces the cell's, and a
		// SILENT player's half is untouched byte for byte (a P1 press no longer wipes P2's authored cell). Release
		// still preserves (the signal test above). The divergence detector fires exactly when bytes change.
		const u32 fr = dojo.frame_number.load() + config::Delay;
		auto it = session_inputs.find(fr);
		if (it != session_inputs.end())
			std::memcpy(m_inputs.data(), it->second.data(), std::min(m_inputs.size(), it->second.size()));
		for (int pl = 0; pl < MAX_PLAYERS && pl < (int)maple_in.size(); pl++)
		{
			const u16 ac = canonFromPacket(maple_in[pl]);
			if (ac != 0)
				tasMergeCanonIntoFrame((FrameInputs *)(m_inputs.data() + pl * sizeof(FrameInputs)), ac);
		}
	}
	else
		std::memcpy(m_inputs.data(), maple_in.data(), sizeof(FrameInputs) * MAX_PLAYERS);

	tas_rw_pad_wrote = true;	// recording the pad into the cell -> the overlay bake COMBINES (does not clear)
	PollRecordAction(dojo.frame_number.load() + config::Delay, m_inputs.size(), m_inputs.data());
}

bool Dojo::IsStateStale(u32 stateFrame, u32 stateSeq) const
{
	for (const auto& r : rewind_log)
		if (r.first > stateSeq && r.second < stateFrame)
			return true;
	return false;
}

// A 64-bit rolling hash of every packet STRICTLY BELOW `frame` - exactly the bytes that
// determined the machine a savestate captured there. Same definition at save time and at
// check time, which is the only property that matters (see below).
//
// WHAT IS COVERED, byte for byte, because a vaguer answer is how two implementations of
// "the same" hash silently disagree:
//   * frame `frame` itself is EXCLUDED - the state was captured before it was applied
//   * each packet contributes its FRAME NUMBER and then its bytes, both players' rows
//     together (session_inputs values are the full 24-byte pair)
//   * an UNAUTHORED frame contributes NOTHING AT ALL, because session_inputs is sparse and
//     has no entry for it. That is distinguishable from an authored-but-neutral frame only
//     because the frame numbers of its neighbours are mixed in - which is why they are.
//     Drop the index and a gap becomes indistinguishable from a run of empty packets.
//
// `[MEASURED 2026-09-08]` IT IS NOT FNV-1a, THOUGH IT WAS CALLED THAT HERE AND IN DAVID'S
// TREE. The prime is FNV's (1099511628211 = 0x100000001b3) but the offset basis is the
// standard's with its LAST DIGIT DROPPED: 1469598103934665603 against 14695981039346656037.
// A typo, and functionally harmless - any odd basis hashes fine, and this value never leaves
// the host that computed it: it is written into a .frame sidecar and compared only against
// MoviePrefixHash() run on the same movie by the same build. There is no cross-host or
// cross-version comparison to break.
//
// SO THE CONSTANT IS LEFT ALONE AND THE NAME IS FIXED, not the other way round. Correcting
// the basis would change every existing sidecar's hash, so states that are already
// seq-suspect would flip from clean to stale on clips the user has recorded - a real cost
// for zero correctness gain. The lie worth removing is the WORD "FNV-1a": a second
// implementation told that name would match the standard, produce different bytes, and
// believe it had agreed.
u64 Dojo::MoviePrefixHash(u32 frame) const
{
	// Not the FNV-1a basis - see above. Deliberately not corrected.
	u64 h = 1469598103934665603ull;
	auto mix = [&h](const void *p, size_t n)
	{
		const unsigned char *b = (const unsigned char *)p;
		for (size_t i = 0; i < n; i++)
		{
			h ^= b[i];
			h *= 1099511628211ull;
		}
	};
	for (const auto& kv : session_inputs)
	{
		if (kv.first >= frame)
			break;				// ordered map
		mix(&kv.first, sizeof(kv.first));
		if (!kv.second.empty())
			mix(kv.second.data(), kv.second.size());
	}
	return h;
}

bool Dojo::IsStateStale(u32 stateFrame, u32 stateSeq, u64 prefixHash) const
{
	if (!IsStateStale(stateFrame, stateSeq))
		return false;
	// Content revalidation (user request): edit + Ctrl+Z with auto-purge OFF nets to
	// NOTHING - the kept state's prefix bytes match its save-time hash again, so it is
	// factually valid and turns clean. Cost is only paid for seq-suspect states.
	if (prefixHash != 0 && MoviePrefixHash(stateFrame) == prefixHash)
		return false;
	return true;
}

void Dojo::SaveStateFrame(const std::string& stateFile)
{
	// TAS re-recording: remember which movie frame this savestate was made at, so a read-only
	// replay can seek here (a WRITE load never truncates - PCSX2 v2.0+ parity - the un-reached tail is marked stale instead).
	std::ofstream f(stateFile + ".frame", std::ios::binary);
	if (!f.good())
		return;
	// Sidecar v2, ADDITIVE: {frame, rerecordSeq, movieLen}. Readers of the old 4-byte form (the
	// VS Code extension included) keep working - they read the first u32 and stop. rerecordSeq is
	// the dead-timeline guard's anchor: it records WHEN in the edit history this state was taken,
	// so a later rewind below its frame can be detected exactly.
	u32 fn = frame_number.load();
	u32 seq = rerecord_base + rerecord_count;
	u32 movieLen = (u32)session_inputs.size();
	f.write((const char *)&fn, sizeof(fn));
	f.write((const char *)&seq, sizeof(seq));
	f.write((const char *)&movieLen, sizeof(movieLen));
	// Sidecar v3, ADDITIVE again: the prefix hash lets a later check prove the movie
	// below this anchor is byte-identical to now - the content-revalidation exoneration.
	u64 prefixHash = MoviePrefixHash(fn);
	f.write((const char *)&prefixHash, sizeof(prefixHash));
	f.close();
	WriteClipStats();
}

void Dojo::LoadStateFrame(const std::string& stateFile)
{
	std::ifstream f(stateFile + ".frame", std::ios::binary);
	if (!f.good())
		return;
	u32 fn = 0;
	f.read((char *)&fn, sizeof(fn));
	if (f.gcount() != (std::streamsize)sizeof(fn))
		return;
	// v2 fields; absent in old 4-byte sidecars, whose seq stays "unknown" (grandfathered - the
	// guard only judges states that carry their vintage).
	u32 stateSeq = 0;
	bool haveSeq = false;
	{
		u32 tmp = 0;
		f.read((char *)&tmp, sizeof(tmp));
		if (f.gcount() == (std::streamsize)sizeof(tmp))
		{
			stateSeq = tmp;
			haveSeq = true;
		}
	}
	u64 prefixHash = 0;		// sidecar v3 (0 = absent -> seq-only verdict)
	{
		u32 mlen = 0;
		f.read((char *)&mlen, sizeof(mlen));
		if (f.gcount() == (std::streamsize)sizeof(mlen))
		{
			u64 ph = 0;
			f.read((char *)&ph, sizeof(ph));
			if (f.gcount() == (std::streamsize)sizeof(ph))
				prefixHash = ph;
		}
	}

	// THE DEAD-TIMELINE GUARD. The one re-record foot-gun: this state was saved, then a later
	// rewind re-recorded the movie from BELOW its frame - so the machine inside the state belongs
	// to an abandoned branch. It will load fine and verify byte-perfect, and the movie will still
	// desync from it. Warn loudly BEFORE the load takes effect; loading anyway stays allowed
	// (re-saving the slot right after is the standard repair).
	if (haveSeq && IsStateStale(fn, stateSeq, prefixHash))
	{
		NOTICE_LOG(NETWORK, "TAS: STALE STATE - saved @%u before a later rewind went below it; "
				"machine is from an abandoned branch of the movie", fn);
		char m[160];
		snprintf(m, sizeof(m), "STALE state (frame %u): a re-record rewound below it - "
				"the movie no longer matches. Re-save this slot.", fn);
		gui_display_notification(m, 5000);
	}

	// Time travel moves the movie clock in BOTH modes:
	// - Read-only replay: seek playback to this state's frame (state 0 @ frame N -> play N..end).
	// - Read-write recording: REWIND the movie position with the machine. Newly recorded inputs
	//   overwrite session_inputs[frame] in place (and the .flyr parser is last-write-wins per
	//   frame), so loading a state and retrying a segment re-records it - PCSX2-rr re-recording.
	//   Previously the counter kept running while the machine rewound, so EVERY mid-recording load
	//   shifted all later inputs by the rewound amount -> the replay desynced at the first retry
	//   point even though every savestate verified byte-perfect.
	u32 prevFrame = frame_number.load();
	frame_number = fn;
	tas_wave::onStateLoad(fn);	// TAS waveform: frames from fn are an old take until re-run (no truncate, like the movie)
	load_seq++;	// every real load - the Notepad's auto re-send polls this
	if (!play_match && recording_started)
	{
		// REFINED (user UX pass): the rewind itself is no longer the timeline event - reviewing
		// your work by seeking back must not stale anything. The event now fires in
		// PollRecordAction at the first write whose bytes actually DIFFER from the movie, with
		// that divergence frame as the event frame. This load only re-arms the detector.
		divergence_open = false;
		NOTICE_LOG(NETWORK, "TAS: rewind to frame %u armed - becomes a re-record only if input diverges", fn);
	}
	// PCSX2-RR v2.0+ parity (David, WRITE_MODE_RESTRUCTURE.md s5.7): a state load in WRITE does NOT truncate the
	// movie. The frames from fn onward stay as the un-reached OLD take and are overwritten in place as you
	// re-record - exactly PCSX2-RR v2.0+ and his 0.9.6. Instead of cutting, mark the tail so the roll can grey it:
	// PCSX2 has no roll view, so its stale tail is invisible; ours is visibly old. READ seeks only; READ-WRITE
	// keeps by design. Macro sessions are NOT excluded (macro-parity audit): Record Macro in WRITE overwrites in place
	// exactly like Record Movie, and the live macro.txt save already writes the un-reached tail (PCSX2's "tail stays
	// on disk"); Play Macro (no .flyr) counts as writing via PlayMacro, the tasWriteGrow rule - in WRITE the loaded
	// macro WILL be clobbered as you advance, so ambering it on F3 is the truthful warning. The marker is display-only.
	// The prefix-hash + STALE warning stay as our safety over the reference.
	if (!play_match && !macro_armed && (replay.HasAppendTarget() || cfgLoadBool("dojo", "PlayMacro", false))
			&& fn > 0 && !session_inputs.empty() && session_inputs.rbegin()->first >= fn)
	{
		stale_tail_from = fn;
		NOTICE_LOG(NETWORK, "TAS: WRITE load @%u - the old take from here is STALE until re-recorded (no truncate, PCSX2 v2.0+)", fn);
	}
	WriteClipStats();
	if (play_match)
	{
		NOTICE_LOG(NETWORK, "TAS: replay seek to movie frame %u", fn);
		const u32 last = session_inputs.empty() ? 0 : (u32)session_inputs.size() - 1;
		char msg[112];
		if (!session_inputs.empty() && fn >= last)
		{
			// The state sits AT or PAST the end of the recorded movie, so playback ends the
			// instant it resumes. Without this you just get "End of Replay" and no idea why.
			NOTICE_LOG(NETWORK, "TAS: state is at/after the movie end (%u of %u) - nothing left to play",
					fn, last);
			snprintf(msg, sizeof(msg), "State %d is at the movie's end (frame %u of %u) - nothing to play",
					hostfs::currentSavestateSlot(), fn, last);
			gui_display_notification(msg, 4000);
		}
		else
		{
			snprintf(msg, sizeof(msg), "Replay seek to frame %u of %u", fn, last);
			gui_display_notification(msg, 1500);
		}
	}
	else if (recording_started)
	{
		NOTICE_LOG(NETWORK, "TAS: recording rewound from frame %u to %u (re-record)", prevFrame, fn);
		char msg[80];
		snprintf(msg, sizeof(msg), "Recording rewound to frame %u (re-record)", fn);
		gui_display_notification(msg, 1500);
	}
}

// TAS: F8 "generation archive" - the internalized PCSX2 backup script. Snapshots the active
// clip's movie + savestates (+ metadata) into gen_NN\ inside the clip folder, leaving the hot
// slots 0-9 untouched: checkpoint overflow for long combos (keep re-using 0-9 past 10 states)
// AND a safety net against re-record timeline mistakes. NOTE: the .flyr on disk is appended in
// batches, so a mid-recording archive may lag the movie by up to a batch - fine for a snapshot
// (the in-memory movie is authoritative until session end). Restore is manual for now (copy the
// gen's files back over the clip folder); a browser-side restore is planned.
// ---- clip.json metadata (schema 2) -----------------------------------------------------------
// Written next to each clip. v1 fields (game/created/tags/notes) are preserved verbatim so
// existing tooling keeps working; v2 adds machine-canonical + human-readable dates and a "stats"
// object holding what only the emulator can know: movie length, re-record count (PCSX2-rr style),
// time spent, and session count.


// The clip.json date helpers moved to tas_clip (P1 refactor) - the names stay usable here.
using tas_clip::utcIso;
using tas_clip::utcNowIso;
using tas_clip::localUsTime;

// Load the stats already on disk so counters accumulate across sessions instead of restarting.
void Dojo::BeginClipStats()
{
	rerecord_count = 0;
	rerecord_base = 0;
	edit_base = 0;
	clip_sessions = 1;
	rewind_log.clear();	// guardrail: the clear used to sit inside if (stats) - a clip born in-process inherited the previous clip's log
	live_from_gen.clear();
	live_from_local.clear();
	live_from_edited = false;
	live_state_writes = 0;
	movie_len_at_begin = (u32)session_inputs.size();
	clip_start_time = os_GetSeconds();
	if (hostfs::savestateFolderOverride.empty())
		return;
	tas_wave::loadClip(hostfs::savestateFolderOverride);	// TAS waveform: the clip audio timeline (audio.env) a previous session saved
	tas_ruler::loadClip(hostfs::savestateFolderOverride);	// REL ruler: the clip skip map
	ReconcileGenerations(hostfs::savestateFolderOverride);	// schema 6: generations[] matches the folder from the first frame
	try
	{
		nlohmann::json j = tas_clip::read(hostfs::savestateFolderOverride);	// P2: one reader
		if (j.contains("stats") && j["stats"].is_object())
		{
			rerecord_base = j["stats"].value("rerecords", 0u);
			rewind_log.clear();
			if (j.contains("rewinds") && j["rewinds"].is_array())
				for (const auto& r : j["rewinds"])
					if (r.is_array() && r.size() == 2)
						rewind_log.emplace_back((u32)r[0].get<u64>(), (u32)r[1].get<u64>());
			edit_base = j["stats"].value("editSeconds", 0.0);
			clip_sessions = j["stats"].value("sessions", 0u) + 1;
		}
		if (j.contains("restoredFrom") && j["restoredFrom"].is_object())
		{	// the live files came from a snapshot (a pre-boot restore) - the session shows it
			live_from_gen = j["restoredFrom"].value("generation", std::string());
			live_from_local = j["restoredFrom"].value("local", std::string());
			live_from_edited = j["restoredFrom"].value("editedSince", false);
		}
	}
	catch (...) {}
}

void Dojo::WriteClipStats()
{
	if (hostfs::savestateFolderOverride.empty())
		return;
	nlohmann::json j = tas_clip::read(hostfs::savestateFolderOverride);	// P2: one reader, one writer (below)
	const nlohmann::json before = j;	// write-on-difference (below)

	if (!j.contains("game"))
		j["game"] = game_name;
	if (!j.contains("tags"))
		j["tags"] = nlohmann::json::array();
	if (!j.contains("notes"))
		j["notes"] = "";

	// canonical UTC (sortable) + the local, human-readable form for any public-facing screen
	std::string createdUtc = j.value("createdUtc", j.value("created", std::string()));
	if (!createdUtc.empty())
	{
		j["createdUtc"] = createdUtc;
		j["createdLocal"] = localUsTime(createdUtc);
	}

	nlohmann::json st = (j.contains("stats") && j["stats"].is_object()) ? j["stats"] : nlohmann::json::object();
	// The movie's ACTUAL length is session_inputs.size(). It used to be max(frame_number, size)
	// kept as a high-water mark, which lies: seek to a state saved past the end and frame_number
	// runs ahead of the movie, so magnetoNew reported 3708 frames for a 3605-frame movie. The
	// frame counter is a position, not a length.
	if (!session_inputs.empty())
	{
		const u32 frames = (u32)session_inputs.size();
		st["frames"] = frames;
		st["durationSeconds"] = (double)((int)(frames / 60.0 * 10)) / 10.0;
	}
	// What the F2 cycle was set to for this session. It can change between generations, so each
	// generation records its own copy below - this is just the current value.
	st["slotCycle"] = hostfs::savestateCycleCount();
	st["rerecords"] = rerecord_base + rerecord_count;
	double secs = edit_base + (clip_start_time > 0 ? os_GetSeconds() - clip_start_time : 0);
	st["editSeconds"] = (u32)secs;
	st["sessions"] = clip_sessions;
	std::string nowIso = utcNowIso();
	st["lastOpenedUtc"] = nowIso;
	st["lastOpenedLocal"] = localUsTime(nowIso);
	j["stats"] = st;
	// EDITED SINCE (David): once the live files differ from the snapshot they were restored from, say so - a rewind or edit
	// (rerecord_count only moves on a byte-changing overwrite or an edit), a state written to the live folder, or a movie
	// that grew / shrank since the clip opened (judged only when the roll was already loaded at open; a macro Full boot
	// injects it later). Sticky: only a new restore (a fresh restoredFrom record) clears it.
	if (j.contains("restoredFrom") && j["restoredFrom"].is_object())
	{
		const bool movieChanged = movie_len_at_begin > 0 && (u32)session_inputs.size() != movie_len_at_begin;
		if (rerecord_count > 0 || live_state_writes > 0 || movieChanged)
			live_from_edited = true;
		if (live_from_edited)
			j["restoredFrom"]["editedSince"] = true;
	}

	// schema 3: mirror the per-slot savestate facts into the clip file so anything reading a clip
	// (the VS Code extension, a packaging script) gets labels, movie positions, timestamps and
	// thumbnail filenames without having to know the sidecar naming rules. This block is DERIVED -
	// it is regenerated wholesale on every write, never merged, so a state that disappeared from
	// the folder disappears here too. The .label sidecar remains the source of truth for labels.
	// The scan is only meaningful once settings.content is populated - at flycast_init time it is
	// NOT yet, and regenerating states[] from that blind scan wiped the slot records whenever
	// WriteClipStats ran early (ApplyEdit at Replay::Init did exactly that; textguard caught it).
	// A scan that cannot see keeps the existing states[] untouched.
	if (!settings.content.fileName.empty())
	{
	nlohmann::json states = nlohmann::json::array();
	std::vector<hostfs::SavestateInfo> slots = hostfs::scanSavestateInfo();
	for (int i = 0; i < (int)slots.size(); i++)
	{
		if (!slots[i].exists)
			continue;
		std::string file = ghc::filesystem::path(hostfs::getSavestatePath(i, false)).filename().string();
		nlohmann::json e = nlohmann::json::object();
		e["slot"] = i;
		e["file"] = file;
		e["label"] = slots[i].label;
		e["movieFrame"] = slots[i].movieFrame;
		// Sidecar v2 vintage - with the top-level "rewinds" array this lets a reader compute the
		// dead-timeline staleness verdict from clip.json alone. Absent for old 4-byte sidecars.
		if (slots[i].haveSeq)
			e["rerecordSeq"] = slots[i].rerecordSeq;
		e["bytes"] = slots[i].size;
		if (slots[i].mtime != 0)
		{
			std::string iso = utcIso((time_t)slots[i].mtime);
			e["savedUtc"] = iso;
			e["savedLocal"] = localUsTime(iso);
		}
		std::error_code tec;
		if (ghc::filesystem::exists(hostfs::getSavestatePath(i, false) + ".png", tec))
			e["thumb"] = file + ".png";
		states.push_back(e);
	}
	j["states"] = states;
	}

	// The rewind log, as [seq, frame] pairs. Compact array-of-pairs on purpose: one entry per
	// re-record, potentially hundreds, and a reader only needs the two numbers.
	nlohmann::json rw = nlohmann::json::array();
	for (const auto& r : rewind_log)
		rw.push_back(nlohmann::json::array({ r.first, r.second }));
	j["rewinds"] = rw;
	// Macro clip (CANON_macro_mode.md): mark it so a reader knows the replay is a DUMMY and State 0 + the
	// <folder>_macro.txt are the pair to load. Absent on Movie clips. macroBase = the frame the macro starts at.
	if (cfgLoadBool("dojo", "MacroMode", false))
	{
		j["mode"] = "macro";
		j["replayDummy"] = true;
		const std::string mbase = ghc::filesystem::path(hostfs::savestateFolderOverride).filename().string();
		j["macroFile"] = mbase + "_macro.txt";
		bool macroHasState0 = false;
		j["macroBase"] = MacroAnchorFrame(macroHasState0);	// State 0's frame - the macro is RELATIVE to it (David)
		j["macroPairState"] = 0;	// State 0 is the EXPECTED launch pair...
		j["macroHasState0"] = macroHasState0;	// ...but NOT required: false = the macro plays from a chosen frame (Stage)
	}
	// schema 6 (David): a manifest of the LIVE (main) set at the top of the folder, refreshed on every write, plus
	// how many backup folders it holds - so a reader tells current from copy without listing the disk.
	{
		nlohmann::json c = nlohmann::json::object();
		const std::string base = ghc::filesystem::path(hostfs::savestateFolderOverride).filename().string();
		int files = 0, genDirs = 0, stateFiles = 0;
		u64 bytes = 0;
		std::string movie, macro;
		bool audioEnv = false, skipMap = false;
		std::error_code cec;
		for (const auto& f : ghc::filesystem::directory_iterator(hostfs::savestateFolderOverride, cec))
		{
			const std::string fn = f.path().filename().string();
			if (f.is_directory(cec))
			{
				std::string kind;
				int num = 0;
				if (tasGenFolderKind(fn, base, kind, num))
					genDirs++;
				continue;
			}
			files++;
			std::error_code se;
			bytes += (u64)ghc::filesystem::file_size(f.path(), se);
			const std::string ext = f.path().extension().string();
			if (ext == ".flyr" || ext == ".flyreplay")
				movie = fn;
			else if (ext == ".state")
				stateFiles++;
			else if (fn == "audio.env")
				audioEnv = true;
			else if (fn == "skip.map")
				skipMap = true;
			else if (ext == ".txt" && fn.size() > 10 && fn.compare(fn.size() - 10, 10, "_macro.txt") == 0)
				macro = fn;
		}
		c["files"] = files;
		c["bytes"] = bytes;
		c["movie"] = movie;
		if (!macro.empty())
			c["macro"] = macro;
		c["audioEnv"] = audioEnv;
		c["skipMap"] = skipMap;
		c["states"] = stateFiles;
		c["generationFolders"] = genDirs;
		c["updatedUtc"] = nowIso;
		j["contents"] = c;
	}
	j["schema"] = 6;

	// (review) write only on a difference - every clip.json write bumps the library version and the browsers / panes
	// re-read on it, and F1 / F3 used to write unconditionally because the lastOpened* stamps changed every call. The
	// volatile fields (lastOpened*, contents.updatedUtc, editSeconds) are ignored for the comparison and still land at
	// least once a minute, so the edit clock is never more than a minute behind.
	{
		static double lastWriteAt = 0;
		nlohmann::json a = j, b = before;
		for (nlohmann::json *x : { &a, &b })
		{
			if (x->contains("stats") && (*x)["stats"].is_object())
			{
				(*x)["stats"].erase("lastOpenedUtc");
				(*x)["stats"].erase("lastOpenedLocal");
				(*x)["stats"].erase("editSeconds");
			}
			if (x->contains("contents") && (*x)["contents"].is_object())
				(*x)["contents"].erase("updatedUtc");
		}
		const double now = os_GetSeconds();
		if (a == b && now - lastWriteAt < 60.0)
			return;
		lastWriteAt = now;
	}
	tas_clip::write(hostfs::savestateFolderOverride, j);
}

// Copy a clip's live files (movie + states + sidecars + metadata) into its next free gen_NN.
// Returns the generation number (files copied via out-param), or -1 on failure. Gens are
// IMMUTABLE once written - the live folder is the scratchpad; gens only ever serve as sources
// for "replace live", never as load targets themselves.

// Generations are immutable, so their history is APPENDED and never rewritten. Each entry records
// what the session looked like at that moment - including the F2 cycle setting, which the user can
// change between backups (start on 0-9, switch to 0-49, and gen 3 legitimately holds more slots
// than gen 1). Without this the JSON could only ever describe the latest setting, and a reader
// would have no way to tell why an older generation has a different shape.

// The folder work lives in tas_clip (P1 refactor, 2026-09-03); Dojo keeps the session-aware wrappers.
int Dojo::ArchiveClipDir(const std::string& clipDir, int *filesCopied, u64 *bytesCopied, const char *tag)
{
	return tas_clip::archive(clipDir, filesCopied, bytesCopied, tag);
}

int Dojo::RestoreClipDir(const std::string& clipDir, const std::string& genName)
{
	// Guardrails (2026-09-03 research). A LIVE restore is unsafe: the loaded movie, the replay writer, the rewind log,
	// undo, bookmarks, the wave / skip stores and the loaded macro all stay stale in memory and their next write undoes
	// the restore. Refuse when this clip is the one open in the session - restore is a pre-boot operation.
	if (!settings.content.path.empty() && !hostfs::savestateFolderOverride.empty()
			&& ghc::filesystem::path(clipDir).lexically_normal() == ghc::filesystem::path(hostfs::savestateFolderOverride).lexically_normal())
	{
		NOTICE_LOG(NETWORK, "TAS GEN: REFUSED a live restore of the open clip %s - close the game first", clipDir.c_str());
		return -1;
	}
	const int n = tas_clip::restore(clipDir, genName);
	if (n >= 0)
		savestate_epoch++;	// the live states changed underneath the HUD and the slot browser - make them re-read
	return n;
}

void Dojo::ReconcileGenerations(const std::string& clipDir)
{
	tas_clip::reconcile(clipDir);
}

void Dojo::RecordGeneration(int gen, int files, u64 bytes, const char *kind)
{
	if (hostfs::savestateFolderOverride.empty())
		return;
	nlohmann::json e = nlohmann::json::object();
	char name[192];
	snprintf(name, sizeof(name), "%s_%s_%02d",
			ghc::filesystem::path(hostfs::savestateFolderOverride).filename().string().c_str(), kind, gen);
	e["gen"] = gen;
	e["kind"] = kind;	// schema 6: "gen" (Movie clip) or "setup" (macro clip) - the folder suffix
	e["name"] = name;
	e["files"] = files;
	e["bytes"] = bytes;
	e["slotCycle"] = hostfs::savestateCycleCount();
	const std::string iso = utcNowIso();
	e["createdUtc"] = iso;
	e["createdLocal"] = localUsTime(iso);
	// schema 6 (David: everything a reader - and the branching UI to come - needs to know what this copy holds):
	// the live facts at backup time. The movie length is the movie's ACTUAL length (see WriteClipStats).
	e["atFrame"] = frame_number.load();
	e["movieFrames"] = (u32)session_inputs.size();
	e["rerecords"] = rerecord_base + rerecord_count;
	e["mode"] = cfgLoadBool("dojo", "MacroMode", false) ? "macro" : "movie";
	// Exactly which slots this generation captured - empty slots write nothing, so this is the
	// real shape of the backup rather than a range - and the movie frame each of them anchors on.
	nlohmann::json slots = nlohmann::json::array();
	nlohmann::json slotFrames = nlohmann::json::array();
	std::vector<hostfs::SavestateInfo> info = hostfs::scanSavestateInfo();
	for (int i = 0; i < (int)info.size(); i++)
		if (info[i].exists)
		{
			slots.push_back(i);
			slotFrames.push_back(nlohmann::json::array({ i, info[i].movieFrame }));
		}
	e["slots"] = slots;
	e["slotFrames"] = slotFrames;
	e["tags"] = nlohmann::json::array();	// user fields (Generations popup) - the only part of an entry meant to change
	e["notes"] = "";
	e["present"] = true;
	tas_clip::appendGeneration(hostfs::savestateFolderOverride, e);	// P2: generations[] + generationCount + latestGeneration + schema
	NOTICE_LOG(NETWORK, "TAS GEN: recorded %s %d in clip.json (%d slots, at frame %u, %u movie frames)",
			kind, gen, (int)slots.size(), frame_number.load(), (u32)session_inputs.size());
}

// schema 6 (David: persistent, ACCURATE data about what the folder holds). Walk the clip's subfolders and make
// generations[] match the disk: a backup folder with no entry (every macro setup before today, a bare gen_NN from an
// old build, a copy dropped in by hand) gets one synthesized from the folder itself (recovered: true, the folder's
// write time as its date); an entry whose folder is gone stays (the schema only ever grows) flagged present: false;
// files / bytes are re-counted. Entries are ordered gens first, then setups, by number. Pre-boot safe: works on any
// clip directory, no session needed.

void Dojo::ArchiveGeneration()
{
	if (hostfs::savestateFolderOverride.empty())
	{
		gui_display_notification("No active clip - a generation needs a recording or replay session", 3500);
		return;
	}
	// ONE generation system (David, 2026-09-04): every F8 is a numbered gen_NN whatever the session mode; "setup" is a
	// TAG the user puts on it in the prompt that follows, not a second kind with its own numbering (two systems were
	// going to be impossible to keep straight). Folders named _setup_NN from before still list - the predicate knows
	// them - and interleave with gens by date. Stats first, so the clip.json COPIED into the snapshot is current.
	WriteClipStats();
	int copied = 0;
	u64 bytes = 0;
	const int gen = ArchiveClipDir(hostfs::savestateFolderOverride, &copied, &bytes, "gen");
	if (gen < 0)
	{
		gui_display_notification("New generation failed (limit reached?)", 3000);
		return;
	}
	RecordGeneration(gen, copied, bytes, "gen");
	ReconcileGenerations(hostfs::savestateFolderOverride);	// the folder walk keeps older, unrecorded copies in step
	savestate_epoch++;	// (review) the HUD's Backups count refreshes now, not on its 2 s tick
	char name[192];
	snprintf(name, sizeof(name), "%s_gen_%02d", ghc::filesystem::path(hostfs::savestateFolderOverride).filename().string().c_str(), gen);
	snapshot_prompt_name = name;
	snapshot_prompt_num = gen;
	snapshot_prompt_files = copied;
	snapshot_prompt_bytes = bytes;
	snapshot_prompt_frame = frame_number.load();
	snapshot_prompt_movie = (u32)session_inputs.size();
	snapshot_prompt_pending = true;
	snapshot_reveal = true;	// the F8 hotkey opens + focuses States; its Generations pane puts the cursor in this row's Tags cell
	NOTICE_LOG(NETWORK, "TAS GEN: snapshot %s (%d files, %.1f MB) at frame %u - tag prompt raised", name, copied, bytes / 1048576.0, frame_number.load());
}

// Entering the End-of-Replay screen has to release every TAS hold. The screen takes keyboard
// focus, so a key held at that moment may never deliver its keyup here - and a latched hold either
// spins the scrub loop on a dead movie or, on BASE, matures into an unattended overwrite.
void Dojo::ReleaseTasHolds()
{
	step_held = false;
	slot_held = false;
	slot_next_repeat = 0;
	save_hold_since = 0;
	shift_held_since = 0;
}

// ---- T3: input fidelity (gate G1) ------------------------------------------------------------
// Compares what the movie SENT against what the game READ out of its own RAM, every frame of a
// replay. The game latches inputs on its own cadence (the frame-skip cycle), so READ is allowed
// to match any of the last few SENT frames; the histogram reports the actual latency instead of
// assuming one. A READ that matches NONE of them is a real fidelity break.
namespace
{
	// Canonical 11-bit button set both sides convert into:
	// bit 0..3 Up Down Left Right, 4 LP, 5 HP, 6 LK, 7 HK, 8 Start, 9 A1, 10 A2
	u16 canonFromPacket(const FrameInputs& fi)
	{
		// The packet stores ~kcode - pressed bits SET (MapleRecordAction line ~465) - and the
		// triggers ride in the same field as BTN_TRIGGER_* bits, which playback turns back into
		// halfAxes. The first draft assumed live-kcode active-low and read the trigger BYTES;
		// every frame mismatched as an exact bit-complement, which is what gave it away.
		const u32 k = fi.kcode;
		u16 c = 0;
		if (k & DC_DPAD_UP)     c |= 1 << 0;
		if (k & DC_DPAD_DOWN)   c |= 1 << 1;
		if (k & DC_DPAD_LEFT)   c |= 1 << 2;
		if (k & DC_DPAD_RIGHT)  c |= 1 << 3;
		if (k & DC_BTN_X)       c |= 1 << 4;	// LP
		if (k & DC_BTN_Y)       c |= 1 << 5;	// HP
		if (k & DC_BTN_A)       c |= 1 << 6;	// LK
		if (k & DC_BTN_B)       c |= 1 << 7;	// HK
		if (k & DC_BTN_START)   c |= 1 << 8;
		// Trigger duality, measured the hard way: GGPO-era recordings carry triggers as kcode
		// BITS, offline recordings carry them as analog BYTES (triggers.l/r) - and playback
		// applies whichever is present. The first bits-only version passed on magneto/hayato
		// (no assist presses anywhere in them) and failed 30 times on the first real combo
		// fixture, every miss being A1: the assist calls. Accept either representation.
		if ((k & BTN_TRIGGER_LEFT) || fi.triggers.l >= 0x20)  c |= 1 << 9;	// A1
		if ((k & BTN_TRIGGER_RIGHT) || fi.triggers.r >= 0x20) c |= 1 << 10;	// A2
		return c;
	}

	u16 canonFromFlags(u8 a, u8 b)
	{
		u16 c = 0;
		if (b & 32)  c |= 1 << 0;
		if (b & 16)  c |= 1 << 1;
		if (b & 8)   c |= 1 << 2;
		if (b & 4)   c |= 1 << 3;
		if (b & 2)   c |= 1 << 4;
		if (b & 1)   c |= 1 << 5;
		if (a & 64)  c |= 1 << 6;
		if (a & 32)  c |= 1 << 7;
		if (b & 128) c |= 1 << 8;
		if (a & 128) c |= 1 << 9;
		if (a & 16)  c |= 1 << 10;
		return c;
	}

	struct FidelityState
	{
		u16 sentHist[2][4] = {};	// per player, last 4 SENT sets (index 0 = most recent)
		u32 checked = 0;
		u32 mismatches = 0;
		u32 lagHist[4] = {};		// which history slot READ matched
		u32 loggedMism = 0;
		u32 lastFrame = 0;
		int warmup = 4;				// frames to skip while the history refills
	} fid;
	// OUTSIDE FidelityState on purpose: Report() resets fid wholesale, and when these lived
	// inside it, the reset re-armed the open-with-truncate - a stray frame applied after replay
	// end then wiped ~780 dumped lines down to one. One dump file per process run.
	std::ofstream fidDump;
	bool fidDumpTried = false;
}

void Dojo::VerifyInputsFrame(const std::vector<u8>& frameData)
{
	if (!cfgLoadBool("dojo", "VerifyInputs", false) || !play_match)
		return;
	// A frame_number discontinuity (seek, rewind, session start) makes the SENT history stale
	// against RAM that was restored wholesale by the savestate - the one "mismatch" left after
	// the convention fix was exactly the first frame after an F3 seek. Cold-restart the window.
	{
		const u32 fn = frame_number.load();
		if (fn != fid.lastFrame + 1)
			fid.warmup = 4;
		fid.lastFrame = fn;
	}
	// The flags in RAM right now are the result of the PREVIOUS frame's processing, so compare
	// them against the history BEFORE pushing this frame's packet in.
	if (fid.warmup > 0)
		fid.warmup--;
	else if (tas_mvc2::mapValidated())
	{
		const tas_mvc2::GameState gs = tas_mvc2::read();
		const u16 readC[2] = { canonFromFlags(gs.in.p1a, gs.in.p1b), canonFromFlags(gs.in.p2a, gs.in.p2b) };
		bool anyChecked = false;
		for (int p = 0; p < 2; p++)
		{
			int hit = -1;
			for (int k = 0; k < 4; k++)
				if (readC[p] == fid.sentHist[p][k])
				{
					hit = k;
					break;
				}
			if (hit >= 0)
				fid.lagHist[hit]++;
			else
			{
				fid.mismatches++;
				if (fid.loggedMism < 12)
				{
					fid.loggedMism++;
					NOTICE_LOG(NETWORK, "INPUT FIDELITY: frame %u P%d read %03X matches no recent"
							" sent (last: %03X %03X %03X %03X)", frame_number.load(), p + 1,
							readC[p], fid.sentHist[p][0], fid.sentHist[p][1],
							fid.sentHist[p][2], fid.sentHist[p][3]);
				}
			}
			anyChecked = true;
		}
		if (anyChecked)
			fid.checked++;
	}
	// Per-frame dump (dojo:FidelityDump): every SENT/READ pair as JSONL in the clip folder, so
	// "were all my inputs handled" is answerable OFFLINE, after the fact - the visualizer is only
	// humanly readable while paused, and this is the machine-readable record for the VS Code
	// extension or a diff script. One line per applied frame: canonical 11-bit sets, hex.
	if (cfgLoadBool("dojo", "FidelityDump", false) && tas_mvc2::mapValidated())
	{
		if (!fidDumpTried)
		{
			fidDumpTried = true;
			if (!hostfs::savestateFolderOverride.empty())
				fidDump.open(hostfs::savestateFolderOverride + "/fidelity.jsonl",
						std::ios::out | std::ios::trunc);
			if (fidDump.is_open())
				NOTICE_LOG(NETWORK, "TAS: fidelity dump -> %s/fidelity.jsonl",
						hostfs::savestateFolderOverride.c_str());
		}
		if (fidDump.is_open())
		{
			const tas_mvc2::GameState gsd = tas_mvc2::read();
			FrameInputs f0, f1;
			memcpy(&f0, frameData.data(), sizeof(FrameInputs));
			memcpy(&f1, frameData.data() + sizeof(FrameInputs), sizeof(FrameInputs));
			char line[160];
			snprintf(line, sizeof(line),
					"{\"f\":%u,\"sp1\":\"%03X\",\"sp2\":\"%03X\",\"rp1\":\"%03X\",\"rp2\":\"%03X\",\"skip\":\"%u/%u\"}\n",
					frame_number.load(), canonFromPacket(f0), canonFromPacket(f1),
					canonFromFlags(gsd.in.p1a, gsd.in.p1b), canonFromFlags(gsd.in.p2a, gsd.in.p2b),
					gsd.skipCount, gsd.skipRate);
			fidDump << line;
			// Flushed per line: this is a forensic artifact, and a killed process (or crash -
			// exactly when you want it) must not take the buffer with it.
			fidDump.flush();
		}
	}

	// Push this frame's SENT sets into the history (even before validation, so the window is
	// warm the moment the map validates).
	for (int p = 0; p < 2; p++)
	{
		FrameInputs fi;
		memcpy(&fi, frameData.data() + p * sizeof(FrameInputs), sizeof(FrameInputs));
		for (int k = 3; k > 0; k--)
			fid.sentHist[p][k] = fid.sentHist[p][k - 1];
		fid.sentHist[p][0] = canonFromPacket(fi);
	}
}

// A2: emu-thread-safe lock test. Scans the GUI-published snapshot under the mutex; the emu
// thread never touches slotScan()/gui_locked_ranges (GUI-thread scans). Empty during replay
// (the publisher clears it when play_match), so replay frame data is untouched.
bool Dojo::FrameLockedEmu(u32 frame)
{
	std::lock_guard<std::mutex> lk(locked_ranges_mtx);
	for (const auto& lr : locked_ranges_cache)
		if (frame >= lr.first && frame < lr.second)
			return true;
	return false;
}

s64 Dojo::ApplyEdit(const std::map<u32, std::vector<u8>>& edited, const char *source)
{
	// Strictness first: the edited movie must cover the original's frames (extension is fine,
	// truncation is not - a shorter movie silently orphaning frames is exactly the ambiguity a
	// funnel exists to refuse).
	if (!session_inputs.empty() && !edited.empty()
			&& (edited.begin()->first > session_inputs.begin()->first
				|| edited.rbegin()->first < session_inputs.rbegin()->first))
	{
		NOTICE_LOG(NETWORK, "TAS EDIT: REFUSED - edited movie (%u..%u) does not cover the original"
				" (%u..%u)", edited.begin()->first, edited.rbegin()->first,
				session_inputs.begin()->first, session_inputs.rbegin()->first);
		return -1;
	}

	// Diff: every differing (or new) frame, and the first of them.
	std::vector<std::pair<u32, std::vector<u8>>> changed;
	s64 first = -1;
	for (const auto& kv : edited)
	{
		auto it = session_inputs.find(kv.first);
		if (it == session_inputs.end() || it->second != kv.second)
		{
			changed.push_back(kv);
			if (first < 0)
				first = (s64)kv.first;
		}
	}
	if (changed.empty())
	{
		// The no-op guard: an edit that changed nothing logs no timeline event - otherwise every
		// open-and-save would stale states for no reason and the guard would cry wolf.
		NOTICE_LOG(NETWORK, "TAS EDIT: %s changed nothing - no timeline event", source);
		return -1;
	}

	// TAS timeline-lock (R7): drop any changed frame that lands in a LOCKED savestate range - a write
	// into a protected range behaves like PROTECT (never recorded). Every in-place UI edit verb (paint
	// / macro place / paste / Input-Sender Replace+Append / sequences) funnels here; edits require
	// !play_match so replay never reaches this. undo/redo (history_replay) passes through so a pre-lock
	// edit can always be reverted.
	if (!history_replay && !play_match && (!locked_slots.empty() || base_prelock))
	{
		std::vector<std::pair<u32, u32>> lr;
		gui_locked_ranges(lr);			// compute the ranges ONCE (not once per changed frame)
		const size_t before = changed.size();
		changed.erase(std::remove_if(changed.begin(), changed.end(),
				[&](const std::pair<u32, std::vector<u8>>& kv){
					for (const auto& r : lr)
						if (kv.first >= r.first && kv.first < r.second) return true;
					return false;
				}),
				changed.end());
		if (changed.size() != before)
		{
			char lm[96];
			snprintf(lm, sizeof(lm), "%u frame(s) blocked - locked range (unlock in Timeline)",
					(u32)(before - changed.size()));
			gui_display_notification(lm, 2500);
			if (changed.empty())
				return -1;
			first = (s64)changed.front().first;	// still frame-ordered (edited is a std::map)
		}
	}

	// History (TASEditor-style Ctrl+Z): the OLD bytes of every touched frame. An empty
	// vector marks "frame did not exist before" (extension); undo ZEROES those instead of
	// deleting - true deletion would truncate, which this funnel refuses by design.
	if (!history_replay)
	{
		EditPatch patch;
		patch.source = source;
		if (edit_meta_capture)
			patch.gui_meta = edit_meta_capture();	// pre-edit bookmarks etc.
		for (const auto& kv : changed)
		{
			auto pit = session_inputs.find(kv.first);
			patch.frames.emplace_back(kv.first,
					pit != session_inputs.end() ? pit->second : std::vector<u8>());
		}
		undo_stack.push_back(std::move(patch));
		if (undo_stack.size() > 128)
			undo_stack.erase(undo_stack.begin());
		redo_stack.clear();
	}

	for (const auto& kv : changed)
		session_inputs[kv.first] = kv.second;
	macro_save_pending.store(true, std::memory_order_relaxed);	// macro autosave: stale until the GUI tick / a flush rewrites the .txt
	// A bake into the WRITE stale-tail region is the NEW take for those frames: advance the amber marker past a
	// contiguous-from-the-marker write (changed is frame-ordered). A write entirely above the marker leaves the
	// old frames between still old, so the marker stays.
	if (stale_tail_from != ~0u && !changed.empty() && changed.front().first <= stale_tail_from)
		stale_tail_from = std::max(stale_tail_from, changed.back().first + 1);

	// The timeline event - the whole point. An edit at frame N invalidates states above N exactly
	// as a rewind to N does, and it uses the SAME clock (the re-record counter), so sidecar seqs
	// and the rewind log stay one coherent history.
	if (first >= 0)
		tas_wave::markStaleFrom((u32)first);	// TAS waveform: the audio after an edit is an old take until re-run
	rerecord_count++;
	rewind_log.emplace_back(rerecord_base + rerecord_count, (u32)first);
	divergence_open = false;	// an edit is its own event; live divergence after it is a new one
	replay.AppendEditedFrames(changed);
	WriteClipStats();
	NOTICE_LOG(NETWORK, "TAS EDIT: applied %u changed frame(s) from %s, first at %u"
			" - timeline event [%u, %u]", (u32)changed.size(), source, (u32)first,
			rerecord_base + rerecord_count, (u32)first);
	return first;
}

s64 Dojo::ApplyEditResize(const std::map<u32, std::vector<u8>>& edited, const char *source)
{
	// Diff both directions: changed/new frames AND removed ones (the shrink).
	std::vector<std::pair<u32, std::vector<u8>>> changed;
	std::vector<u32> removed;
	s64 first = -1;
	for (const auto& kv : edited)
	{
		auto it = session_inputs.find(kv.first);
		if (it == session_inputs.end() || it->second != kv.second)
		{
			changed.push_back(kv);
			if (first < 0 || kv.first < (u32)first)
				first = (s64)kv.first;
		}
	}
	for (const auto& kv : session_inputs)
		if (edited.find(kv.first) == edited.end())
		{
			removed.push_back(kv.first);
			if (first < 0 || kv.first < (u32)first)
				first = (s64)kv.first;
		}
	if (changed.empty() && removed.empty())
	{
		NOTICE_LOG(NETWORK, "TAS EDIT: %s changed nothing - no timeline event", source);
		return -1;
	}

	// TAS timeline-lock (R7): a structural edit renumbers frames from the first touched frame
	// onward, dragging any locked range whose end is past it. Refuse ONLY then - an edit entirely
	// after every lock is safe (covers manual forward locks AND the pre-BASE auto-lock). UI-only /
	// !play_match, and undo/redo (history_replay) passes through, so sync stays identical.
	if (!history_replay && !play_match)
	{
		std::vector<std::pair<u32, u32>> lr;
		gui_locked_ranges(lr);
		u32 maxHi = 0;
		for (const auto& r : lr)
			if (r.second > maxHi) maxHi = r.second;
		if (first >= 0 && (u32)first < maxHi)
		{
			gui_display_notification("Structural edit blocked - it would shift a locked range (unlock in Timeline)", 2800);
			return -1;
		}
	}

	// History: old bytes for changed frames AND for removed ones (undo re-extends and
	// restores fully; REDO of a delete zeroes the tail instead of re-shrinking - the
	// documented v1 limitation of absent-frame patches).
	if (!history_replay)
	{
		EditPatch patch;
		patch.source = source;
		if (edit_meta_capture)
			patch.gui_meta = edit_meta_capture();	// pre-edit bookmarks etc.
		for (const auto& kv : changed)
		{
			auto pit = session_inputs.find(kv.first);
			patch.frames.emplace_back(kv.first,
					pit != session_inputs.end() ? pit->second : std::vector<u8>());
		}
		for (u32 rf : removed)
			patch.frames.emplace_back(rf, session_inputs[rf]);
		undo_stack.push_back(std::move(patch));
		if (undo_stack.size() > 128)
			undo_stack.erase(undo_stack.begin());
		redo_stack.clear();
	}

	for (const auto& kv : changed)
		session_inputs[kv.first] = kv.second;
	for (u32 rf : removed)
		session_inputs.erase(rf);
	macro_save_pending.store(true, std::memory_order_relaxed);	// macro autosave: stale until the GUI tick / a flush rewrites the .txt
	if (stale_tail_from != ~0u && !changed.empty() && changed.front().first <= stale_tail_from)
		stale_tail_from = std::max(stale_tail_from, changed.back().first + 1);	// bake clears the amber it writes

	if (first >= 0)
		tas_wave::markStaleFrom((u32)first);	// TAS waveform: the audio after an edit is an old take until re-run
	rerecord_count++;
	rewind_log.emplace_back(rerecord_base + rerecord_count, (u32)first);
	divergence_open = false;
	if (first >= 0)
		tas_ruler::eraseFrom((u32)first);	// REL ruler: the rows from the edit have not run yet
	replay.RewriteReplayFile();		// the whole point: a shorter movie must persist as shorter
	WriteClipStats();
	NOTICE_LOG(NETWORK, "TAS EDIT: resize from %s - %u changed, %u removed, first %u"
			" - timeline event [%u, %u]", source, (u32)changed.size(), (u32)removed.size(),
			(u32)first, rerecord_base + rerecord_count, (u32)first);
	return first;
}

// Replay one patch (the OLD side of an edit) through the same funnel, capturing the current
// bytes as the inverse for the opposite stack. Undo and redo are mirror images.
static void tasBuildPatchApply(Dojo& d, Dojo::EditPatch&& p, std::vector<Dojo::EditPatch>& inverseStack)
{
	Dojo::EditPatch inv;
	inv.source = p.source;
	if (d.edit_meta_capture)
		inv.gui_meta = d.edit_meta_capture();	// current (post-edit) state, for the mirror
	std::map<u32, std::vector<u8>> edited = d.session_inputs;
	for (const auto& fr : p.frames)
	{
		auto cur = d.session_inputs.find(fr.first);
		inv.frames.emplace_back(fr.first,
				cur != d.session_inputs.end() ? cur->second : std::vector<u8>());
		if (fr.second.empty())
		{
			auto eit = edited.find(fr.first);
			if (eit != edited.end())
				std::fill(eit->second.begin(), eit->second.end(), 0);
		}
		else
			edited[fr.first] = fr.second;
	}
	d.history_replay = true;
	d.ApplyEdit(edited, p.source.c_str());
	d.history_replay = false;
	if (d.edit_meta_apply && !p.gui_meta.empty())
		d.edit_meta_apply(p.gui_meta);		// bookmarks come back with the frames
	inverseStack.push_back(std::move(inv));
}

bool Dojo::ApplyUndo()
{
	if (undo_stack.empty())
		return false;
	EditPatch p = std::move(undo_stack.back());
	undo_stack.pop_back();
	tasBuildPatchApply(*this, std::move(p), redo_stack);
	return true;
}

bool Dojo::ApplyRedo()
{
	if (redo_stack.empty())
		return false;
	EditPatch p = std::move(redo_stack.back());
	redo_stack.pop_back();
	tasBuildPatchApply(*this, std::move(p), undo_stack);
	return true;
}

void Dojo::VerifyInputsReport()
{
	if (!cfgLoadBool("dojo", "VerifyInputs", false) || fid.checked == 0)
		return;
	if (fidDump.is_open())
		fidDump.close();
	NOTICE_LOG(NETWORK, "INPUT FIDELITY: %u frames checked, %u mismatches"
			" (read lag histogram: same=%u -1f=%u -2f=%u -3f=%u)%s",
			fid.checked, fid.mismatches,
			fid.lagHist[0], fid.lagHist[1], fid.lagHist[2], fid.lagHist[3],
			fid.mismatches == 0 ? "  fidelity-ok" : "  FIDELITY-FAIL");
	fid = {};
}

// SOCD: opposite cardinals can't coexist. Drop up+down / left+right if both are set
// (a single clean direction should never conflict; this is the guard).
static u16 tasSocdClean(u16 c)
{
	if ((c & (1 << 0)) && (c & (1 << 1))) c &= (u16)~((1 << 0) | (1 << 1));	// up+down
	if ((c & (1 << 2)) && (c & (1 << 3))) c &= (u16)~((1 << 2) | (1 << 3));	// left+right
	return c;
}

// Write a canon mask into one player's FrameInputs (inverse of canonFromPacket): DC_* kcode
// bits for dirs/buttons/START, A1/A2 via the trigger BYTES (offline reads triggers.l/r).
// DIRECTIONS are LAST-WRITE-WINS - a mask carrying a direction CLOBBERS whatever direction
// was in the frame (a fresh Right beats a movie Left). BUTTONS are additive. Pass a
// SOCD-clean mask. Shared by the tas_auto hold overlay and the -> Live sequence.
static void tasWriteCanonIntoFrame(FrameInputs *fi, u16 ac)
{
	if (ac & 0x0F)		// any direction present -> replace the frame's whole direction
		fi->kcode &= (u32)~(DC_DPAD_UP | DC_DPAD_DOWN | DC_DPAD_LEFT | DC_DPAD_RIGHT);
	if (ac & (1 << 0))  fi->kcode |= DC_DPAD_UP;
	if (ac & (1 << 1))  fi->kcode |= DC_DPAD_DOWN;
	if (ac & (1 << 2))  fi->kcode |= DC_DPAD_LEFT;
	if (ac & (1 << 3))  fi->kcode |= DC_DPAD_RIGHT;
	if (ac & (1 << 4))  fi->kcode |= DC_BTN_X;
	if (ac & (1 << 5))  fi->kcode |= DC_BTN_Y;
	if (ac & (1 << 6))  fi->kcode |= DC_BTN_A;
	if (ac & (1 << 7))  fi->kcode |= DC_BTN_B;
	if (ac & (1 << 8))  fi->kcode |= DC_BTN_START;
	if (ac & (1 << 9))  { fi->kcode |= BTN_TRIGGER_LEFT;  fi->triggers.l = 0xff; }
	if (ac & (1 << 10)) { fi->kcode |= BTN_TRIGGER_RIGHT; fi->triggers.r = 0xff; }
}

// Clear every canon-carried input from a frame (dpad + face buttons + START + triggers). The READ-WRITE
// STOMP uses this to wipe the PRIOR cell before writing a re-authored signal, so the signal REPLACES it
// (CANON s3 "Replace @ Active") instead of OR-ing on top - where a button could never be cleared.
static void tasClearPlayerCanon(FrameInputs *fi)
{
	fi->kcode &= (u32)~(DC_DPAD_UP | DC_DPAD_DOWN | DC_DPAD_LEFT | DC_DPAD_RIGHT
	                  | DC_BTN_X | DC_BTN_Y | DC_BTN_A | DC_BTN_B | DC_BTN_START
	                  | BTN_TRIGGER_LEFT | BTN_TRIGGER_RIGHT);
	fi->triggers.l = 0;
	fi->triggers.r = 0;
}

// MERGE rule (David: "OR-style input merging"): ADD a canon mask onto a player's existing packet - buttons OR; the
// added direction replaces the packet's only when it is non-neutral (two directions are never ORed into SOCD
// garbage); nothing is ever removed; the analog bytes stay. The GUI's bake verbs apply the same rule on their side
// (tasCombineCell in dojo_gui.cpp); this one serves the emu thread (the pad in READ-WRITE).
static u16 tasMergeCanon(u16 have, u16 add)
{
	const u16 ad = tasSocdClean((u16)(add & 0x0F));	// a pad SOCD pair cleans to 0 -> the cell keeps its direction (nothing is ever removed)
	const u16 dir = ad ? ad : (u16)(have & 0x0F);
	return (u16)(dir | ((have | add) & 0x7F0));
}
static void tasMergeCanonIntoFrame(FrameInputs *fi, u16 add)
{
	if (add == 0)
		return;
	const u16 have = canonFromPacket(*fi);
	const u16 merged = tasMergeCanon(have, add);
	if (merged == have)
		return;	// nothing new: leave the bytes alone (a re-encode of a GGPO-era cell differs byte-wise and would fire a spurious re-record)
	tasClearPlayerCanon(fi);
	tasWriteCanonIntoFrame(fi, merged);
}

// OnEnter boot seed (user): inject a boot-navigation snippet at frame 0 of a fresh Record session so
// the movie itself carries the menu inputs (power-on -> VS mode) and a replay is input-aligned from
// frame 0 - pure inputs, no savestate, so it cannot desync the way a state pairing can. The launch
// menu resolves the Snippets-library OnEnter tag into dojo:OnEnterFile; a CLI boot can pass
// -config dojo:OnEnterFile=<path>. Runs on the GUI thread in gui_start_game (guiMutex held, emulator
// not yet running), AFTER Reset() so nothing below is clobbered.
void Dojo::SeedOnEnter()
{
	const std::string oePath = cfgLoadStr("dojo", "OnEnterFile", "");
	// One-shot: consume the staging immediately, whatever happens next. A virtual entry outlives the
	// session, and boot paths that bypass the launch menu (games list, Test Game, CLI) must never
	// inherit a stale seed (audit).
	if (!oePath.empty())
		cfgSetVirtual("dojo", "OnEnterFile", "");
	if (oePath.empty())
		return;
	// Fresh-authoring sessions only. Read the LIVE cfg the way gui_start_game itself does - the
	// config:: Options are caches only refreshed mid-boot, and settings.network.online is not set
	// until the handshake, so neither can gate this reliably (audit: netplay/spectate could seed).
	if (cfgLoadBool("network", "GGPO", false) || cfgLoadBool("network", "Enable", false)
			|| cfgLoadBool("dojo", "Receiving", false) || play_match)
	{
		NOTICE_LOG(NETWORK, "TAS ONENTER: seed skipped - online/spectate session");
		return;
	}
	// An auto-loaded savestate jumps the machine past power-on while the seed still plays from
	// frame 0 - the two features are mutually exclusive by definition (audit).
	if (config::AutoLoadState || config::AutoLoadTrainingNetState)
	{
		WARN_LOG(NETWORK, "TAS ONENTER: seed refused - Auto-Load State is enabled");
		gui_display_notification("OnEnter: Auto-Load State is on - seed skipped (it needs a power-on boot)", 4000);
		return;
	}
	tas_macro::Macro m;
	std::string mErr;
	if (!tas_macro::Load(oePath, m, mErr) || m.frames.empty())
	{
		WARN_LOG(NETWORK, "TAS ONENTER: seed load failed for '%s' - %s", oePath.c_str(), mErr.c_str());
		gui_display_notification("OnEnter: couldn't load the seed snippet - booting clean", 3500);
		return;
	}
	// The seed is authored against a power-on machine with NO input delay. A stale dojo:Delay (left
	// over from delay netplay) would resurrect in the mid-boot Settings reload and let FillDelayFrames
	// overwrite the seed head - pin it to 0 the way Replay::Init pins the savestate slot (a virtual
	// entry wins the reload). The frame-0 timeline clear lives in gui_start_game now - EVERY record
	// boot gets it, seeded or not (audit: the boot-clean path inherited a stale timeline).
	config::Delay = 0;
	cfgSetVirtual("dojo", "Delay", "0");
	for (u32 f = 0; f < (u32)m.frames.size(); f++)
	{
		// SOCD-clean the direction nibble - tasWriteCanonIntoFrame's contract expects it, and a
		// hand-edited snippet line can carry Up+Down (audit).
		const u16 oeP1 = (u16)(tasSocdClean((u16)(m.frames[f].p1 & 0x0F)) | (m.frames[f].p1 & 0x7F0));
		const u16 oeP2 = (u16)(tasSocdClean((u16)(m.frames[f].p2 & 0x0F)) | (m.frames[f].p2 & 0x7F0));
		std::vector<u8> row(sizeof(FrameInputs) * MAX_PLAYERS, 0);
		tasWriteCanonIntoFrame((FrameInputs *)row.data(), oeP1);
		tasWriteCanonIntoFrame((FrameInputs *)(row.data() + sizeof(FrameInputs)), oeP2);
		session_inputs[f] = std::move(row);
	}
	// READ-WRITE is the mode that makes the seed WORK: the cells play while the released pad preserves
	// them, and past the end the infinite roll hands control to the user. WRITE would clobber the seed
	// with neutral as it advanced; READ could never hand control over. For Record MOVIE the handoff
	// stop in gui.cpp restores WRITE - the seed only BORROWS READ-WRITE (audit: the session otherwise
	// silently stayed READ-WRITE and dropped idle frames).
	macro_armed = true;
	// Run-to-handoff: land PAUSED on the first user frame (the step-hold in gui.cpp stops at frame >=
	// target). The boot fast-forward is NOT armed here - Emulator::loadGame unconditionally clears
	// fastForwardMode after this runs (audit: dead-on-arrival) - the OSD loop arms it once the game
	// is really running, keyed on onenter_ff, and the handoff stop drops it again.
	stepping = true;
	target_step_frame = (u32)m.frames.size();
	onenter_ff = true;
	boot_ready_arm = true;	// the handoff pause announces the staged macro
	NOTICE_LOG(NETWORK, "TAS ONENTER: seeded %u frames from '%s' - READ-WRITE, handoff pause @ %u",
			(u32)m.frames.size(), oePath.c_str(), target_step_frame);
}

// Play Macro FULL load (CANON_macro_mode.md §12 "PLAY MACRO WIRED"): the pre-boot Macros browser (PIECE A)
// picked a clip whose State 0 is the macro's ANCHOR. The macro.txt is RELATIVE (WriteMacroFile emits from the
// anchor, so line 0 == State 0's frame): stash the rows and ARM a deferred State-0 load, and the boot handoff
// injects line i at (State 0's frame + i) - the freshly-booted game pauses AT the anchor with the machine ==
// State 0 and session_inputs[State-0-frame] == the macro's first cell (EXACT frame sync). Runs on the GUI
// thread in gui_start_game (guiMutex held, emulator NOT yet running) - the session_inputs writes are safe for
// exactly the same reason SeedOnEnter's are. The STATE itself is loaded post-boot (no machine exists here),
// WHILE PAUSED, by the OSD-loop handoff (gui.cpp), so no macro cell can run before the anchor. The RELATIVE
// rebase (InjectPendingMacroAt: session_inputs[startFrame + i]) pairs macro line 0 with State 0's frame for
// ANY anchor; State 0 at frame 0 is the byte-identical special case. Returns false (caller boots via
// SeedOnEnter) if the macro file will not load.
bool Dojo::LoadMacroFull(const std::string& clipDir, const std::string& macroFile)
{
	tas_macro::Macro m;
	std::string mErr;
	if (!tas_macro::Load(macroFile, m, mErr) || m.frames.empty())
	{
		WARN_LOG(NETWORK, "TAS MACRO FULL: load failed for '%s' - %s - booting without the Full load",
				macroFile.c_str(), mErr.c_str());
		gui_display_notification("Play Macro: couldn't load that macro - booting clean", 4000);
		return false;
	}
	// A macro is RELATIVE - a combo authored from its OWN frame 0, with no inherent tie to the game frame it
	// will play at. So DON'T place it at absolute 0; stash the rows and let the boot handoff inject them at
	// State 0's frame (David: "inject the macro into whatever frame state0 was"), known only AFTER the
	// deferred State-0 load. SOCD-clean each direction nibble first (a hand-edited line can carry Up+Down).
	macro_pending.clear();
	for (u32 f = 0; f < (u32)m.frames.size(); f++)
	{
		const u16 p1 = (u16)(tasSocdClean((u16)(m.frames[f].p1 & 0x0F)) | (m.frames[f].p1 & 0x7F0));
		const u16 p2 = (u16)(tasSocdClean((u16)(m.frames[f].p2 & 0x0F)) | (m.frames[f].p2 & 0x7F0));
		std::vector<u8> row(sizeof(FrameInputs) * MAX_PLAYERS, 0);
		tasWriteCanonIntoFrame((FrameInputs *)row.data(), p1);
		tasWriteCanonIntoFrame((FrameInputs *)(row.data() + sizeof(FrameInputs)), p2);
		macro_pending.push_back(std::move(row));
	}
	session_inputs.clear();
	frame_number = 0;
	macro_armed = true;		// READ-WRITE: the roll drives the guest; a released pad preserves it
	// Pin Delay=0 like SeedOnEnter: a stale dojo:Delay resurrecting in the mid-boot Settings reload would let
	// FillDelayFrames overwrite the macro HEAD (and the anchor cell when State 0 = frame 0).
	config::Delay = 0;
	cfgSetVirtual("dojo", "Delay", "0");
	// Point savestates at THIS clip (slot 0 -> its State 0). The state LOAD is DEFERRED to the boot handoff
	// (the machine does not exist yet). Arm a boot pause whose target is already satisfied at power-on, plus
	// the one-shot Full-load flag the handoff consumes to load State 0 while paused.
	hostfs::savestateFolderOverride = clipDir;
	// (research 2026-09-04) the Full boot skipped BeginClipStats: no live_from, counters inherited from the previous clip,
	// and the wave / skip stores started EMPTY so their next save truncated the clip's audio.env / skip.map. Before
	// loaded_macro_rr below, since this zeroes rerecord_count.
	BeginClipStats();
	boot_ready_arm = true;	// the handoff pause announces the loaded clip and opens F4
	stepping = true;
	target_step_frame = 0;
	macro_fullload = true;
	loaded_macro_path = macroFile;		// remember the file so the Macros window can save the edited movie back to it
	loaded_macro_rr = rerecord_count;	// relative snapshot (not necessarily 0); nothing zeroes rerecord_count mid-macro-session, so a later edit bumps it -> enables save-back
	NOTICE_LOG(NETWORK, "TAS MACRO FULL: %u macro frames staged from '%s'; clip '%s' - State 0 + macro inject at the boot pause (relative to State 0's frame)",
			(u32)macro_pending.size(), macroFile.c_str(), clipDir.c_str());
	return true;
}

// Play Macro STAGE with a State 0 (David, 2026-09-04: "it is staged if there is no State 0, and if there is a State 0 it is
// prebaked to play at State 0's first frame"): the macro went to the STAGE buffer pre-boot (dojo_gui tasMacroStageFile - the
// buffer survives the boot); here the session opens exactly like a Full load - the deferred State-0 load at the boot handoff -
// but with NOTHING in the roll, so the playhead sits on State 0's frame with the staged macro ready to PLACE / fire there.
// The handoff's InjectPendingMacroAt is a no-op on the empty stash. The caller verified the State 0 file (hasState0).
bool Dojo::LoadClipState0Boot(const std::string& clipDir)
{
	macro_pending.clear();
	session_inputs.clear();
	frame_number = 0;
	macro_armed = true;		// READ-WRITE, like every macro session
	config::Delay = 0;
	cfgSetVirtual("dojo", "Delay", "0");
	hostfs::savestateFolderOverride = clipDir;
	BeginClipStats();
	boot_ready_arm = true;
	stepping = true;
	target_step_frame = 0;
	macro_fullload = true;	// the handoff loads State 0 while paused (shared with the Full load)
	loaded_macro_path.clear();	// no save-back target: the roll is empty, the macro lives in the stage buffer
	loaded_macro_rr = rerecord_count;
	NOTICE_LOG(NETWORK, "TAS MACRO STAGE: clip '%s' - State 0 at the boot pause, empty roll (the macro is staged)", clipDir.c_str());
	return true;
}

// Play Macro FULL load, step 2 (relative injection): lay the pending macro into the roll STARTING at State 0's
// frame. Called from the boot handoff (gui.cpp) right after gui_loadState set frame_number to State 0's .frame.
// A macro carries its own 0-based line numbering (no inherent tie to the game frame), so line i drives session
// frame (startFrame + i) - the combo plays from wherever State 0 landed, exact-aligned to its anchor.
void Dojo::InjectPendingMacroAt(u32 startFrame)
{
	if (macro_pending.empty())
		return;
	session_inputs.clear();		// drop the few neutral cells the power-on boot frames materialized before the pause
	stale_tail_from = ~0u;		// a REPLACED roll has no old take - the marker is per-roll, not per-session (audit hygiene)
	for (u32 i = 0; i < (u32)macro_pending.size(); i++)
		session_inputs[startFrame + i] = macro_pending[i];
	NOTICE_LOG(NETWORK, "TAS MACRO FULL: injected %u macro frames at State 0's frame %u (relative)",
			(u32)macro_pending.size(), startFrame);
	macro_pending.clear();
}

void Dojo::MapleApplyAction(MapleInputState inputState[4])
{
	// TAS WRITE (recording, NOT replaying): the movie is UNBOUNDED. Past the last authored cell we materialize a
	// NEUTRAL frame (below) so frame_number keeps advancing - the piano roll is infinite while writing, and
	// pause/step stay alive. Only play_match (replay) has a fixed length / the ReplayEnd dead-end.
	// A "writing session" is not only RecordMatches: a replay that the user flipped to READ-WRITE/WRITE
	// (R / banner) still has a filename attached, and the .flyr append path (below) treats HasAppendTarget()
	// as writing too. Include it here so running off the movie end in that state GROWS the roll instead of
	// pinning frame_number (which would kill pause/step/Space - the infinite-roll freeze on a sibling path).
	const bool tasWriteGrow = !dojo.play_match && !settings.network.online && !replay.ggpo_session
			&& (cfgLoadBool("dojo", "RecordMatches", false) || cfgLoadBool("dojo", "PlayMacro", false)
				|| replay.HasAppendTarget());
	if (dojo.session_inputs.empty() && !tasWriteGrow)
		return;
	tas_ruler::onPoll(frame_number.load());	// REL ruler: remember the MvC2 skip cadence at this poll (a few byte reads)
	tas_mvc2::comboPoll();	// the combo meters: last + peak since the Frame Skip Test's bake (David, 2026-09-05)

	// Wait-for-Frameskip: release a held Input-Sender Send on the next MvC2 skip/reset frame (toggle 0x8C289622
	// == 255, the "0 0 0 255" cadence), injecting at skip+N via the same live path as a normal Send. N is the
	// tunable dojo:FrameskipOffset (default 1) - the frame-perfect alignment knob (David: try 1/2/3). The frame
	// deadline is the fallback if no skip is seen (e.g. sent outside a match) so the send can never hang. Emu
	// thread + guest-RAM-local read = safe.
	if (dojo.frameskip_send_pending)
	{
		const u32 fnow = dojo.frame_number.load();
		const bool skipFrame = tas_mvc2::readSkipToggle() == 255;
		if (skipFrame || fnow >= dojo.frameskip_send_deadline)
		{
			const u32 fsOff = (u32)cfgLoadInt("dojo", "FrameskipOffset", 1);	// skip+N alignment
			tas_auto::playLive(dojo.frameskip_send_p1, dojo.frameskip_send_p2, (u64)fnow + fsOff);
			dojo.frameskip_send_pending = false;
			if (!skipFrame)	// fallback fired (sent outside a match); success is silent (the SEND button shows "Sending")
				gui_display_notification("No frameskip seen - sending anyway", 1500);
		}
	}

	if (!settings.network.online && dojo.frame_number < config::Delay)
	{
		tas_wave::onFrameEnd(frame_number.load(std::memory_order_relaxed));	// TAS waveform: file this frame's audio before the counter moves
		frame_number++;
		return;
	}

	// TAS: READ on a MACRO pauses on the roll's last authored frame instead of the Movie "End of Replay" dead-end.
	// Both modes read session_inputs; only READ (play_match) has the ReplayEnd exits below (keyed on MovieEnd(), the
	// true end, since the macro-parity audit; size() was a bad length proxy). Pause at the true last key (rbegin, so
	// an offset Play-Macro roll stops on its real end too); the user then quits or reloads State 0 to rewatch.
	// MacroMode is set explicitly on every boot (yes for Record/Play Macro, no for Movie/replay), so it's reliable.
	const bool macroReadSession = dojo.play_match && cfgLoadBool("dojo", "MacroMode", false);
	if (macroReadSession)
	{
		const u32 macroLast = dojo.session_inputs.empty() ? 0 : dojo.session_inputs.rbegin()->first;
		if (dojo.frame_number >= macroLast && !dojo.stepping)
		{
			dojo.stepping = true;
			dojo.target_step_frame = macroLast;	// the OSD step-hold (gui.cpp) stops the emu here -> Paused on the last frame
		}
	}

	if (dojo.play_match && !macroReadSession && !dojo.session_inputs.empty() && dojo.frame_number == dojo.MovieEnd() - 1)
	{
		if (gui_state != GuiState::ReplayEnd)
		{
			NOTICE_LOG(NETWORK, "TAS: replay end at frame %u (movie exhausted)", dojo.frame_number.load());
			dojo.ReleaseTasHolds();
			dojo.VerifyInputsReport();
			if (cfgLoadBool("dojo", "AutoCapture", false) && videorec::isRecording())
				videorec::requestStop();	// headless capture: stop + mux when the movie ends
		}
		gui_setState(GuiState::ReplayEnd);
	}

	// set by reading replay/spectating header
	u32 analogAxes = replay.analog;

	if (replay.ggpo_session)
	{
		if (last_applied_frame == frame_number)
			return;
	}

	u32 inputSize = sizeof(FrameInputs);
	// TAS: guard against reading a frame the movie doesn't have (seeking past the recorded range,
	// or toggling read-only mid-record). Bare session_inputs[frame] would create an empty buffer
	// and the FrameInputs cast below would dereference null -> GPF.
	auto tas_sit = dojo.session_inputs.find(dojo.frame_number);
	if (tas_sit == dojo.session_inputs.end()
			|| tas_sit->second.size() < sizeof(FrameInputs) * MAX_PLAYERS)
	{
		if (dojo.play_match && !macroReadSession)	// macro READ pauses at the roll's end (armed above), not ReplayEnd
		{
			if (gui_state != GuiState::ReplayEnd)
			{
				NOTICE_LOG(NETWORK, "TAS: replay end at frame %u (no frame data)", dojo.frame_number.load());
				dojo.ReleaseTasHolds();
				dojo.VerifyInputsReport();
				if (cfgLoadBool("dojo", "AutoCapture", false) && videorec::isRecording())
					videorec::requestStop();	// headless capture: stop + mux when the movie ends
			}
			gui_setState(GuiState::ReplayEnd);
			return;
		}
		if (!tasWriteGrow)
			return;		// no data + not a recording write session -> keep the old guard (avoid a null-deref GPF)
		// recording WRITE past the end: the roll is INFINITE -> materialize a neutral cell and fall through, so this
		// frame applies neutral input and frame_number++ (end of function) advances. Pause/step stay alive.
		// NOTE: the row may EXIST but be undersized (FillDelayFrames writes per-player 12-byte rows) - emplace
		// keeps an existing key, so the short row would survive and the per-player reads below would run past
		// its end (heap overread, audit). Grow such a row in place instead.
		if (tas_sit == dojo.session_inputs.end())
			tas_sit = dojo.session_inputs.emplace(dojo.frame_number.load(),
					std::vector<u8>(sizeof(FrameInputs) * MAX_PLAYERS, 0)).first;
		else
			tas_sit->second.assign(sizeof(FrameInputs) * MAX_PLAYERS, 0);
	}
	std::vector<u8> current_inputs = tas_sit->second;
	VerifyInputsFrame(current_inputs);		// T3: SENT-vs-READ, one call per applied frame

	if (cfgLoadBool("dojo", "Training", false))
	{
		if (training.player_switched)
		{
			std::vector<u8> swapped_frame = training.SwapPlayerInputs(current_inputs.size(), current_inputs.data());
			std::memcpy(current_inputs.data(), swapped_frame.data(), current_inputs.size());
		}

		if (training.recording)
		{
			auto filtered_frame = training.FilterPlayerInput(training.control_player, current_inputs.size(), current_inputs.data());

			if (config::RecordOnFirstInput)
			{
				std::vector<u8> blank_inputs(current_inputs.size(), 0);
				blank_inputs[8] = 2;
				blank_inputs[20] = 2;

				if (!training.recording_started &&
					std::memcmp(current_inputs.data(), blank_inputs.data(), current_inputs.size()) != 0)
				{
					training.recording_started = true;
				}
			}

			if (training.recording_started)
				training.record_slot[training.current_record_slot].push_back(std::string((const char *)filtered_frame.data(), sizeof(FrameInputs) * MAX_PLAYERS));
		}

		if (!training.recording && !training.playing_input &&
			training.playback_loop && training.trigger_playback &&
			dojo.frame_number > training.next_playback_frame)
		{
			if (training.rnd_playback_loop)
			{
				auto it = training.recorded_slots.cbegin();
				int rnd = rand() % training.recorded_slots.size();
				std::advance(it, rnd);
				training.current_record_slot = *it;
			}
			training.PlayRecording(training.current_record_slot);
		}

		if (rec_inputs.count(frame_number))
		{
			std::vector<u8> m_inputs;
			std::vector<u8> existing = rec_inputs[frame_number];

			for (int i = 0; i < current_inputs.size(); i++)
			{
				m_inputs.push_back((unsigned char)(current_inputs[i] | existing[i]));
			}

			std::memcpy(current_inputs.data(), m_inputs.data(), current_inputs.size());
		}
	}

	// tas_auto LIVE injection (hold/auto-fire overlayCanon + the -> Live sequence liveCanon). LIVE ONLY
	// (skipped when play_match), a no-op until armed/playing. In BOTH writable modes the whole overlay bakes
	// into session_inputs AND current_inputs HERE, before the .flyr append; a LOCKED range only DRIVES the
	// guest (after the append) and is never baked. See the detailed note just below.
	u16 injAc[MAX_PLAYERS] = { 0 };
	bool anyInj = false;
	// TAS timeline-lock (R7): in a LOCKED range live (SEND / LIVE AUTO) drives the guest but is NOT baked
	// into the movie. Reads the emu-safe cache; false under play_match, so replay is untouched.
	const bool frameLocked = FrameLockedEmu((u32)dojo.frame_number.load());
	if (!dojo.play_match && (tas_auto::anyArmed() || tas_auto::liveActive()))
	{
		const u64 fr = dojo.frame_number.load();
		for (int pl = 0; pl < MAX_PLAYERS; pl++)
		{
			const u16 hold = tas_auto::overlayCanon(pl, fr);
			const u16 seq  = tas_auto::liveCanon(pl, fr);
			const u16 dir = tasSocdClean((seq & 0x0F) ? (u16)(seq & 0x0F) : (u16)(hold & 0x0F));
			injAc[pl] = (u16)(dir | ((hold | seq) & 0x7F0));	// SOCD dir + union of buttons
			if (injAc[pl])
				anyInj = true;
		}
		tas_auto::liveTick(fr);
		// READ/WRITE model (CANON_readwrite_model.md): every source is a SIGNAL that writes the piano roll. The
		// whole live overlay - auto-fire hold/turbo AND the -> Live sequence - bakes into current_inputs AND the
		// movie cell here (before the .flyr append), in BOTH writable modes (READ-WRITE + WRITE). It ORs its armed
		// bits ON TOP, so it COMBINES with a hand-held pad frame (the pad's own preserve/clobber lives in
		// MapleRecordAction). A LOCKED range only DRIVES the guest (after the append, below) and is warned - never baked.
		if (frameLocked && anyInj)
		{
			static double lastLiveLockWarn = 0;
			const double nowLlw = os_GetSeconds();
			if (nowLlw - lastLiveLockWarn > 1.5)
			{
				gui_display_notification("Locked range - live input driving only, not recorded", 1500);
				lastLiveLockWarn = nowLlw;
			}
		}
		else if (anyInj)
			for (int pl = 0; pl < MAX_PLAYERS; pl++)
			{
				if (!injAc[pl] || (size_t)(pl + 1) * inputSize > current_inputs.size())
					continue;
				// READ-WRITE STOMP (CANON s3): when the pad did NOT record this frame the cell still holds the
				// PRIOR recording - clear this player's canon so the overlay REPLACES it (Replace @ Active),
				// not OR onto it (a button could never be cleared). When the pad DID record, the cell already
				// holds the current pad, so skip the clear and OR to COMBINE (edge #4). WRITE always records,
				// so tas_rw_pad_wrote is true there and this never fires.
				// MERGE sends ON (David): no clear - the live send ORs onto the cell (a baked button then survives a live
				// send; clearing is the brush's / Clear's job while Merge is on).
				if (dojo.macro_armed && !dojo.tas_rw_pad_wrote && !dojo.send_merge.load(std::memory_order_relaxed))
				{
					tasClearPlayerCanon((FrameInputs *)(current_inputs.data() + pl * inputSize));
					if ((size_t)(pl + 1) * inputSize <= tas_sit->second.size())
						tasClearPlayerCanon((FrameInputs *)(tas_sit->second.data() + pl * inputSize));
				}
				tasWriteCanonIntoFrame((FrameInputs *)(current_inputs.data() + pl * inputSize), injAc[pl]);
				if ((size_t)(pl + 1) * inputSize <= tas_sit->second.size())
					tasWriteCanonIntoFrame((FrameInputs *)(tas_sit->second.data() + pl * inputSize), injAc[pl]);
			}
	}

	// The third clause is the replay-turned-write fix: a LOADED replay in write mode (R)
	// must keep appending to its own file. RecordMatches stays "no" in replay sessions and
	// R only flips play_match, so without this the whole block was skipped and hand-recorded
	// frames lived only in memory - the reload silently lost them (user-reported).
	// StartRecording is safe in that mode: with RecordMatches=no it creates NO new file
	// (the attached one already has its header) and just marks recording_started.
	if (!config::GGPOEnable && !dojo.play_match &&
		(config::RecordMatches || config::Transmitting || replay.HasAppendTarget()))
	{
		if (!recording_started)
			replay.StartRecording();

		// create frame container for export
		unsigned char new_frame[MAPLE_FRAME_SIZE] = {0};
		memcpy(new_frame, (unsigned char *)&dojo.frame_number, sizeof(unsigned int));
		memcpy(new_frame + 4, (unsigned char *)current_inputs.data(), current_inputs.size());
		std::string frame_((const char *)new_frame, MAPLE_FRAME_SIZE);

		replay.AppendToReplay(frame_, 4);
	}

	// LOCKED range: drive the GUEST with live input AFTER the append, so the movie/.flyr stays the protected
	// cell (never baked in a locked range). Not-locked writable modes already baked above (before the append).
	if (!dojo.play_match && frameLocked && anyInj)
		for (int pl = 0; pl < MAX_PLAYERS; pl++)
		{
			if (!injAc[pl] || (size_t)(pl + 1) * inputSize > current_inputs.size())
				continue;
			tasWriteCanonIntoFrame((FrameInputs *)(current_inputs.data() + pl * inputSize), injAc[pl]);
		}

	FrameInputs *player_inputs;

	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		MapleInputState &state = inputState[player];
		player_inputs = (FrameInputs *)(current_inputs.data() + (player * inputSize));
		PrintInputs(player, *player_inputs);
		state.kcode = ~player_inputs->kcode;

		// if (player_inputs->kcode > 0)
		//	NOTICE_LOG(NETWORK, "FRAME %u KCODE %d", dojo.frame_number.load(), player_inputs->kcode);
		// else
		//	NOTICE_LOG(NETWORK, "FRAME %u", dojo.frame_number.load());

		if (analogAxes > 0)
		{
			state.fullAxes[PJAI_X1] = player_inputs->u.analog.x << 8;
			if (analogAxes >= 2)
				state.fullAxes[PJAI_Y1] = player_inputs->u.analog.y << 8;
		}

		// only apply precise triggers offline
		if (settings.network.online || (dojo.play_match && !dojo.precise_triggers))
		{
			state.halfAxes[PJTI_R] = (state.kcode & BTN_TRIGGER_RIGHT) == 0 ? 0xffff : 0;
			state.halfAxes[PJTI_L] = (state.kcode & BTN_TRIGGER_LEFT) == 0 ? 0xffff : 0;
		}
		else
		{
			state.halfAxes[PJTI_R] = player_inputs->triggers.r << 8;
			state.halfAxes[PJTI_L] = player_inputs->triggers.l << 8;
		}
	}

	if (replay.ggpo_session)
	{
		last_applied_frame = dojo.frame_number;
	}

	if (cfgLoadBool("dojo", "Training", false) && config::ShowTrainingInputDisplay ||
		dojo.play_match && config::ShowReplayInputDisplay)
		AddToInputDisplay(inputState);

	PrintMapleInputState(inputState);

	if (!settings.network.online && !replay.ggpo_session)
	{
		tas_wave::onFrameEnd(frame_number.load(std::memory_order_relaxed));	// TAS waveform: file this frame's audio before the counter moves
		frame_number++;
	}
}

void Dojo::PrintInputs(int player, FrameInputs inputs)
{
	if (inputs.kcode == 0)
		return;
}

void Dojo::PrintMapleInputState(MapleInputState inputState[4])
{
	for (int player = 0; player < 4; player++)
	{
		u32 kcode = ~inputState[player].kcode;
		if (kcode == 0)
			continue;
	}
}

void Dojo::GGPORecordAction(int frame, int size, unsigned char *bits)
{
	std::vector<u8> m_inputs(sizeof(FrameInputs) * MAX_PLAYERS, 0);
	int player_input_size = sizeof(u32) + config::GGPOAnalogAxes.get();
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		std::memcpy(m_inputs.data() + (p * sizeof(FrameInputs)), bits + (p * player_input_size), player_input_size);
	}

	PollRecordAction(frame, m_inputs.size(), m_inputs.data());
}

void Dojo::AddToInputDisplay(MapleInputState inputState[4])
{
	u32 frame = dojo.frame_number.load();
	u32 effective_frame = dojo.frame_number.load();
	// set by reading replay/spectating header
	u32 analogAxes = dojo.replay.analog;

	u32 inputSize = sizeof(u32) + analogAxes;
	std::vector<u8> inputs = dojo.session_inputs[dojo.frame_number];

	constexpr int MAX_PLAYERS = 2;

	std::bitset<16> input_bitset;

	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		MapleInputState &state = inputState[player];

		input_bitset = std::bitset<16>(~state.kcode);

		std::string dc_buttons[18] = {
			"C",
			"B",
			"A",
			"Start",
			"Up",
			"Down",
			"Left",
			"Right",
			"Z",
			"Y",
			"X",
			"D",
			"",
			"",
			"",
			"",
			"LT",
			"RT"};
		std::string aw_buttons[18] = {
			"3",
			"2",
			"1",
			"Start",
			"Up",
			"Down",
			"Left",
			"Right",
			"",
			"5",
			"4",
			"",
			"",
			"",
			"",
			"",
			"LT",
			"RT"};
		std::string naomi_buttons[18] = {
			"3",
			"2",
			"1",
			"Start",
			"Up",
			"Down",
			"Left",
			"Right",
			"6",
			"5",
			"4",
			"",
			"",
			"",
			"",
			"",
			"",
			""};

		// tracks buttons & triggers in single digital bitset
		std::bitset<18> bt_bitset;
		std::vector<bool> dir_bits{
			false,
			false,
			false,
			false};

		for (size_t i = 0; i < input_bitset.size(); i++)
		{
			if (input_bitset.test(i))
			{
				bt_bitset.set(i);
			}
		}

		if (settings.platform.system == DC_PLATFORM_DREAMCAST)
		{
			if (state.halfAxes[PJTI_L] >= 0x2000)	// A1 = L trigger from the ANALOG value: kcode trigger bits are unset offline, and the local BTN_TRIGGER_* shadow points at the wrong bits
				bt_bitset.set(16);

			if (state.halfAxes[PJTI_R] >= 0x2000)	// A2 = R trigger (same analog source the Input Viz uses: triggers<<8 offline, 0xffff when pressed online)
				bt_bitset.set(17);
		}
		int num_dir_notation = 5;

		std::stringstream ss("");
		std::stringstream dir_ss("");

		if (bt_bitset.test(4) && bt_bitset.test(6))
			num_dir_notation = 7;
		else if (bt_bitset.test(4) && bt_bitset.test(7))
			num_dir_notation = 9;
		else if (bt_bitset.test(5) && bt_bitset.test(6))
			num_dir_notation = 1;
		else if (bt_bitset.test(5) && bt_bitset.test(7))
			num_dir_notation = 3;
		else if (bt_bitset.test(4))
			num_dir_notation = 8;
		else if (bt_bitset.test(5))
			num_dir_notation = 2;
		else if (bt_bitset.test(6))
			num_dir_notation = 4;
		else if (bt_bitset.test(7))
			num_dir_notation = 6;

		for (size_t i = 0; i < bt_bitset.size(); i++)
		{
			if (bt_bitset.test(i))
			{
				// assign direction bitset
				// U D L R
				if (settings.platform.system == DC_PLATFORM_DREAMCAST || settings.platform.system == DC_PLATFORM_ATOMISWAVE)
				{
					if (i >= 4 && i <= 7)
					{
						dir_bits[i - 4] = true;
					}
					else
					{
						if (settings.platform.system == DC_PLATFORM_DREAMCAST)
						{
							if (!dc_buttons[i].empty())
								ss << " " << dc_buttons[i];
						}
						else if (settings.platform.system == DC_PLATFORM_ATOMISWAVE)
						{
							if (!aw_buttons[i].empty())
								ss << " " << aw_buttons[i];
						}
					}
				}
				else if (settings.platform.system == DC_PLATFORM_NAOMI ||
						 settings.platform.system == DC_PLATFORM_NAOMI2)
				{
					if (i >= 4 && i <= 7)
					{
						dir_bits[i - 4] = true;
					}
					else
					{
						if (!naomi_buttons[i].empty())
							ss << " " << naomi_buttons[i];
					}
					std::reverse(dir_bits.begin(), dir_bits.end());
				}
			}
		}

		ss.flush();

		for (int i = 0; i < 4; i++)
		{
			if (dir_bits[i])
			{
				switch (i)
				{
				case 0:
					dir_ss << "U";
					break;
				case 1:
					dir_ss << "D";
					break;
				case 2:
					dir_ss << "L";
					break;
				case 3:
					dir_ss << "R";
					break;
				}
			}
		}
		dir_ss.flush();

		if (last_held_input[player] != bt_bitset)
		{
			last_held_input[player] = bt_bitset;
			displayed_inputs[player][effective_frame] = bt_bitset;
			displayed_inputs_str[player][effective_frame] = ss.str();
			displayed_dirs_str[player][effective_frame] = dir_ss.str();
			displayed_dirs[player][effective_frame] = dir_bits;
			displayed_inputs_duration[player][effective_frame] = 1;
			displayed_num_dirs[player][effective_frame] = num_dir_notation;
		}
	}
}

void Dojo::ResetInputDisplay()
{
	int players = 2;
	for (int p = 0; p < players; p++)
	{
		displayed_inputs[p].clear();
		displayed_inputs_str[p].clear();
		last_displayed_inputs_str.clear();
		displayed_dirs_str[p].clear();
		displayed_inputs_duration[p].clear();
		displayed_dirs[p].clear();
		displayed_num_dirs[p].clear();
	}
}

void Dojo::ProcessBody(unsigned int cmd, unsigned int body_size, const char *buffer, int *offset)
{
	// NOTICE_LOG(NETWORK, "CMD %u", cmd);
	std::cout << "CMD " << cmd << " BODY SIZE " << body_size << std::endl;
	if (cmd == 0)
		return;

	if (cmd == SPECTATE_START)
	{
		unsigned int v = MessageReader::ReadInt((const char *)buffer, offset);
		std::string GameName = MessageReader::ReadString((const char *)buffer, offset);
		std::string PlayerName = MessageReader::ReadString((const char *)buffer, offset);
		std::string OpponentName = MessageReader::ReadString((const char *)buffer, offset);
		std::string Quark = MessageReader::ReadString((const char *)buffer, offset);
		std::string RelayKey = MessageReader::ReadString((const char *)buffer, offset);
		unsigned int analog = MessageReader::ReadInt((const char *)buffer, offset);
		unsigned int precise_triggers = MessageReader::ReadInt((const char *)buffer, offset);
		unsigned int ggpo = MessageReader::ReadInt((const char *)buffer, offset);
		std::string P1CountryCode = MessageReader::ReadString((const char *)buffer, offset);
		std::string P2CountryCode = MessageReader::ReadString((const char *)buffer, offset);

		replay.version = v;
		game_name = GameName;
		// Resolve the ROM for this replay from the dojo game list - but NEVER clobber an
		// already-chosen content path with an empty result. With no Content Location configured,
		// GetEntryPath() returns "", which erased the CLI-passed ROM and stranded replay launches
		// on an empty game list. Fall back to the last successfully-booted ROM as a final resort.
		{
			std::string entry_path = dojo.GetEntryPath(dojo.game_name);
			if (!entry_path.empty())
				settings.content.path = entry_path;
			else if (settings.content.path.empty())
				settings.content.path = cfgLoadStr("dojo", "LastRomPath", "");
		}
		config::Quark = Quark;
		config::RelayKey = RelayKey;
		precise_triggers = (bool)precise_triggers;
		if (ggpo)
			replay.ggpo_session = true;

		settings.dojo.P1CountryCode = P1CountryCode;
		settings.dojo.P2CountryCode = P2CountryCode;

		NOTICE_LOG(NETWORK, "v %u GameName %s PlayerName %s OpponentName %s analog %d", v, GameName.data(), PlayerName.data(), OpponentName.data(), analog);

		// settings.content.path = GetEntryPath(GameName);
		// std::string entry_path =  GetEntryPath(game_name);
		// settings.content.path = entry_path + ".zip";
		// NOTICE_LOG(NETWORK, "ENTRY %s", settings.content.path.data());

		// if (version >= 3)
		//{
		//	settings.dojo.state_md5 = MessageReader::ReadString((const char*)buffer, offset);
		//	settings.dojo.state_commit = MessageReader::ReadString((const char*)buffer, offset);
		// }

		std::cout << "Replay Version: " << replay.version << std::endl;

		if (replay.version >= 2)
		{
			replay.analog = analog;
			// last_consecutive_common_frame = 0;
			frame_number = 0;
		}
		else
			replay.analog = 0;

		// std::cout << "REPLAY VERSION " << version << "ANALOG " << analog << std::endl;

		std::cout << "Game: " << GameName << std::endl;

		std::cout << "Player: " << PlayerName << std::endl;
		std::cout << "Opponent: " << OpponentName << std::endl;
		// if (!received_player_info)
		//{
		settings.dojo.PlayerName = PlayerName;
		settings.dojo.OpponentName = OpponentName;
		AssignPlayerNames();
		//	std::cout << "Player: " << PlayerName << std::endl;
		//	std::cout << "Opponent: " << OpponentName << std::endl;
		// }

		std::cout << "Quark: " << Quark << std::endl;
		std::cout << "Relay Key: " << RelayKey << std::endl;

		// if (version == 3)
		//{
		//	std::cout << "Savestate MD5: " << settings.dojo.state_md5 << std::endl;
		//	std::cout << "Savestate Commit SHA: " << settings.dojo.state_commit << std::endl;
		// }

		// dojo.receiver_header_read = true;
		// dojo.receiver_start_read = true;
	}
	else if (cmd == PLAYER_INFO)
	{
		auto p1_info = MessageReader::ReadPlayerInfo(buffer, offset);
		auto p2_info = MessageReader::ReadPlayerInfo(buffer, offset);

		auto player_name = p1_info[0];
		auto opponent_name = p2_info[0];

		std::cout << "P1: " << player_name << std::endl;
		std::cout << "P2: " << opponent_name << std::endl;

		settings.dojo.PlayerName = player_name;
		settings.dojo.OpponentName = opponent_name;

		// received_player_info = true;

		// AssignPlayerNames();
	}
	else if (cmd == MAPLE_BUFFER)
	{
		unsigned int frame_size = MessageReader::ReadInt((const char *)buffer, offset);

		// read frames
		while ((unsigned int)*offset < body_size)
		{
			std::string frame = MessageReader::ReadContinuousData((const char *)buffer, offset, frame_size);

			// if (memcmp(frame.data(), { 0 }, FRAME_SIZE) == 0)
			if (memcmp(frame.data(), "00000000000000000000", MAPLE_FRAME_SIZE) == 0)
			{
				// dojo.receiver_ended = true;
			}
			else
			{
				u32 frame_num = replay.GetFrameNumber((u8 *)frame.data());
				// NOTICE_LOG(NETWORK, "FRAME NUM %u", frame_num);
				std::string maple_input(frame.data() + 4, (MAPLE_FRAME_SIZE - 4));
				std::vector<u8> inputs(maple_input.begin(), maple_input.end());

				session_inputs[frame_num] = inputs;

				/*
				std::cout << "GGPO FRAME " << frame_num << " ";

				for (int i = 0; i < (MAPLE_FRAME_SIZE - 4); i++)
				{
				  std::bitset<8> b(inputs[i]);
				  std::cout << b.to_string();
				}

				std::cout << std::endl;
				*/

				// buffer stream
				/*
				if (dojo.session_inputs.size() == config::RxFrameBuffer.get() &&
					dojo.frame_number < dojo.last_consecutive_common_frame)
					dojo.resume();
					*/
			}
		}
	}
	else if (cmd == PLAYER_WIN)
	{
		// u32 player = MessageReader::ReadPlayerWin(buffer, offset);

		// if (player == 0)
		//	final_p1_wins++;
		// else if (player == 1)
		//	final_p2_wins++;
	}
	else if (cmd == RECORD_BUFFER)
	{
		unsigned int slot_index = MessageReader::ReadInt((const char *)buffer, offset);
		unsigned int slot_size = MessageReader::ReadInt((const char *)buffer, offset);

		training.record_slot[slot_index].clear();
		training.recorded_slots.insert(slot_index);

		while (*offset < body_size)
		{
			std::string frame = MessageReader::ReadContinuousData((const char *)buffer, offset, MAPLE_FRAME_SIZE);
			training.record_slot[slot_index].push_back(frame);
		}
	}
}

void Dojo::SaveRecordSlotsFile()
{
	std::string rec_dir = get_writable_data_path("recordings");
	std::string game_rec_dir = rec_dir + "/" + get_game_name();

	if (!ghc::filesystem::exists(game_rec_dir))
		ghc::filesystem::create_directories(game_rec_dir);

	std::string filename = game_rec_dir + "/" + get_game_name() + "_" + std::to_string(config::RecSlotFile.get()) + ".rec";

	std::ofstream fout(filename,
					   std::ios::out | std::ios::binary | std::ios_base::app);

	for (unsigned int i : training.recorded_slots)
	{
		MessageWriter record_msg;
		record_msg.AppendHeader(0, RECORD_BUFFER);
		record_msg.AppendInt(i);
		record_msg.AppendInt(training.record_slot[i].size());

		for (auto s : training.record_slot[i])
		{
			record_msg.AppendContinuousData(s.data(), MAPLE_FRAME_SIZE);
		}

		std::vector<unsigned char> message = record_msg.Msg();
		fout.write((const char *)&message[0], message.size());
	}

	fout.close();
}

void Dojo::LoadRecordSlotsFile()
{
	std::string rec_dir = get_writable_data_path("recordings");
	std::string game_rec_dir = rec_dir + "/" + get_game_name();
	std::string filename = game_rec_dir + "/" + get_game_name() + "_" + std::to_string(config::RecSlotFile.get()) + ".rec";

	LoadRecordSlotsFile(filename);
}

void Dojo::LoadRecordSlotsFile(std::string filename)
{
	if (!ghc::filesystem::exists(filename))
		return;

	std::ifstream fin(filename,
					  std::ios::in | std::ios::binary);

	char header_buf[HEADER_LEN] = {0};
	std::vector<unsigned char> body_buf;

	training.recorded_slots.clear();

	while (fin)
	{
		// read header
		memset((void *)header_buf, 0, HEADER_LEN);
		fin.read(header_buf, HEADER_LEN);

		unsigned int body_size = HeaderReader::GetSize((unsigned char *)header_buf);
		unsigned int seq = HeaderReader::GetSeq((unsigned char *)header_buf);
		unsigned int cmd = HeaderReader::GetCmd((unsigned char *)header_buf);

		// read body
		body_buf.resize(body_size);
		fin.read((char *)body_buf.data(), body_size);

		int offset = 0;

		ProcessBody(cmd, body_size, (const char *)body_buf.data(), &offset);
	}
}

void Dojo::Split(std::string const &str, const char delim, std::vector<std::string> &out)
{
	size_t start;
	size_t end = 0;

	while ((start = str.find_first_not_of(delim, end)) != std::string::npos)
	{
		end = str.find(delim, start);
		out.push_back(str.substr(start, end - start));
	}
}

void Dojo::Replace(std::string &subject, const std::string &search, const std::string &replace)
{
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != std::string::npos)
	{
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
}

uint64_t Dojo::UnixTimestamp()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// The frame the recorded macro is anchored to: State 0's frame (slot 0's .state.frame sidecar = frame_number
// at save) if a State 0 exists, else the first recorded input frame. Sets hasState0. A macro is RELATIVE to
// this anchor - the Full-load injects it starting at State 0's frame (David: "the macro begins on State 0").
u32 Dojo::MacroAnchorFrame(bool& hasState0)
{
	std::vector<hostfs::SavestateInfo> slots = hostfs::scanSavestateInfo();
	hasState0 = !slots.empty() && slots[0].exists;
	if (hasState0)
		return slots[0].movieFrame;
	return session_inputs.empty() ? 0 : session_inputs.begin()->first;
}
// Build the session macro from session_inputs and write it to <clip-folder>_macro.txt (the Notepad CE text
// format, openable/editable). Called at teardown AND live during macro recording (David: the file should
// exist + update as you record, not only when the session ends). No-op without inputs / a clip folder.
u32 Dojo::WriteMacroFile()
{
	if (session_inputs.empty() || hostfs::savestateFolderOverride.empty())
		return 0;
	macro_save_pending.store(false, std::memory_order_relaxed);	// cleared BEFORE the snapshot: a write landing during it re-marks; a failed save re-marks below
	bool hasState0 = false;
	// The macro is RELATIVE to State 0's frame (the pass-off anchor): line 0 == the anchor cell, so the
	// relative Full-load places it AT State 0. Frames before State 0 are pre-anchor setup (baked into State 0
	// + kept in the .flyr) and are dropped (David). No State 0 -> anchor to the first input (Stage places freely).
	const u32 anchor = MacroAnchorFrame(hasState0);
	tas_macro::Macro macroOut;
	const u32 mMax = session_inputs.rbegin()->first;
	for (u32 mfi = anchor; mfi <= mMax; mfi++)
	{
		tas_macro::Frame fr;
		fr.p1 = 0;
		fr.p2 = 0;
		const auto mit = session_inputs.find(mfi);
		if (mit != session_inputs.end() && mit->second.size() >= sizeof(FrameInputs) * 2)
		{
			FrameInputs f0, f1;
			memcpy(&f0, mit->second.data(), sizeof(FrameInputs));
			memcpy(&f1, mit->second.data() + sizeof(FrameInputs), sizeof(FrameInputs));
			fr.p1 = canonFromPacket(f0);
			fr.p2 = canonFromPacket(f1);
		}
		macroOut.frames.push_back(fr);
	}
	std::string mErr;
	const std::string mBase = ghc::filesystem::path(hostfs::savestateFolderOverride).filename().string();
	const std::string mPath = hostfs::savestateFolderOverride + "/" + mBase + "_macro.txt";
	if (tas_macro::Save(mPath, macroOut, mErr))
	{
		macro_save_result.store(1, std::memory_order_relaxed);
		macro_save_frames.store((u32)macroOut.frames.size(), std::memory_order_relaxed);
		macro_save_time.store((double)time(nullptr), std::memory_order_relaxed);
		NOTICE_LOG(NETWORK, "TAS MACRO: wrote %d frames -> %s", (int)macroOut.frames.size(), mPath.c_str());
	}
	else
	{
		macro_save_result.store(-1, std::memory_order_relaxed);
		macro_save_pending.store(true, std::memory_order_relaxed);	// still stale: the title bar says SAVE FAILED, the tick retries with a back-off
		WARN_LOG(NETWORK, "TAS MACRO: macro save failed - %s", mErr.c_str());
	}
	return (u32)macroOut.frames.size();
}

// Flush point (ESC / pause / teardown / unloadGame): rewrite the clip's macro .txt if this session owns one and it is
// stale. Record Macro always rewrites (the file IS the session's product, as before); Play Macro only when something was
// edited, so a merely-played macro keeps its hand-written comments (a rewrite emits bare letters).
void Dojo::MacroFlush()
{
	if (!cfgLoadBool("dojo", "MacroMode", false))
		return;
	if (!recording_started && !macro_save_pending.load(std::memory_order_relaxed))
		return;
	WriteMacroFile();
}

void Dojo::Reset()
{
	// PR1 codec probe (headless): dojo:MacroProbe=<file> parses a CE-trainer macro, logs
	// its shape and a semantic round-trip verdict, writes <file>.echo.txt. Pure file work -
	// runs once, no UI, no emu coupling; how we validate the codec against the user's real
	// Demul archive before any macro UI exists.
	{
		static bool macroProbed = false;
		if (!macroProbed)
		{
			macroProbed = true;
			std::string mp = cfgLoadStr("dojo", "MacroProbe", "");
			if (!mp.empty())
				tas_macro::Probe(mp);
		}
	}

	if (play_match)
	{
		tcp_client.Stop();
		session_inputs.clear();
		play_match = false;
	}

	InitScore();

	if (recording_started)
		replay.FlushReplay();	// don't lose the tail batch (< 120 frames) of the recording
	replay.DetachFile();	// UNCONDITIONAL (every session, not just recording - the misleading indent was a
				// missing-brace latent bug): a stale filename must not make the NEXT session (e.g. Just Play)
				// append into the previous clip's .flyr

	// Macro mode (CANON_macro_mode.md): the session's macro IS session_inputs; persist it as macro.txt in the
	// Notepad's text format (openable/editable there). tas_macro::Save writes canonical letters; canonFromPacket
	// decodes each stored packet. (A redundant .flyr is still written by the record path - skipping it is a later slice.)
	MacroFlush();	// <folder>_macro.txt: Record Macro always, Play Macro when edited (it had NO teardown write before the 2026-09-03 loss)
	if (recording_started && cfgLoadBool("dojo", "MacroMode", false))
		WriteClipStats();	// stamp clip.json: mode=macro, the macro file, State 0 pairing, base frame

	stepping = false;
	buffering = false;
	// Forget EVERY reason, not just the user's. This is session teardown, and a
	// reason that outlives the machine it referred to would stop the next one -
	// a script that paused and never resumed before the game was closed would
	// otherwise hand its pause to the following session.
	pausing::resetAll();
	target_step_frame = 0;
	replay_bootload = false;
	macro_fullload = false;	// an aborted Play Macro Full load (quit mid-boot) must not leak its deferred State-0 arm
	macro_pending.clear();	// and drop any macro rows not yet injected at the handoff
	loaded_macro_path.clear();	// a fresh session has no loaded macro until a Full load sets it again
	loaded_macro_rr = 0;
	live_from_gen.clear();
	live_from_local.clear();
	live_from_edited = false;
	live_state_writes = 0;
	movie_len_at_begin = 0;
	boot_ready_arm = false;
	clip_ready_pending = false;
	clip_ready_text.clear();
	macro_save_pending.store(false, std::memory_order_relaxed);	// the autosave status is session-scoped
	macro_save_result.store(0, std::memory_order_relaxed);
	macro_save_time.store(0.0, std::memory_order_relaxed);
	recording_started = false;
	// leak fix (David): teardown cleared play_match but never macro_armed - a stale READ-WRITE could bleed into
	// the next Record Macro (Play Macro re-forces macro_armed itself, so this only matters for the Record path).
	macro_armed = false;
	stale_tail_from = ~0u;	// the old-take marker is session-scoped
	tas_wave::saveClip(hostfs::savestateFolderOverride);	// TAS waveform: <clip>/audio.env (no-op without a clip or without measured frames)
	tas_wave::reset();	// TAS waveform store is session-scoped
	tas_ruler::saveClip(hostfs::savestateFolderOverride);	// REL ruler: <clip>/skip.map
	tas_ruler::reset();
	if (onenter_ff)
	{	// an aborted seed (quit mid-boot) must not leak its arm - the next session's first ordinary
		// step-stop would silently kill a user-enabled fast-forward (audit)
		onenter_ff = false;
		settings.input.fastForwardMode = false;
	}
	// Auto-fire arms + a live send are session-scoped: a stale arm from the last clip would inject
	// the moment a writable mode runs - and in READ-WRITE, STOMP cells (including an OnEnter seed).
	tas_auto::clearAll();
	tas_auto::stopLive();

	settings.dojo.Training = false;

	settings.dojo.PlayerName = "";
	settings.dojo.OpponentName = "";

	dojo_file.Reset();
	training.Reset();
	ResetInputDisplay();
}
