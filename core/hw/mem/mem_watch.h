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
#include "types.h"
#include "_vmem.h"
#include "hw/aica/aica_if.h"
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/pvr/elan.h"
#include "rend/TexCache.h"
#include <cstring>
#include <memory>
#include <vector>

namespace memwatch
{

/*
 * Pages captured during one frame, in the order they were first written.
 *
 * This used to be an unordered_map<u32, Page> whose Page held an inline
 * PAGE_SIZE array, so every newly dirtied page cost a hash-node allocation of
 * over 4 KB - inside the SIGSEGV handler, where malloc is not
 * async-signal-safe. The restore path only ever iterates these, never looks a
 * page up, so the map bought nothing for the cost.
 *
 * Capture now writes into a slab sized once to the whole region, with a bitmap
 * marking which pages are already held, so the fault path performs no
 * allocation at all.
 */
struct PageList
{
	std::vector<u32> offsets;
	std::vector<u8> data;		// offsets.size() * PAGE_SIZE bytes

	u32 size() const { return (u32)offsets.size(); }
	u32 offsetAt(u32 i) const { return offsets[i]; }
	const u8 *dataAt(u32 i) const { return &data[(size_t)i * PAGE_SIZE]; }
	void clear() { offsets.clear(); data.clear(); }
};
// Legacy name, still used by the rollback code.
using PageMap = PageList;

template<typename T>
class Watcher
{
	bool started = false;

	// Capture buffers, sized once to the whole region. Untouched slab pages are
	// never faulted in, so the reservation costs address space rather than RSS.
	std::unique_ptr<u8[]> slab;
	std::unique_ptr<u32[]> offsets;
	std::unique_ptr<u32[]> bitmap;	// one bit per page index
	u32 maxPages = 0;
	u32 count = 0;

	u32 bitmapWords() const { return (maxPages + 31) / 32; }

	void clearBitmap()
	{
		if (bitmap)
			memset(bitmap.get(), 0, (size_t)bitmapWords() * sizeof(u32));
	}

	// Region sizes are runtime settings, so this runs at the first protect()
	// rather than at construction. Never called from the fault handler.
	void initCapture()
	{
		const u32 memSize = static_cast<T&>(*this).getMemSize();
		const u32 pages = (memSize + PAGE_SIZE - 1) / PAGE_SIZE;
		if (pages == maxPages)
			return;
		maxPages = pages;
		slab.reset(new u8[(size_t)pages * PAGE_SIZE]);
		offsets.reset(new u32[pages]);
		bitmap.reset(new u32[bitmapWords()]);
		count = 0;
		clearBitmap();
	}

public:
	void protect()
	{
		initCapture();
		if (!started)
		{
			static_cast<T&>(*this).protectMem(0, 0xffffffff);
			started = true;
		}
		else
		{
			for (u32 i = 0; i < count; i++)
				static_cast<T&>(*this).protectMem(offsets[i], PAGE_SIZE);
		}
	}

	void unprotect()
	{
		static_cast<T&>(*this).unprotectMem(0, 0xffffffff);
	}

	void reset()
	{
		started = false;
		count = 0;
		clearBitmap();
	}

	// Runs in the SIGSEGV handler: no allocation, no locks.
	bool hit(void *addr)
	{
		u32 offset = static_cast<T&>(*this).getMemOffset(addr);
		if (offset == (u32)-1)
			return false;
		offset &= ~PAGE_MASK;
		const u32 idx = offset / PAGE_SIZE;
		if (idx >= maxPages)
			// Not sized yet, so this region is not being watched.
			return false;

		u32& word = bitmap[idx >> 5];
		const u32 bit = 1u << (idx & 31);
		if (word & bit)
			// already saved
			return true;
		word |= bit;

		offsets[count] = offset;
		memcpy(&slab[(size_t)count * PAGE_SIZE],
				static_cast<T&>(*this).getMemPage(offset), PAGE_SIZE);
		count++;

		static_cast<T&>(*this).unprotectMem(offset, PAGE_SIZE);
		return true;
	}

