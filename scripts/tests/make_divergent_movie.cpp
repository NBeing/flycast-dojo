// make_divergent_movie - append ONE valid override frame-record to a .flyr, so a
// branch's movie differs from main's for scripts/branchtest.sh.
//
//   RUN:   compiled + run by scripts/branchtest.sh; no emulator, no ROM.
//   USAGE: make_divergent_movie <path.flyr> <frame>
//   DOES:  appends a self-contained batch in the EXACT shape Replay::AppendEditedFrames
//          writes (core/dojo/replay.cpp) - AppendHeader(0, MAPLE_BUFFER) +
//          AppendInt(MAPLE_FRAME_SIZE) + one MAPLE_FRAME_SIZE record {u32 frame,
//          input bytes}. The .flyr parser is last-write-wins per frame, so this
//          OVERRIDES that frame's inputs on the next load while staying a valid
//          movie. The record carries a distinctive non-neutral input so the
//          overridden frame genuinely differs from the original.
//
// WHY the REAL MessageWriter rather than hand-emitted bytes: the batch framing
// (a 12-byte header whose first int is a back-patched payload length) is exactly
// the sort of format that drifts silently when hand-copied. Including the
// shipping header means this helper cannot disagree with the emulator's own
// writer - the same reason scripts/tests/engine_diff.cpp transcribes as little
// as it can and asserts source-parity for the rest.
#include "../../core/dojo/message_writer.h"     // MessageWriter, MAPLE_BUFFER, HEADER_LEN
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>

#define MAPLE_FRAME_SIZE 28                       // core/dojo/dojo.h

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: %s <path.flyr> <frame>\n", argv[0]);
		return 2;
	}
	const std::string path = argv[1];
	const unsigned int frame = (unsigned int)strtoul(argv[2], nullptr, 10);

	MessageWriter mw;
	mw.AppendHeader(0, MAPLE_BUFFER);
	mw.AppendInt(MAPLE_FRAME_SIZE);
	unsigned char rec[MAPLE_FRAME_SIZE] = { 0 };
	memcpy(rec, &frame, sizeof(unsigned int));            // record[0..3] = frame number, LE
	// A distinctive, VALID controller state (buttons are a bitmask; any value is a
	// legal pad state). Non-neutral so the frame differs from the original.
	rec[4] = 0xF0;
	rec[5] = 0x0F;
	mw.AppendContinuousData((const char *)rec, MAPLE_FRAME_SIZE);
	const std::vector<unsigned char> msg = mw.Msg();

	std::ofstream fout(path, std::ios::out | std::ios::binary | std::ios_base::app);
	if (!fout)
	{
		fprintf(stderr, "make_divergent_movie: cannot open %s\n", path.c_str());
		return 1;
	}
	fout.write((const char *)msg.data(), msg.size());
	fout.close();
	printf("make_divergent_movie: appended override for frame %u to %s (%zu bytes)\n",
			frame, path.c_str(), msg.size());
	return 0;
}
