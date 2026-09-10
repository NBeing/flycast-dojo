#pragma once
#include <functional>
#include <string>

/*
	GUI-SIDE STATE THAT RIDES THE UNDO STACK.

	dojo's EditPatch carries one opaque `gui_meta` string, captured before an
	edit and reapplied when that edit is undone. It is exactly the right rail -
	its own comment says "the piano roll registers these so undo/redo restore
	bookmarks alongside the frames" - and it is ONE string with ONE setter.

	`[MEASURED 2026-09-10]` that was fine while savestate anchors were the only
	customer. Bookmarks are the second, and the second customer is where a single
	setter becomes a race to assign it: whichever module runs installHost() last
	wins and the other silently stops being restored. Nothing would report that.

	So the funnel keeps one string and this owns it, as a registry. Each provider
	names itself, and the blob is self-describing, so a payload from a build that
	had a provider this one does not is IGNORED rather than misparsed.

	NOT A GENERAL EVENT BUS. It answers one question - "what GUI state must
	survive an undo" - and anything that is not that belongs somewhere else.
*/
namespace roll
{

//! Register a provider. `key` must be a short stable name and outlive the call;
//! registering the same key twice REPLACES, so a reinstall is idempotent.
void metaRegister(const char *key, std::function<std::string()> capture,
		std::function<void(const std::string&)> apply);

//! Every provider's payload, in one blob. Empty when nothing has anything.
std::string metaCapture();

//! Hand each provider its own payload back. Unknown keys are skipped.
void metaApply(const std::string& blob);

//! Point dojo's edit_meta_capture / edit_meta_apply at this registry. Idempotent.
void metaInstall();

//! Runs under dojo:PanelSelfTest, like the other seams in this tree.
void metaSelfTest();

}	// namespace roll
