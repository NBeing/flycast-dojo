#pragma once
// `[PORTED 2026-09-15]` from dev's 0915 tree (test-infra adoption, not a pin bump):
// the live control plane the flycast-test MCP drives. Byte-identical to his modulo
// David->dev; the lone tas_roi mention was a comment (that module is unported here).

// tas_ctl - the live control server (Step 3, slice 1). An external process (the flycast-test MCP)
// drives an ALREADY-RUNNING emulator by writing a JSON command to <ctlDir>/_ctl/cmd.json; the emulator
// polls that file's mtime once per rendered frame from mainui_rend_frame - on the single render/UI thread,
// so every verb runs in-process with NO fake keyboard input and NO locks (SH4 + GUI + hotkeys share that
// one thread; rend.ThreadedRendering=no). The answer goes to <ctlDir>/_ctl/resp.json. A monotonic "seq"
// gives synchronous request/response: the client writes cmd (seq N) and polls resp until resp.seq == N.
//
//   cmd.json : { "seq": 42, "verb": "step", "args": { "n": 1 } }
//   resp.json: { "seq": 42, "ok": true, "frame": 4312, "mode": "WRITE", "error": null }
//
// This slice is the CONTROL PLANE only - verbs: query, read, step, save, load. Mode-setting, advance_until
// and input/piano-roll writes are later slices. Hard no-op unless dojo:ControlServer=yes, so it perturbs
// nothing on normal runs or headless tests. Transport pattern mirrors tas_roi's clip.json hot-reload.

namespace tas_ctl
{
void tick();     // once per rendered frame from mainui_rend_frame (render/UI thread); no-op when disabled
}
