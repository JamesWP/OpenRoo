/* ENDGAME_PLAN.md E1 — the `doublesoundbuff` TU (0x00442e60..0x004430e0),
 * all nine functions.
 *
 * This is the record the SoundManager keeps for one loaded sound file, and
 * the bookkeeping that decides when that file may be thrown away.  It is a
 * closed island: `tools/xref.py` finds references to these nine only from
 * inside the TU itself and from the SoundManager TU above it, so replacing
 * the whole of it leaves no game caller needing the originals.
 *
 * It is replaced now, ahead of the SoundManager's five callbacks, because
 * those five are written in terms of these nine -- the two release-by-owner
 * functions are little more than "find the entry, then call
 * ReleaseCloneOrOwnBuffer / ReleaseVoicePoolFromEntry and, if the entry is
 * fully released, destroy it".  Doing this layer first means that cycle adds
 * no new callback.
 *
 * Everything these nine call is already ours: CStaticSoundbuffer (static.h),
 * LinkedList (linkedlist.h) and VoicePool::Wipe (voicepool.h).  The only
 * call that still lands in the game binary is `FactAlloc::Free2` through
 * alloc.h, and that is the documented shim -- the pools and clones freed
 * here were allocated by SoundManager, which is still game code.  Both sides
 * become ours in the next cycle, and the switch to plain delete belongs
 * there (ENDGAME_PLAN.md, "alloc.h retires by attrition"), not here.
 *
 * ── The SEH frames are not reproduced ────────────────────────────────────
 *
 * Init and DestroyAssetEntry both carry an MSVC unwind frame: a
 * partial-construction record so that a throw part-way through Init unwinds
 * the sub-objects already built.  Nothing in the chain throws -- CStatic_Init
 * and List_Init are field stores -- so the frames have no observable effect
 * and are omitted.  What IS reproduced is the *order*: Destruct tears down in
 * exactly the reverse of Init, which is what the unwind indices encode.
 */
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include "doublesoundbuff.h"
#include "voicepool.h"
#include "alloc.h"
#include "log.h"

/* A CStaticSoundbuffer's vtable has a single slot, the scalar/vector deleting
 * destructor; the original calls it with flag 1 (free the object too).  The
 * clones on cloneList were allocated with the game's operator new, so this
 * has to stay the virtual call the original makes rather than a `delete`. */
static void virtual_delete_static(void *obj)
{
    typedef void (__attribute__((thiscall)) *dtor_fn)(void *self, int flags);
    void **vtbl = *(void ***)obj;
    ((dtor_fn)vtbl[0])(obj, 1);
}

/* ─── KAROO_DSB_DIAG — the census ─────────────────────────────────────────
 *
 * Read by value, never by presence (CLAUDE.md).  Entries are built and torn
 * down a handful of times per level load, so every call can report without a
 * sampling threshold.  `masterLent` / `spareLent` count the two identity
 * paths in ReleaseCloneOrOwnBuffer -- the ones that hand a buffer back
 * without it ever having been on a list -- which is how "the spare half of
 * this class is dead" would show itself.
 */
static int dsb_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_DSB_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static unsigned long g_nInit, g_nDestruct, g_nClear,
                     g_nPurgeClone, g_nPurgePool,
                     g_nClonesFreed, g_nPoolsFreed,
                     g_nRelStatic, g_nRelStaticHit, g_nMasterLent, g_nSpareLent,
                     g_nRelPool, g_nRelPoolHit,
                     g_nBorrowerCount, g_nFullyReleased, g_nWasFullyReleased;

static void dsb_first(const char *what, unsigned long *seen)
{
    if (!dsb_diag() || *seen) return;
    *seen = 1;
    log_write("doublesoundbuff: first call to %s\n", what);
}

static void dsb_census(void)
{
    if (!dsb_diag()) return;
    log_write("doublesoundbuff: DIAG init=%lu destruct=%lu clear=%lu "
              "purgeClone=%lu purgePool=%lu clonesFreed=%lu poolsFreed=%lu "
              "relStatic=%lu/%lu masterLent=%lu spareLent=%lu "
              "relPool=%lu/%lu borrowerCount=%lu fullyReleased=%lu/%lu\n",
              g_nInit, g_nDestruct, g_nClear,
              g_nPurgeClone, g_nPurgePool, g_nClonesFreed, g_nPoolsFreed,
              g_nRelStaticHit, g_nRelStatic, g_nMasterLent, g_nSpareLent,
              g_nRelPoolHit, g_nRelPool,
              g_nBorrowerCount, g_nWasFullyReleased, g_nFullyReleased);
}

