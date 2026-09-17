#!/usr/bin/env bash
# fixtures-check - the MvC2 combo fixture's RECIPE, checked against what is on disk
# and (one claim) against the running emulator.
#
#   RUN:   scripts/fixtures-check.sh              (F1-F3 need only python3; F4 needs a
#          ROM, Xvfb, the build, and an OnEnter-wired binary)
#   PASS:  exit 0. Four claims, printed `  ok  F<n>  <claim, measured value inlined>`
#          / `  FAIL F<n>  <measured> <why>` / `  SKIP F<n>  <why>`, then the RESULT line
#          `FIXTURES RESULT: passed=N failed=N skipped=N unmeasured=N`:
#            F1 fastVS parity     - the boot seed and the globe seed hash to the RECIPE's
#                                   pins under seqHashMacro's own rule (FNV-1a 64 over each
#                                   frame's p1,p2 canon u16, the library.json index's hash),
#                                   parsed with tas_macro::FromText's rules. NO EMULATOR.
#            F2 RECIPE honest     - RECIPE.toml parses, every pin is either "unmeasured" or
#                                   well-shaped WITH a measured_on, and the resolved
#                                   addresses it pins agree with SPREADSHEET.json by NAME.
#            F3 vocabulary        - SPREADSHEET.json's md5 is the RECIPE's pin.
#            V1 vmu               - the VMU image is present and its md5 is the RECIPE's pin.
#                                   THE FIXTURE IS THE ROM AND THE VMU: `[MEASURED 2026-09-17]`
#                                   a sandbox's fresh XDG_DATA_HOME has an EMPTY VMU, the game
#                                   opens on "Press the Start button to create a file", and
#                                   every seed press lands one screen late. F4 stages it.
#            F4 charselect        - the first EXPECTED SEQUENCE on the real machine: boot the
#                                   globe seed, read ID_2 (RubyHeart 19), walk D,D,R,R (Venom
#                                   14), press D (Hulk 13) - David's verified "v_down", read
#                                   through ctlserver `read` at the SPREADSHEET-resolved
#                                   address, inputs through `input`+`step` in READ-WRITE.
#   EXIT:  0 every claim ok · 1 a CLAIM failed · 2 usage / refused · 4 --sabotage: the arm
#          FAILED TO FIRE (the target stayed green - the check is decorative) · 77 SKIP
#          (a fixture input is absent, or a claim could not run - a skipped check is not a
#          passing one, so a run with any SKIP is 77, never 0).
#   ARMS:  --sabotage <class> | --list-sabotage | --self-test (== --sabotage hash). Each arm
#          restores one defect this file exists to catch and is judged by the lifted
#          arms.lua rules in scripts/lib/arms.sh (applied / broke its target / left its
#          control green AND the control ran): `hash` corrupts ONE frame of a temp copy of
#          the boot seed (F1 must redden, F3 stays green); `recipe` pins a fake machine_hash
#          with no measured_on (F2 must redden, F1 stays green); `charselect` expects
#          Venom+D == 14 (F4 must redden, F1 stays green); `vmu` boots with NO VMU staged -
#          the create-save prompt eats the seed (F4 must redden with "never reached the
#          globe - globe=0", V1 stays green). INCONCLUSIVE exit 2 when the target could not
#          run. Inverted exits as above.
#   ROMS:  --verify-roms - the ROM at $FLYCAST_TEST_ROM (default David's NoBGM_VMU.cdi) is
#          present and its size + sha256 are the RECIPE's [rom] pins (three copies exist on
#          this machine; identity is the bytes, never the name).
#   REGEN: --regenerate - REFUSED unless FIXTURES_REGENERATE=iknow. Recomputes only the
#          no-emulator pins (snippet frames/hashes, spreadsheet md5), prints every
#          before/after, and never touches a result field: those are the hunt's.
#
# THE FAILURE THIS WOULD HAVE CAUGHT. `[MEASURED 2026-09-17]` library.json's entry for
# fastVS_mcp.txt says 633 frames / 4db6bf3690958f6b; the file is 697 / 442574011190bd6b
# (LK x4 at 634..637 picks the stage). The index went stale when the file grew, and a
# fixture that trusted the index would have booted 64 frames short of the globe and read
# ID_2 off a stage-select screen. F1 pins the FILE. F2 exists for the second failure: a
# RECIPE that claims a pin it never measured - "a correct picture of the wrong thing".
set -uo pipefail

