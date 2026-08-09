/*
	Copyright 2021 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "dxcontext.h"
#include "d3d_renderer.h"
#include "rend/osd.h"
#ifdef USE_SDL
#include "sdl/sdl.h"
#endif
#include "hw/pvr/Renderer_if.h"
#include "emulator.h"
#include "dx9_driver.h"
#include "imgui_impl_dx9.h"
#include "rend/video_recorder.h"
#include <vector>

DXContext theDXContext;

bool DXContext::init(bool keepCurrentWindow)
{
	NOTICE_LOG(RENDERER, "DX9 Context initializing");
	GraphicsContext::instance = this;
#ifdef USE_SDL
	if (!keepCurrentWindow && !sdl_recreate_window(0))
		return false;
#endif

	pD3D.reset(Direct3DCreate9(D3D_SDK_VERSION));
	if (!pD3D) {
		ERROR_LOG(RENDERER, "Direct3DCreate9 failed");
		return false;
	}
	memset(&d3dpp, 0, sizeof(d3dpp));
	d3dpp.hDeviceWindow = (HWND)window;
	d3dpp.Windowed = true;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
	d3dpp.EnableAutoDepthStencil = FALSE;						// No need for depth/stencil buffer for the backbuffer
	swapOnVSync = !settings.input.fastForwardMode && config::VSync;
	if (swapOnVSync)
	{
		switch ((int)(settings.display.refreshRate / 60))
		{
		case 0:
		case 1:
			d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
			break;
		case 2:
			d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_TWO;
			break;
		case 3:
			d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_THREE;
			break;
		case 4:
		default:
			d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_FOUR;
			break;
		}
	}
	else
		d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	// TODO should be 0 in windowed mode
	//d3dpp.FullScreen_RefreshRateInHz = swapOnVSync ? 60 : 0;
	HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, (HWND)window,
			D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &pDevice.get());
	if (FAILED(hr))
	{
		ERROR_LOG(RENDERER, "DirectX9 device creation failed: %x", hr);
	    return false;
	}
	imguiDriver = std::unique_ptr<ImGuiDriver>(new DX9Driver(pDevice));
	overlay.init(pDevice);

	D3DADAPTER_IDENTIFIER9 id;
	pD3D->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
	driverName = std::string(id.Description);
	driverVersion = std::to_string(id.DriverVersion.HighPart >> 16) + "." + std::to_string((u16)id.DriverVersion.HighPart)
		+ "." + std::to_string(id.DriverVersion.LowPart >> 16) + "." + std::to_string((u16)id.DriverVersion.LowPart);
	deviceReady = true;

	return true;
}

void DXContext::term()
{
	NOTICE_LOG(RENDERER, "DX9 Context terminating");
	videorec::stop();
	TermCapture();
	GraphicsContext::instance = nullptr;
	overlay.term();
	imguiDriver.reset();
	pDevice.reset();
	pD3D.reset();
	deviceReady = false;
}

void DXContext::TermCapture()
{
	captureSurface.reset();
}

bool DXContext::CreateCaptureResources()
{
	ComPtr<IDirect3DSurface9> backBuffer;
	if (FAILED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer.get())))
	{
		ERROR_LOG(RENDERER, "[rec] could not get back buffer");
		return false;
	}
	D3DSURFACE_DESC desc;
	if (FAILED(backBuffer->GetDesc(&desc)))
		return false;

	// GetRenderTargetData requires a system-memory surface of matching size
	// and format.
	if (FAILED(pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
			D3DPOOL_SYSTEMMEM, &captureSurface.get(), nullptr)))
	{
		ERROR_LOG(RENDERER, "[rec] could not create capture surface");
		return false;
	}
	return true;
}

/*
 * Video capture hook.
 *
 * Runs just before IDirect3DDevice9::Present, so the back buffer holds the
 * composited frame: game, OSD, and anything Lua drew through ImGui.
 *
 * D3D9 offers no asynchronous readback, so GetRenderTargetData() is a
 * synchronous copy and Lock() blocks until it lands. Unlike the GL, Vulkan and
 * DX11 paths there is no overlap here; capture costs a stall per frame.
 */
