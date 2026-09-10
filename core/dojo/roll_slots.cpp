#include "roll_host.h"
#include "dojo.h"
#include "oslib/oslib.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include <string>

/*
	THE PRODUCTION HOST - the piano roll's four questions, answered by flycast.

	`[MEASURED 2026-09-09]` this file exists because roll::setHost() was called
	exactly TWICE in the tree, both inside roll_profile.cpp's own self-test,
	against a Probe that answers slotFrame() = 7. There was no real host
	anywhere, so the roll's savestate gutter had never displayed a real slot: the
	panel is written against a possibly-null host and prints "No host installed".

	A SELF-TESTED SEAM WITH NO CUSTOMER, for the third time in one session. The
	panel registry had three tested loops and zero call sites; the edit funnel
	refused a map every unit test accepted. This was the same shape and it
	shipped.

	NOTHING HERE IS NEW LOGIC. Every fact already existed in this tree -
	hostfs::scanSavestateInfo for the slots, Dojo::IsStateStale for the verdict,
	dojo.rewind_log for the timeline, dojo.savestate_epoch for invalidation. What
	was missing was the four accessors and, more importantly, the CACHING that
	makes them affordable to call from a draw.

	WHY THE CACHE IS NOT AN OPTIMISATION.

	  scanSavestateInfo() walks a directory and opens up to three sidecars per
	  occupied slot - a few hundred syscalls. The roll asks framesToSlots() once
	  per draw and slotStale() once per drawn row.

	  IsStateStale re-hashes the movie prefix BELOW the state on every call
	  (MoviePrefixHash walks session_inputs), and the fork measured its States
	  window asking ~200 times a frame. Its comment says so, and says memoising
	  was the fix.

	So both are computed on a CHANGE, never on a draw. The invalidation is
	explicit and there are four triggers, because each can move without the
	others: the savestate epoch (a state written or a generation restored), the
	rewind log growing (a re-record), the re-record counter, and a half-second
	tick that catches anything nobody thought to bump - notably dc_savestate,
	whose comment claims it bumps the epoch and which does not.
*/
namespace roll
{

namespace {

class SlotHost final : public Host
{
public:
	bool slotStale(int slot) const override
	{
		ensureFresh();
		return slot >= 0 && slot < (int)stale_.size() && stale_[slot] != 0;
	}

	u32 slotFrame(int slot) const override
	{
		ensureFresh();
		return slotFrameRaw(slot);
	}

	void framesToSlots(std::map<u32, std::vector<int>>& out) const override
	{
		ensureFresh();
		build(out);
	}

	int staleNoticePhase() const override
	{
		ensureFresh();
		if (noticeAt_ <= 0.0)
			return -1;
		const double age = os_GetSeconds() - noticeAt_;
		if (age < 0.0 || age > NOTICE_SECONDS)
			return -1;
		return ((int)(age * 4.0)) & 1;		// 4 Hz, phase 0/1
	}

	bool staleNoticeWasDeletion() const override
	{
		ensureFresh();
		return noticeDeletion_;
	}

private:
	// The accessor's body without ensureFresh, so the trace can call it from
	// inside a refresh without recursing.
	u32 slotFrameRaw(int slot) const
	{
		if (slot < 0 || slot >= (int)info_.size() || !info_[slot].exists)
			return 0;
		return info_[slot].movieFrame;
	}

	static constexpr double NOTICE_SECONDS = 5.0;
	static constexpr double TICK_SECONDS   = 0.5;

	void ensureFresh() const
	{
		const double now = os_GetSeconds();
		const u32 epoch = dojo.savestate_epoch.load();
		// The half-second tick is the SAFETY NET, not the mechanism. It exists
		// because dojo.savestate_epoch's own comment says "bumped on every
		// savestate write" and dc_savestate does not bump it
		// (docs/STATES-LIFT.md G2). A tick that catches an un-bumped write is
		// cheaper than a wrong gutter.
		if (epoch != epoch_ || now - scannedAt_ >= TICK_SECONDS)
		{
			epoch_ = epoch;
			scannedAt_ = now;
			rescan(now);
			return;
		}
		// A re-record can strand a state without touching the folder, so the
		// verdicts move even when the scan does not.
		if (dojo.rewind_log.size() != rewinds_ || dojo.rerecord_count != rerecords_)
			revalidate(now);
	}

	void rescan(double now) const
	{
		std::vector<hostfs::SavestateInfo> next;
		if (!settings.content.path.empty())
			next = hostfs::scanSavestateInfo();

		// A slot that HELD a state and now does not is a deletion, and the
		// notice says so differently from a stranding - the roll draws them the
		// same shape with different words, so the two must be told apart here.
		bool deleted = false;
		for (size_t i = 0; i < info_.size() && i < next.size(); i++)
			if (info_[i].exists && !next[i].exists)
				deleted = true;
		if (next.size() < info_.size())
			for (size_t i = next.size(); i < info_.size(); i++)
				if (info_[i].exists)
					deleted = true;

		info_.swap(next);
		revalidate(now, deleted);
		trace();
	}