SKIP=77
export LP_NUM_THREADS="${LP_NUM_THREADS:-4}"
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
EXE="${FLYCAST_BIN:-$ROOT/build-dojo7/flycast}"
ROM="${FLYCAST_TEST_ROM:-$HOME/dev/davids_fly/NoBGM_VMU.cdi}"
FIX="$ROOT/scripts/fixtures/mvc2"
RECIPE="${FIXTURES_RECIPE:-$FIX/RECIPE.toml}"
SPREADSHEET="$ROOT/core/dojo/mvc2_data/SPREADSHEET.json"
SPREADSHEET_MD5_CONST="23c1827fc4fe3b04313ee8c944565b20"	# the copy's md5 the day it was taken
ARMS="$ROOT/scripts/lib/arms.sh"
KNOWN_ARMS="hash recipe charselect vmu"

usage() { echo "usage: $0 [--verify-roms | --regenerate | --list-sabotage | --sabotage <class> | --self-test | --no-emu]   (exit 2: usage)"; exit 2; }
MODE=check; ARM=""; NOEMU=0
while [ $# -gt 0 ]; do
	case "$1" in
		--verify-roms) MODE=roms ;;
		--regenerate) MODE=regen ;;
		--list-sabotage) MODE=list ;;
		--sabotage) shift; [ $# -gt 0 ] || usage; ARM="$1" ;;
		--self-test) ARM=hash ;;
		--no-emu) NOEMU=1 ;;
		*) usage ;;
	esac
	shift
done
if [ -n "$ARM" ]; then
	case " $KNOWN_ARMS " in *" $ARM "*) ;; *) echo "fixtures-check: unknown sabotage class '$ARM' (known: $KNOWN_ARMS)"; exit 2 ;; esac
fi

if [ "$MODE" = list ]; then
	echo "hash        F1 fastVS parity     - one frame of a temp copy of the boot seed flipped; F1 must redden, F3 must stay green"
	echo "recipe      F2 RECIPE honest     - a fake machine_hash pinned with no measured_on; F2 must redden, F1 must stay green"
	echo "charselect  F4 charselect        - Venom+D expected 14 (it is 13, Hulk); F4 must redden, F1 must stay green"
	echo "vmu         F4 charselect        - no VMU staged: the create-save prompt eats the seed; F4 must redden (globe=0), V1 must stay green"
	exit 0
fi

command -v python3 >/dev/null || { echo "fixtures-check: SKIP - no python3"; exit $SKIP; }
[ -f "$RECIPE" ] || { echo "fixtures-check: SKIP - no RECIPE ($RECIPE)"; exit $SKIP; }
[ -f "$SPREADSHEET" ] || { echo "fixtures-check: SKIP - no SPREADSHEET.json ($SPREADSHEET)"; exit $SKIP; }
[ -f "$FIX/snippets/library.json" ] || { echo "fixtures-check: SKIP - no snippets/library.json"; exit $SKIP; }

OUT="$(mktemp -d)"; trap 'chmod -R u+w "$OUT" 2>/dev/null; rm -rf "$OUT"' EXIT

# ---- the shared python: tas_macro::FromText + seqHashMacro, transliterated -----------------
# The letter table and the frame rules are core/dojo/tasmacro.cpp's (P1 WSADZXCVBNM, P2
# TGFHUIOJKLP; '#' opens a comment, a comment-only line is an annotation, a digits-only
# line is trainer noise, '.' forces a neutral frame). The hash is dojo_gui.cpp's
# seqHashMacro (David): h = FNV-1a 64 basis; per frame h = (h ^ p1) * prime, (h ^ p2) * prime.
PY="$OUT/fix.py"
cat > "$PY" <<'PYEOF'
import json, sys, hashlib, tomllib
P1={'W':1,'S':2,'A':4,'D':8,'Z':16,'X':32,'C':512,'V':64,'B':128,'N':1024,'M':256}
P2={'T':1,'G':2,'F':4,'H':8,'U':16,'I':32,'O':512,'J':64,'K':128,'L':1024,'P':256}
def parse(path):
    frames=[]
    for line in open(path,encoding='utf-8',errors='replace'):
        line=line.rstrip('\n').rstrip('\r')
        at=line.find('#'); had=at>=0
        if had: line=line[:at]
        p1=p2=0; sawD=sawL=False
        for c in line:
            if c in ' \t.': continue
            if c.isdigit(): sawD=True; continue
            c=c.upper()
            if c in P1: p1|=P1[c]; sawL=True
            elif c in P2: p2|=P2[c]; sawL=True
        if sawD and not sawL: continue
        if had and not sawL and not sawD and not any(ch not in ' \t' for ch in line): continue
        frames.append((p1,p2))
    return frames
def fnv(frames):
    h=1469598103934665603
    for p1,p2 in frames:
        h=((h^p1)*1099511628211)&0xFFFFFFFFFFFFFFFF
        h=((h^p2)*1099511628211)&0xFFFFFFFFFFFFFFFF
    return '%016x'%h
def sheet(path):
    return json.load(open(path))['SPREADSHEET']['PlayerMemoryAddresses']
