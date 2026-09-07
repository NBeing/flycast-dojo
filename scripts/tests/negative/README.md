# Tests that must NOT pass

Each of these is broken on purpose, and `scripts/testrun.sh --self-test` asserts
that the runner reports the RIGHT KIND of failure for each. The expected verdict
is the filename prefix: `fail_`, `timeout_`, `inconclusive_`.

They exist because a test harness that has never reported a failure is not known
to be able to. CLAUDE.md rule 1 is "every check must be able to fail; make it
fail once, on purpose, before you trust it" - these are that rule applied to the
runner itself, and they are the mechanism, not the intention.

| file | proves |
|---|---|
| `fail_assertion.lua` | a failed `check()` becomes FAIL, not PASS |
| `inconclusive_no_summary.lua` | printing `done` with no SUMMARY is INCONCLUSIVE. A test that never asserted anything must not read as success - the commonest way a suite lies |
| `timeout_never_finishes.lua` | a test that never finishes is TIMEOUT, and the runner still tears down |
| `timeout_done_substring.lua` | prints `baseline done, continuing`. Whole-line matching must not treat a line CONTAINING the marker as the end of the run - this exact bug turned a truncated run into a false pass in lemalta |
