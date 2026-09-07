// serialize.cpp : save states
#include "serialize.h"
#include "types.h"
#include "hw/aica/aica_if.h"
#include "hw/holly/sb.h"
#include "hw/flashrom/nvmem.h"
#include "hw/gdrom/gdrom_if.h"
#include "hw/maple/maple_cfg.h"
#include "hw/modem/modem.h"
#include "hw/pvr/pvr.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_mmr.h"
#include "reios/gdrom_hle.h"
#include "hw/naomi/naomi.h"
#include "hw/naomi/naomi_cart.h"
#include "hw/bba/bba.h"
#include "cfg/option.h"
#include "cfg/cfg.h"
#include "imgread/common.h"

void dc_serialize(Serializer& ser)
{
	// TAS desync harness: with -config dojo:VerifyState=yes, log each subsystem's start offset so the
	// idempotency probe's "first diff at offset N" maps to the exact culprit subsystem. Grep "SERMAP".
	// SERMAP is a DEBUGGING firehose (every subsystem offset, on every save AND load), so it has
	// its own key rather than riding on VerifyState, which is now always on.
	const bool mapLog = cfgLoadBool("dojo", "StateMapLog", false);

	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u aica",       (u32)ser.size());
	aica::serialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u sb",         (u32)ser.size());
	sb_serialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u nvmem",      (u32)ser.size());
	nvmem::serialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u gdrom",      (u32)ser.size());
	gdrom::serialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u maple",      (u32)ser.size());
	mcfg_SerializeDevices(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u pvr",        (u32)ser.size());
	pvr::serialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u sh4",        (u32)ser.size());
	sh4::serialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u bba_modem",  (u32)ser.size());
	ser << config::EmulateBBA.get();
	if (config::EmulateBBA)
		bba_Serialize(ser);
	ModemSerialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u sh4_2",      (u32)ser.size());
	sh4::serialize2(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u libGDR",     (u32)ser.size());
	libGDR_serialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u naomi",      (u32)ser.size());
	naomi_Serialize(ser);

	ser << config::Broadcast.get();
	ser << config::Cable.get();
	ser << config::Region.get();

	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u naomi_cart", (u32)ser.size());
	naomi_cart_serialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u gd_hle",     (u32)ser.size());
	gd_hle_state.Serialize(ser);
	if (mapLog) NOTICE_LOG(SAVESTATE, "SERMAP %10u END",        (u32)ser.size());

	DEBUG_LOG(SAVESTATE, "Saved %d bytes", (u32)ser.size());
}

static void dc_deserialize_libretro(Deserializer& deser)
{
	aica::deserialize(deser);

	sb_deserialize(deser);

	nvmem::deserialize(deser);

	gdrom::deserialize(deser);

	mcfg_DeserializeDevices(deser);

	pvr::deserialize(deser);

	sh4::deserialize(deser);

	if (deser.version() >= Deserializer::V13_LIBRETRO)
		deser.skip<bool>();		// settings.network.EmulateBBA
	config::EmulateBBA.override(false);

	ModemDeserialize(deser);

	sh4::deserialize2(deser);

	libGDR_deserialize(deser);

	deser.skip<u32>();	// FLASH_SIZE
	deser.skip<u32>();	// BBSRAM_SIZE
	deser.skip<u32>();	// BIOS_SIZE
	deser.skip<u32>();	// RAM_SIZE
	deser.skip<u32>();	// ARAM_SIZE
	deser.skip<u32>();	// VRAM_SIZE
	deser.skip<u32>();	// RAM_MASK
	deser.skip<u32>();	// ARAM_MASK
	deser.skip<u32>();	// VRAM_MASK

	naomi_Deserialize(deser);

	deser >> config::Broadcast.get();
	deser >> config::Cable.get();
	deser >> config::Region.get();

	naomi_cart_deserialize(deser);
	gd_hle_state.Deserialize(deser);

	DEBUG_LOG(SAVESTATE, "Loaded %d bytes (libretro compat)", (u32)deser.size());
}

void dc_deserialize(Deserializer& deser)
{
	if (deser.version() >= Deserializer::V9_LIBRETRO && deser.version() <= Deserializer::VLAST_LIBRETRO)
	{
		dc_deserialize_libretro(deser);
		sh4_sched_ffts();
		return;
	}
	DEBUG_LOG(SAVESTATE, "Loading state version %d", deser.version());

	aica::deserialize(deser);

	sb_deserialize(deser);

	nvmem::deserialize(deser);

	gdrom::deserialize(deser);

	mcfg_DeserializeDevices(deser);

	pvr::deserialize(deser);

	sh4::deserialize(deser);

	if (deser.version() >= Deserializer::V13)
		deser >> config::EmulateBBA.get();
	else
		config::EmulateBBA.override(false);
	if (config::EmulateBBA)
		bba_Deserialize(deser);
	ModemDeserialize(deser);

	sh4::deserialize2(deser);

	libGDR_deserialize(deser);

	naomi_Deserialize(deser);

	deser >> config::Broadcast.get();
	verify(config::Broadcast >= 0 && config::Broadcast <= 4);
	deser >> config::Cable.get();
	verify(config::Cable >= 0 && config::Cable <= 3);
	deser >> config::Region.get();
	verify(config::Region >= 0 && config::Region <= 3);

	naomi_cart_deserialize(deser);
	gd_hle_state.Deserialize(deser);
	sh4_sched_ffts();

	DEBUG_LOG(SAVESTATE, "Loaded %d bytes", (u32)deser.size());
}
