#pragma once
#include "types.h"
#include <map>
#include <string>
#include <vector>

// T5: the text movie codec - inputs as human-editable text, round-tripping BYTE-IDENTICALLY with
// the binary movie (session_inputs). Binary stays authoritative at runtime; text is the authoring
// and interchange layer. Grammar per ROADMAP Appendix B (bk2-derived):
//
//   ; comment                                (any line not starting with '|' or '@' is ignored)
//   LogKey:#P1 Up|P1 Down|...|#P2 Up|...|    (self-describing column map, written once)
//   |UD..A....... |  128,  128,    0,    0,|... P2 ...|      one line = one frame = both players
//   |.............|  128,  128,    0,    0,|...| *37         repeat: this frame 37 times
//   @raw 1234 <48 hex>                       exact-bytes escape for packets the named columns
//                                            cannot express (never seen in controller movies;
//                                            exists so round-trip exactness holds for ANY movie)
//
// Rules: '.' is the only "unpressed" char; bools before axes; axes are comma-terminated ints;
// strict group validation (unlike bk2, malformed lines are ERRORS, not silent misassignment);
// frame numbers and players are positional, never serialized (except @raw's cross-check).
namespace tas_text
{
	// session_inputs -> text file. Fails (with err set) rather than writing something lossy.
	bool ExportText(const std::map<u32, std::vector<u8>>& inputs, const std::string& path,
			const std::string& gameName, std::string& err);

	// text file -> a fresh map. Strict: any malformed line is an error naming the line number.
	bool ImportText(const std::string& path, std::map<u32, std::vector<u8>>& out, std::string& err);

	// Export + re-import + byte-compare against the live movie; logs a TAS TEXT verdict line.
	// Called from Replay::Init when dojo:TextRoundTrip=yes. The written file stays for inspection.
	void RoundTripSelfTest(const std::map<u32, std::vector<u8>>& inputs, const std::string& clipDir,
			const std::string& gameName);
}
