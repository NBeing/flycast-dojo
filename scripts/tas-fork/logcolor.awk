# Colourise flycast log lines for the Git Bash tail window.
#
# The colour lives in the TERMINAL, never in the file: build\flycast.log stays plain text, so
# grep, the VS Code reader and every script keep working on it unchanged. Git Bash (mintty),
# Windows Terminal and modern conhost all understand these escapes - no special terminal needed,
# and the emulator is not involved at all since it only ever writes the file.
#
#   tail -F flycast.log | awk -f logcolor.awk
BEGIN {
	R   = "\033[0m";   DIM = "\033[2m";  BOLD = "\033[1m"
	RED = "\033[91m";  YEL = "\033[93m"; GRN  = "\033[92m"
	CYA = "\033[96m";  MAG = "\033[95m"; WHT  = "\033[97m"
}
{
	s = $0

	# "00:12:345 dojo/dojo.cpp:539 N[NETWORK]: message"
	#  \_______ dimmed prefix ___/ \_ level _/  \_ body _/
	pre = ""; rest = s
	if (match(s, /^[0-9]+:[0-9]+:[0-9]+ [^ ]+ /)) {
		pre  = substr(s, 1, RLENGTH)
		rest = substr(s, RLENGTH + 1)
	}

	lvl = "N"
	if (match(rest, /^[ENWID]\[/))
		lvl = substr(rest, 1, 1)

	# Severity sets the floor...
	c = WHT
	if (lvl == "E")      c = RED
	else if (lvl == "W") c = YEL
	else if (lvl == "I") c = DIM WHT
	else if (lvl == "D") c = DIM

	# ...and what the line actually SAYS can raise it. These are the markers worth spotting from
	# across the room while re-recording.
	if (rest ~ /TAS[ :]/)                     c = CYA
	if (rest ~ /hotkey:/)                     c = MAG
	if (rest ~ /idempotent OK/)               c = GRN
	if (rest ~ /replay end|movie end/)        c = YEL
	if (rest ~ /NOT-idempotent|FAIL|\[GPF\]|BLOCKED/) c = BOLD RED

	printf "%s%s%s%s%s%s\n", DIM, pre, R, c, rest, R
	fflush()
}
