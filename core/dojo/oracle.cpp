#include "oracle.h"
#include "dojo.h"
#include "serialize.h"
#include "emulator.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <xxhash.h>
#include <string>
#include <vector>

/*
	See oracle.h for what these are and the domain each hash states. machineHash() is
	sendequiv.cpp's hashNow, moved - not copied - so there is exactly one owner and
	sendequiv calls this. The body is verbatim: any change to what is hashed changes
	the meaning of every gate that compares it.
*/
namespace roll {
namespace oracle {

u32 machineHash()
{
	Serializer sizer;
	dc_serialize(sizer);
	std::vector<u8> buf(sizer.size());
	Serializer ser(buf.data(), buf.size());
	dc_serialize(ser);
	// Same-build A/B/N compare, so the exact trailer the lua hash adds is not
	// needed - only that this is a consistent function of the machine.
	return (u32)XXH32(buf.data(), ser.size(), 0);
}

u64 movieHash()
{
	return dojo.MoviePrefixHash(~0u);	// every row: MoviePrefixHash(f) hashes rows < f
}

bool machineStopped()
{
	return !emu.running();
}

void selfTest()
{
	if (!cfgLoadBool("dojo", "PanelSelfTest", false))
		return;
	int pass = 0, fail = 0;
	auto claim = [&](const char *what, bool ok) {
		(ok ? pass : fail)++;
		NOTICE_LOG(RENDERER, "ORACLE SELFTEST: %s  %s", ok ? "PASS" : "FAIL", what);
	};
	// The function under the machine hash, on bytes we control: it is a function
	// (same bytes, same number) and it is sensitive (one byte moves it). A hash
	// that failed either would make every "moved"/"unchanged" verdict noise.
	std::vector<u8> a(4096), b;
	for (size_t i = 0; i < a.size(); i++)
		a[i] = (u8)(i * 7 + 3);
	b = a;
	const u32 ha = (u32)XXH32(a.data(), a.size(), 0);
	claim("the same bytes hash to the same number", ha == (u32)XXH32(b.data(), b.size(), 0));
	b[1234] ^= 0x01;
	claim("one flipped bit changes the number", ha != (u32)XXH32(b.data(), b.size(), 0));
	claim("...and flipping it back restores it", (b[1234] ^= 0x01, ha == (u32)XXH32(b.data(), b.size(), 0)));
	// The movie hash inherits MoviePrefixHash's deliberate basis (dojo.cpp:637-650).
	// At self-test time no movie is loaded, so the whole-movie hash IS the basis;
	// a "corrected" FNV-1a reimplementation would fail this on day one.
	claim("an empty movie hashes to MoviePrefixHash's own basis",
			dojo.session_inputs.empty() ? movieHash() == 1469598103934665603ull : true);
	NOTICE_LOG(RENDERER, "ORACLE SELFTEST: %d passed, %d failed", pass, fail);
}

}	// namespace oracle
}	// namespace roll
