#!/usr/bin/env python3
"""
configaudit - is every registered config option actually read?

  RUN:   scripts/configaudit.py
  PASS:  exit 0, every option is read somewhere
  FAIL:  exit 1, naming options nothing reads
  SELF:  scripts/configaudit.py --self-test - proves the audit can say no

WHY. An `Option` in core/cfg/option.cpp is REGISTERED: it gets a default, it is
loaded from and saved to the user's emu.cfg, and it shows up as a line in that
file. A key nothing reads is therefore a lie the user can edit - it looks like a
setting, it persists like a setting, and it does nothing.

`[MEASURED 2026-09-10]` the first run found one: `OptionString
DojoServerIP("ServerIP", "127.0.0.1", "dojo")`, declared in option.cpp, externed
in option.h and referenced NOWHERE else in the tree. Dojo netplay reads
SpectatorIP / RelayServer / RelayKey instead. It has now been removed.

TWO WAYS TO READ ONE KEY, which is the other thing this reports. Several options
are never touched through `config::Name` but ARE read through the raw
`cfgLoadBool("section", "key", …)` API under the same key. That is two
mechanisms for one fact - the Option owns the default and the persistence, the
raw read owns its own default, and nothing makes them agree.
docs/SESSION-KINDS.md §4 #11 records exactly this for `Training`. The existing
five are listed by name below so the SET IS FROZEN: a sixth fails.

ALSO SURVEYED AND REJECTED, so nobody repeats it: "is any raw cfg key read with
TWO DIFFERENT DEFAULTS", which would mean behaviour depending on which path read
it first. `[MEASURED 2026-09-10]` 112 distinct raw keys, 7 with more than one
default, and essentially all of them benign - `"no"` versus `false` is the same
value through cfgLoadStr and cfgLoadBool, and every window:* case is a
platform-specific fallback in files that never both run (x11.cpp, sdl.cpp,
dispmanx.cpp). A check there would carry more exemptions than findings, which is
how an audit becomes a thing people skim past. The lens that found the two real
audits in this tree does not generalise to this one.

COMMENTS AND STRINGS ARE STRIPPED FIRST, and that is not tidiness. `[MEASURED
2026-09-10]` the first version of this check grepped raw text, so
`config::Training`'s only two mentions - both in comments SAYING IT IS READ BY
NOBODY - counted as reads, and the audit reported it healthy. A check whose
false negative is caused by a comment about the very defect is worth the extra
twenty lines.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Read through the raw cfg API rather than through config::Name. Each is a
# second mechanism for one key; the list is frozen so a new one is a failure.
RAW_ONLY = {
    'Relay':          'read as cfgLoadBool("dojo", "Relay", …) in gui.cpp and dojo_gui.cpp',
    'ReplayFilename': 'read as cfgLoadStr("dojo", "ReplayFilename", …) in replay.cpp and avi_dump.cpp',
    'SpectateKey':    'read as cfgLoadStr("dojo", "SpectateKey", …) in tcp_client.cpp',
    'TestGame':       'read as cfgLoadBool("dojo", "TestGame", …) in gui.cpp',
    'Training':       'read as cfgLoadBool("dojo", "Training", false) in session.cpp - '
                      'docs/SESSION-KINDS.md §4 #11, deliberately the one owner',
}


def strip(src):
    """Comments and string literals out, so a mention in prose is not a read."""
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    src = re.sub(r'//[^\n]*', ' ', src)
    src = re.sub(r'"(\\.|[^"\\])*"', '""', src)
    return src


def audit(root):
    decl_path = os.path.join(root, 'core/cfg/option.cpp')
    decl = open(decl_path).read()
    names = sorted(set(re.findall(
        r'^\s*Option\w*\s*(?:<[^>]*>)?\s*(\w+)\s*\(', strip(decl), re.M)))
    # NON-VACUITY: an option.cpp that stopped parsing would otherwise report
    # "every option is read" over nothing at all.
    if len(names) < 100:
        raise SystemExit(f"configaudit: only parsed {len(names)} options from "
                         f"option.cpp; expected 100+. The parser is broken.")

    used = set()
    for base in ('core', 'shell'):
        for dp, _, fns in os.walk(os.path.join(root, base)):
            if '/deps/' in dp:
                continue
            for fn in fns:
                if not fn.endswith(('.cpp', '.h', '.mm')):
                    continue
                p = os.path.join(dp, fn)
                if p.endswith(('cfg/option.cpp', 'cfg/option.h')):
                    continue
                try:
                    body = strip(open(p, errors='ignore').read())
                except OSError:
                    continue
                used.update(re.findall(r'config::(\w+)', body))

    problems = []
    for n in names:
        if n in used:
            if n in RAW_ONLY:
                problems.append((n, 'listed as read-raw-only, but IS read through '
                                    'config:: now - drop it from RAW_ONLY'))
            continue
        if n not in RAW_ONLY:
            problems.append((n, 'registered in option.cpp and read NOWHERE - it is a '
                                'line in the user\'s emu.cfg that does nothing'))
    return names, problems


def run(root, quiet=False):
    names, problems = audit(root)
    if problems:
        if not quiet:
            for n, why in problems:
                print(f"  {n:28} {why}")
            print(f"FAIL configaudit - {len(problems)} of {len(names)} options")
        return 1
    if not quiet:
        print(f"PASS configaudit - {len(names)} options, "
              f"{len(RAW_ONLY)} read through the raw cfg API by name, none dead")
    return 0


def self_test():
    import shutil, tempfile
    rc = 0
    # A: an option nothing reads must be caught.
    tmp = tempfile.mkdtemp()
    try:
        for f in ('core/cfg/option.cpp', 'core/cfg/option.h'):
            os.makedirs(os.path.join(tmp, os.path.dirname(f)), exist_ok=True)
            shutil.copy(os.path.join(ROOT, f), os.path.join(tmp, f))
        os.makedirs(os.path.join(tmp, 'core/x'), exist_ok=True)
        open(os.path.join(tmp, 'core/x/x.cpp'), 'w').write('int x;\n')
        p = os.path.join(tmp, 'core/cfg/option.cpp')
        src = open(p).read()
        src += '\nOption<bool> NobodyReadsThis("NobodyReadsThis", false, "dojo");\n'
        open(p, 'w').write(src)
        _, problems = audit(tmp)
        if [w for n, w in problems if n == 'NobodyReadsThis']:
            print("  ok - an option nothing reads is caught")
        else:
            print(f"FAIL configaudit --self-test - a dead option went unnoticed: {problems}")
            rc = 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # B: a comment mentioning the option must NOT count as a read - the exact
    # false negative the first version of this check had.
    tmp = tempfile.mkdtemp()
    try:
        for f in ('core/cfg/option.cpp', 'core/cfg/option.h'):
            os.makedirs(os.path.join(tmp, os.path.dirname(f)), exist_ok=True)
            shutil.copy(os.path.join(ROOT, f), os.path.join(tmp, f))
        os.makedirs(os.path.join(tmp, 'core/x'), exist_ok=True)
        open(os.path.join(tmp, 'core/x/x.cpp'), 'w').write(
            '// config::NobodyReadsThis is registered and read by nobody\n'
            'const char *s = "config::NobodyReadsThis";\n')
        p = os.path.join(tmp, 'core/cfg/option.cpp')
        open(p, 'a').write('\nOption<bool> NobodyReadsThis("NobodyReadsThis", false, "dojo");\n')
        _, problems = audit(tmp)
        if [w for n, w in problems if n == 'NobodyReadsThis']:
            print("  ok - a mention in a comment or a string is not a read")
        else:
            print("FAIL configaudit --self-test - a comment counted as a read, which is "
                  "the false negative this check was rewritten to remove")
            rc = 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # THE CONTROL. Without it a judge that flags everything passes both arms.
    if run(ROOT, quiet=True) == 0:
        print("  ok - the real tree still passes")
    else:
        print("FAIL configaudit --self-test - the audit fails on the real tree; "
              "it is not discriminating, it is just saying no")
        rc = 1
    if rc == 0:
        print("PASS configaudit --self-test - the audit can say no, and can say yes")
    return rc


if __name__ == '__main__':
    sys.exit(self_test() if '--self-test' in sys.argv else run(ROOT))
