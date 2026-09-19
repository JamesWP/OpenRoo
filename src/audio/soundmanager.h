/* SoundManager -- the game's sound asset service, embedded in Game at
 * +0x13cba8 (Game::soundManager()).
 *
 * NO LONGER A PLACEHOLDER.  The five public methods below are reimplemented
 * in soundmanager.cpp (ENDGAME_PLAN E1, 2026-09-20), together with the
 * private loader helper `0x004435f0` they are all written on.  That closes
 * the largest callback group E1 had left.
 *
 * ── Why it could not go earlier ───────────────────────────────────────────
 *
 * Every one of the five is written in terms of two layers below it, and both
 * had to be ours first -- which is what the previous two cycles were for:
 *
 *   doublesoundbuff (2026-09-19)  the per-file asset entry: Init, Destruct,
 *                                 ReleaseStatic/Pool, IsFullyReleased
 *   NamedEntryList  (2026-09-19)  the name-keyed list: Find, Insert, Remove
 *
 * `releaseStaticForOwner` is, in full, "Find the entry, give the buffer
 * back, and if the entry is fully released Remove it and destroy it".  With
 * either layer still the game's, replacing this one would have retired five
 * callbacks by creating six or more.  With both ours, the five reduce to
 * bookkeeping over code we already own, and this cycle adds no new callback
 * at all -- every callee is ours except the game heap.
 *
 * ── The layout, and a field that was recorded wrongly ─────────────────────
 *
 * `+0x90` was `char *filename` in Ghidra's struct and in this header's old
 * note.  It is not a pointer: it is the DEFAULT DIRECTSOUND BUFFER-CAPS FLAG
 * SET, passed straight to CStaticSoundbuffer::CreateAndLoad*.  Three
 * independent readings agree, and none of them is "it looks like flags":
 *
 *   - `SoundMgrPurgeAssets` 0x00443520 assigns it the constant **2**, which
 *     is DSBCAPS_STATIC.  As a `char *` that is a wild pointer nothing could
 *     survive dereferencing.
 *   - The spare-buffer paths rewrite it as `(flags & ~4) | 8` -- clear
 *     DSBCAPS_LOCHARDWARE, set DSBCAPS_LOCSOFTWARE.  That is exactly the
 *     hardware-to-software fallback the spare buffer exists for, and it is
 *     meaningless as pointer arithmetic.
 *   - It is handed to the `dwDsFlags` parameter of CreateAndLoadFile, whose
 *     signature we already own.
 *
 * `+0x8c` was `undefined1 created`; every read of it is a full dword
 * (`MOV EAX,[ESI+0x8c]`), so it is a DWORD.
 *
 * The two entry lists were missing from the struct entirely.  They are
 * `NamedEntryList` (16 bytes each) at +0x94 and +0xa4, and they tile the gap
 * up to `pTimeOut` at +0xb4 EXACTLY -- 148 + 16 + 16 = 180 -- which is the
 * arithmetic cross-check CLAUDE.md asks for rather than a plausible reading.
 *
 * Which list is which matters and is easy to get backwards:
 *
 *   +0x94  entriesPlain  chosen when the caller's `bWant3D` argument is 0
 *   +0xa4  entries3D     chosen when it is non-zero; a buffer here is loaded
 *                        with the 3D interface only if `mode_3d` is also on
 *
 * `releaseStaticForOwner` searches **plain first, then 3D**, and `setup`
 * walks **only the 3D list** -- a 2D buffer has nothing to re-load when the
 * listener mode changes.
 */
#pragma once

#include "layout.h"
#include "namedlist.h"
#include "cfaktsound.h"

struct CStaticSoundbuffer;
struct VoicePool;
struct GameLogger;

class __attribute__((packed)) SoundManager {
public:
    static const int ORIGIN = 0;

    unsigned char  gap_00[0x04];
    GameLogger    *logger_;           /* +0x04 */
    unsigned char  gap_08[0x04];
    unsigned long  dwMode3D_;         /* +0x0c  the live listener mode */
    unsigned long  dwPendingMode3D_;  /* +0x10  set alongside it by setup() */
    CFaktSound     cfaktSound_;       /* +0x14  0x78 bytes */
    unsigned long  dwCreated_;        /* +0x8c  zero until sound comes up */
    unsigned long  dwDefaultDsFlags_; /* +0x90  DSBCAPS_*, NOT a filename */
    NamedEntryList entriesPlain_;     /* +0x94  bWant3D == 0 */
    NamedEntryList entries3D_;        /* +0xa4  bWant3D != 0 */

