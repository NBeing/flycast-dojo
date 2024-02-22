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
    void AppendToFile(std::string frame, int version);
    void AppendHeaderToFile(std::string rom_name);

    std::string GetRomNamePrefix();
    std::string GetRomNamePrefix(std::string state_file);

    std::string CreateReplayFile();
    std::string CreateReplayFile(std::string rom_name, int version = 1);

    void LoadReplayFile(std::string path);
    void LoadReplayFileV1(std::string path);

    u32 GetFrameNumber(u8 *data);
    void ProcessBody(unsigned int cmd, unsigned int body_size, const char *buffer, int *offset);

    bool replay_loaded = false;

    MessageWriter replay_msg;
    u32 replay_frame_count;
};

extern Replay replay;
