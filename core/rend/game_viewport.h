/*
	Where the emulator's picture lives inside the host window.

	THE IDEA (this is a portable interface concept, not a flycast detail):

	  window  >=  content area  >=  game viewport

	  * The WINDOW is the whole drawable surface (settings.display.*).
	  * The CONTENT AREA is the part of it the UI leaves for the emulator. With
	    no tool windows it is the whole window; with docked tools it is the
	    dockspace's central node. The UI publishes it; nothing else may.
	  * The GAME VIEWPORT is the content area with the picture's aspect ratio
	    letterboxed into it. It is a pure function of (content area, aspect) -
	    derived, never stored, so it cannot go stale.

	Two owners, one derivation: the UI owns the AREA, the renderer owns the
	ASPECT, and everyone downstream - the GL/DX presents, the Lua scripting
	layer, overlays that draw in game pixels - reads the same gameViewport()
	instead of re-deriving it from the window. Before this existed the same
	letterbox arithmetic was written out twice (gldraw.cpp and lua.cpp) against
	the raw window size, so docking broke both at once: the picture kept drawing
	full-window underneath the docked panels.

	Coordinates are WINDOW PIXELS with a TOP-LEFT origin, matching ImGui and the
	Lua API. GL's bottom-left origin is a conversion at the call site
	(y_gl = display.height - (y + h)), deliberately not hidden here.
*/
#pragma once
#include <cstdint>

namespace rend
{

struct ViewportRect
{
	int x = 0, y = 0, w = 0, h = 0;
};

//! Publish the area the UI leaves for the picture. Pass a degenerate rect
//! (w or h <= 0) to mean "no reservation - the whole window". Called from the
//! UI layer once per frame; safe to call every frame with the same value.
void setContentArea(int x, int y, int w, int h);

//! The current content area, already resolved against the window: never
//! degenerate, never larger than the window.
ViewportRect contentArea();

//! The content area with `pictureAspectRatio` letterboxed into it. Renderers
//! pass their own aspect (GL's may differ from the config-derived one when it
//! is presenting an output framebuffer).
ViewportRect gameViewport(float pictureAspectRatio);

//! gameViewport() using the config-derived aspect ratio that every backend
//! shares (getDCFramebufferAspectRatio). For consumers that have no renderer -
//! the Lua API, overlays - and correct for all of them.
ViewportRect gameViewport();

//! Is the picture being drawn as a dockable PANEL rather than blitted behind
//! the UI? Renderers ask before presenting: when this is true the panel owns
//! the picture and the present only paints the ground under the dockspace.
//!
//! WHY THE TWO MODES COEXIST. The panel needs the backend to publish its frame
//! as a texture (Renderer::GetFrameTexture), which not every backend does yet,
//! so the blit stays as the fallback rather than as a legacy path to delete.
//! `dojo:GamePanel` turns it on; a backend that publishes no texture falls back
//! on its own, without the config having to know which backend is running.
bool gamePanelActive();
void setGamePanelActive(bool active);

//! Does the USER want the game as a panel? Distinct from gamePanelActive(),
//! and the distinction is load-bearing: the panel needs the backend's offscreen
//! buffer to be a sampleable TEXTURE rather than a renderbuffer, and that
//! decision is made when the buffer is allocated - before any panel could have
//! drawn. Keying the allocation off "active" would deadlock: no texture, so no
//! panel, so never active, so never a texture. This is the intent; the other is
//! the outcome.
bool gamePanelWanted();
void setGamePanelWanted(bool wanted);

}	// namespace rend
