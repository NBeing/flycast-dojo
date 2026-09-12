# `scripts/tests/open` — tests for defects that are still open

A test in here **fails on purpose**, because the thing it asserts is true of the
emulator we want and false of the one we have. It is not a sabotage arm (those
live in `negative/`, and prove a JUDGE can say no); it is a claim we intend to
make good on, kept executable so that it cannot rot into a paragraph.

`scripts/testrun.sh` globs `scripts/tests/*.lua` and does not recurse, so these
do not turn the green suite red. Each is registered in `CMakeLists.txt` through
`scripts/openarm.sh`, which requires the NAMED claim to be the failing one and
nothing else to be red. That buys the property that matters:

**when someone fixes the defect, the arm goes red and says so in words.** A test
that quietly starts passing is how an `[OPEN]` becomes a stale doc; this one
demands that whoever fixed it come back and move the item.

`ctest`'s own `WILL_FAIL` was the first version and is not enough: it inverts
ANY non-zero exit, so a crash, a Lua error or a missing ROM read as "the defect
is present and correctly shaped" from a run that never reached the claim.

Run one directly:

    scripts/testrun.sh scripts/tests/open/<name>.lua