	// Hands this frame's pages over and starts a new frame. Compacts into
	// exactly-sized buffers so a stored frame costs what it actually dirtied,
	// rather than holding a whole-region reservation.
	void getPages(PageList& other)
	{
		other.offsets.assign(offsets.get(), offsets.get() + count);
		other.data.resize((size_t)count * PAGE_SIZE);
		if (count != 0)
			memcpy(other.data.data(), slab.get(), (size_t)count * PAGE_SIZE);
		count = 0;
		clearBitmap();
	}
};

class VramWatcher : public Watcher<VramWatcher>
{
	friend class Watcher<VramWatcher>;

protected:
	void protectMem(u32 addr, u32 size)
	{
		_vmem_protect_vram(addr, std::min(VRAM_SIZE - addr, size) & ~PAGE_MASK);
	}

	void unprotectMem(u32 addr, u32 size)
	{
		_vmem_unprotect_vram(addr, std::min(VRAM_SIZE - addr, size) & ~PAGE_MASK);
	}

	u32 getMemOffset(void *p)
	{
		return _vmem_get_vram_offset(p);
	}

	u32 getMemSize() { return VRAM_SIZE; }

public:
	void *getMemPage(u32 addr)
	{
		return &vram[addr];
	}
};

class RamWatcher : public Watcher<RamWatcher>
{
	friend class Watcher<RamWatcher>;

protected:
	void protectMem(u32 addr, u32 size)
	{
		bm_LockPage(addr, std::min(RAM_SIZE - addr, size) & ~PAGE_MASK);
	}

	void unprotectMem(u32 addr, u32 size)
	{
		bm_UnlockPage(addr, std::min(RAM_SIZE - addr, size) & ~PAGE_MASK);
	}

	u32 getMemOffset(void *p)
	{
		return bm_getRamOffset(p);
	}

	u32 getMemSize() { return RAM_SIZE; }

public:
	void *getMemPage(u32 addr)
	{
		return &mem_b[addr];
	}
};

class AicaRamWatcher : public Watcher<AicaRamWatcher>
{
	friend class Watcher<AicaRamWatcher>;

protected:
	void protectMem(u32 addr, u32 size);
	void unprotectMem(u32 addr, u32 size);
	u32 getMemOffset(void *p);
	u32 getMemSize() { return ARAM_SIZE; }

public:
	void *getMemPage(u32 addr)
	{
		return &aica_ram[addr];
	}
};

class ElanRamWatcher : public Watcher<ElanRamWatcher>
{
	friend class Watcher<ElanRamWatcher>;

protected:
	void protectMem(u32 addr, u32 size);
	u32 getMemOffset(void *p);
	u32 getMemSize() { return elan::ERAM_SIZE; }

public:
	void unprotectMem(u32 addr, u32 size);
	void *getMemPage(u32 addr)
	{
		return &elan::RAM[addr];
	}
};

extern VramWatcher vramWatcher;
extern RamWatcher ramWatcher;
extern AicaRamWatcher aramWatcher;
extern ElanRamWatcher elanWatcher;

inline static bool writeAccess(void *p)
{
	if (!config::GGPOEnable)
		return false;
	if (ramWatcher.hit(p))
	{
		bm_RamWriteAccess(p);
		return true;
	}
	if (vramWatcher.hit(p))
	{
		VramLockedWrite((u8 *)p);
		return true;
	}
	if (settings.platform.isNaomi2() && elanWatcher.hit(p))
		return true;
	return aramWatcher.hit(p);
}

inline static void protect()
{
	if (!config::GGPOEnable)
		return;
	vramWatcher.protect();
	ramWatcher.protect();
	aramWatcher.protect();
	elanWatcher.protect();
}

inline static void unprotect()
{
	vramWatcher.unprotect();
	ramWatcher.unprotect();
	aramWatcher.unprotect();
	elanWatcher.unprotect();
}

inline static void reset()
{
	vramWatcher.reset();
	ramWatcher.reset();
	aramWatcher.reset();
	elanWatcher.reset();
}

}
