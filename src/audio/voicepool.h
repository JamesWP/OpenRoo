/* Voice pools: a small round-robin bank of static sound buffers that all play
 * one sound, so it can overlap with itself (footsteps, impacts, a foe's
 * noises).  Playing a voice halts and re-triggers the current slot and
 * advances the cursor for next time. */
#pragma once

#include <windows.h>

#include "layout.h"
#include "static.h"

struct IDirectSound;

/* One pool.  The voices array is heap-allocated with a leading count word, and
 * destroyed through the voices' own vector destructor. */
struct __attribute__((packed)) VoicePool {
    static const int ORIGIN = 0;

    void               *logger;        // the logger the voices report to
    CStaticSoundbuffer *pBufs;         // dwVoiceCount voices, or NULL
    int                 dwCurrentIdx;  // the voice the next play uses
    int                 dwNestDepth;   // re-entrancy depth of Fill3D; Wipe leaves it
    int                 dwVoiceCount;

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

/* The voices are an array of CStaticSoundbuffer with a stride of 0x18. */
static_assert(sizeof(CStaticSoundbuffer) == 0x18, "pBufs stride must stay 0x18");

/* Halts and re-triggers the current voice, then advances the cursor.  Returns
 * the trigger's HRESULT, or DSERR_UNINITIALIZED with no voices. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolCycle(VoicePool *self, DWORD dwLoopFlags);

/* Zeroes the pool; returns it. */
extern "C" __declspec(dllexport) VoicePool * __attribute__((thiscall))
Sim_VoicePoolBlank(VoicePool *self);

/* Resets and frees every voice and clears the pool, except the nesting depth.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_VoicePoolWipe(VoicePool *self);

/* Loads count voices of one file: voice 0 from disk, the rest duplicated from
 * it (reloading any the duplicate refuses).  Returns 1, or 0 on failure. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolFill3D(VoicePool *self, int count, IDirectSound *pDS,
                    DWORD dwDsFlags, const char *filename, void *logger);

/* Fills the pool with count duplicates of src.  Non-NULL on success. */
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
Sim_VoicePoolClone(VoicePool *self, int count, IDirectSound *pDS,
                   CStaticSoundbuffer *src, int noFallback);

/* The voice at index, or NULL if out of range. */
extern "C" __declspec(dllexport) CStaticSoundbuffer * __attribute__((thiscall))
Sim_VoicePoolGetVoiceAt(VoicePool *self, int index);

/* Voice 0's file name, or NULL for an empty pool. */
extern "C" __declspec(dllexport) char * __attribute__((thiscall))
Sim_VoicePoolFirstFilename(VoicePool *self);

/* Positions every voice in 3D space. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_BroadcastPoolVoiceCoordinates(VoicePool *self,
                                  float x, float y, float z, DWORD dwApply);
