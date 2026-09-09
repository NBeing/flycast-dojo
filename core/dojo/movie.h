/*
	The movie's frontier, asked in one vocabulary.

	`Dojo::MovieEnd()` already owns the METRIC - "one past the last authored
	key", rather than a frame count - and that closed one seam. It left another
	open, and this header is for that one: **who owns the +-1**.

	`[MEASURED 2026-09-08]` after MovieEnd() landed, the two live end-of-movie
	detectors still disagreed with each other:

	    core/dojo/dojo.cpp   frame_number == MovieEnd() - 1   emulation thread,
	                                                          BEFORE the frame is applied
	    core/rend/gui.cpp    frame_number == MovieEnd()       render thread,
	                                                          AFTER it

	Both were "already converted". Neither was obviously wrong, because each was
	correct for its own phase - and that is exactly why it is a seam rather than
	a bug: the relationship between the two had no owner, so each site spelled
	the frontier for itself and the spellings drifted. There were two more
	spellings besides.

	THE FIX IS THAT atEnd() TAKES THE FRAME. A caller says which frame it means
	and the predicate answers; the phase difference becomes visible at the call
	site (`atEnd(n + 1)` before applying, `atEnd(n)` after) instead of hidden in
	two different constants. The +-1 is now stated once per caller, in the
	caller's own terms, rather than encoded in the comparison.

	These are thin on purpose. The point is a shared vocabulary for four
	questions that were being answered ad hoc, not a new abstraction over the
	movie.
*/
#pragma once
#include "dojo.h"

namespace movie
{

//! One past the last authored frame. 0 for an empty movie.
inline u32 end() { return dojo.MovieEnd(); }

//! Does the movie have any frames at all? `atEnd()` is true for EVERY frame of
//! an empty movie (end() is 0), so a caller that means "playable and finished"
//! wants both this and atEnd().
inline bool authored() { return !dojo.session_inputs.empty(); }

//! Is this specific frame authored? NOT the same question as atEnd(): an
//! unauthored frame in the MIDDLE of a movie is a hole, not an end, and
//! re-recording can leave one.
inline bool has(u32 frame)
{
	return dojo.session_inputs.find(frame) != dojo.session_inputs.end();
}

//! Is `frame` at or past the frontier? `>=` rather than `==` so a seek that
//! lands beyond the end is caught rather than stepped over.
inline bool atEnd(u32 frame) { return frame >= end(); }

}	// namespace movie