def addr(sh, field, key):          # 'P1_A' etc -> the Demul 0x2C.. string
    e=sh.get(field)
    if e is None: raise SystemExit('no field %s in SPREADSHEET'%field)
    return e['%s_%s'%(key,field)]
cmd=sys.argv[1]
if cmd=='hash':                     # hash <snippet.txt> -> "<frames> <hash>"
    fr=parse(sys.argv[2]); print(len(fr), fnv(fr))
elif cmd=='index':                  # index <library.json> <file> -> "<frames> <hash>" or "absent"
    for e in json.load(open(sys.argv[2]))['snippets']:
        if e.get('file')==sys.argv[3]: print(e.get('frames'), e.get('hash')); break
    else: print('absent')
elif cmd=='md5':
    print(hashlib.md5(open(sys.argv[2],'rb').read()).hexdigest())
elif cmd=='addr':                   # addr <sheet> <field> <P1_A> -> 0x2C......
    print(addr(sheet(sys.argv[2]), sys.argv[3], sys.argv[4]))
elif cmd=='get':                    # get <recipe> <sect> <key> -> value (lists joined by ',')
    d=tomllib.load(open(sys.argv[2],'rb')); v=d[sys.argv[3]][sys.argv[4]]
    print(','.join(map(str,v)) if isinstance(v,list) else v)
elif cmd=='honest':                 # honest <recipe> <sheet> -> lines: "ok|FAIL <what>", then "UNMEASURED a.b c.d"
    d=tomllib.load(open(sys.argv[2],'rb')); sh=sheet(sys.argv[3]); bad=[]; un=[]
    def ok(c,what):
        print(('ok   ' if c else 'FAIL ')+what)
        if not c: bad.append(what)
    ok(d.get('schema')==1,'schema == 1')
    for s in ('rom','vocabulary','base','charselect','candidate','phase','result'):
        ok(s in d,'section [%s] present'%s)
    # machine hashes are oracle::machineHash (XXH32, 8 upper hex, the hunt's %08X); the seeds' are seqHashMacro (16 lower hex)
    hexre=lambda v: isinstance(v,str) and len(v) in (8,16) and all(c in '0123456789abcdefABCDEF' for c in v)
    measured_on=d['result'].get('measured_on','')
    pins={'base.machine_hash':hexre,'base.machine_frame':lambda v:isinstance(v,int) and v>=0,
          'candidate.source':lambda v:isinstance(v,str) and v!='','candidate.frames':lambda v:isinstance(v,int) and v>0,
          'phase.value':lambda v:isinstance(v,int) and 0<=v<=3,
          'result.found':lambda v:v in ('yes','no'),'result.candidate':lambda v:isinstance(v,str) and v!='',
          'result.delay':lambda v:isinstance(v,int) and v>=0,'result.combo_peak':lambda v:isinstance(v,int) and v>=0,
          'result.after_hash':hexre}
    for k,shape in pins.items():
        s,key=k.split('.'); v=d[s].get(key)
        if v=='unmeasured': un.append(k); continue
        ok(shape(v),'%s is well-shaped (%r)'%(k,v))
        ok(measured_on!='','%s is pinned AND result.measured_on says when (%r)'%(k,measured_on))
    v=d['vocabulary']
    ok(v['combo_p1_a']==addr(sh,v['combo_field'],'P1_A'),'vocabulary.combo_p1_a == SPREADSHEET %s P1_A (%s)'%(v['combo_field'],v['combo_p1_a']))
    ok(v['combo_p2_a']==addr(sh,v['combo_field'],'P2_A'),'vocabulary.combo_p2_a == SPREADSHEET %s P2_A (%s)'%(v['combo_field'],v['combo_p2_a']))
    ok(v['cursor_p1_a']==addr(sh,v['cursor_field'],'P1_A'),'vocabulary.cursor_p1_a == SPREADSHEET %s P1_A (%s)'%(v['cursor_field'],v['cursor_p1_a']))
    c=d['charselect']; note2=sh['ID_2']['Note2'].replace('\r','').split('\n'); names={int(l.split(':')[0]):l.split(':')[1].strip() for l in note2 if ':' in l}
    for k in ('start','via','expect'):
        ok(names.get(c[k+'_id'])==c[k],'charselect.%s_id %d is %s in SPREADSHEET ID_2 Note2 (says %r)'%(k,c[k+'_id'],c[k],names.get(c[k+'_id'])))
    print('UNMEASURED '+' '.join(un))
    sys.exit(1 if bad else 0)
elif cmd=='corrupt':                # corrupt <in> <out> - flip the first neutral frame to a P1 LP press
    lines=open(sys.argv[2],encoding='utf-8',errors='replace').read().split('\n'); done=False
    for i,l in enumerate(lines):
        if not done and l.strip()=='.': lines[i]='Z'; done=True
    open(sys.argv[3],'w').write('\n'.join(lines))
