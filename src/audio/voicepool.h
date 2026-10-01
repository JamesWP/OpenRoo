/* Voice pools: a small round-robin bank of static sound buffers that all play
 * one sound, so it can overlap with itself (footsteps, impacts, a foe's
 * noises).  Playing a voice halts and re-triggers the current slot and
 * advances the cursor for next time. */
#pragma once

#include "audiodev.h"

class VoicePool {
public:
    /* Halts and re-triggers the current voice, then advances the cursor.  Does
     * nothing with no voices. */
    void cycle(bool loop = false);

    /* An empty pool.  The destructor wipes it. */
    VoicePool();
    ~VoicePool();
    VoicePool(const VoicePool &) = delete;
    VoicePool &operator=(const VoicePool &) = delete;

    /* Resets and frees every voice and clears the pool. */
    void wipe();

    /* Fills the pool with count duplicates of src.  False on failure, with the
     * pool left empty. */
    bool clone(int count, audiodev::Device &dev, const audiodev::Buffer &src);

    /* The voice at index, or NULL if out of range. */
    audiodev::Buffer *voiceAt(int index);

    /* Voice 0's file name, or NULL for an empty pool. */
    const char *firstFilename();

    /* Positions every voice in 3D space. */
    void broadcastCoordinates(float x, float y, float z, bool immediate = true);

    int voiceCount() const { return voiceCount_; }

private:
    audiodev::Buffer *bufs_;        // voiceCount_ voices, or NULL
    int               currentIdx_;  // the voice the next play uses
    int               voiceCount_;
};
