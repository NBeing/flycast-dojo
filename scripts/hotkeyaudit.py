#!/usr/bin/env python3
"""
hotkeyaudit - do the five files that must agree about a hotkey actually agree?

  RUN:   scripts/hotkeyaudit.py            (from anywhere; paths are repo-relative)
  PASS:  exit 0, "N actions, all consistent"
  FAIL:  exit 1, one line per action naming which site is missing it
  SELF:  scripts/hotkeyaudit.py --self-test - proves the audit can say no,
         by running it against doctored copies with a known site removed.

WHY. A hotkey in this tree is not one declaration, it is five:

    core/input/gamepad.h            the id exists
    core/input/mapping.cpp          it PERSISTS (section + option name)
    core/rend/gui.cpp   dcButtons   it is bindable on a Dreamcast layout
    core/rend/gui.cpp   arcadeButtons   ...and on an arcade one
    core/input/gamepad_device.cpp   it DOES something

`[MEASURED 2026-09-10]` the first run of this found six hotkeys that had four
of the five: EMU_BTN_RECORD_3/4/5 and EMU_BTN_PLAY_3/4/5. Training::record_slot
is [6], the mapping window has always offered "Record Slot 4/5/6", and binding
one did nothing and was forgotten on restart. Nothing in the tree could see it,
because each individual file was self-consistent and complete-looking.

THIS IS A SOURCE-LEVEL CHECK ON PURPOSE. The property being asserted is that
five tables in five files agree; that property lives in the source, and the
tables are file-static, so a runtime check would need surface added for the sole
benefit of the test. Reading the source is the honest instrument here, not a
workaround.

KNOWN EXCEPTIONS ARE NAMED, NOT INFERRED. An audit that silently skips whatever
does not fit has no failure mode. Each entry below says why it is exempt, and an
exemption for an action that HAS since been wired is itself reported, so the
list cannot rot into a permanent excuse.
"""
import os, re, sys, tempfile, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# id -> why it legitimately misses a site
EXEMPT = {
    # Consumed at the keyboard device, before the emulator sees it - a gamepad
    # cannot bypass an emulated keyboard, so there is nothing to dispatch.
    # `[SOURCE]` keyboard_device.h: `if (keycode == input_mapper->get_button_code(0, EMU_BTN_BYPASS_KB))`
    'EMU_BTN_BYPASS_KB': {'dispatch'},

    # LEGACY ALIASES. `[MEASURED 2026-09-10]` these two dispatch to exactly the
    # same button sets as EMU_CMB_X_A and EMU_CMB_Y_B - one combo declared
    # twice, once Dreamcast-named and once arcade-named - so offering them for
    # binding puts two rows with identical behaviour in front of the user, which
    # is what dcButtons did. The ids and their persistence rows are kept so a
    # binding already in emu.cfg still fires; they are just not advertised.
    'EMU_CMB_1_4': {'dcButtons', 'arcadeButtons'},
    'EMU_CMB_2_5': {'dcButtons', 'arcadeButtons'},

    # Dreamcast-only: an arcade cabinet has no A button and no Start in that
    # sense, so its absence from the arcade layout is the correct answer rather
    # than a gap.
    'EMU_CMB_A_START': {'arcadeButtons'},
}

SITES = ('persist', 'dcButtons', 'arcadeButtons', 'dispatch')


def strip(src):
    """Comments and string literals out.

    `[MEASURED 2026-09-10]` scripts/configaudit.py needed this because
    config::Training's only two mentions in the tree are COMMENTS SAYING IT IS
    READ BY NOBODY, and the prose counted as a read. The same hazard lives here:
    this file greps for `{ EMU_BTN_X,` and several of the sources it greps carry
    comments recording ids that were REMOVED - gui.cpp's retired EMU_CMB_1_4 row
    among them. Strings are stripped too, but the table patterns below look for
    a quote, so stripping runs before those and they use the raw text.
    """
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', src)


