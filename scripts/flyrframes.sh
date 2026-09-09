#!/bin/sh
# Print the number of DISTINCT frames in a .flyr movie, exactly.
#
# A .flyr is NOT a header followed by flat frame records. It is a stream of
# messages, each 12 bytes of header (u32 size, u32 seq, u32 cmd - all little
# endian) followed by `size` bytes of body. Movie frames arrive in MAPLE_BUFFER
# (cmd 6) messages, whose body is one u32 record-size followed by N records of
# that size. FRAME_BATCH is 120, so a full batch body is 4 + 120*28 = 3364.
#
# So every formula of the shape (filesize - K) / 28 is structurally wrong: it
# counts each batch's 16 bytes of framing (12 header + 4 record-size) as movie
# data. scripts/testrun.sh used (size - 93) / 28 and over-reported an 11520-frame
# clip as 11574 - by 0.57 frames per batch, which is 16/28.
#
# DISTINCT, not a record count: the parser is last-write-wins per frame
# (replay.cpp AppendFramesToReplay), so a re-recorded clip legitimately carries
# the same frame number in more than one batch. Counting records would inflate
# exactly the clips that have been edited most.
#
# The record size is READ FROM THE FILE rather than assumed, because the file
# declares it.
#
# Usage: flyrframes.sh <file.flyr>   ->  prints an integer, or exits nonzero.
set -eu

# ---------------------------------------------------------------------------
# --self-test: synthetic movies, no emulator, no fixture, milliseconds.
#
# Every claim gets an arm that must FAIL if the claim stops holding. The
# discriminating pair is duplicate-vs-new: a record COUNTER passes the
# "new frames" arm and fails the "duplicate" one, and the old size formula
# fails both, so together they pin the behaviour that matters.
# ---------------------------------------------------------------------------
if [ "${1:-}" = "--self-test" ]; then
	self="$0"
	tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
	pass=0; fail=0
	claim() {  # claim <what> <expected> <actual>
		if [ "$2" = "$3" ]; then pass=$((pass+1)); echo "FLYRFRAMES SELFTEST: PASS  $1"
		else fail=$((fail+1)); echo "FLYRFRAMES SELFTEST: FAIL  $1 (expected $2, got $3)"; fi
	}
	# mkflyr <out> <nbatches> <perbatch> <firstframe> <recsize>
	mkflyr() {
		LC_ALL=C awk -v nb="$2" -v per="$3" -v first="$4" -v rec="$5" '
			function put32(v) { printf "%c%c%c%c", v%256, int(v/256)%256, int(v/65536)%256, int(v/16777216)%256 }
			BEGIN {
				# SPECTATE_START (cmd 3) with a body of 67, matching a real clip:
				# the header message is variable length, which is the whole reason
				# a fixed-offset formula cannot work.
				put32(67); put32(0); put32(3)
				for (i = 0; i < 67; i++) printf "%c", 65
				body = 4 + per * rec
				for (m = 0; m < nb; m++) {
					put32(body); put32(m + 1); put32(6)
					put32(rec)
					for (r = 0; r < per; r++) {
						put32(first + m * per + r)
						for (i = 4; i < rec; i++) printf "%c", 0
					}
				}
			}' > "$1"
	}

	mkflyr "$tmp/a.flyr" 2 5 0 28
	claim "counts frames across batches" 10 "$(sh "$self" "$tmp/a.flyr")"

	# A DUPLICATE batch is a re-record override, not new movie. Appending one
	# must not move the count - this is the arm a record counter fails.
	cat "$tmp/a.flyr" > "$tmp/dup.flyr"
	dd if="$tmp/a.flyr" bs=1 skip=79 count=156 status=none >> "$tmp/dup.flyr"
	claim "a duplicate batch does not inflate the count" 10 "$(sh "$self" "$tmp/dup.flyr")"

	# ...and genuinely new frames MUST be counted, or the line above could be
	# satisfied by a parser that ignores appended batches entirely.
	mkflyr "$tmp/b.flyr" 1 5 900 28
	cat "$tmp/a.flyr" > "$tmp/new.flyr"
	dd if="$tmp/b.flyr" bs=1 skip=79 status=none >> "$tmp/new.flyr"
	claim "genuinely new frames ARE counted" 15 "$(sh "$self" "$tmp/new.flyr")"

	# The record size is declared in the file. A parser with 28 baked in reports
	# 5 here (it would read 4 + 5*32 = 164 as 5 records of 28 with slack).
	mkflyr "$tmp/w.flyr" 1 5 0 32
	claim "record size is read from the file, not assumed to be 28" 5 "$(sh "$self" "$tmp/w.flyr")"

	# Damaged input must be refused, not silently reported as a short clip.
	# `|| rc=$?` rather than a bare call: under `set -e` a command that fails
	# on purpose would abort the script before its status could be read.
	head -c 100 "$tmp/a.flyr" > "$tmp/t.flyr"
	rc=0; sh "$self" "$tmp/t.flyr" >/dev/null 2>&1 || rc=$?
	claim "a truncated movie exits nonzero" 3 "$rc"
	head -c 64 /dev/zero > "$tmp/z.flyr"
	rc=0; sh "$self" "$tmp/z.flyr" >/dev/null 2>&1 || rc=$?
	claim "a file that is not a movie exits nonzero" 3 "$rc"

	echo "FLYRFRAMES SELFTEST: $pass passed, $fail failed"
	[ "$fail" -eq 0 ] || exit 1
	exit 0
