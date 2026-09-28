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
class __attribute__((packed)) VoicePool {
public:
    static const int ORIGIN = 0;

    /* Halts and re-triggers the current voice, then advances the cursor.
     * Returns the trigger's HRESULT, or DSERR_UNINITIALIZED with no voices. */
    int cycle(DWORD dwLoopFlags);

    /* Zeroes the pool; returns it. */
    VoicePool *blank();

    /* Resets and frees every voice and clears the pool, except the nesting
     * depth. */
    void wipe();

    /* Loads count voices of one file: voice 0 from disk, the rest duplicated
     * from it (reloading any the duplicate refuses).  Returns 1, or 0 on
     * failure. */
    int fill3D(int count, IDirectSound *pDS, DWORD dwDsFlags, const char *filename, void *logger);

    /* Fills the pool with count duplicates of src.  Non-NULL on success. */
    void *clone(int count, IDirectSound *pDS, CStaticSoundbuffer *src, int noFallback);

    /* The voice at index, or NULL if out of range. */
    CStaticSoundbuffer *voiceAt(int index);

    /* Voice 0's file name, or NULL for an empty pool. */
    char *firstFilename();

    /* Positions every voice in 3D space. */
    void broadcastCoordinates(float x, float y, float z, DWORD dwApply);

    void               *logger() const { return logger_; }
    int voiceCount() const { return dwVoiceCount_; }

private:
    void               *logger_;        // the logger the voices report to
    CStaticSoundbuffer *pBufs_;         // dwVoiceCount voices, or NULL
    int                 dwCurrentIdx_;  // the voice the next play uses
    int                 dwNestDepth_;   // re-entrancy depth of Fill3D; Wipe leaves it
    int                 dwVoiceCount_;
    KAROO_LAYOUT_REGISTER(VoicePool);
};

KAROO_LAYOUT_CHECKS(VoicePool)
{
    KAROO_LAYOUT_AT(logger_,       0x00);
    KAROO_LAYOUT_AT(pBufs_,        0x04);
    KAROO_LAYOUT_AT(dwCurrentIdx_, 0x08);
    KAROO_LAYOUT_AT(dwNestDepth_,  0x0c);
    KAROO_LAYOUT_AT(dwVoiceCount_, 0x10);
    KAROO_LAYOUT_SIZE(0x14);
}

/* The voices are an array of CStaticSoundbuffer with a stride of 0x18. */
static_assert(sizeof(CStaticSoundbuffer) == 0x18, "pBufs stride must stay 0x18");

