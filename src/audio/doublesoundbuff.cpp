/* Teardown is the exact reverse of construction: buffers, then lists, going
 * up; lists, then buffers, coming down. */

#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include "doublesoundbuff.h"
#include "voicepool.h"
#include <stdlib.h>
#include "log.h"

/* A static buffer's vtable has one slot, the deleting destructor; flag 1 frees
 * the object too. */
static void virtual_delete_static(void *obj)
{
    typedef void (  *dtor_fn)(void *self, int flags);
    void **vtbl = *(void ***)obj;
    ((dtor_fn)vtbl[0])(obj, 1);
}

/* KAROO_DSB_DIAG=1 counts every call, logs each function's first call, and
 * counts the two paths that give back the master or spare directly, which
 * would show if the spare half were dead. */
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

/* KAROO_DSB_FX=stickyentry is a negative control: an entry is never fully
 * released, so every sound stays loaded for good.  It can only leak, never
 * free something in use, so both gates can host it; KAROO_DSB_DIAG's
 * fullyReleased count shows it (0 of N). */
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

/* Both buffers, then both lists, then the two lent-out flags. */
doublesoundbuff *doublesoundbuff::init()
{
    ++g_nInit; { static unsigned long seen; dsb_first("Init", &seen); }
    master()->init();
    spare()->init();
    clones()->init();
    pools()->init();
    dwMasterTaken_ = 0;
    dwSpareTaken_  = 0;
    return this;
}

/* Drops every borrower, releases both buffers and forgets that either was lent
 * out.  Also used by the sound manager to recycle an entry when the 2D/3D mode
 * changes. */
void doublesoundbuff::clear()
{
    ++g_nClear; { static unsigned long seen; dsb_first("Clear", &seen); }
    doublesoundbuff::purgeCloneList(clones());
    doublesoundbuff::purgeVoicePoolList(pools());
    master()->reset();
    spare()->reset();
    dwMasterTaken_ = 0;
    dwSpareTaken_  = 0;
    dsb_census();
}

/* Clear, then the lists and buffers in reverse order of construction.
 * ReinitBuffer, unlike Reset, reinstalls the vtable first. */
void doublesoundbuff::destruct()
{
    ++g_nDestruct; { static unsigned long seen; dsb_first("Destruct", &seen); }
    clear();
    pools()->destruct();
    clones()->destruct();
    spare()->reinitBuffer();
    master()->reinitBuffer();
    dsb_census();
}

/* Takes the list, not the entry.  The next node is read before the delete, so
 * a destructor that unlinked its own node would not strand the walk;
 * LinkedList::clear frees the nodes afterwards. */
void __attribute__((stdcall))
doublesoundbuff::purgeCloneList(LinkedList *list)
{
    ++g_nPurgeClone; { static unsigned long seen; dsb_first("PurgeCloneList", &seen); }
    LinkedListNode *node = list->head();
    while (node != 0) {
        void *clone = node->value();
        node = node->next();
        if (clone != 0) {
            ++g_nClonesFreed;
            virtual_delete_static(clone);
        }
    }
    list->clear();
}

/* The same walk; a pool has no vtable, so it is wiped and freed. */
void __attribute__((stdcall))
doublesoundbuff::purgeVoicePoolList(LinkedList *list)
{
    ++g_nPurgePool; { static unsigned long seen; dsb_first("PurgeVoicePoolList", &seen); }
    LinkedListNode *node = list->head();
    while (node != 0) {
        VoicePool *pool = (VoicePool *)node->value();
        node = node->next();
        if (pool != 0) {
            ++g_nPoolsFreed;
            pool->wipe();
            free(pool);
        }
    }
    list->clear();
}

/* Gives back one buffer.  It is recognised if it is on the duplicate list
 * (unlinked and deleted) or is the master or spare itself (its taken flag
 * cleared).  Otherwise 0, and the caller tries the manager's other list. */
int doublesoundbuff::releaseStatic(CStaticSoundbuffer *buf)
{
    ++g_nRelStatic; { static unsigned long seen; dsb_first("ReleaseStatic", &seen); }

    LinkedListNode *node = clones()->find(buf, 0);
    if (node != 0) {
        clones()->unlink(node);
        if (buf != 0)
            virtual_delete_static(buf);
        ++g_nRelStaticHit;
        return 1;
    }
    if (buf == master()) {
        dwMasterTaken_ = 0;
        ++g_nMasterLent; ++g_nRelStaticHit;
        return 1;
    }
    if (buf == spare()) {
        dwSpareTaken_ = 0;
        ++g_nSpareLent; ++g_nRelStaticHit;
        return 1;
    }
    return 0;
}

/* A pool is always built for a borrower, so there is no identity case: if it
 * is not on the list it is not this entry's. */
int doublesoundbuff::releasePool(VoicePool *pool)
{
    ++g_nRelPool; { static unsigned long seen; dsb_first("ReleasePool", &seen); }

    LinkedListNode *node = pools()->find(pool, 0);
    if (node == 0)
        return 0;
    pools()->unlink(node);
    if (pool != 0) {
        pool->wipe();
        free(pool);
    }
    ++g_nRelPoolHit;
    return 1;
}

/* DETERMINISM: summed as signed ints; the caller's compare is signed. */
int doublesoundbuff::borrowerCount()
{
    ++g_nBorrowerCount;
    return (int)voicePoolList_.count() + (int)cloneList_.count();
}

/* The single predicate that lets the sound manager destroy a loaded sound. */
int doublesoundbuff::isFullyReleased()
{
    ++g_nFullyReleased; { static unsigned long seen; dsb_first("IsFullyReleased", &seen); }

    if (dsb_fx() == DSB_FX_STICKYENTRY)
        return 0;  // KAROO_DSB_FX=stickyentry: nothing is ever freed

    if (borrowerCount() > 0)
        return 0;
    if (dwMasterTaken_ != 0)
        return 0;
    int yes = (dwSpareTaken_ == 0);
    if (yes) ++g_nWasFullyReleased;
    return yes;
}
