/* voicepool.cpp's exports -- the VoicePool leaves under UpdateEntityMovement.
 * Declared here so callers get them from the owner rather than redeclaring
 * them (COHESION_PLAN.md, template 10). */
#pragma once

#include <windows.h>

struct VoicePool;
struct CStaticSoundbuffer;
struct IDirectSound;

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolCycle(VoicePool *self, DWORD dwLoopFlags);
extern "C" __declspec(dllexport) VoicePool * __attribute__((thiscall))
Sim_VoicePoolBlank(VoicePool *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_VoicePoolWipe(VoicePool *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolFill3D(VoicePool *self, int count, IDirectSound *pDS,
                    DWORD dwDsFlags, const char *filename, void *logger);
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
Sim_VoicePoolClone(VoicePool *self, int count, IDirectSound *pDS,
                   CStaticSoundbuffer *src, int noFallback);
extern "C" __declspec(dllexport) CStaticSoundbuffer * __attribute__((thiscall))
Sim_VoicePoolGetVoiceAt(VoicePool *self, int index);
extern "C" __declspec(dllexport) char * __attribute__((thiscall))
Sim_VoicePoolFirstFilename(VoicePool *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_BroadcastPoolVoiceCoordinates(VoicePool *self,
                                  float x, float y, float z, DWORD dwApply);
