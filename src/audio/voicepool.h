/* Voice pools: a small round-robin bank of static sound buffers that all play
 * one sound, so it can overlap with itself (footsteps, impacts, a foe's
 * noises).  Playing a voice halts and re-triggers the current slot and
 * advances the cursor for next time. */
#pragma once

#include <windows.h>

#include "static.h"

struct IDirectSound;

/* One pool.  The voices array is heap-allocated with a leading count word, and
 * destroyed through the voices' own vector destructor. */
struct VoicePool {

    void               *logger;        // the logger the voices report to
    CStaticSoundbuffer *pBufs;         // dwVoiceCount voices, or NULL
    int                 dwCurrentIdx;  // the voice the next play uses
    int                 dwNestDepth;   // re-entrancy depth of Fill3D; Wipe leaves it
    int                 dwVoiceCount;

private:
};

/* Halts and re-triggers the current voice, then advances the cursor.  Returns
 * the trigger's HRESULT, or DSERR_UNINITIALIZED with no voices. */
int Sim_VoicePoolCycle(VoicePool *self, DWORD dwLoopFlags);

/* Zeroes the pool; returns it. */
VoicePool *Sim_VoicePoolBlank(VoicePool *self);

/* Resets and frees every voice and clears the pool, except the nesting depth.
 */
void Sim_VoicePoolWipe(VoicePool *self);

/* Loads count voices of one file: voice 0 from disk, the rest duplicated from
 * it (reloading any the duplicate refuses).  Returns 1, or 0 on failure. */
int Sim_VoicePoolFill3D(VoicePool *self, int count, IDirectSound *pDS,
                        DWORD dwDsFlags, const char *filename, void *logger);

/* Fills the pool with count duplicates of src.  Non-NULL on success. */
void *Sim_VoicePoolClone(VoicePool *self, int count, IDirectSound *pDS,
                         CStaticSoundbuffer *src, int noFallback);

/* The voice at index, or NULL if out of range. */
CStaticSoundbuffer *Sim_VoicePoolGetVoiceAt(VoicePool *self, int index);

/* Voice 0's file name, or NULL for an empty pool. */
char *Sim_VoicePoolFirstFilename(VoicePool *self);

/* Positions every voice in 3D space. */
void Sim_BroadcastPoolVoiceCoordinates(VoicePool *self,
                                       float x, float y, float z, DWORD dwApply);
