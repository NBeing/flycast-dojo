/*
	Which TAS panel has the user's attention, and at what zoom.

	ONE OWNER FOR ONE FACT. In David's fork this is five file-static variables
	and four functions scattered across `dojo_gui.cpp` - `tasSelectedKey`,
	`tasLastSelected`, `tasFocusNow`, `tasFocusPrev`, `tasLastClickT` - which
	every panel reads and writes directly. That file has 156 such statics, and
	`docs/MODULARIZATION.md` S1 is the bill for them.

	IT IS KEYED ON THE PANEL ID, not on a cfg-key string, and that is the one
	real change from his design. In his tree a window is identified by the
	string "InputVizUiScale", which does three jobs at once: the cfg key for its
	zoom, the window's identity for focus and selection, and - through a
	twelve-branch strcmp chain in `tasZoomKeyName` - its human label. The panel
	registry already holds identity and label (core/rend/panel.h), so:

	  * the strcmp chain becomes `panels::find(id)->label`
	  * "reset every window's zoom" becomes a loop over the registry instead of
	    a hand-maintained table of twelve cfg keys that has to be edited every
	    time a panel is added
	  * a panel that is registered cannot be missing from either

	The zoom cfg key is derived as `Zoom.<id>`. Nothing on our side reads the
	old per-window keys, so there is no migration to do here; a port of his
	saved settings would need one.
*/
#pragma once

namespace tas_ui
{

//! The panel that owns the menu bar and hotkeys right now, or nullptr for the
//! global bar. A panel id, resolvable through panels::find().
const char *selected();

//! Click-to-select. Panels call this when clicked; it also remembers the
//! previous selection so deselect() can toggle back to it.
void select(const char *panelId);

//! Back to the global bar, remembering what was selected. Calling it again
//! restores - that is the [MAIN] chip's toggle.
void deselect();

//! A panel declares it holds ImGui focus this frame. Used to detect focus
//! CHANGES, which is what catches title-bar and tab clicks that never produce
//! a click inside the window body.
void noteFocused(const char *panelId);

//! Did focus move between panels since last frame? Valid after endFrame().
bool focusChanged();

//! Rotate this frame's focus into last frame's. Call once per frame, after the
//! panels have drawn.
void endFrame();

//! Ctrl+wheel UI zoom for a panel. Call once immediately after its window
//! opens; returns the scale factor (0.6 - 1.4) for SetWindowFontScale. Also
//! does the click-to-select and draws the active-window outline, because those
//! three want the same "is this window hovered/focused" answer and splitting
//! them would ask ImGui the same question three times.
float zoom(const char *panelId, int defaultPercent = 100);

//! Reset every registered panel's zoom to its default. A loop over the
//! registry, not a table.
void resetAllZoom();

//! The human label for a panel id, for menus. Falls back to the id.
const char *labelFor(const char *panelId);

//! Exercise everything above that does not need an ImGui frame. Gated on
//! `dojo:PanelSelfTest`, alongside the panel registry's.
void selfTest();

}	// namespace tas_ui