void DXContext::DoSwapCapture()
{
	if (videorec::stopPending())
	{
		videorec::stop();
		TermCapture();
	}

	if (videorec::startPending())
	{
		int w = settings.display.width;
		int h = settings.display.height;
		ComPtr<IDirect3DSurface9> backBuffer;
		if (SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer.get())))
		{
			D3DSURFACE_DESC desc;
			if (SUCCEEDED(backBuffer->GetDesc(&desc)))
			{
				w = desc.Width;
				h = desc.Height;
			}
		}
		// D3DFMT_X8R8G8B8 is BGRX in memory order; rows are already top-down.
		if (!videorec::start(w, h, videorec::PixelFormat::BGRA32, false))
			return;
	}

	if (!videorec::isRecording() || !pDevice)
		return;

	ComPtr<IDirect3DSurface9> backBuffer;
	if (FAILED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer.get())))
		return;
	D3DSURFACE_DESC desc;
	if (FAILED(backBuffer->GetDesc(&desc)))
		return;
	if ((int)desc.Width != videorec::width() || (int)desc.Height != videorec::height())
	{
		WARN_LOG(RENDERER, "[rec] back buffer resized to %ux%u (recording %dx%d), stopping",
				desc.Width, desc.Height, videorec::width(), videorec::height());
		videorec::requestStop();
		return;
	}

	// The surface is released on device loss/reset, so reallocate lazily.
	if (!captureSurface && !CreateCaptureResources())
	{
		videorec::requestStop();
		return;
	}

	if (FAILED(pDevice->GetRenderTargetData(backBuffer, captureSurface)))
		return;

	D3DLOCKED_RECT locked;
	if (SUCCEEDED(captureSurface->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
	{
		const size_t rowBytes = (size_t)videorec::width() * 4;
		std::vector<u8> frame(videorec::frameBytes());
		const u8 *src = (const u8 *)locked.pBits;
		// Pitch can exceed the visible row width.
		for (int y = 0; y < videorec::height(); y++)
			memcpy(&frame[y * rowBytes], src + (size_t)y * locked.Pitch, rowBytes);
		captureSurface->UnlockRect();
		videorec::submitFrame(std::move(frame));
	}
}

void DXContext::Present()
{
	if (!frameRendered)
		return;
	if (!pDevice)
	{
		if (init(true))
		{
			renderer = new D3DRenderer();
			rend_init_renderer();
		}
		return;
	}
	DoSwapCapture();
	HRESULT result = pDevice->Present(NULL, NULL, NULL, NULL);
	// Handle loss of D3D9 device
	if (result == D3DERR_DEVICELOST)
	{
		deviceReady = false;
		result = pDevice->TestCooperativeLevel();
		if (result == D3DERR_DEVICENOTRESET)
			resetDevice();
	}
	else if (FAILED(result))
		WARN_LOG(RENDERER, "Present failed %x", result);
	else
	{
		frameRendered = false;
		if (swapOnVSync != (!settings.input.fastForwardMode && config::VSync))
		{
			DEBUG_LOG(RENDERER, "Switch vsync %d", !swapOnVSync);
			if (renderer != nullptr)
			{
				renderer->Term();
				delete renderer;
				renderer = nullptr;
			}
			term();
			if (init(true))
			{
				renderer = new D3DRenderer();
				rend_init_renderer();
			}
			else
			{
				deviceReady = false;
			}
		}
	}
}

void DXContext::EndImGuiFrame()
{
	if (deviceReady)
	{
		verify((bool)pDevice);
		pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
		pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		if (!overlayOnly)
		{
			pDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_RGBA(0, 0, 0, 255), 1.0f, 0);
			if (renderer != nullptr)
				renderer->RenderLastFrame();
		}
		if (SUCCEEDED(pDevice->BeginScene()))
		{
			if (overlayOnly)
			{
				if (crosshairsNeeded() || config::FloatVMUs)
					overlay.draw(settings.display.width, settings.display.height, config::FloatVMUs, true);
			}
			else
			{
				overlay.draw(settings.display.width, settings.display.height, true, false);
			}
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			pDevice->EndScene();
		}
	}
	frameRendered = true;
}

void DXContext::resize()
{
	if (!pDevice)
		return;
	RECT rect;
	GetClientRect((HWND)window, &rect);
	d3dpp.BackBufferWidth = settings.display.width = rect.right;
	d3dpp.BackBufferHeight = settings.display.height = rect.bottom;
	if (settings.display.width == 0 || settings.display.height == 0)
		// window minimized
		return;
	resetDevice();
}

void DXContext::resetDevice()
{
	D3DRenderer *dxrenderer{};
	if (renderer != nullptr)
		dxrenderer = dynamic_cast<D3DRenderer*>(renderer);
	if (dxrenderer != nullptr)
		dxrenderer->preReset();
	// Reallocated lazily on the next captured frame.
	TermCapture();
	overlay.term();
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = pDevice->Reset(&d3dpp);
    if (FAILED(hr))
    {
        ERROR_LOG(RENDERER, "DX9 device reset failed: %x", hr);
        deviceReady = false;
        return;
    }
    deviceReady = true;
    ImGui_ImplDX9_CreateDeviceObjects();
    overlay.init(pDevice);
	if (dxrenderer != nullptr)
		dxrenderer->postReset();
}
