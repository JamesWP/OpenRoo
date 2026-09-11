/* voicepool.cpp's exports -- the VoicePool leaves under UpdateEntityMovement.
 * Declared here so callers get them from the owner rather than redeclaring
 * them (COHESION_PLAN.md, template 10). */
#pragma once

#include <windows.h>

struct VoicePool;

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolCycle(VoicePool *self, DWORD dwLoopFlags);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_BroadcastPoolVoiceCoordinates(VoicePool *self,
                                  float x, float y, float z, DWORD dwApply);
