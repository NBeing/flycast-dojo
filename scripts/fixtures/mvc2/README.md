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

## Running

```
scripts/fixtures-check.sh                 # F1-F3 (no emulator) + F4 (boots the globe seed on a private Xvfb)
scripts/fixtures-check.sh --no-emu        # F1-F3 only
scripts/fixtures-check.sh --verify-roms   # the ROM by size + sha256
scripts/fixtures-check.sh --list-sabotage
scripts/fixtures-check.sh --sabotage hash | recipe | charselect     (--self-test == hash)
FIXTURES_REGENERATE=iknow scripts/fixtures-check.sh --regenerate   # no-emulator pins only, before/after printed
```

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

## The fields marked `unmeasured`

They are the hunt's (`core/dojo/combohunt.cpp`, `dojo:ComboHunt`): the base's
machine hash and frame, the candidate's source and length, the PHASE, and the
result (found / candidate / delay / combo peak / after hash) with `measured_on`.
F2 lists them on every run. A pin without a `measured_on` is a FAIL, by design.