    /* The embedded sub-object needs its address; the one
     * -Waddress-of-packed-member suppression lives here rather than at the
     * use site (the doublesoundbuff.h / textrenderer.h idiom).  +0x14 is
     * 4-aligned, so nothing is actually misaligned. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    CFaktSound *cfaktSound() { return &cfaktSound_; }
#pragma GCC diagnostic pop

    /* The IDirectSound lives in the embedded CFaktSound, at its +0x0c --
     * i.e. SoundManager +0x20, which is what the originals load. */
    IDirectSound *directSound() const { return cfaktSound_.directsound; }

    /* 0x004439d0 SoundSetup(mode_3d).  Returns 0 if sound is not created
     * or the 3D listener fails; otherwise, on a mode change, reloads every
     * buffer, clone and voice pool for the new mode.  Returns 1. */
    int setup(int mode3d);

    /* 0x004432f0 ReleaseStaticSoundBufferForOwner.  Finds the buffer in
     * either entry list; with bDestroyIfUnused and no other owner left it
     * removes the entry and frees the buffer.  Logs if the buffer is in
     * neither list. */
    void releaseStaticForOwner(void *buffer, int bDestroyIfUnused);

    /* 0x00443400 -- the voice-pool counterpart, same argument shape. */
    void releasePooledForOwner(void *buffer, int bDestroyIfUnused);

    /* 0x00443660 AcquireSoundBuffer -- load (or share) the named static
     * buffer.  `bWant3D` selects the entry list; callers use 0 and 1. */
    CStaticSoundbuffer *acquireStatic(const char *name, int bWant3D);

    /* 0x00443810 AcquireVoicePool -- `count` voices on the named file;
     * `bWant3D` as for acquireStatic. */
    VoicePool *acquirePool(int count, const char *name, int bWant3D);

private:
    SoundManager() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(SoundManager);
};

/* The embedded CFaktSound is the cross-check rather than an assumption:
 * 0x14 + sizeof(CFaktSound) == 0x14 + 0x78 == 0x8c, which lands exactly on
 * dwCreated_, and the two 16-byte lists then run 0x94 -> 0xb4, which is
 * exactly where pTimeOut (fixedsounds.cpp's) begins.  The fields tile the
 * region with nothing left over. */
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
    /* No KAROO_LAYOUT_SIZE: the object continues past +0xb4 with the fixed
     * sound pointers (pTimeOut, pSwitch, ...) that fixedsounds.cpp owns. */
}

/* ─── The reimplementations, for patch.py and for callers ─────────────────
 *
 * All six are __thiscall with `this` in ECX; the stack argument counts were
 * read off each original's `RET n` rather than off the decompiler.
 */
extern "C" {

/* 0x004432f0, RET 8. */
__declspec(dllexport) void __attribute__((thiscall))
SoundMgr_ReleaseStaticForOwner(SoundManager *self, CStaticSoundbuffer *buf,
                               int bDestroyIfUnused);

/* 0x00443400, RET 8. */
__declspec(dllexport) void __attribute__((thiscall))
SoundMgr_ReleasePoolForOwner(SoundManager *self, VoicePool *pool,
                             int bDestroyIfUnused);

/* 0x004435f0, RET 0x10.  The private loader: load `filename` into the
 * entry's master buffer, in 3D or 2D, and apply the 3D mode on success.
 * Every one of its six call sites is inside the five above, so it takes a
 * stub and no CALL_PATCHES entry. */
__declspec(dllexport) int __attribute__((thiscall))
SoundMgr_LoadEntryMaster(SoundManager *self, void *entry,
                         const char *filename, unsigned long dwDsFlags,
                         int bDo3D);

/* 0x00443660, RET 8. */
__declspec(dllexport) CStaticSoundbuffer *__attribute__((thiscall))
SoundMgr_AcquireStatic(SoundManager *self, const char *name, int bWant3D);

/* 0x00443810, RET 0xc. */
__declspec(dllexport) VoicePool *__attribute__((thiscall))
SoundMgr_AcquirePool(SoundManager *self, int nVoices, const char *name,
                     int bWant3D);

/* 0x004439d0, RET 4. */
__declspec(dllexport) int __attribute__((thiscall))
SoundMgr_Setup(SoundManager *self, int mode3d);

}