/* ─── KAROO_DSB_FX — the negative control (CONTROLS.md) ───────────────────
 *
 *   stickyentry — EntryIsFullyReleased always answers "no".
 *
 * WHY THIS ONE.  These nine functions write no pixels and move no geometry,
 * so nothing here can reach the unbounded bridge/slide spawn scans that
 * crash levelreport.py (CLAUDE.md's blast-radius rule).  What this TU owns
 * outright is the *lifetime decision*: EntryIsFullyReleased is the single
 * predicate that lets the manager destroy a loaded sound, and no other code
 * makes that call.  Forcing it false changes an outcome rather than a value
 * -- every sound file stays resident once loaded, across every level -- and
 * it can only ever leak, never free something still in use, so it is safe to
 * run under both gates.  KAROO_DSB_DIAG reads it directly: `fullyReleased`
 * goes to 0/N with N unchanged.
 */
enum DsbFx { DSB_FX_OFF = 0, DSB_FX_STICKYENTRY = 1 };

static DsbFx dsb_fx(void)
{
    static int cached = -1;
    if (cached >= 0) return (DsbFx)cached;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("KAROO_DSB_FX", buf, sizeof(buf));
    DsbFx fx = DSB_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "stickyentry") == 0) fx = DSB_FX_STICKYENTRY;
    }
    log_write("doublesoundbuff: FX mode = %s\n",
              fx == DSB_FX_STICKYENTRY ? "stickyentry" : "off");
    cached = (int)fx;
    return fx;
}

/* ─── doublesoundbuff::Init (0x00442e60) ──────────────────────────────────
 *
 * `__thiscall`, RET 0, returns `this`.  Both buffers, then both lists, then
 * the two lent-out flags.  Note the lists are constructed AFTER the buffers
 * and so are destructed before them; see Destruct.
 */
extern "C" __declspec(dllexport) doublesoundbuff * __attribute__((thiscall))
Dsb_Init(doublesoundbuff *self)
{
    ++g_nInit; { static unsigned long seen; dsb_first("Init", &seen); }
    CStatic_Init(self->master());
    CStatic_Init(self->spare());
    List_Init(self->clones());
    List_Init(self->pools());
    self->dwMasterTaken = 0;
    self->dwSpareTaken  = 0;
    return self;
}

/* ─── ClearSoundEntry (0x00442f40) ────────────────────────────────────────
 *
 * `__thiscall`, RET 0.  Drop every borrower, release both buffers, and
 * forget that either was lent out.  Used both by Destruct and, from the
 * SoundManager, to recycle an entry in place when the 2D/3D mode changes.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Dsb_Clear(doublesoundbuff *self)
{
    ++g_nClear; { static unsigned long seen; dsb_first("Clear", &seen); }
    Dsb_PurgeCloneList(self->clones());
    Dsb_PurgeVoicePoolList(self->pools());
    CStatic_Reset(self->master());
    CStatic_Reset(self->spare());
    self->dwMasterTaken = 0;
    self->dwSpareTaken  = 0;
    dsb_census();
}

/* ─── DestroyAssetEntry (0x00442ed0) ──────────────────────────────────────
 *
 * `__thiscall`, RET 0.  The destructor body: it does NOT free `self` -- every
 * caller in the SoundManager follows it with FactAlloc::Free2.  Teardown is
 * the exact reverse of Init: Clear, then voicePoolList, cloneList, spareBuf,
 * masterBuf.  (ReinitBuffer, not Reset: it re-installs the vtable first,
 * which Clear's Reset does not.)
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Dsb_Destruct(doublesoundbuff *self)
{
    ++g_nDestruct; { static unsigned long seen; dsb_first("Destruct", &seen); }
    Dsb_Clear(self);
    List_Destruct(self->pools());
    List_Destruct(self->clones());
    CStatic_ReinitBuffer(self->spare());
    CStatic_ReinitBuffer(self->master());
    dsb_census();
}

/* ─── PurgeCloneList (0x00442f70) ─────────────────────────────────────────
 *
 * `__stdcall`, RET 4 -- the list arrives on the stack, not in ECX, even
 * though it is only ever called with a member of a doublesoundbuff.
 *
 * The walk reads pValue and advances to pNextNode BEFORE the virtual delete,
 * so a destructor that unlinked its own node would not strand the walk.  The
 * nodes themselves are not freed here; List_Clear does that afterwards.
 */
extern "C" __declspec(dllexport) void __attribute__((stdcall))
Dsb_PurgeCloneList(LinkedList *list)
{
    ++g_nPurgeClone; { static unsigned long seen; dsb_first("PurgeCloneList", &seen); }
    LinkedListNode *node = list->pHead;
    while (node != 0) {
        void *clone = node->pValue;
        node = node->pNextNode;
        if (clone != 0) {
            ++g_nClonesFreed;
            virtual_delete_static(clone);
        }
    }
    List_Clear(list);
}

