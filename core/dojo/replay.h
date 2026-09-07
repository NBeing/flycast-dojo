#pragma once

#include "dojo.h"

class Replay
{
public:
    u32 version;
    u32 analog = 2;
    std::string filename = "";
    void Init();
    void StartRecording();
    void AppendToReplay(std::string frame, int version);
    // T6: persist edited frames by APPENDING their records - the parser is last-write-wins per
    // frame, so the file stays valid, history-preserving, and never rewritten in place.
    void AppendEditedFrames(const std::vector<std::pair<u32, std::vector<u8>>>& frames);
    void FlushReplay();		// write the partial (< FRAME_BATCH) tail batch; call when recording ends
	// "an append target is attached": true for record sessions (CreateReplayFile) and for
	// loaded replays (Init). False in Just Play - nothing to ossify into.
	bool HasAppendTarget() const { return !filename.empty(); }
	// PR3 delete: last-write-wins appends cannot express a SHORTER movie (old tail frames
	// would resurrect on reload) - a length-reducing edit rewrites the whole file.
	bool RewriteReplayFile();
	void DetachFile() { filename.clear(); file_header.clear(); }
	// The raw SPECTATE_START message of the attached file, captured at load/create time.
	// A rewrite reproduces it VERBATIM: GenHeader would re-derive analog/trigger/GGPO
	// flags from CURRENT config and silently change how the frames decode.
	std::vector<u8> file_header;

    std::vector<u8> GenHeader(std::string rom_name);
    void AppendHeaderToReplay(std::string rom_name);

    std::string GetRomNamePrefix();
    std::string GetRomNamePrefix(std::string state_file);

    std::string CreateReplayFile();
    std::string CreateReplayFile(std::string rom_name, int version = 1);

    bool LoadReplayFile(std::string path);
    void LoadReplayFileV1(std::string path);

    u32 GetFrameNumber(u8 *data);

    bool replay_loaded = false;

    MessageWriter replay_msg;
    u32 replay_frame_count;

    bool ggpo_session = false;

    std::string DownloadReplayJson(std::string game_name);
    std::string remote_replay_json = "";
};

// NOTE: there is deliberately NO `extern Replay replay;` here. The instance is a
// MEMBER of DojoSession (dojo.h:95, `Replay replay;`), which is why dojo.cpp can
// say `replay.Foo()` unqualified. A file-scope extern used to sit here with no
// definition anywhere: it compiled fine and failed only at LINK time, for any TU
// outside dojo.cpp that included this header and trusted it. Reach it as
// `dojo.replay`.