elif cmd=='setpin':                 # setpin <recipe-in> <recipe-out> <sect.key> <toml-literal>
    s,key=sys.argv[4].split('.'); out=[]; insect=None
    for l in open(sys.argv[2]):
        t=l.strip()
        if t.startswith('[') and t.endswith(']'): insect=t[1:-1]
        elif insect==s and t.split('=')[0].strip()==key:
            l='%s = %s\n'%(key,sys.argv[5])
        out.append(l)
    open(sys.argv[3],'w').write(''.join(out))
PYEOF
py() { python3 "$PY" "$@"; }

# ---- claim bookkeeping (surfacetourtest's grammar) ---------------------------------------
PASSED=0; FAILED=0; SKIPPED=0; UNMEASURED=0
SEEN="$OUT/seen"; BROKEN="$OUT/broken"; SKIPIDS="$OUT/skipped"; : > "$SEEN"; : > "$BROKEN"; : > "$SKIPIDS"
claim() {	# claim <id> <ok|FAIL|SKIP> <text>
	echo "$1" >> "$SEEN"
	case "$2" in
		ok)   PASSED=$((PASSED+1));  printf '  ok   %s  %s\n' "$1" "$3" ;;
		FAIL) FAILED=$((FAILED+1));  echo "$1" >> "$BROKEN"; printf '  FAIL %s  %s\n' "$1" "$3" ;;
		SKIP) SKIPPED=$((SKIPPED+1)); echo "$1" >> "$SKIPIDS"; printf '  SKIP %s  %s\n' "$1" "$3" ;;
	esac
}

# ---- --verify-roms ------------------------------------------------------------------------
if [ "$MODE" = roms ]; then
	[ -f "$ROM" ] || { echo "fixtures-check: SKIP - no ROM ($ROM)"; exit $SKIP; }
	wsize=$(py get "$RECIPE" rom size); wsha=$(py get "$RECIPE" rom sha256)
	size=$(stat -c %s "$ROM"); sha=$(sha256sum "$ROM" | cut -d' ' -f1)
	if [ "$size" = "$wsize" ]; then claim R1 ok "rom size $size == RECIPE [rom].size"; else claim R1 FAIL "rom size $size != RECIPE $wsize ($ROM)"; fi
	if [ "$sha" = "$wsha" ]; then claim R2 ok "rom sha256 ${sha:0:16}… == RECIPE [rom].sha256"; else claim R2 FAIL "rom sha256 $sha != RECIPE $wsha ($ROM)"; fi
	echo "FIXTURES ROMS: passed=$PASSED failed=$FAILED rom=$ROM"
	[ "$FAILED" -eq 0 ] && { echo "PASS fixtures-check --verify-roms - the ROM is the RECIPE's ROM, by its bytes"; exit 0; }
	echo "FAIL fixtures-check --verify-roms - $FAILED claim(s) red"; exit 1
fi

# ---- --regenerate -------------------------------------------------------------------------
if [ "$MODE" = regen ]; then
	echo "fixtures-check --regenerate: NEVER REGENERATE TO MAKE A RED GATE GREEN. A pin that moved is a"
	echo "  finding: either the emulator changed (name the change) or the fixture's inputs changed (then"
	echo "  the RECIPE says which, by hash, and the old pin stays in git). Only the no-emulator pins are"
	echo "  recomputed here (snippet frames/hashes, spreadsheet md5); result fields are the hunt's."
	if [ "${FIXTURES_REGENERATE:-}" != "iknow" ]; then
		echo "REFUSED - set FIXTURES_REGENERATE=iknow to proceed (exit 2)"; exit 2
	fi
	changed=0
	for pair in "boot_seed:boot_frames:boot_hash" "globe_seed:globe_frames:globe_hash"; do
		IFS=: read -r fkey nkey hkey <<<"$pair"
		f="$FIX/$(py get "$RECIPE" base "$fkey")"
		[ -f "$f" ] || { echo "  SKIP $fkey: $f absent"; continue; }
		read -r n h < <(py hash "$f")
		on=$(py get "$RECIPE" base "$nkey"); oh=$(py get "$RECIPE" base "$hkey")
		if [ "$n" != "$on" ] || [ "$h" != "$oh" ]; then
			echo "  base.$nkey/$hkey: $on / $oh -> $n / $h  ($(basename "$f"))"
			py setpin "$RECIPE" "$OUT/r.toml" "base.$nkey" "$n" && py setpin "$OUT/r.toml" "$RECIPE" "base.$hkey" "\"$h\""
			changed=$((changed+1))
		else echo "  base.$nkey/$hkey: unchanged ($n / $h)"; fi
	done
	m=$(py md5 "$SPREADSHEET"); om=$(py get "$RECIPE" vocabulary spreadsheet_md5)
	if [ "$m" != "$om" ]; then echo "  vocabulary.spreadsheet_md5: $om -> $m"; py setpin "$RECIPE" "$OUT/r.toml" vocabulary.spreadsheet_md5 "\"$m\"" && cp "$OUT/r.toml" "$RECIPE"; changed=$((changed+1))
	else echo "  vocabulary.spreadsheet_md5: unchanged ($m)"; fi
	echo "fixtures-check --regenerate: $changed pin(s) rewritten in $RECIPE - review the diff and SAY WHY in the commit"
	exit 0
