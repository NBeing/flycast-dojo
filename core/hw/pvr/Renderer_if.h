#pragma once
#include "types.h"
#include "ta_ctx.h"

extern u32 FrameCount;

bool rend_init_renderer();
void rend_term_renderer();
void rend_vblank();
void rend_start_render();
int rend_end_render(int tag, int cycles, int jitter, void *arg);
void rend_cancel_emu_wait();
bool rend_single_frame(const bool& enabled);
void rend_swap_frame(u32 fb_r_sof1);
void rend_set_fb_write_addr(u32 fb_w_sof1);
void rend_reset();
void rend_disable_rollback();
void rend_start_rollback();
void rend_allow_rollback();
void rend_enable_renderer(bool enabled);
bool rend_is_enabled();
void rend_serialize(Serializer& ser);
void rend_deserialize(Deserializer& deser);

///////
extern TA_context* _pvrrc;

#define pvrrc (_pvrrc->rend)

struct FramebufferInfo
{
	void update()
	{
		fb_r_size.full = FB_R_SIZE.full;
		fb_r_ctrl.full = FB_R_CTRL.full;
		spg_control.full = SPG_CONTROL.full;
		spg_status.full = SPG_STATUS.full;
		fb_r_sof1 = FB_R_SOF1;
		fb_r_sof2 = FB_R_SOF2;
		vo_control.full = VO_CONTROL.full;
		vo_border_col.full = VO_BORDER_COL.full;
	}

	FB_R_SIZE_type fb_r_size;
	FB_R_CTRL_type fb_r_ctrl;
	SPG_CONTROL_type spg_control;
	SPG_STATUS_type spg_status;
	u32 fb_r_sof1;
	u32 fb_r_sof2;
	VO_CONTROL_type vo_control;
	VO_BORDER_COL_type vo_border_col;
};

struct Renderer
{
	virtual ~Renderer() = default;

	virtual bool Init() = 0;
	virtual void Term() = 0;

	virtual void Process(TA_context *ctx) = 0;
	virtual bool Render() = 0;
	virtual void RenderFramebuffer(const FramebufferInfo& info) = 0;
	virtual bool RenderLastFrame() { return false; }

	virtual bool Present() { return true; }

	virtual void DrawOSD(bool clear_screen) { }

	virtual BaseTextureCacheData *GetTexture(TSP tsp, TCW tcw) { return nullptr; }

	//! The last presented frame, as something the UI can draw.
	//!
	//! THE GAME IS A PANEL, NOT A HOLE. The frame is already a texture by the
	//! time it reaches the screen - every backend renders into an offscreen
	//! buffer and blits it last - so handing that texture to the UI lets the
	//! picture be an ImGui window that docks, splits and resizes like any
	//! other, instead of a full-window blit that docked panels have to be
	//! letterboxed around. fbneo-rr's architecture review reached the same
	//! conclusion: "the emulated frame is already a texture, so make ImGui the
	//! whole frontend."
	//!
	//! `handle` is the NATIVE handle, widened rather than typed, so this header
	//! does not have to include imgui.h - the UI layer casts it back to
	//! ImTextureID, which is what it means on every backend ImGui supports.
	//! A zero handle means this backend does not publish one yet, and the
	//! caller must fall back to the blit.
	struct FrameTexture
	{
		uintptr_t handle = 0;
		float aspectRatio = 0.f;	//!< the picture's, for the fit
		bool yUp = false;			//!< row 0 is the BOTTOM of the image (GL)
	};
	virtual FrameTexture GetFrameTexture() { return {}; }
};

extern Renderer* renderer;

extern u32 fb_watch_addr_start;
extern u32 fb_watch_addr_end;
extern bool fb_dirty;

void check_framebuffer_write();
