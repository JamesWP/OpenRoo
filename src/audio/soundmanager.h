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
 * Nothing is declared as a member until a method of ours reads it.
 */
#pragma once

struct CStaticSoundbuffer;

class SoundManager {
public:
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

private:
    SoundManager() = delete;   /* game-owned; only ever reached by pointer */
};
