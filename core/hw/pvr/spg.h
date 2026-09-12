#pragma once
#include "ta_ctx.h"

extern bool SH4FastEnough;

bool spg_Init();
void spg_Term();
void spg_Reset(bool Manual);
void spg_Serialize(Serializer& ser);
void spg_Deserialize(Deserializer& deser);
//! Repair a raster that restored descheduled. Call AFTER the whole machine has
//! deserialized - see the comment on the definition for why that matters.
void spg_RepairSchedule();
//! Did the last one actually repair something? For reports that would otherwise
//! blame the serializer for a change this code made on purpose.
bool spg_ScheduleWasRepaired();

void CalculateSync();
void read_lightgun_position(int x, int y);
void scheduleRenderDone(TA_context *cntx);
void rescheduleSPG();
