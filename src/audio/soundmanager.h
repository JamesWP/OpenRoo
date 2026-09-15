/* SoundManager -- the game's sound asset service, embedded in Game at
 * +0x13cba8 (Game::soundManager()).
 *
 * PLACEHOLDER CLASS.  Every method below still calls the game's original at
 * its fixed address; nothing here is reimplemented yet.  It exists so that
 * callers already speak to the interface: when a method is replaced, only
 * its body in soundmanager.cpp changes, never a call site.
 *
 * What is known of the layout (from ReleaseStaticSoundBufferForOwner):
 *   +0x04  Logger *        -- its error line goes through this
 *   +0x94  sound-entry list, searched first
 *   +0xa4  sound-entry list, searched second
 * and from SoundSetup 0x4439d0: +0x14 is an embedded CFaktSound (Ghidra,
 * 0x78 bytes) whose IDirectSound is its +0x0c, so the device is +0x20.
 * Nothing is declared as a member until a method of ours reads it.
 */
#pragma once

#include "layout.h"

struct CStaticSoundbuffer;
struct VoicePool;
struct IDirectSound;

class __attribute__((packed)) SoundManager {
public:
    static const int ORIGIN = 0;

    /* 0x004439d0 SoundSetup(mode_3d).  Returns 0 if sound is not created
     * or the 3D listener fails; otherwise, on a mode change, reloads every
     * buffer, clone and voice pool for the new mode.  Returns 1. */
    int setup(int mode3d);

    /* The IDirectSound (CFaktSound +0x0c); the script player keeps a copy. */
    IDirectSound *directSound() const { return directSound_; }

    /* 0x004432f0 ReleaseStaticSoundBufferForOwner.  Finds the buffer in
     * either entry list; with bDestroyIfUnused and no other owner left it
     * removes the entry and frees the buffer.  Logs if the buffer is in
     * neither list. */
    void releaseStaticForOwner(void *buffer, int bDestroyIfUnused);

    /* 0x00443400 -- the voice-pool counterpart, same argument shape. */
    void releasePooledForOwner(void *buffer, int bDestroyIfUnused);

    /* 0x00443660 AcquireSoundBuffer -- load (or share) the named static
     * buffer.  `mode` is passed through; callers use 0 and 1. */
    CStaticSoundbuffer *acquireStatic(const char *name, int mode);

    /* 0x00443810 AcquireVoicePool -- `count` voices on the named file;
     * `mode` as for acquireStatic. */
    VoicePool *acquirePool(int count, const char *name, int mode);

private:
    SoundManager() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(SoundManager);

    unsigned char gap_00[0x20];
    IDirectSound *directSound_;   /* +0x20  CFaktSound +0x14, its +0x0c */
};

KAROO_LAYOUT_CHECKS(SoundManager)
{
    KAROO_LAYOUT_AT(directSound_, 0x20);
}
