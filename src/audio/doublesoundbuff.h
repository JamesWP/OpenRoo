/* doublesoundbuff -- the SoundManager's sound-asset entry (0x58 bytes).
 *
 * One of these hangs off every sound entry in the manager's two entry lists,
 * at entry+0x100.  It holds the loaded file twice -- a `master` buffer and a
 * `spare` used for the alternate 2D/3D flag set -- plus the two lists of
 * things currently borrowing it:
 *
 *   +0x00  masterBuf      CStaticSoundbuffer, the file as first loaded
 *   +0x18  spareBuf       CStaticSoundbuffer, the same file, other flags
 *   +0x30  dwMasterTaken  non-zero while masterBuf itself is lent out
 *   +0x34  dwSpareTaken   the same for spareBuf
 *   +0x38  cloneList      LinkedList of CStaticSoundbuffer* duplicates
 *   +0x48  voicePoolList  LinkedList of VoicePool* built from it
 *
 * The TU (progress.py's `doublesoundbuff`, 0x00442e60..0x004430e0) is nine
 * functions and an island: every reference to any of them comes either from
 * inside it or from the SoundManager TU above it.  Nothing else in the game
 * knows the type exists.
 *
 * `dwMasterTaken` / `dwSpareTaken` are *only ever cleared* by
 * ReleaseCloneOrOwnBuffer and Clear; AcquireSoundBuffer sets the master one
 * to 1 when it hands the master out directly rather than a clone.
 */
#pragma once

#include "layout.h"
#include "static.h"
#include "linkedlist.h"

struct VoicePool;

/* The game allocates this itself (AcquireSoundBuffer's `operator new(0x58)`),
 * so the size is relied on as well as the offsets -- hence KAROO_LAYOUT_SIZE.
 * ORIGIN is 0: our first byte is the pointer `operator new` returned, which
 * is also `this` for every method below. */
struct __attribute__((packed)) doublesoundbuff {
    static const int ORIGIN = 0;

    CStaticSoundbuffer masterBuf;       // +0x00
    CStaticSoundbuffer spareBuf;        // +0x18
    unsigned long      dwMasterTaken;   // +0x30
    unsigned long      dwSpareTaken;    // +0x34
    LinkedList         cloneList;       // +0x38
    LinkedList         voicePoolList;   // +0x48

    /* The four sub-objects are addressed through accessors so the one
     * -Waddress-of-packed-member suppression lives here rather than at every
     * use (the textrenderer.h / game.h idiom).  Nothing is actually
     * misaligned: all four offsets -- 0, 0x18, 0x38, 0x48 -- are 4-aligned,
     * and the layout checks below are what hold them there. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    CStaticSoundbuffer *master() { return &masterBuf; }
    CStaticSoundbuffer *spare()  { return &spareBuf; }
    LinkedList         *clones() { return &cloneList; }
    LinkedList         *pools()  { return &voicePoolList; }
#pragma GCC diagnostic pop

private:
    KAROO_LAYOUT_REGISTER(doublesoundbuff);
};

KAROO_LAYOUT_CHECKS(doublesoundbuff)
{
    KAROO_LAYOUT_AT(masterBuf,     0x00);
    KAROO_LAYOUT_AT(spareBuf,      0x18);
    KAROO_LAYOUT_AT(dwMasterTaken, 0x30);
    KAROO_LAYOUT_AT(dwSpareTaken,  0x34);
    KAROO_LAYOUT_AT(cloneList,     0x38);
    KAROO_LAYOUT_AT(voicePoolList, 0x48);
    KAROO_LAYOUT_SIZE(0x58);
}

/* ─── Our reimplementations, defined in doublesoundbuff.cpp ─────────────── */
extern "C" {

/* 0x00442e60 Init -- the ctor.  Returns `self`.  RET 0. */
__declspec(dllexport) doublesoundbuff * __attribute__((thiscall))
Dsb_Init(doublesoundbuff *self);

/* 0x00442ed0 DestroyAssetEntry -- the dtor body; does NOT free `self`.
 * RET 0. */
__declspec(dllexport) void __attribute__((thiscall))
Dsb_Destruct(doublesoundbuff *self);

/* 0x00442f40 ClearSoundEntry -- purge both borrower lists, release both
 * buffers, clear both taken flags.  RET 0. */
__declspec(dllexport) void __attribute__((thiscall))
Dsb_Clear(doublesoundbuff *self);

/* 0x00442f70 PurgeCloneList -- virtual-delete every clone, then empty the
 * list.  `__stdcall`, RET 4. */
__declspec(dllexport) void __attribute__((stdcall))
Dsb_PurgeCloneList(LinkedList *list);

/* 0x00442fa0 PurgeVoicePoolList -- Wipe + game-free every pool, then empty
 * the list.  `__stdcall`, RET 4. */
__declspec(dllexport) void __attribute__((stdcall))
Dsb_PurgeVoicePoolList(LinkedList *list);

/* 0x00442fe0 ReleaseCloneOrOwnBuffer -- give back one static buffer.
 * RET 4.  1 if it belonged to this entry, 0 if not. */
__declspec(dllexport) int __attribute__((thiscall))
Dsb_ReleaseStatic(doublesoundbuff *self, CStaticSoundbuffer *buf);

/* 0x00443050 ReleaseVoicePoolFromEntry -- the voice-pool counterpart.
 * RET 4. */
__declspec(dllexport) int __attribute__((thiscall))
Dsb_ReleasePool(doublesoundbuff *self, VoicePool *pool);

/* 0x004430a0 CountEntryBorrowers -- voicePoolList.dwCount +
 * cloneList.dwCount.  RET 0. */
__declspec(dllexport) int __attribute__((thiscall))
Dsb_BorrowerCount(doublesoundbuff *self);

/* 0x004430b0 EntryIsFullyReleased -- no borrowers and neither buffer lent
 * out.  RET 0. */
__declspec(dllexport) int __attribute__((thiscall))
Dsb_IsFullyReleased(doublesoundbuff *self);

}