fi

[ $# -eq 1 ] || { echo "usage: flyrframes.sh <file.flyr>" >&2; exit 2; }
f="$1"
[ -f "$f" ] || { echo "flyrframes: no such file: $f" >&2; exit 2; }

od -A n -v -t u1 -- "$f" | awk '
	{ for (i = 1; i <= NF; i++) b[n++] = $i }
	function u32(p) { return b[p] + b[p+1]*256 + b[p+2]*65536 + b[p+3]*16777216 }
	END {
		MAPLE_BUFFER = 6; HEADER_LEN = 12
		pos = 0; msgs = 0
		while (pos + HEADER_LEN <= n) {
			size = u32(pos); cmd = u32(pos + 8); body = pos + HEADER_LEN
			# A body running past EOF means the file is truncated - a partially
			# flushed movie from a killed run. Report it rather than silently
			# returning the frames that happened to survive, because "short clip"
			# and "damaged clip" call for different actions.
			if (body + size > n) {
				printf("flyrframes: truncated at offset %d (message wants %d bytes, %d left)\n",
						pos, size, n - body) > "/dev/stderr"
				exit 3
			}
			if (cmd == MAPLE_BUFFER && size >= 4) {
				rec = u32(body)
				if (rec >= 4) {
					nrec = int((size - 4) / rec)
					for (i = 0; i < nrec; i++) seen[u32(body + 4 + i*rec)] = 1
				}
			}
			pos = body + size; msgs++
			if (cmd == MAPLE_BUFFER) maple++
		}
		# A well-formed movie ends exactly on a message boundary. Anything left
		# over is a partial message from a killed writer.
		if (pos != n) {
			printf("flyrframes: %d trailing byte(s) after the last message at %d\n",
					n - pos, pos) > "/dev/stderr"
			exit 3
		}
		# msgs > 0 is NOT enough: a run of zero bytes parses as a chain of
		# well-formed empty messages and would report 0 frames, which reads as
		# "a short clip" rather than "not a movie". Requiring actual movie data
		# is what separates those. [MEASURED] found by the --self-test arm; the
		# earlier random-bytes check passed only because random size fields are
		# huge and tripped the truncation path instead.
		if (maple == 0) {
			printf("flyrframes: %d message(s) but no movie data - not a .flyr?\n",
					msgs) > "/dev/stderr"
			exit 3
		}
		print length(seen)
	}
'