fi

# ---- arm the sabotage (a temp copy; the tree is never modified) -----------------------------
BOOT_OVERRIDE=""; EXPECT_OVERRIDE=""; NOVMU=0
case "$ARM" in
	hash)
		BOOT_OVERRIDE="$OUT/fastVS_corrupt.txt"
		py corrupt "$FIX/$(py get "$RECIPE" base boot_seed)" "$BOOT_OVERRIDE"
		echo "SABOTAGE armed: hash - one neutral frame of a temp copy of the boot seed is now a P1 LP press" ;;
	recipe)
		# BOTH halves of the lie, on purpose: a fake pin AND no measured_on. `[MEASURED 2026-09-17]`
		# faking only the pin was green by accident while result.measured_on happened to be
		# empty - once the hunt filled it, F2's "pinned AND says when" claim held and the arm
		# was decorative (ctest flycast.fixturescheck_can_fail_recipe caught it, exit 4).
		py setpin "$RECIPE" "$OUT/RECIPE_fake0.toml" base.machine_hash "\"0123456789abcdef\"" \
			&& py setpin "$OUT/RECIPE_fake0.toml" "$OUT/RECIPE_fake.toml" result.measured_on "\"\""
		RECIPE="$OUT/RECIPE_fake.toml"
		echo "SABOTAGE armed: recipe - base.machine_hash pinned to a fake value with result.measured_on empty" ;;
	charselect)
		EXPECT_OVERRIDE=14
		echo "SABOTAGE armed: charselect - Venom+D is now expected to read 14 (Venom itself), not 13" ;;
	vmu)
		NOVMU=1
		echo "SABOTAGE armed: vmu - F4 boots with NO VMU staged (the sandbox's empty card; the create-save prompt eats the seed)" ;;
esac
[ -n "$ARM" ] && echo "SABOTAGE arm $ARM: the run below is EXPECTED to be red"

echo "fixtures-check: RECIPE $RECIPE"

# ---- F1 fastVS parity (no emulator) -------------------------------------------------------
f1ok=1; f1txt=""
# the two seeds AND the candidate (a CANDIDATE by provenance - PS2-converted - pinned by the
# same rule so the file the hunt ran cannot drift under the RECIPE's result).
for pair in "base:boot_seed:boot_frames:boot_hash" "base:globe_seed:globe_frames:globe_hash" "candidate:file:frames:hash"; do
	IFS=: read -r sect fkey nkey hkey <<<"$pair"
	rel="$(py get "$RECIPE" "$sect" "$fkey")"; f="$FIX/$rel"
	[ "$fkey" = boot_seed ] && [ -n "$BOOT_OVERRIDE" ] && f="$BOOT_OVERRIDE"
	[ -f "$f" ] || { echo "fixtures-check: SKIP - fixture input absent: $f"; exit $SKIP; }
	read -r n h < <(py hash "$f")
	wn=$(py get "$RECIPE" "$sect" "$nkey"); wh=$(py get "$RECIPE" "$sect" "$hkey")
	if [ "$n" = "$wn" ] && [ "$h" = "$wh" ]; then f1txt="$f1txt $(basename "$rel")=$n/$h"
	else f1ok=0; f1txt="$f1txt $(basename "$rel")=$n/$h EXPECTED $wn/$wh"; fi
	[ "$sect" = base ] || continue		# only the seeds have a library.json entry
	idx=$(py index "$FIX/snippets/library.json" "$(basename "$rel")")
	[ "$idx" = "$n $h" ] || echo "  note F1  library.json says $(basename "$rel") is ${idx:-absent} - a STALE INDEX (the file is $n/$h); the RECIPE pins the file, not the index"
done
if [ "$f1ok" = 1 ]; then claim F1 ok "fastVS parity: seqHashMacro over the .txt matches the RECIPE pins ($f1txt )"
else claim F1 FAIL "fastVS parity:$f1txt"; fi

