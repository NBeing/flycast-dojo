#include "roll_host.h"
#include "dojo.h"
#include "oslib/oslib.h"
#include "roll_edit.h"
#include "roll_meta.h"
#include "roll_marks.h"
#include "movie.h"
#include "cfg/cfg.h"
#include "log/LogManager.h"
#include "stdclass.h"
#include <string>
#include <fstream>
#include <vector>

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

	/*
		FOLLOW A RENUMBER, by rewriting the `.frame` sidecars.

		SAFETY, because this writes the user's files and a savestate is their
		work. Six constraints, each for a reason:

		  ONLY WITH A CLIP FOLDER OPEN. `hostfs::savestateFolderOverride` empty
		  means the states live in the shared data path - where
		  `docs/STATES-LIFT.md` G13 records that the READ derivation and the
		  WRITE derivation can name different directories. Rewriting a file the
		  scan did not come from is exactly the mistake worth refusing.

		  ONLY THE SIDECAR, never the .state. The machine is untouched.

		  ONLY SLOTS THAT ALREADY HAVE ONE. Read-modify-write; a state with no
		  anchor does not acquire one here.

		  THE FILE'S LENGTH AND EVERY OTHER FIELD SURVIVE. The sidecar is
		  versioned by extension - v1 is 4 bytes, v2 is 12, v3 is 20 - so this
		  patches the first u32 in place rather than rewriting the record. A v1
		  sidecar must not silently become a v3 with invented fields.

		  ATOMIC. Temp plus rename, the idiom tas_clip.cpp already uses for
		  clip.json after a torn read cost it a whole record set.

		  AND AN OFF SWITCH. `dojo:RemapAnchors=no`.

		THE STALENESS VERDICT IS NOT TOUCHED and must not be. Moving the anchor
		puts the marker on the right ROW; whether the state still belongs to the
		timeline is a separate question the rewind log already answers, and a
		resize logs an event at or below every row this moves.
	*/
	void rowsRemapped(const Remap& m) override
	{
		if (m.isIdentity())
			return;
		if (!cfgLoadBool("dojo", "RemapAnchors", true))
			return;
		if (hostfs::savestateFolderOverride.empty())
		{
			// Said, not skipped silently: with no clip folder the states are in
			// the shared data path and their anchors are now wrong.
			NOTICE_LOG(RENDERER, "ROLL ANCHORS: no clip folder - %d sidecar(s) left unmoved",
					(int)info_.size());
			return;
		}
		ensureFresh();
		int moved = 0, gone = 0;
		for (int i = 0; i < (int)info_.size(); i++)
		{
			if (!info_[i].exists || info_[i].movieFrame == 0)
				continue;
			const u32 was = info_[i].movieFrame;
			u32 to = 0;
			const bool survived = m.at(was, to);
			if (!survived)
			{
				to = m.collapsed(was);
				gone++;
			}
			if (to == was)
				continue;
			if (writeAnchor(i, to))
				moved++;
		}
		if (moved != 0)
		{
			// Our own cache and everyone else's: the epoch is what the States
			// window and the roll both watch.
			dojo.savestate_epoch++;
			scannedAt_ = -1000.0;
		}
		NOTICE_LOG(RENDERER, "ROLL ANCHORS: remapped %d sidecar(s), %d had lost their row",
				moved, gone);
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

public:
	//! The anchor as it is ON DISK. Deliberately not the cache: the probe that
	//! proves this works has to read the file, or it proves the cache.
	static bool readAnchor(int slot, u32& out)
	{
		std::ifstream f(hostfs::getSavestatePath(slot, false) + ".frame", std::ios::binary);
		if (!f.good())
			return false;
		u32 v = 0;
		f.read((char *)&v, sizeof(v));
		if (f.gcount() != (std::streamsize)sizeof(v))
			return false;
		out = v;
		return true;
	}

	static bool writeAnchor(int slot, u32 frame)
	{
		const std::string path = hostfs::getSavestatePath(slot, true) + ".frame";
		std::vector<char> buf;
		{
			std::ifstream in(path, std::ios::binary);
			if (!in.good())
				return false;			// no sidecar: nothing to move, none created
			buf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
		if (buf.size() < sizeof(u32))
			return false;				// truncated beyond even v1 - leave it alone
		memcpy(buf.data(), &frame, sizeof(frame));	// EVERY other byte survives

		const std::string tmp = path + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.good())
				return false;
			out.write(buf.data(), (std::streamsize)buf.size());
			if (!out.good())
				return false;
		}
		std::error_code ec;
		ghc::filesystem::rename(tmp, path, ec);
		if (ec)
		{
			ghc::filesystem::remove(tmp, ec);
			WARN_LOG(RENDERER, "ROLL ANCHORS: could not replace %s", path.c_str());
			return false;
		}
		return true;
	}

	/*
		THE ANCHORS AS A BLOB, so undo can put them back.

		dojo's EditPatch already carries an opaque `gui_meta` string captured
		pre-edit and reapplied on undo - the rails written for bookmarks, whose
		comment says "the piano roll registers these so undo/redo restore
		bookmarks alongside the frames". `[MEASURED 2026-09-09]` neither
		std::function had ever been assigned anywhere in the tree. They are the
		right rails and they had no customer; this is the customer.

		Captured from the CACHE, which is correct rather than merely cheap: the
		cache is only stale with respect to writes this class makes, and it
		refreshes after each one.
	*/
	std::string captureAnchors() const
	{
		ensureFresh();
		std::string out;
		for (int i = 0; i < (int)info_.size(); i++)
			if (info_[i].exists && info_[i].movieFrame != 0)
				out += (out.empty() ? "" : ",") + std::to_string(i) + "="
						+ std::to_string(info_[i].movieFrame);
		return out;
	}

	void applyAnchors(const std::string& blob)
	{
		if (blob.empty() || hostfs::savestateFolderOverride.empty())
			return;
		int restored = 0;
		size_t pos = 0;
		while (pos < blob.size())
		{
			const size_t comma = blob.find(',', pos);
			const std::string one = blob.substr(pos, comma == std::string::npos
					? std::string::npos : comma - pos);
			pos = comma == std::string::npos ? blob.size() : comma + 1;
			const size_t eq = one.find('=');
			if (eq == std::string::npos)
				continue;
			const int slot = atoi(one.substr(0, eq).c_str());
			const u32 want = (u32)strtoul(one.substr(eq + 1).c_str(), nullptr, 10);
			u32 have = 0;
			// A WRITE ONLY WHERE IT DIFFERS. Undo fires on every edit, most of
			// which move nothing, and rewriting an unchanged file on each one
			// is churn against the user's states for no gain.
			if (readAnchor(slot, have) && have != want && writeAnchor(slot, want))
				restored++;
		}
		if (restored != 0)
		{
			dojo.savestate_epoch++;
			scannedAt_ = -1000.0;
			NOTICE_LOG(RENDERER, "ROLL ANCHORS: restored %d sidecar(s) with the undo", restored);
		}
	}

