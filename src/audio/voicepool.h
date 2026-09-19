/* voicepool.cpp's exports -- the VoicePool leaves under UpdateEntityMovement.
 * Declared here so callers get them from the owner rather than redeclaring
 * them (COHESION_PLAN.md, template 10). */
#pragma once

#include <windows.h>

#include "layout.h"
#include "static.h"

struct IDirectSound;

/* VoicePool -- 20 bytes (0x14).  Moved here from voicepool.cpp so the layout
 * registrar is visible to `make check-layout`. */
struct __attribute__((packed)) VoicePool {
    static const int ORIGIN = 0;

    void               *logger;        // +0x00  opaque Logger*
    CStaticSoundbuffer *pBufs;         // +0x04  array of dwVoiceCount voices
    int                 dwCurrentIdx;  // +0x08  round-robin cursor
    int                 dwNestDepth;   // +0x0c  not touched by either method
    int                 dwVoiceCount;  // +0x10

private:
    KAROO_LAYOUT_REGISTER(VoicePool);
};

KAROO_LAYOUT_CHECKS(VoicePool)
{
    KAROO_LAYOUT_AT(logger,       0x00);
    KAROO_LAYOUT_AT(pBufs,        0x04);
    KAROO_LAYOUT_AT(dwCurrentIdx, 0x08);
    KAROO_LAYOUT_AT(dwNestDepth,  0x0c);
    KAROO_LAYOUT_AT(dwVoiceCount, 0x10);
    KAROO_LAYOUT_SIZE(0x14);
}

/* The disassembly indexes pBufs with LEA EAX,[EAX+EAX*2] then
 * LEA ECX,[ECX+EAX*8] -- a multiply by 24.  If CStaticSoundbuffer ever grew,
 * plain C++ pointer arithmetic would silently stop matching, so pin the
 * stride here where pBufs is declared.  (CStaticSoundbuffer's own size check
 * is KAROO_LAYOUT_SIZE(0x18) in static.h; this one is about the array.) */
static_assert(sizeof(CStaticSoundbuffer) == 0x18, "pBufs stride must stay 0x18");

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
