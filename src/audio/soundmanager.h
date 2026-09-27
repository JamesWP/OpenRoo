/* The sound manager: the game's sound asset service, a sub-object of Game.
 * Callers acquire a static buffer or a voice pool by file name and release it
 * when done; each file is loaded once, into an asset entry
 * (doublesoundbuff.h), and shared by duplication.  An entry is destroyed when
 * its last borrower releases it.
 *
 * Entries live in one of two name-keyed lists, chosen by the caller's bWant3D:
 * the plain list, or the 3D list, whose buffers get a 3D interface when 3D
 * sound is on.  Switching the 3D mode reloads only the 3D list. */
#pragma once

#include "layout.h"
#include "namedlist.h"
#include "cfaktsound.h"

class CStaticSoundbuffer;
class VoicePool;
struct GameLogger;

class __attribute__((packed)) SoundManager {
public:
    static const int ORIGIN = 0;

/* The embedded device's address; 4-aligned, so the packed-member warning is
 * moot. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    CFaktSound *cfaktSound() { return &cfaktSound_; }
#pragma GCC diagnostic pop

    IDirectSound *directSound() const { return cfaktSound_.directsound(); }

    // Finds the buffer in either list (plain first) and gives it back; with
    // bDestroyIfUnused and no borrower left, removes and destroys the entry.
    // Logs if the buffer is in neither list.
    void releaseStaticForOwner(CStaticSoundbuffer *buf, int bDestroyIfUnused);

    // The voice-pool counterpart.
    void releasePooledForOwner(VoicePool *pool, int bDestroyIfUnused);

    /* Loads filename into an entry's master buffer (or, given the spare's address,
     * the spare), in 3D or 2D, and applies the pending 3D mode on success. */
    int loadEntryMaster(void *entry, const char *filename,
                        unsigned long dwDsFlags, int bDo3D);

    // Loads, or shares, the named sound.  The first acquirer gets the entry's
    // master buffer, later ones a duplicate.  NULL if sound is not up or the
    // load fails.
    CStaticSoundbuffer *acquireStatic(const char *name, int bWant3D);

    // nVoices voices of the named sound.  A pool is always built from
    // duplicates.
    VoicePool *acquirePool(int nVoices, const char *name, int bWant3D);

    // Returns 0 if sound is not up or the 3D listener fails.  On a mode
    // change, reloads every buffer, duplicate and voice pool in the 3D list
    // for the new mode.  Otherwise 1.
    int setup(int mode3d);

    /* Construction and destruction, the asset purge (also the reset), and
     * start-up.  Init creates the device, with the 3D listener if enable3d; with
     * no logger it creates its own, SoundManager.log. */
    SoundManager *construct();
    void destruct();
    static SoundManager *__attribute__((thiscall))
    scalarDestructor(SoundManager *self, unsigned char flags);
    void purgeAssets();
    int init(int enable3d, HWND window, UINT bufferflags, short channels,
             int samplespersec, USHORT bitspersample, GameLogger *logger);

private:
    void          *vtable_;  // our one-slot table
    GameLogger    *logger_;
    unsigned long  ownsLogger_;        // 1 if Init created logger_
    unsigned long  dwMode3D_;          // the live 3D listener mode
    unsigned long  dwPendingMode3D_;   // set with it; the mode new loads apply
    CFaktSound     cfaktSound_;        // the device
    unsigned long  dwCreated_;         // 0 until the device is up
    unsigned long  dwDefaultDsFlags_;  // DSBCAPS_* for new loads; DSBCAPS_STATIC
    NamedEntryList entriesPlain_;      // bWant3D == 0
    NamedEntryList entries3D_;         // bWant3D != 0

    SoundManager() = delete;  // only ever reached through the Game
    KAROO_LAYOUT_REGISTER(SoundManager);
};

/* The device ends where the two lists begin, and the lists end where the fixed
 * sounds (fixedsounds.cpp) begin. */
KAROO_LAYOUT_CHECKS(SoundManager)
{
    KAROO_LAYOUT_AT(logger_,           0x04);
    KAROO_LAYOUT_AT(dwMode3D_,         0x0c);
    KAROO_LAYOUT_AT(dwPendingMode3D_,  0x10);
    KAROO_LAYOUT_AT(cfaktSound_,       0x14);
    KAROO_LAYOUT_AT(dwCreated_,        0x8c);
    KAROO_LAYOUT_AT(dwDefaultDsFlags_, 0x90);
    KAROO_LAYOUT_AT(entriesPlain_,     0x94);
    KAROO_LAYOUT_AT(entries3D_,        0xa4);

/* No size: the object continues with the fixed sounds. */
}