private:
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
	// THE UNDO RAILS, which is what makes rewriting the user's sidecars a
	// reversible act rather than a one-way one. dojo captures gui_meta pre-edit
	// inside both funnels and reapplies it on undo.
	//
	// THROUGH THE REGISTRY, not by assigning dojo's hooks directly. There is one
	// setter and there are now two customers - anchors and bookmarks - so a
	// direct assignment is a race in which whoever installs last wins and the
	// other silently stops being restored (roll_meta.h).
	// A ROW-INDEX HOLDER. The sidecars are row indices on disk, so they follow a
	// renumber like anything else that holds one.
	remapRegister([](const Remap& m) { theSlotHost.rowsRemapped(m); });

	metaRegister("anchors",
			[]() { return theSlotHost.captureAnchors(); },
			[](const std::string& blob) { theSlotHost.applyAnchors(blob); });
	metaInstall();
	// A LOOKUP THAT CAN FAIL rather than trusting the call, the same rule the
	// panel registration follows: an uninstalled host is invisible in exactly
	// the same way as one that is installed and finds no states.
	setHost(&theSlotHost);
	NOTICE_LOG(RENDERER, "ROLL HOST: installed=%s", host() != nullptr ? "yes" : "NO");
}

/*
	THE INTEGRATION PROBE for the anchor rewrite - `dojo:RollAnchorProbe=yes`.

	A self-test cannot reach this. The whole question is whether bytes on disk
	moved and then moved back, so the probe reads the FILE both times, never the
	cache - the cache is the thing that would agree with itself.

	Runs ONCE. Safe under scripts/rolltest.sh, which copies the clip and its
	savestates into a temp directory per run; off by default everywhere else,
	because it really does edit the movie it finds.

	NOTE ON THE UNDO IT ASSERTS. It checks that the ANCHOR came back, not that
	the movie shrank: undoing a resize is replayed through ApplyEdit, which
	permits extension and not truncation, so the inserted rows survive as a
	documented v1 limitation of absent-frame patches (dojo.cpp says so). The
	anchor is what this feature owns.
*/
void anchorProbe()
{
	static bool done = false;
	if (done || !cfgLoadBool("dojo", "RollAnchorProbe", false))
		return;
	if (dojo.session_inputs.empty() || hostfs::savestateFolderOverride.empty())
		return;					// nothing to move, or nowhere safe to move it

	std::map<u32, std::vector<int>> byFrame;
	theSlotHost.framesToSlots(byFrame);
	if (byFrame.empty())
		return;					// no anchored slot yet; try again next frame
	done = true;

	const int slot = byFrame.begin()->second.front();
	u32 before = 0;
	if (!SlotHost::readAnchor(slot, before))
	{
		NOTICE_LOG(RENDERER, "ROLL ANCHORPROBE: slot %d has no sidecar on disk => FAIL", slot);
		return;
	}

	// A BOOKMARK RIDES ALONG, because the interesting claim is no longer "the
	// anchor moved" but "TWO providers both survived one undo". A single
	// provider works with no registry at all, which is how the single-setter
	// version looked correct right up until bookmarks arrived (roll_meta.h).
	marks().set(before, "probe");

	const u32 N = 3;
	std::map<u32, Row> whole;
	for (const auto& kv : dojo.session_inputs)
		whole[kv.first] = kv.second;
	// BELOW the anchor on purpose: an insert above it would move nothing, and a
	// probe that cannot tell "it worked" from "there was nothing to do" is the
	// failure this tree keeps finding.
	Resize r = insertBlanks(whole, 0, N);
	const s64 first = dojo.ApplyEditResize(r.edit, "roll: anchor probe");
	remapAll(r.remap);		// the anchors, the marks and the selection, in one

	u32 after = 0;
	const bool readBack = SlotHost::readAnchor(slot, after);
	const bool moved = readBack && after == before + N;
	const bool markMoved = marks().has(before + N) && !marks().has(before);

	const bool undone = dojo.ApplyUndo();
	u32 back = 0;
	const bool restored = SlotHost::readAnchor(slot, back) && back == before;
	const bool markBack = marks().has(before) && !marks().has(before + N);
	marks().erase(before);
	marks().erase(before + N);

	NOTICE_LOG(RENDERER, "ROLL ANCHORPROBE: slot=%d first=%lld anchor %u -> %u (want %u)"
			" mark=%s undo=%s -> anchor %u mark=%s  => %s",
			slot, (long long)first, before, after, before + N,
			markMoved ? "moved" : "NO", undone ? "yes" : "NO", back,
			markBack ? "back" : "NO",
			(moved && markMoved && undone && restored && markBack) ? "PASS" : "FAIL");
}

}	// namespace roll