	void revalidate(double now, bool deleted = false) const
	{
		rewinds_   = dojo.rewind_log.size();
		rerecords_ = dojo.rerecord_count;

		std::vector<char> next(info_.size(), 0);
		for (size_t i = 0; i < info_.size(); i++)
		{
			const hostfs::SavestateInfo& s = info_[i];
			// haveSeq gates the question, not just the answer. A sidecar
			// predating re-record sequencing cannot be judged, and "unknown" is
			// deliberately not folded into "clean" anywhere else in this tree -
			// a state that MIGHT be dead is not a state that is fine. The roll's
			// interface is a bool, so unknown draws as clean HERE and the
			// distinction is lost; that is a limit of Host::slotStale, recorded
			// rather than papered over.
			if (s.exists && s.haveSeq && s.movieFrame != 0)
				next[i] = dojo.IsStateStale(s.movieFrame, s.rerecordSeq, s.prefixHash) ? 1 : 0;
		}

		// THE EVENT IS A CLEAN -> STALE FLIP, not "something is stale". A window
		// that pulsed while any stale state existed would pulse forever after
		// one re-record; what the user needs to see is the moment it happened.
		bool flipped = false;
		for (size_t i = 0; i < next.size() && i < stale_.size(); i++)
			if (next[i] != 0 && stale_[i] == 0)
				flipped = true;

		stale_.swap(next);
		if (flipped || deleted)
		{
			noticeAt_ = now;
			noticeDeletion_ = deleted;
		}
	}

	// The map the roll draws from. Separate from framesToSlots() only so the
	// trace can report what the HOST ANSWERS rather than what the scan holds -
	// which is not a distinction worth a comment until it costs you.
	//
	// `[MEASURED 2026-09-09]` it cost me one. The trace built its own count
	// straight off info_, so sabotaging BOTH slotFrame() and framesToSlots()
	// changed nothing it printed and the harness reported PASS over two broken
	// accessors. An instrument that reads around the thing it measures cannot
	// fail with it - the same shape as the drag trace earlier the same day,
	// which read the very function that was corrupting the position.
	void build(std::map<u32, std::vector<int>>& out) const
	{
		out.clear();
		for (int i = 0; i < (int)info_.size(); i++)
		{
			// frame 0 means "no anchor" here, NOT "anchored at frame 0". The
			// conflation is inherited - savestateAnchor and the fork's
			// gui_state_frames both make it, and SavestateInfo carries a haveSeq
			// flag for the neighbouring field but no haveFrame for this one
			// (docs/STATES-LIFT.md §4.3). A power-on BASE state is exactly the
			// case it gets wrong, so it is written down rather than hidden.
			if (info_[i].exists && info_[i].movieFrame != 0)
				out[info_[i].movieFrame].push_back(i);
		}
	}

	void trace() const
	{
		if (!cfgLoadBool("dojo", "RollSlotTrace", false))
			return;
		// Logged on CHANGE. An empty gutter is what "no states exist" and "the
		// host was never installed" both look like, and only this tells them
		// apart - which is the exact confusion that let a null host ship.
		int occupied = 0, stale = 0;
		for (size_t i = 0; i < info_.size(); i++)
		{
			if (info_[i].exists) occupied++;
			if (i < stale_.size() && stale_[i]) stale++;
		}
		// THROUGH THE ACCESSORS, so a broken one is visible here. anchored comes
		// from the map the panel draws, and the frame from slotFrame().
		std::map<u32, std::vector<int>> byFrame;
		build(byFrame);
		int anchored = 0;
		std::string first = "-";
		for (const auto& kv : byFrame)
		{
			anchored += (int)kv.second.size();
			if (first == "-" && !kv.second.empty())
				first = std::to_string(kv.second.front()) + "@"
						+ std::to_string(slotFrameRaw(kv.second.front()));
		}
		char sig[96];
		snprintf(sig, sizeof(sig), "%d/%d/%d/%s", occupied, anchored, stale, first.c_str());
		if (lastSig_ == sig)
			return;
		lastSig_ = sig;
		NOTICE_LOG(RENDERER, "ROLL SLOTS: scanned=%d occupied=%d anchored=%d stale=%d first=%s",
				(int)info_.size(), occupied, anchored, stale, first.c_str());
	}

	mutable std::vector<hostfs::SavestateInfo> info_;
	mutable std::vector<char> stale_;
	mutable double scannedAt_ = -1000.0;
	mutable double noticeAt_ = 0.0;
	mutable bool   noticeDeletion_ = false;
	mutable u32    epoch_ = ~0u;
	mutable size_t rewinds_ = (size_t)-1;
	mutable u32    rerecords_ = ~0u;
	mutable std::string lastSig_;
};

SlotHost theSlotHost;

}	// namespace

void installHost()
{
	// A LOOKUP THAT CAN FAIL rather than trusting the call, the same rule the
	// panel registration follows: an uninstalled host is invisible in exactly
	// the same way as one that is installed and finds no states.
	setHost(&theSlotHost);
	NOTICE_LOG(RENDERER, "ROLL HOST: installed=%s", host() != nullptr ? "yes" : "NO");
}

}	// namespace roll