def table(text, name):
    m = re.search(re.escape(name) + r'\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if m is None:
        raise SystemExit(f"hotkeyaudit: cannot find table {name} - the audit "
                         f"itself is broken, which is not a PASS")
    return m.group(1)


def audit(root, quiet=False, exempt=None):
    """-> (actions, problems). Reads the five files under `root`."""
    exempt = EXEMPT if exempt is None else exempt
    def read(p):
        with open(os.path.join(root, p)) as f:
            return f.read()

    enum = strip(read('core/input/gamepad.h'))
    mapping = strip(read('core/input/mapping.cpp'))
    gui = strip(read('core/rend/gui.cpp'))
    disp = strip(read('core/input/gamepad_device.cpp'))
    reg = strip(read('core/input/hotkeys.cpp'))

    ids = []
    seen = set()
    for i in re.findall(r'\b(EMU_BTN_[A-Z0-9_]+|EMU_CMB_[A-Z0-9_]+)\b', enum):
        if i not in seen and i != 'EMU_BTN_NONE':
            seen.add(i)
            ids.append(i)
    # NON-VACUITY. An enum that suddenly parses to nothing would otherwise
    # report "all consistent" over zero actions.
    if len(ids) < 40:
        raise SystemExit(f"hotkeyaudit: only parsed {len(ids)} actions out of "
                         f"gamepad.h; expected 40+. The parser is broken.")

    dc = table(gui, 'dcButtons')
    arc = table(gui, 'arcadeButtons')

    # THE REGISTRY, core/input/hotkeys.cpp. `[2026-09-10]` one row there gives
    # an action its persistence AND both of its settings-window rows, so for
    # anything listed in it those three sites are satisfied BY CONSTRUCTION -
    # which is the point of the registry and why this check has to understand
    # it rather than report the move as five regressions (it did, first run).
    #
    # What the registry does NOT give an action is a DISPATCH case. That is
    # still checked, so a registry row with nothing behind it fails here.
    registered = set(re.findall(r'\{\s*(EMU_BTN_\w+)\s*,\s*"', reg))

    problems = []
    # NON-VACUITY FOR THE REGISTRY ITSELF. If hotkeys.cpp stopped parsing, every
    # action in it would fall back to the three text greps, find nothing, and be
    # reported as broken - noisy but safe. The reverse is the danger: a registry
    # that parses to a name with no cfg or label would satisfy the three sites
    # here while writing nothing anywhere, so the rows are checked for shape.
    for row in re.findall(r'\{\s*(EMU_BTN_\w+)\s*,([^}]*)\}', reg):
        name, rest = row
        if rest.count('"') < 4:
            problems.append((name, 'registry row is missing its cfg name or its label'))

    for i in ids:
        inReg = i in registered
        have = {
            'persist':       inReg or re.search(r'\{\s*' + i + r'\s*,\s*"', mapping) is not None,
            'dcButtons':     inReg or re.search(r'\{\s*' + i + r'\s*,', dc) is not None,
            'arcadeButtons': inReg or re.search(r'\{\s*' + i + r'\s*,', arc) is not None,
            'dispatch':      ('case ' + i + ':') in disp,
        }
        ex = exempt.get(i, set())
        missing = [s for s in SITES if not have[s] and s not in ex]
        # An exemption that is no longer needed is reported too, so the list
        # cannot quietly become a place where real gaps go to hide.
        stale = [s for s in ex if have[s]]
        if missing:
            problems.append((i, 'missing from ' + ', '.join(missing)))
        if stale:
            problems.append((i, 'EXEMPTION IS STALE - now present in ' + ', '.join(stale)))
    return ids, problems


def run(root, quiet=False):
    ids, problems = audit(root)
    if problems:
        if not quiet:
            for i, why in problems:
                print(f"  {i:32} {why}")
            print(f"FAIL hotkeyaudit - {len(problems)} of {len(ids)} actions "
                  f"are not wired at every site")
        return 1
    if not quiet:
        print(f"PASS hotkeyaudit - {len(ids)} actions, all consistent")
    return 0


def self_test():
    """Each arm removes ONE site's row for a known-good action and requires the
    audit to notice. A judge that answers 'inconsistent' unconditionally passes
    every arm, so the last arm is the control: an untouched tree must PASS."""
    rc = 0
    # (file, a regex whose match is deleted, the action, the site it should break)
    arms = [
        ('core/input/mapping.cpp', r'\{ EMU_BTN_PAUSE, "emulator", "btn_pause" \},', 'EMU_BTN_PAUSE', 'persist'),
        ('core/rend/gui.cpp',      r'\{ EMU_BTN_STEP, "Step Frame" \},',            'EMU_BTN_STEP',  'dcButtons'),
        ('core/input/gamepad_device.cpp', r'case EMU_BTN_PAUSE:',                    'EMU_BTN_PAUSE', 'dispatch'),
    ]
    for path, pat, action, site in arms:
        tmp = tempfile.mkdtemp()
        try:
            for f in ('core/input/gamepad.h', 'core/input/mapping.cpp',
                      'core/rend/gui.cpp', 'core/input/gamepad_device.cpp',
                      'core/input/hotkeys.cpp'):
                os.makedirs(os.path.join(tmp, os.path.dirname(f)), exist_ok=True)
                shutil.copy(os.path.join(ROOT, f), os.path.join(tmp, f))
            p = os.path.join(tmp, path)
            src = open(p).read()
            out, n = re.subn(pat, '', src, count=1)
            # A SABOTAGE THAT DID NOT APPLY runs the unmodified code and
            # "passes" - CLAUDE.md calls that a defect wearing a lab coat.
            if n != 1:
                print(f"FAIL hotkeyaudit --self-test - the sabotage for {site} "
                      f"did not apply; it patched nothing")
                rc = 1
                continue
            open(p, 'w').write(out)
            _, problems = audit(tmp)
            hit = [w for i, w in problems if i == action and site in w]
            if hit:
                print(f"  ok - removing {action} from {site} is caught")
            else:
                print(f"FAIL hotkeyaudit --self-test - removing {action} from "
                      f"{site} went unnoticed (problems: {problems})")
                rc = 1
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    # A STALE EXEMPTION is reported too, and that claim needs its own arm:
    # the exemption list is the one place a real gap could hide permanently,
    # so "we would notice" has to be demonstrated rather than asserted.
    _, problems = audit(ROOT, exempt={'EMU_BTN_PAUSE': {'dispatch'}})
    if [w for i, w in problems if i == 'EMU_BTN_PAUSE' and 'STALE' in w]:
        print("  ok - an exemption for something already wired is reported")
    else:
        print("FAIL hotkeyaudit --self-test - a stale exemption went unnoticed; "
              "the exemption list can hide a real gap")
        rc = 1

    # THE CONTROL.
    if run(ROOT, quiet=True) == 0:
        print("  ok - an untouched tree still passes")
    else:
        print("FAIL hotkeyaudit --self-test - the audit fails on a clean tree; "
              "it is not discriminating, it is just saying no")
        rc = 1
    if rc == 0:
        print("PASS hotkeyaudit --self-test - the audit can say no, and can say yes")
    return rc


if __name__ == '__main__':
    sys.exit(self_test() if '--self-test' in sys.argv else run(ROOT))
