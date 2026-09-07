#include "tas_auto.h"

namespace tas_auto
{
	static int g_pickerHz = 60;					// rate new arms use (60 = hold)
	static int g_hz[2][CANON_BITS] = {};		// [player][canonBit] fire rate; 0 = off

	void setAutoHz(int hz)
	{
		if (hz < 1)  hz = 1;
		if (hz > 60) hz = 60;
		g_pickerHz = hz;
	}
	int autoHz() { return g_pickerHz; }

	bool tickOn(int hz, u64 phase)
	{
		if (hz <= 0)
			return false;
		if (hz >= 60)
			return true;					// hold: pressed every frame
		int period = 60 / hz;				// frames per press (30Hz -> 2 = 1 on, 1 off)
		if (period < 1)
			period = 1;
		return (phase % (u64)period) == 0;	// one frame on, the rest off
	}

	void arm(int player, int b, int hz)
	{
		if (player >= 0 && player < 2 && b >= 0 && b < CANON_BITS)
		{
			if (hz < 0)  hz = 0;
			if (hz > 60) hz = 60;
			g_hz[player][b] = hz;
		}
	}
	int hzOf(int player, int b)
	{
		return (player >= 0 && player < 2 && b >= 0 && b < CANON_BITS) ? g_hz[player][b] : 0;
	}
	void clearPlayer(int player)
	{
		if (player >= 0 && player < 2)
			for (int b = 0; b < CANON_BITS; b++)
				g_hz[player][b] = 0;
	}
	void clearAll()
	{
		clearPlayer(0);
		clearPlayer(1);
	}
	bool anyArmed(int player)
	{
		if (player < 0 || player > 1)
			return false;
		for (int b = 0; b < CANON_BITS; b++)
			if (g_hz[player][b] > 0)
				return true;
		return false;
	}
	bool anyArmed() { return anyArmed(0) || anyArmed(1); }

	u16 overlayCanon(int player, u64 frame)
	{
		if (player < 0 || player > 1)
			return 0;
		u16 c = 0;
		for (int b = 0; b < CANON_BITS; b++)
			if (g_hz[player][b] > 0 && tickOn(g_hz[player][b], frame))
				c |= (u16)(1u << b);
		return c;
	}

	// ---- live sequence ----
	static std::vector<u16> g_seqP1, g_seqP2;
	static u64  g_seqStart = 0;
	static bool g_seqActive = false;

	void playLive(const std::vector<u16>& p1, const std::vector<u16>& p2, u64 startFrame)
	{
		g_seqP1 = p1;
		g_seqP2 = p2;
		g_seqStart = startFrame;
		g_seqActive = !p1.empty();
	}
	void stopLive()
	{
		g_seqActive = false;
		g_seqP1.clear();
		g_seqP2.clear();
	}
	bool liveActive() { return g_seqActive; }

	u16 liveCanon(int player, u64 frame)
	{
		if (!g_seqActive || player < 0 || player > 1 || frame < g_seqStart)
			return 0;
		const u64 idx = frame - g_seqStart;
		const std::vector<u16>& v = player == 0 ? g_seqP1 : g_seqP2;
		return idx < v.size() ? v[idx] : (u16)0;
	}
	void liveTick(u64 frame)
	{
		// clear once the LAST frame has been applied, so SENDING ends even if the guest stops
		// advancing exactly on the sequence's final frame (the button was sticking on "Stop").
		if (g_seqActive && !g_seqP1.empty() && frame + 1 >= g_seqStart + g_seqP1.size())
			g_seqActive = false;
	}
	u64 liveRemaining(u64 frame)
	{
		if (!g_seqActive)
			return 0;
		const u64 endF = g_seqStart + g_seqP1.size();
		return frame < endF ? endF - frame : 0;
	}

	void expand(int hz, u16 canon, int frames, std::vector<u16>& out)
	{
		out.clear();
		for (int f = 0; f < frames; f++)
			out.push_back(tickOn(hz, (u64)f) ? canon : (u16)0);
	}
}
