# Per-frame tracers over a clip through the control server

Throwaway-grade but kept: each boots a fixture clip in a sandbox (`probe_david.sh`'s shape:
Replay + AutoSeekState, VMU staged, `audio:backend=null`), pauses, steps EXACTLY to a frame,
then reads guest RAM per frame through ctlserver `read` at SPREADSHEET addresses.

- `probe_david.sh <slot>` - seek a slot, read the six ID_2 + combo, screenshot.
- `trace_david.sh <slot> <from> <to>` - per-frame: combo, all three P1 slots' state/attack, P2 state, skip toggle, distance.
- `trace_storm.sh` - Storm (P1 slot B) animation value/timer/flags per frame.
- `trace_objcount.sh` - the live OBJECT COUNTS (0x2C287DDE/DF) per frame - the one that found the finding.
- `dump_objs.sh <slot> <from> <to>` - every live object's pointer, arena X/Y, state, attack, facing at two frames.

`[2026-09-20]` these found docs/HYPER-OBJECTS.md: David's 94-hit Team Hyper lands 70 here because
Storm's Hail Storm shard list reads EMPTY on alternate frames from ~16410 and the hits stop.