/* ─── PurgeVoicePoolList (0x00442fa0) ─────────────────────────────────────
 *
 * `__stdcall`, RET 4.  The same walk, but a VoicePool has no vtable, so it
 * is Wipe + the game's free rather than a virtual destructor.
 */
extern "C" __declspec(dllexport) void __attribute__((stdcall))
Dsb_PurgeVoicePoolList(LinkedList *list)
{
    ++g_nPurgePool; { static unsigned long seen; dsb_first("PurgeVoicePoolList", &seen); }
    LinkedListNode *node = list->pHead;
    while (node != 0) {
        VoicePool *pool = (VoicePool *)node->pValue;
        node = node->pNextNode;
        if (pool != 0) {
            ++g_nPoolsFreed;
            Sim_VoicePoolWipe(pool);
            game_free2(pool);
        }
    }
    List_Clear(list);
}

/* ─── ReleaseCloneOrOwnBuffer (0x00442fe0) ────────────────────────────────
 *
 * `__thiscall`, RET 4.  Hand one CStaticSoundbuffer back to its entry and
 * say whether the entry recognised it.  Three ways it can:
 *
 *   - it is on cloneList: unlink the node and virtually delete the clone;
 *   - it IS masterBuf (`buf == self`): clear dwMasterTaken;
 *   - it IS spareBuf: clear dwSpareTaken.
 *
 * Otherwise 0, and the caller goes on to look in the manager's other entry
 * list.  The identity compares are pointer compares against the entry's own
 * address, which is why the master buffer sits at offset 0.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Dsb_ReleaseStatic(doublesoundbuff *self, CStaticSoundbuffer *buf)
{
    ++g_nRelStatic; { static unsigned long seen; dsb_first("ReleaseStatic", &seen); }

    LinkedListNode *node = List_Find(self->clones(), buf, 0);
    if (node != 0) {
        List_Unlink(self->clones(), node);
        if (buf != 0)
            virtual_delete_static(buf);
        ++g_nRelStaticHit;
        return 1;
    }
    if (buf == self->master()) {
        self->dwMasterTaken = 0;
        ++g_nMasterLent; ++g_nRelStaticHit;
        return 1;
    }
    if (buf == self->spare()) {
        self->dwSpareTaken = 0;
        ++g_nSpareLent; ++g_nRelStaticHit;
        return 1;
    }
    return 0;
}

/* ─── ReleaseVoicePoolFromEntry (0x00443050) ──────────────────────────────
 *
 * `__thiscall`, RET 4.  The voice-pool counterpart, and simpler: a pool is
 * always built for a borrower, so there is no identity case -- if it is not
 * on voicePoolList it is not ours.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Dsb_ReleasePool(doublesoundbuff *self, VoicePool *pool)
{
    ++g_nRelPool; { static unsigned long seen; dsb_first("ReleasePool", &seen); }

    LinkedListNode *node = List_Find(self->pools(), pool, 0);
    if (node == 0)
        return 0;
    List_Unlink(self->pools(), node);
    if (pool != 0) {
        Sim_VoicePoolWipe(pool);
        game_free2(pool);
    }
    ++g_nRelPoolHit;
    return 1;
}

/* ─── CountEntryBorrowers (0x004430a0) ────────────────────────────────────
 *
 * `__thiscall`, RET 0, four instructions: voicePoolList.dwCount +
 * cloneList.dwCount, read in that order and added as signed ints.  The
 * counts are unsigned in the struct; the original's compare downstream is a
 * signed `JLE`, which is preserved by summing into an int here.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Dsb_BorrowerCount(doublesoundbuff *self)
{
    ++g_nBorrowerCount;
    return (int)self->voicePoolList.dwCount + (int)self->cloneList.dwCount;
}

/* ─── EntryIsFullyReleased (0x004430b0) ───────────────────────────────────
 *
 * `__thiscall`, RET 0.  True only when nothing borrows the entry and neither
 * of its own two buffers is lent out.  This is the single predicate that
 * lets the SoundManager destroy a loaded sound, which is why the negative
 * control lives here.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Dsb_IsFullyReleased(doublesoundbuff *self)
{
    ++g_nFullyReleased; { static unsigned long seen; dsb_first("IsFullyReleased", &seen); }

    if (dsb_fx() == DSB_FX_STICKYENTRY)
        return 0;                  /* CONTROLS.md: nothing is ever freed */

    if (Dsb_BorrowerCount(self) > 0)
        return 0;
    if (self->dwMasterTaken != 0)
        return 0;
    int yes = (self->dwSpareTaken == 0);
    if (yes) ++g_nWasFullyReleased;
    return yes;
}
