# scripts/fixtures/mvc2 - the MvC2 combo fixture

The fixture is BUILT, not found: nothing on this machine records a combo landing
(see `docs/TEST-PLAN.md` §5.3). What lives here is the recipe, the inputs it pins,
and the graph the first machine claim is checked against.

| file | what | pinned by |
|---|---|---|
| `RECIPE.toml` | the fixture: ROM, vocabulary, base, charselect sequence, candidate, phase, result | `scripts/fixtures-check.sh` F2; the never-regenerate rule is in the file |
| `snippets/fastVS.txt` | David's DC-native boot seed, power-on -> VS mode, handoff on STAGE SELECT | F1: 633 frames / `4db6bf3690958f6b` |
| `snippets/fastVS_mcp.txt` | the same + LK x4 at 634..637 (the stage pick) -> the char-select globe | F1: 697 frames / `442574011190bd6b` |
| `snippets/library.json` | David's index of his snippet library, copied verbatim | NOT pinned - its `fastVS_mcp` entry is stale (633 / `4db6bf…`); F1 prints that as a note |
| `charselect_nodes.json` | David's char-select node graph (cardinals emulator-verified) | the `[charselect]` section of the RECIPE |
| `vmu_save_A1.bin` | David's VMU. **The fixture is the ROM and the VMU**: a fresh sandbox's empty card opens the game on "Press the Start button to create a file" and every seed press lands one screen late | V1: 131072 bytes / md5 `08baab93cdd2f4fcea3e8bf8d199d3ca`; staged into `<XDG_DATA_HOME>/flycast-dojo/` before F4 boots |
| `candidates/Combo_Dhalsim97_pcsx2_macro.txt` | David's PS2-converted candidate; markers 4212-5699. A CANDIDATE by provenance - a fixture only because the hunt observed it connect (peak 19) | F1: 6707 frames / `2cbafba30a4c25fc`; `scripts/combohunttest.sh` H1-H6 |

## Running

```
scripts/fixtures-check.sh                 # F1-F3 + V1 (no emulator) + F4 (boots the globe seed on a private Xvfb)
scripts/fixtures-check.sh --no-emu        # F1-F3 + V1 only
scripts/fixtures-check.sh --verify-roms   # the ROM by size + sha256
scripts/fixtures-check.sh --list-sabotage
scripts/fixtures-check.sh --sabotage hash | recipe | charselect | vmu     (--self-test == hash)
FIXTURES_REGENERATE=iknow scripts/fixtures-check.sh --regenerate   # no-emulator pins only, before/after printed

scripts/combohunttest.sh                  # the hunt from slot 0 of the tour's clip, held against [result]/[base]/[phase]
scripts/combohunttest.sh --sabotage window   # the pre-combo window 84-1500 must give found=no
```

Measured `[2026-09-17]`: `fixtures-check.sh` `passed=5 failed=0 skipped=0`;
`combohunttest.sh` `passed=6 failed=0` on
`COMBO HUNT RESULT: found=yes candidate=Combo_Dhalsim97_pcsx2[4212-5699] phase=0 d=0 peak=19 base=27FA5D20 after=64FF89AB`;
every arm `SABOTAGE BEHAVED AS PREDICTED`.

Exit 0 every claim ok · 1 a claim failed · 2 usage / refused · 4 an arm failed to
fire · 77 a fixture input is absent or a claim could not run. **A run with any
SKIP is 77, never 0.**

## The hash

`seqHashMacro` (David's `dojo_gui.cpp`), transliterated in the check: FNV-1a 64
(basis `1469598103934665603`, prime `1099511628211`) folded over each frame's
`p1` then `p2` canon u16, frames parsed by `tas_macro::FromText`'s rules
(`core/dojo/tasmacro.cpp`: P1 `WSADZXCVBNM`, P2 `TGFHUIOJKLP`; `#` opens a
comment; a comment-only line is an annotation; a digits-only line is noise; `.`
forces a neutral frame). Proven against `library.json`'s own `fastVS.txt` entry.

## The measured fields, and the ones that were `unmeasured`

The base's machine hash and frame, the candidate's source and length, the PHASE,
and the result (found / candidate / delay / combo peak / after hash) are the
hunt's (`core/dojo/combohunt.cpp`, `dojo:ComboHunt`). They were written as
`"unmeasured"` and F2 listed them on every run until the hunt measured them
(`2026-09-17`, filled from its own RESULT line - MEASURED, not regenerated;
`measured_on` says when). A pin without a `measured_on` is a FAIL, by design; a
fresh run that disagrees with a pin is a FINDING, not a reason to regenerate.

The hunt's base is slot 0 of the tour's clip (David's V48 state, frame 9928,
in a match) - not the boot seeds, which reach the globe and not a fight. Its pin
is the machine hash AFTER the load on this build (`27FA5D20`), never the V48
file's digest. Phase: peak 19 on all four phases with four distinct end hashes -
this candidate is phase-insensitive on this base; the phase is pinned because the
rule stands, not because this candidate exercises it.