# ---- F2 RECIPE honest (no emulator) --------------------------------------------------------
py honest "$RECIPE" "$SPREADSHEET" > "$OUT/honest.txt"; hcode=$?
un=$(sed -n 's/^UNMEASURED //p' "$OUT/honest.txt"); UNMEASURED=$(wc -w <<<"$un")
grep -a '^FAIL' "$OUT/honest.txt" | sed 's/^/       /'
if [ "$hcode" -eq 0 ]; then claim F2 ok "RECIPE honest: $(grep -ac '^ok' "$OUT/honest.txt") shape/name claims hold; $UNMEASURED field(s) still unmeasured"
else claim F2 FAIL "RECIPE honest: $(grep -ac '^FAIL' "$OUT/honest.txt") claim(s) red (above)"; fi
[ "$UNMEASURED" -gt 0 ] && echo "       unmeasured: $un"

# ---- F3 vocabulary md5 (no emulator) -------------------------------------------------------
m=$(py md5 "$SPREADSHEET"); wm=$(py get "$RECIPE" vocabulary spreadsheet_md5)
if [ "$m" = "$wm" ] && [ "$m" = "$SPREADSHEET_MD5_CONST" ]; then claim F3 ok "vocabulary: SPREADSHEET.json md5 $m == RECIPE pin == the copy's md5"
else claim F3 FAIL "vocabulary: SPREADSHEET.json md5 $m, RECIPE $wm, copy $SPREADSHEET_MD5_CONST"; fi

# ---- V1 the VMU (no emulator) --------------------------------------------------------------
# The fixture is the ROM AND the VMU: David's seeds presume a card that already holds the
# MvC2 save. Absent => the whole run is SKIP (a fixture input is missing), like a missing seed.
VMU="$FIX/$(py get "$RECIPE" vmu file)"
[ -f "$VMU" ] || { echo "fixtures-check: SKIP - fixture input absent: $VMU"; exit $SKIP; }
vm=$(py md5 "$VMU"); wvm=$(py get "$RECIPE" vmu md5); vsz=$(stat -c %s "$VMU"); wvsz=$(py get "$RECIPE" vmu size)
if [ "$vm" = "$wvm" ] && [ "$vsz" = "$wvsz" ]; then claim V1 ok "vmu: $(basename "$VMU") $vsz bytes md5 $vm == RECIPE [vmu] pins"
else claim V1 FAIL "vmu: $(basename "$VMU") $vsz bytes md5 $vm, RECIPE $wvsz / $wvm"; fi

