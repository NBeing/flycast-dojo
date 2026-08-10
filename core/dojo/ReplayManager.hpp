/*
	Copyright 2026 flycast-dojo contributors

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include "types.h"
#include <string>
#include <vector>

/*
 * Replay library management.
 *
 * Replays used to carry their metadata in the filename, as
 *
 *     <game>__<iso8601>__<host>__<guest>__.flyr
 *
 * and the browser recovered it by splitting on "__". That made renaming a
 * replay impossible: a name without three separators produced an empty token
 * list which the browser then indexed, and any organisation scheme a user
 * invented corrupted the listing.
 *
 * Metadata now lives in a JSON sidecar next to the replay ("<replay>.json"),
 * so the filename is free to be anything. Replays recorded before this - and
 * any whose sidecar is missing - are still read by parsing the old naming
 * convention, and fall back to the bare filename when even that does not fit.
 */
namespace replaymgr
{

struct ReplayInfo
{
	std::string path;			// absolute path to the .flyr/.flyreplay
	std::string displayName;	// user-facing; free-form
	std::string game;			// rom name prefix, used to launch it
	std::string date;			// ISO8601, or file mtime when unknown
	std::string hostPlayer;
	std::string guestPlayer;
	std::string notes;
	bool hasMetadata = false;	// false => derived from the filename

	std::string playersLabel() const;
};

// <data path>/replays, created if absent.
std::string replaysDir();

// Sidecar path for a replay ("foo.flyr" -> "foo.flyr.json").
std::string metadataPath(const std::string& replayPath);

// Every replay in the library, newest first.
std::vector<ReplayInfo> list();

// Reads one replay's metadata, deriving it if there is no sidecar.
ReplayInfo describe(const std::string& replayPath);

bool writeMetadata(const ReplayInfo& info);

// Changes the display name only; the file keeps its path, so in-flight
// references and any transmission already using it stay valid.
bool setDisplayName(const std::string& replayPath, const std::string& name);

bool setNotes(const std::string& replayPath, const std::string& notes);

// Deletes the replay and its sidecar.
bool remove(const std::string& replayPath);

// --- Transfer ---------------------------------------------------------------
// A bundle is a zip holding the replay plus its metadata, so a shared replay
// arrives with its name, players and notes intact.
bool exportBundle(const std::string& replayPath, const std::string& destZip);

// Returns the imported replay's path, or "" on failure.
std::string importBundle(const std::string& srcZip);

}
