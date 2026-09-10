/*
	WHAT KIND OF SESSION IS THIS - one answer, derived, never stored.

	Today it is an ad-hoc conjunction at every site that asks. The most complete
	version in the tree is a LOCAL VARIABLE inside MapleApplyAction
	(`core/dojo/dojo.cpp`, `tasWriteGrow`) carrying eight lines of comment about
	why a replay the user flipped to READ-WRITE is a writing session - and
	nothing outside that function body can see it. `[MEASURED 2026-09-08]`
	`play_match` is read 74 times; the seven cfg keys that answer the rest of
	the question, 81 times between them.

	TWO INDEPENDENT AXES, and conflating them is the mistake this prevents.

	  KIND - what the session is FOR
	  MODE - who drives the guest RIGHT NOW

	They are not a product. A Replay flipped with R becomes MODE=Write while
	KIND stays Replay, which is exactly the case `tasWriteGrow` exists to handle
	and the case a single enum would have to spell as a seventh kind.
	`dojo.h`'s READ / READ-WRITE / WRITE comment block already states the MODE
	rule in prose; this gives it a name.

	BUILT ON determinism::runKind(), NOT BESIDE IT. That function already
	answers a coarser KIND with one owner, and a second implementation of one
	rule does not disagree when you write it - it disagrees when one of them is
	later changed. runKind() is reimplemented over kind() in the same commit
	that adds this, so there is still one owner and it is here.

	DERIVED PER CALL, like rend::gameViewport(). The inputs are a few cfg reads
	and two bools; caching would need invalidation on every R press, every boot
	handoff and every Settings::load - three more things to get wrong than the
	reads cost.
*/
#pragma once

namespace session
{

enum class Kind
{
	JustPlay,		//!< no movie: the plain single-player boot
	RecordMovie,	//!< dojo:RecordMatches - a .flyr being written from power-on
	Replay,			//!< dojo.play_match - a .flyr driving the guest
	RecordMacro,	//!< dojo:MacroMode without PlayMacro
	PlayMacro,		//!< dojo:MacroMode with PlayMacro
	Training,		//!< dojo:Training - the practice mode, with its own overlays
	Netplay,		//!< GGPO or settings.network.online, INCLUDING spectate
};

//! WHO DRIVES THE GUEST, which is not the same question as Kind.
enum class Mode
{
	Read,		//!< the movie drives; the live pad is ignored
	ReadWrite,	//!< the movie drives, but a live signal stomps the active cell
	Write,		//!< advancing overwrites every frame it passes
};

/*
	PRECEDENCE IS DECLARED, NOT EMERGENT:

	    Netplay > macro pair > Replay > Training > RecordMovie > JustPlay

	Writing it down is the point. The fork being ported computes the same order
	as an accident of how a `?:` chain was typed, and nothing there says so - so
	nobody can tell a deliberate ordering from the one that fell out.

	Netplay first because an online session is online whatever else is set, and
	every TAS answer below it would be wrong about who is driving.

	TRAINING SITS BELOW REPLAY, and that is read out of the code rather than
	chosen: core/dojo/dojo_gui.cpp already spells the pair as
	`if (dojo.play_match) ... else if (Training)` at two sites. `[MEASURED
	2026-09-09]` docs/SESSION-KINDS.md Q-d - training was the largest kind with
	no representation here at all, 21 raw sites and nothing to adopt.
*/
Kind kind();
Mode mode();

//! The predicates a panel's `enabled()` will ask for. Each is one line over
//! kind()/mode(), and each exists so a panel does NOT re-derive it - a panel
//! spelling its own conjunction is the 25 raw call sites coming back one panel
//! at a time.
bool recording();	//!< RecordMovie or RecordMacro
bool replaying();	//!< Replay or PlayMacro
bool macro();		//!< RecordMacro or PlayMacro
/*
	IS THIS A NETPLAY SESSION - one with a peer that would desync if this side
	rewrote the tape?

	`[CORRECTED 2026-09-10]` there was briefly a second predicate here,
	livePeer(), added because netplay() answered TRUE for a purely LOCAL replay
	of a clip recorded from a GGPO match. That was a defect in kind(), not a
	missing question: replay.cpp sets config::GGPOEnable when it loads such a
	clip, and kind() read the Option. Fixing kind() made the two identical, so
	the second predicate is gone rather than kept as a synonym.

	The lesson is the one docs/SESSION-KINDS.md is about: a new predicate looked
	like the answer because the existing one gave a wrong result, and adding it
	would have left two owners of one question disagreeing in a third case
	neither had been tested on.
*/
bool netplay();

//! kind() == Kind::Training. THE KIND QUESTION, and NOT what most of the tree
//! is asking - see trainingEnabled() directly below, and pick deliberately.
bool training();

/*
	IS THE TRAINING FEATURE TURNED ON? Deliberately NOT kind() == Kind::Training.

	A kind is EXCLUSIVE - a session is one thing - while `dojo:Training` is a
	TOGGLE that can be set alongside a replay or a recording. `[MEASURED
	2026-09-09]` all 21 raw sites in docs/SESSION-KINDS.md Q-d ask the toggle:
	they read it as `Training && ShowTrainingInputDisplay`, or beside play_match
	as an alternative arm, never as "what kind of session is this".

	So both exist and neither is a synonym for the other. Migrating those sites
	to the KIND would have silently changed behaviour anywhere the toggle is set
	under a higher kind - which is the shape of mistake this whole census is
	about, and the reason the two are named apart rather than one being quietly
	preferred.

	This is also the one owner of the key. `config::Training` is registered and
	read by nobody, and `settings.dojo.Training` is written once and read never
	(SESSION-KINDS §4 #11): three representations of one fact, and this is the
	one that carries the traffic.
*/
bool trainingEnabled();

bool readOnly();	//!< mode() == Read

/*
	IS THE MOVIE GROWABLE PAST ITS LAST AUTHORED FRAME?

	`tasWriteGrow`, promoted out of a function body. Deliberately NOT spelled
	`!readOnly()`: it also excludes online and GGPO sessions, and the reason
	belongs with the predicate rather than at one of its call sites - running
	off the end must GROW the roll instead of pinning frame_number, and pinning
	it killed pause, step and Space on a sibling path.
*/
bool writeGrow();

//! For the status pill, logs and the HUD. Never nullptr.
const char *label();

//! Exercise the precedence table against synthetic cfg states. Gated on
//! `dojo:PanelSelfTest`, alongside the panel registry's.
void selfTest();

}	// namespace session