# ---- F4 charselect (emulator) --------------------------------------------------------------
f4() {
	[ "$NOEMU" -eq 0 ] || { claim F4 SKIP "charselect: --no-emu"; return; }
	[ -x "$EXE" ] || { claim F4 SKIP "charselect: not built ($EXE)"; return; }
	[ -f "$ROM" ] || { claim F4 SKIP "charselect: no ROM ($ROM)"; return; }
	command -v Xvfb >/dev/null || { claim F4 SKIP "charselect: no Xvfb"; return; }
	if [ -n "$(find "$ROOT/core" -newer "$EXE" -name '*.cpp' -o -newer "$EXE" -name '*.h' 2>/dev/null | head -1)" ]; then
		claim F4 SKIP "charselect: refusing to report on a stale binary (rebuild flycast)"; return; fi
	local globe="$FIX/$(py get "$RECIPE" base globe_seed)" settle start_id via_id expect_id path press cursor
	settle=$(py get "$RECIPE" charselect settle); start_id=$(py get "$RECIPE" charselect start_id)
	via_id=$(py get "$RECIPE" charselect via_id); expect_id="${EXPECT_OVERRIDE:-$(py get "$RECIPE" charselect expect_id)}"
	path=$(py get "$RECIPE" charselect path); press=$(py get "$RECIPE" charselect press)
	cursor=$(py addr "$SPREADSHEET" "$(py get "$RECIPE" vocabulary cursor_field)" P1_A)	# resolved by NAME, never typed here
	[ -f "$globe" ] || { claim F4 SKIP "charselect: globe seed absent ($globe)"; return; }
	# the seed the emulator plays: the globe seed + settle neutral frames; the handoff pause lands
	# on the last line, ON the globe, and the walk is injected live from there.
	local seed="$OUT/seed.txt" n
	{ cat "$globe"; for _ in $(seq 1 "$settle"); do echo "."; done; } > "$seed"
	read -r n _ < <(py hash "$seed")

	mkdir -p "$OUT/cfg/flycast-dojo" "$OUT/data/flycast-dojo" "$OUT/ctl/_ctl/resp"
	# stage the VMU (V1's pinned image) where the sandboxed emulator will look for its card;
	# the `vmu` arm leaves the sandbox's card EMPTY and the seed must fail to reach the globe.
	if [ "$NOVMU" -eq 0 ]; then cp "$VMU" "$OUT/data/flycast-dojo/$(basename "$VMU")"; fi
	local DN=$((172 + ($$ % 60))); while [ -e "/tmp/.X11-unix/X$DN" ] || [ -e "/tmp/.X$DN-lock" ]; do DN=$((DN+1)); [ "$DN" -gt 260 ] && { claim F4 SKIP "charselect: no free display"; return; }; done
	local D=":$DN"
	nohup Xvfb "$D" -screen 0 900x700x24 >"$OUT/xvfb.log" 2>&1 & local XPID=$!; sleep 2
	[ -e "/tmp/.X11-unix/X$DN" ] || { kill "$XPID" 2>/dev/null; claim F4 SKIP "charselect: Xvfb did not come up on $D"; return; }
	# a fresh RECORD boot (the seed needs power-on frame 0), Training so step lands frame-exact
	# (ctltest), the control server on an explicit CtlDir so no clip is needed.
	XDG_CONFIG_HOME="$OUT/cfg" XDG_DATA_HOME="$OUT/data" DISPLAY="$D" "$EXE" \
		-config dojo:UiIni=no -config dojo:NativeConsole=no -config dojo:StartupPrompt=no \
		-config dojo:Training=yes -config dojo:RecordMatches=yes -config dojo:Replay=no \
		-config dojo:AutoLoadNetState=no -config dojo:AutoLoadTrainingNetState=no -config dojo:Transmitting=no -config dojo:Receiving=no \
		-config "dojo:OnEnterFile=$seed" \
		-config dojo:ControlServer=yes -config "dojo:CtlDir=$OUT/ctl" \
		-config window:width=900 -config window:height=700 -config window:fullscreen=no \
		"$ROM" > "$OUT/out.log" 2>&1 & local FC=$!
	local BASE="$OUT/ctl/_ctl" LOG="$OUT/out.log"
	teardown() { kill "$FC" 2>/dev/null; kill "$XPID" 2>/dev/null; sleep 2; kill -0 "$FC" 2>/dev/null && kill -9 "$FC" 2>/dev/null; kill -0 "$XPID" 2>/dev/null && kill -9 "$XPID" 2>/dev/null; }
	logged() { tr -d '\0' < "$LOG" | grep -a -- "$1" > /dev/null; }
	send() {	# send <seq> <verb> <args-json> -> resp json on stdout (ctltest's client, verbatim)
		local seq="$1" verb="$2" args="${3:-{\}}" i
		printf '{"seq":%s,"verb":"%s","args":%s}\n' "$seq" "$verb" "$args" > "$BASE/cmd.json.tmp"
		mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"
		for i in $(seq 1 100); do
			[ -f "$BASE/resp/$seq.json" ] && { cat "$BASE/resp/$seq.json"; return 0; }
			kill -0 "$FC" 2>/dev/null || return 1
			sleep 0.2
		done
		return 1
	}
	field() { python3 -c "import json,sys; d=json.loads(sys.argv[1]); print(d.get(sys.argv[2]))" "$1" "$2" 2>/dev/null; }

	# (a) the seed must be SEEDED - the one engine trace that says the boot plays it.
	local i seeded=0
	for i in $(seq 1 80); do kill -0 "$FC" 2>/dev/null || break; logged "TAS ONENTER: seeded" && { seeded=1; break; }; logged "gui_start_game" && [ "$i" -gt 30 ] && break; sleep 0.5; done
	if [ "$seeded" -eq 0 ]; then
		teardown
		if logged "gui_start_game"; then
			claim F4 SKIP "charselect: the OnEnter seed is NOT WIRED in this build - the game booted but never logged 'TAS ONENTER: seeded' (Dojo::SeedOnEnter exists; gui.cpp never calls it - David's gui.cpp:1072 does, after the Play-Macro branch)"
		else claim F4 SKIP "charselect: the emulator never reached gui_start_game ($(tr -d '\0' < "$LOG" | grep -a -c '' ) log lines)"; fi
		return
	fi
	# (b) the handoff pause: paused at frame >= the seed length (fast-forwarded through the boot).
	printf '{"seq":0,"verb":"query","args":{}}\n' > "$BASE/cmd.json.tmp"; mv -f "$BASE/cmd.json.tmp" "$BASE/cmd.json"; sleep 2
	local seq=1 r fr paused=False
	for i in $(seq 1 120); do
		r=$(send $seq query "{}") || { teardown; claim F4 SKIP "charselect: control server not answering (seq $seq)"; return; }; seq=$((seq+1))
		fr=$(field "$r" frame); paused=$(field "$r" paused)
		[ "$paused" = True ] && [ "${fr:-0}" -ge "$n" ] && break
		sleep 1
	done
	if [ "$paused" != True ] || [ "${fr:-0}" -lt "$n" ]; then teardown; claim F4 SKIP "charselect: no handoff pause at frame >= $n (paused=$paused frame=$fr)"; return; fi
	# (c) READ-WRITE so injected cells DRIVE the guest (the seed only borrows it; a Record Movie handoff restores WRITE).
	r=$(send $seq set_mode '{"mode":"READWRITE"}'); seq=$((seq+1)); local mode; mode=$(field "$r" mode)
	rd() { local a; a=$(send $seq read "{\"addr\":\"$cursor\",\"width\":1}"); seq=$((seq+1)); field "$a" value; }
	local v0 v1 v2 f
	v0=$(rd)
	# the walk: each press is ONE frame (press-edge; a held direction moves once), then a gap frame.
	local bits; declare -A bits=([U]=1 [D]=2 [L]=4 [R]=8)
	for d in ${path//,/ }; do
		r=$(send $seq query "{}"); seq=$((seq+1)); f=$(field "$r" frame)
		send $seq input "{\"p1\":${bits[$d]},\"frame\":$f,\"hold\":1}" >/dev/null; seq=$((seq+1))
		send $seq step '{"n":3}' >/dev/null; seq=$((seq+1))
	done
	v1=$(rd)
	r=$(send $seq query "{}"); seq=$((seq+1)); f=$(field "$r" frame)
	send $seq input "{\"p1\":${bits[$press]},\"frame\":$f,\"hold\":1}" >/dev/null; seq=$((seq+1))
	send $seq step '{"n":6}' >/dev/null; seq=$((seq+1))
	v2=$(rd)
	tr -d '\0' < "$LOG" | grep -a "CTL: \|TAS ONENTER" | tail -4 | sed 's/^/       /'
	teardown
	local m="globe=$v0 (want $start_id) after $path=$v1 (want $via_id) after $press=$v2 (want $expect_id) mode=$mode seed=$n frames @$cursor"
	if [ "$v0" = "$start_id" ] && [ "$v1" = "$via_id" ] && [ "$v2" = "$expect_id" ]; then
		claim F4 ok "charselect: ID_2 $v0 -> $v1 -> $v2 == RubyHeart -> Venom -> Hulk ($m)"
	elif [ "$v0" != "$start_id" ]; then
		claim F4 FAIL "charselect: never reached the globe - $m"
	else claim F4 FAIL "charselect: the cursor did not go where the graph predicts - $m"; fi
}
f4

echo "FIXTURES RESULT: passed=$PASSED failed=$FAILED skipped=$SKIPPED unmeasured=$UNMEASURED"

# ---- the verdict, armed or not -----------------------------------------------------------
if [ -n "$ARM" ]; then
	[ -x "$ARMS" ] || { echo "fixtures-check: no judge at $ARMS"; exit 2; }
	case "$ARM" in
		hash)       target=F1; control=F3; what="one corrupt frame in the boot seed" ;;
		recipe)     target=F2; control=F1; what="a fake pin with no measured_on" ;;
		charselect) target=F4; control=F1; what="Venom+D expected to read Venom" ;;
		vmu)        target=F4; control=V1; what="no VMU staged - the create-save prompt eats the seed" ;;
	esac
	# an arm whose target could not run is INCONCLUSIVE, not a pass - the judge says so when
	# the target is not in SEEN, so a SKIPped target is struck from SEEN before judging.
	if grep -a "^$target$" "$SKIPIDS" > /dev/null; then
		grep -av "^$target$" "$SEEN" > "$SEEN.t"; mv "$SEEN.t" "$SEEN"
	fi
	"$ARMS" judge "$ARM" "$what" "$target" "$control" "$FAILED" "$SEEN" "$BROKEN"; j=$?
	case "$j" in
		0) echo "PASS fixtures-check --sabotage $ARM - the arm fired as predicted ($target reddened, $control stayed green)"; exit 0 ;;
		2) echo "INCONCLUSIVE fixtures-check --sabotage $ARM - $target could not run, so this arm proves nothing (exit 2)"; exit 2 ;;
		*) if grep -aq "^$target$" "$BROKEN"; then echo "FAIL fixtures-check --sabotage $ARM - it fired but broke its control $control, or the tally lies"; exit 1; fi
		   echo "FAIL fixtures-check --sabotage $ARM - the arm did NOT fire: $target stayed green, the check is decorative (exit 4)"; exit 4 ;;
	esac
fi
if [ "$FAILED" -gt 0 ]; then echo "FAIL fixtures-check - $FAILED claim(s) red"; exit 1; fi
if [ "$SKIPPED" -gt 0 ]; then echo "SKIP fixtures-check - $PASSED claim(s) ok, $SKIPPED skipped (a skipped check is not a passing one)"; exit $SKIP; fi
echo "PASS fixtures-check - every claim held ($PASSED ok, $UNMEASURED field(s) still unmeasured and SAID so)"
exit 0
