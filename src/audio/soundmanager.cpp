/* SoundManager reimplementation -- the five public methods and the private
 * loader they are all written on.  ENDGAME_PLAN E1, 2026-09-20.
 *
 *   0x004432f0 ReleaseStaticSoundBufferForOwner  (this, buf, bDestroy)  RET 8
 *   0x00443400 ReleaseVoicePoolBufferForOwner    (this, pool, bDestroy) RET 8
 *   0x004435f0 LoadEntryMaster (private)         (this,e,name,fl,3d)    RET 0x10
 *   0x00443660 AcquireSoundBuffer                (this, name, want3D)   RET 8
 *   0x00443810 AcquireVoicePool                  (this, n, name, w3D)   RET 0xc
 *   0x004439d0 SoundSetup                        (this, mode3d)         RET 4
 *
 * Every stack-argument count is the original's `RET n`.  soundmanager.h
 * holds the layout, the two corrected fields and why this cycle had to come
 * third.
 *
 * ── The TU names itself ───────────────────────────────────────────────────
 *
 * The logging calls pass `0x004678b4`, which is
 * "E:\WORK\VC++\JumpinJohn\Ra\FAKTSound\SoundManager.cpp", together with a
 * real source line: 209 and 258 for the two releases, 488/494/511/521/545/555
 * inside SoundSetup.  So the TU boundary guessed here is confirmed by the
 * binary's own strings, and the original class is `CSoundManager`.
 *
 * ── Two contracts that look like their opposite ───────────────────────────
 *
 * `CStaticSoundbuffer::Copy` and `VoicePool::Clone` both return the **source**
 * on success and NULL on failure -- not the destination.  Every caller here
 * therefore tests `result == src`, and because `masterBuf` is at offset 0 of
 * the entry, `&entry->masterBuf == (CStaticSoundbuffer *)entry`; the
 * decompiler renders that test as a comparison against the entry itself,
 * which reads like a type confusion and is not one.
 *
 * Both are called with the trailing flag **1** at every site here -- read off
 * the pushes, not the decompile, which omits the argument entirely.  One
 * means "do not fall back to reloading from file if DuplicateSoundBuffer
 * fails"; the SoundManager wants the failure so it can try the spare.
 *
 * ── Bugs and quirks preserved deliberately ───────────────────────────────
 *
 *   - Neither `operator new` result is null-checked before use in the way a
 *     reader expects.  Both acquires do test the pointer, but on failure
 *     they carry the NULL onward: AcquireSoundBuffer hands a NULL entry to
 *     LoadEntryMaster, which dereferences it.  The test buys nothing and the
 *     fault happens one call deeper.  Reproduced exactly.
 *   - `ReleaseVoicePoolBufferForOwner`'s "not found" log calls
 *     `GetVoiceAt(pool, 0)` and reads `->filename` off the result without
 *     checking it, so an empty pool faults while reporting an error.
 *   - Both releases search the plain list, and only if the buffer is not
 *     found *or the entry refused the release* do they search the 3D list.
 *     A buffer present in the plain list whose release returns 0 is looked
 *     for a second time in the other list.
 *   - `SoundSetup` commits the new mode even when the entry list is empty,
 *     and even when individual reloads logged failures: nothing it does can
 *     make it return 0 once the 3D listener has been created.
 *   - `SoundSetup` walks ONLY the 3D list.
 *   - In `AcquireVoicePool` the 3D flag is computed twice, once at the top of
 *     the search loop and again inside the miss branch, from the same two
 *     values.  Kept as two expressions because that is what the original
 *     does; they cannot disagree.
 *
 * ── The `src` carry, which the decompiler hides ──────────────────────────
 *
 * In `SoundSetup`, ESI holds "the buffer clones are currently copied from".
 * It starts as the entry's master, and if a clone's copy fails it is moved
 * to the spare -- and it **stays there for the remaining clones AND for the
 * whole voice-pool loop that follows**.  The two loops share one variable.
 * Reading the decompile alone, they look independent.
 */
#include "soundmanager.h"
#include "doublesoundbuff.h"
#include "namedlist.h"
#include "static.h"
#include "voicepool.h"
#include "linkedlist.h"
#include "gamelog.h"
#include "cfaktsound.h"
#include <stdlib.h>
#include <stddef.h>
#include <windows.h>
#include "log.h"
#include "gamestr.h"

/* The original's own source file and line numbers, used verbatim so the log
 * lines this produces are byte-identical to the game's. */
static const char SRCFILE[] =
    "E:\\WORK\\VC++\\JumpinJohn\\Ra\\FAKTSound\\SoundManager.cpp";

/* ─── KAROO_SNDMGR_FX — the negative control (CONTROLS.md) ────────────────
 *
 *   nosharemaster — the first acquirer of a sound never receives the entry's
 *                   master buffer; it gets a duplicate, like every later one.
 *
 * `dwMasterTaken` is the one thing this class decides outright: whether a
 * caller is handed the original buffer or a copy of it.  Nothing else in the
 * game makes that choice, and it is a change of OUTCOME rather than a
 * perturbed value.
 *
 * Blast radius is bounded, which is the point after the NamedEntryList
 * cycle: sharing still works, the entry is still found, and the only cost is
 * one extra DirectSound duplicate per distinct sound -- unlike
 * KAROO_NAMEDLIST_FX=nofind, which had to be unbounded because it broke the
 * lookup itself.  It moves no geometry, so it cannot reach the bridge/slide
 * spawn scans that crash levelreport.py.
 *
 * Prediction, before running: our own `masterGrants` goes to 0 and `clones`
 * rises by exactly the number that used to be granted -- and, in a different
 * census entirely, KAROO_DSB_DIAG's `masterLent` should fall to 0 too,
 * because the master is what `ReleaseCloneOrOwnBuffer` recognises by
 * identity.
 */
enum SndMgrFx { SM_FX_OFF = 0, SM_FX_NOSHAREMASTER = 1 };

static SndMgrFx sndmgr_fx(void)
{
    static int cached = -1;
    if (cached >= 0)
        return (SndMgrFx)cached;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("KAROO_SNDMGR_FX", buf, sizeof(buf));
    SndMgrFx fx = SM_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "nosharemaster") == 0) fx = SM_FX_NOSHAREMASTER;
    }
    log_write("soundmgr: FX mode = %s\n",
              fx == SM_FX_NOSHAREMASTER ? "nosharemaster" : "off");
    cached = (int)fx;
    return fx;
}

/* ─── KAROO_SNDMGR_DIAG — the census ─────────────────────────────────────── */
static bool sndmgr_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SNDMGR_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static unsigned long g_acqStatic, g_acqStaticHit, g_acqStaticNew;
static unsigned long g_masterGrants, g_clones, g_cloneFail;
static unsigned long g_acqPool, g_acqPoolHit, g_acqPoolNew, g_pools, g_poolFail;
static unsigned long g_relStatic, g_relStaticPlain, g_relStatic3D, g_relStaticLost;
static unsigned long g_relPool, g_relPoolPlain, g_relPool3D, g_relPoolLost;
static unsigned long g_entriesDestroyed, g_loadMaster, g_loadMaster3D, g_loadFail;
static unsigned long g_setup, g_setupModeChange, g_setupReloaded, g_setupSpare;
static unsigned long g_acqSpare;   /* an acquire that fell through to the spare */

static void sndmgr_first(const char *fn, unsigned long *pSeen)
{
    if (!sndmgr_diag() || *pSeen != 0)
        return;
    *pSeen = 1;
    log_write("soundmgr: first call to %s\n", fn);
}

/* Dumped from the two releases and from setup -- cold paths, as
 * namedlist.cpp's census had to learn.  Also sampled on the acquire path so
 * a run that never releases still reports. */
static void sndmgr_census(void)
{
    if (!sndmgr_diag())
        return;
    log_write("soundmgr: census acqStatic=%lu (hit %lu new %lu) masterGrants=%lu "
              "clones=%lu (fail %lu) acqPool=%lu (hit %lu new %lu) pools=%lu "
              "(fail %lu) relStatic=%lu (plain %lu 3d %lu lost %lu) "
              "relPool=%lu (plain %lu 3d %lu lost %lu) destroyed=%lu "
              "load=%lu (3d %lu fail %lu) setup=%lu (change %lu reloaded %lu "
              "spare %lu) acqSpare=%lu\n",
              g_acqStatic, g_acqStaticHit, g_acqStaticNew, g_masterGrants,
              g_clones, g_cloneFail, g_acqPool, g_acqPoolHit, g_acqPoolNew,
              g_pools, g_poolFail, g_relStatic, g_relStaticPlain,
              g_relStatic3D, g_relStaticLost, g_relPool, g_relPoolPlain,
              g_relPool3D, g_relPoolLost, g_entriesDestroyed, g_loadMaster,
              g_loadMaster3D, g_loadFail, g_setup, g_setupModeChange,
              g_setupReloaded, g_setupSpare, g_acqSpare);
}

/* The spare buffer's flag rewrite, in one place because it appears four
 * times: drop DSBCAPS_LOCHARDWARE, add DSBCAPS_LOCSOFTWARE. */
static inline unsigned long spare_flags(unsigned long dwFlags)
{
    return (dwFlags & 0xfffffffbUL) | 0x8UL;
}

/* Destroy an entry and free it.  The tail both releases share. */
static void destroy_entry(doublesoundbuff *entry)
{
    ++g_entriesDestroyed;
    Dsb_Destruct(entry);
    /* Our heap: every allocator and freer of an entry is in this file. */
    operator delete(entry);
}

extern "C" {

/* ─── 0x004435f0 the private loader ──────────────────────────────────────
 *
 * `entry` is typed `void *` on purpose.  Two of its six call sites pass the
 * entry, and four pass `&entry->spareBuf` -- a pointer into the middle of an
 * entry, used as though it were one.  That works only because `masterBuf` is
 * at offset 0, which is the same identity the `result == src` tests rely on.
 * Taking a CStaticSoundbuffer* here would be the honest type; taking void *
 * and saying so keeps the call sites reading like the original's.
 */
__declspec(dllexport) int __attribute__((thiscall))
SoundMgr_LoadEntryMaster(SoundManager *self, void *entry,
                         const char *filename, unsigned long dwDsFlags,
                         int bDo3D)
{
    ++g_loadMaster;
    { static unsigned long seen; sndmgr_first("LoadEntryMaster", &seen); }

    CStaticSoundbuffer *buf = (CStaticSoundbuffer *)entry;

    if (bDo3D) {
        ++g_loadMaster3D;
        int ok = CStatic_CreateAndLoad3DSoundFile(buf, self->directSound(),
                                                  dwDsFlags, filename,
                                                  self->logger_);
        if (ok)
            CStatic_Apply3DMode(buf, (int)self->dwPendingMode3D_);
        else
            ++g_loadFail;
        return ok;
    }

    int ok = CStatic_CreateAndLoadFile(buf, self->directSound(), dwDsFlags,
                                       filename, self->logger_);
    if (!ok)
        ++g_loadFail;
    return ok;
}

/* ─── 0x004432f0 ReleaseStaticSoundBufferForOwner ────────────────────────
 *
 * The buffer names itself: the lookup key is `buf->filename`.
 */
__declspec(dllexport) void __attribute__((thiscall))
SoundMgr_ReleaseStaticForOwner(SoundManager *self, CStaticSoundbuffer *buf,
                               int bDestroyIfUnused)
{
    ++g_relStatic;
    { static unsigned long seen; sndmgr_first("ReleaseStaticForOwner", &seen); }

    NamedEntryList *lists[2] = { &self->entriesPlain_, &self->entries3D_ };
    for (int i = 0; i < 2; i++) {
        NamedEntry *e = NamedList_Find(lists[i], buf->filename);
        if (e == NULL)
            continue;
        doublesoundbuff *entry = (doublesoundbuff *)e->pPayload;
        if (!Dsb_ReleaseStatic(entry, buf))
            continue;              /* the entry did not own it: try the other */
        if (i == 0) ++g_relStaticPlain; else ++g_relStatic3D;
        if (!bDestroyIfUnused)
            return;
        if (!Dsb_IsFullyReleased(entry))
            return;
        NamedList_Remove(lists[i], e);
        if (entry == NULL)         /* the original tests it; it cannot be NULL */
            return;
        destroy_entry(entry);
        sndmgr_census();
        return;
    }

    ++g_relStaticLost;
    sndmgr_census();
    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3, SRCFILE, 0xd1,
        "Could not release StaticSoundbuffer '%s', because the buffer was "
        "not found !", buf->filename);
}

/* ─── 0x00443400 ReleaseVoicePoolBufferForOwner ──────────────────────────
 *
 * Same shape, but the key comes from the pool's first voice, re-read for
 * each list exactly as the original does.
 */
__declspec(dllexport) void __attribute__((thiscall))
SoundMgr_ReleasePoolForOwner(SoundManager *self, VoicePool *pool,
                             int bDestroyIfUnused)
{
    ++g_relPool;
    { static unsigned long seen; sndmgr_first("ReleasePoolForOwner", &seen); }

    NamedEntryList *lists[2] = { &self->entriesPlain_, &self->entries3D_ };
    for (int i = 0; i < 2; i++) {
        /* Re-fetched per list, as in the original -- not hoisted. */
        char *name = Sim_VoicePoolFirstFilename(pool);
        NamedEntry *e = NamedList_Find(lists[i], name);
        if (e == NULL)
            continue;
        doublesoundbuff *entry = (doublesoundbuff *)e->pPayload;
        if (!Dsb_ReleasePool(entry, pool))
            continue;
        if (i == 0) ++g_relPoolPlain; else ++g_relPool3D;
        if (!bDestroyIfUnused)
            return;
        if (!Dsb_IsFullyReleased(entry))
            return;
        NamedList_Remove(lists[i], e);
        if (entry == NULL)
            return;
        destroy_entry(entry);
        sndmgr_census();
        return;
    }

    ++g_relPoolLost;
    sndmgr_census();
    /* Preserved: GetVoiceAt(0) is dereferenced unchecked, so reporting the
     * failure on an empty pool faults. */
    CStaticSoundbuffer *voice0 = Sim_VoicePoolGetVoiceAt(pool, 0);
    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3, SRCFILE, 0x102,
        "Could not release MultiStaticSoundbuffer '%s', because the buffer "
        "was not found !", voice0->filename);
}

/* ─── 0x00443660 AcquireSoundBuffer ──────────────────────────────────────
 *
 * Find-or-create the entry, then hand out either its master (once) or a
 * fresh duplicate appended to its clone list.
 */
__declspec(dllexport) CStaticSoundbuffer *__attribute__((thiscall))
SoundMgr_AcquireStatic(SoundManager *self, const char *name, int bWant3D)
{
    ++g_acqStatic;
    { static unsigned long seen; sndmgr_first("AcquireStatic", &seen); }

    if (!self->dwCreated_)
        return NULL;

    NamedEntry     *e;
    NamedEntryList *list;
    int             bDo3D;

    for (;;) {
        bDo3D = 0;
        if (bWant3D == 0) {
            list = &self->entriesPlain_;
        } else {
            list = &self->entries3D_;
            if (self->dwMode3D_)
                bDo3D = 1;
        }

        e = NamedList_Find(list, name);
        if (e != NULL)
            break;

        ++g_acqStaticNew;
        doublesoundbuff *fresh =
            (doublesoundbuff *)operator new(sizeof(doublesoundbuff));
        if (fresh != NULL)
            fresh = Dsb_Init(fresh);
        /* fresh may be NULL here, and is passed on regardless -- the
         * original's fault, one call deeper. */
        if (!SoundMgr_LoadEntryMaster(self, fresh, name,
                                      self->dwDefaultDsFlags_, bDo3D)) {
            if (fresh == NULL)
                return NULL;
            destroy_entry(fresh);
            return NULL;
        }
        NamedList_Insert(list, name, fresh);
        if (!self->dwCreated_)
            return NULL;
        /* Round again; the Find now hits. */
    }

    ++g_acqStaticHit;
    doublesoundbuff *entry = (doublesoundbuff *)e->pPayload;

    if (entry->dwMasterTaken == 0 && sndmgr_fx() != SM_FX_NOSHAREMASTER) {
        entry->dwMasterTaken = 1;
        ++g_masterGrants;
        return entry->master();
    }

    CStaticSoundbuffer *clone = NULL;
    CStaticSoundbuffer *raw =
        (CStaticSoundbuffer *)malloc(sizeof(CStaticSoundbuffer));
    if (raw != NULL)
        clone = CStatic_Init(raw);

    void *r = CStatic_Copy(clone, self->directSound(), entry->master(), 1);
    if (r == (void *)entry->master()
        || ((entry->spareBuf.soundbuffer != NULL
             || SoundMgr_LoadEntryMaster(self, entry->spare(), name,
                                         spare_flags(self->dwDefaultDsFlags_),
                                         bDo3D))
            && CStatic_Copy(clone, self->directSound(), entry->spare(), 1)
               != NULL)) {
        ++g_clones;
        if (r != (void *)entry->master())
            ++g_acqSpare;      /* the master copy failed; the spare carried it */
        LinkedList_Append(entry->clones(), clone);
        sndmgr_census();
        return clone;
    }

    ++g_cloneFail;
    if (clone != NULL) {
        /* vtable slot 0 with the free flag -- CStaticSoundbuffer's scalar
         * vector destructor, still the game's, reached through the object's
         * own table exactly as the original does. */
        typedef void (__attribute__((thiscall)) *dtor_fn)(void *, int);
        (*(dtor_fn *)clone->vtable)(clone, 1);
    }
    return NULL;
}

/* ─── 0x00443810 AcquireVoicePool ────────────────────────────────────────
 *
 * The same find-or-create, then a VoicePool of `nVoices` duplicates.  There
 * is no "take the master" shortcut here: a pool is always built.
 */
__declspec(dllexport) VoicePool *__attribute__((thiscall))
SoundMgr_AcquirePool(SoundManager *self, int nVoices, const char *name,
                     int bWant3D)
{
    ++g_acqPool;
    { static unsigned long seen; sndmgr_first("AcquirePool", &seen); }

    if (!self->dwCreated_)
        return NULL;

    NamedEntry     *e;
    NamedEntryList *list;
    int             bDo3D;

    for (;;) {
        bDo3D = 0;
        if (bWant3D == 0) {
            list = &self->entriesPlain_;
        } else {
            list = &self->entries3D_;
            if (self->dwMode3D_)
                bDo3D = 1;
        }

        e = NamedList_Find(list, name);
        if (e != NULL)
            break;

        ++g_acqPoolNew;
        doublesoundbuff *fresh =
            (doublesoundbuff *)operator new(sizeof(doublesoundbuff));
        if (fresh != NULL)
            fresh = Dsb_Init(fresh);

        /* Recomputed here from the same two values -- the original's second
         * copy of the expression, kept rather than hoisted. */
        int bDo3DAgain = 0;
        if (bWant3D != 0 && self->dwMode3D_ != 0)
            bDo3DAgain = 1;

        if (!SoundMgr_LoadEntryMaster(self, fresh, name,
                                      self->dwDefaultDsFlags_, bDo3DAgain)) {
            if (fresh == NULL)
                return NULL;
            Dsb_Destruct(fresh);
            ++g_entriesDestroyed;
            operator delete(fresh);
            return NULL;
        }
        NamedList_Insert(list, name, fresh);
        if (!self->dwCreated_)
            return NULL;
    }

    ++g_acqPoolHit;
    doublesoundbuff *entry = (doublesoundbuff *)e->pPayload;

    VoicePool *pool = NULL;
    VoicePool *raw  = (VoicePool *)malloc(0x14);
    if (raw != NULL)
        pool = Sim_VoicePoolBlank(raw);

    void *r = Sim_VoicePoolClone(pool, nVoices, self->directSound(),
                                 entry->master(), 1);
    if (r == (void *)entry->master()
        || ((entry->spareBuf.soundbuffer != NULL
             || SoundMgr_LoadEntryMaster(self, entry->spare(), name,
                                         spare_flags(self->dwDefaultDsFlags_),
                                         bDo3D))
            && Sim_VoicePoolClone(pool, nVoices, self->directSound(),
                                  entry->spare(), 1) != NULL)) {
        ++g_pools;
        LinkedList_Append(entry->pools(), pool);
        sndmgr_census();
        return pool;
    }

    ++g_poolFail;
    if (pool == NULL)
        return NULL;
    Sim_VoicePoolWipe(pool);
    free(pool);
    return NULL;
}

/* ─── 0x004439d0 SoundSetup ──────────────────────────────────────────────
 *
 * Switch the 3D listener mode and re-load everything the 3D list holds.
 */
__declspec(dllexport) int __attribute__((thiscall))
SoundMgr_Setup(SoundManager *self, int mode3d)
{
    ++g_setup;
    { static unsigned long seen; sndmgr_first("Setup", &seen); }

    if (!self->dwCreated_)
        return 0;

    if ((unsigned long)mode3d != self->dwMode3D_) {
        ++g_setupModeChange;
        if (!CFaktSound_Create3DListener(self->cfaktSound(), mode3d))
            return 0;

        for (NamedEntry *n = self->entries3D_.pHead; n != NULL; ) {
            doublesoundbuff *entry = (doublesoundbuff *)n->pPayload;
            n = n->pNext;                 /* advanced before the body */
            ++g_setupReloaded;

            if (!CStatic_CreateAndLoad(entry->master(), self->directSound(),
                                       mode3d))
                GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                    SRCFILE, 0x1e8,
                    "CSoundManager::Set3D_LoadNew(...) switch 3D of "
                    "OrgSoundBuffer failed");

            if (entry->spareBuf.soundbuffer != NULL
                && !CStatic_CreateAndLoad(entry->spare(),
                                          self->directSound(), mode3d))
                GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                    SRCFILE, 0x1ee,
                    "CSoundManager::Set3D_LoadNew(...) switch 3D of "
                    "SecOrgSoundBuffer failed");

            /* `src` is carried across BOTH loops below -- see the header
             * comment.  It starts as the master and may move to the spare. */
            CStaticSoundbuffer *src = entry->master();

            for (LinkedListNode *c = entry->cloneList.pHead; c != NULL; ) {
                CStaticSoundbuffer *clone = (CStaticSoundbuffer *)c->pValue;
                c = c->pNextNode;

                CStatic_Reset(clone);
                if (CStatic_Copy(clone, self->directSound(), src, 1)
                    == (void *)src)
                    continue;

                if (src == entry->spare()) {
                    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                        SRCFILE, 0x1ff,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                    continue;      /* already on the spare: nothing left */
                }
                if (entry->spareBuf.soundbuffer == NULL
                    && !SoundMgr_LoadEntryMaster(self, entry->spare(),
                            src->filename, spare_flags(src->dwDsFlags),
                            mode3d))
                    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                        SRCFILE, 0x209,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                src = entry->spare();
                ++g_setupSpare;
                CStatic_Copy(clone, self->directSound(), src, 1);
            }

            for (LinkedListNode *p = entry->voicePoolList.pHead; p != NULL; ) {
                VoicePool *pool = (VoicePool *)p->pValue;
                p = p->pNextNode;

                int nVoices = (int)pool->dwVoiceCount;
                Sim_VoicePoolWipe(pool);
                if (Sim_VoicePoolClone(pool, nVoices, self->directSound(),
                                       src, 1) == (void *)src)
                    continue;

                if (src == entry->spare()) {
                    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                        SRCFILE, 0x221,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                    continue;
                }
                if (entry->spareBuf.soundbuffer == NULL
                    && !SoundMgr_LoadEntryMaster(self, entry->spare(),
                            src->filename, spare_flags(src->dwDsFlags),
                            mode3d))
                    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                        SRCFILE, 0x22b,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                src = entry->spare();
                ++g_setupSpare;
                Sim_VoicePoolClone(pool, nVoices, self->directSound(), src, 1);
            }
        }

        /* Committed even when the list was empty and even when reloads
         * logged failures -- nothing above can make this return 0. */
        self->dwMode3D_        = (unsigned long)mode3d;
        self->dwPendingMode3D_ = (unsigned long)mode3d;
    }

    sndmgr_census();
    return 1;
}

} // extern "C"

/* ─── The class methods callers already speak to ─────────────────────────
 *
 * These were the placeholder's whole purpose: every caller in src/
 * already goes through SoundManager::, so replacing the bodies changed no
 * call site.  They now forward to our own code instead of to an absolute
 * address in the game binary.
 */
void SoundManager::releaseStaticForOwner(void *buffer, int bDestroyIfUnused)
{ SoundMgr_ReleaseStaticForOwner(this, (CStaticSoundbuffer *)buffer,
                                 bDestroyIfUnused); }

void SoundManager::releasePooledForOwner(void *buffer, int bDestroyIfUnused)
{ SoundMgr_ReleasePoolForOwner(this, (VoicePool *)buffer, bDestroyIfUnused); }

CStaticSoundbuffer *SoundManager::acquireStatic(const char *name, int bWant3D)
{ return SoundMgr_AcquireStatic(this, name, bWant3D); }

VoicePool *SoundManager::acquirePool(int count, const char *name, int bWant3D)
{ return SoundMgr_AcquirePool(this, count, name, bWant3D); }

int SoundManager::setup(int mode3d)
{ return SoundMgr_Setup(this, mode3d); }

/* ─── The lifecycle, 0x004430e0..0x004435f0 ───────────────────────────────
 *
 * Read from the disassembly.  The originals' SEH frames guard only the
 * member ctors/dtors, which cannot throw here, so they are dropped.  The
 * vtable is ours (one slot); 0x45efa0 is written nowhere else (a byte scan
 * finds exactly the ctor's and dtor's two immediates). */
static void *const g_SoundMgrVtable[1] = { (void *)&SoundMgr_ScalarDestructor };

/* Destroy every payload of one entry list, then empty it.  The next
 * pointer is read before the payload is destroyed, as the original does. */
static void purge_list(NamedEntryList *list)
{
    for (NamedEntry *e = list->pHead; e != NULL; ) {
        doublesoundbuff *payload = (doublesoundbuff *)e->pPayload;
        e = e->pNext;
        if (payload != NULL) {
            Dsb_Clear(payload);
            Dsb_Destruct(payload);
            operator delete(payload);
        }
    }
    NamedList_Clear(list);
}

extern "C" {

__declspec(dllexport) SoundManager *__attribute__((thiscall))
SoundMgr_Construct(SoundManager *self)
{
    CFaktSound_BlankFields(self->cfaktSound());
    NamedList_Construct(&self->entriesPlain_);
    NamedList_Construct(&self->entries3D_);
    self->logger_           = NULL;
    self->ownsLogger_       = 0;
    self->dwMode3D_         = 0;
    self->dwPendingMode3D_  = 0;
    self->dwCreated_        = 0;
    self->vtable_           = (void *)g_SoundMgrVtable;
    self->dwDefaultDsFlags_ = 2;   /* DSBCAPS_STATIC */
    return self;
}

/* 0x443520.  Also the reset: InitSoundManager runs it first, and Game's
 * teardown (0x414ce2) runs it on its own.  An owned logger is deleted
 * through its vtable slot 0 with flag 1. */
__declspec(dllexport) void __attribute__((thiscall))
SoundMgr_PurgeAssets(SoundManager *self)
{
    purge_list(&self->entriesPlain_);
    purge_list(&self->entries3D_);
    CFaktSound_ReleaseComRefs(self->cfaktSound());
    if (self->ownsLogger_ != 0 && self->logger_ != NULL) {
        typedef void *(__attribute__((thiscall)) *ScalarDtor)(void *, int);
        (*(ScalarDtor *)*(void **)self->logger_)(self->logger_, 1);
    }
    self->logger_           = NULL;
    self->ownsLogger_       = 0;
    self->dwMode3D_         = 0;
    self->dwPendingMode3D_  = 0;
    self->dwCreated_        = 0;
    self->dwDefaultDsFlags_ = 2;
}

__declspec(dllexport) void __attribute__((thiscall))
SoundMgr_Destruct(SoundManager *self)
{
    self->vtable_ = (void *)g_SoundMgrVtable;
    SoundMgr_PurgeAssets(self);
    NamedList_DtorBody(&self->entries3D_);
    NamedList_DtorBody(&self->entriesPlain_);
    CFaktSound_ClearState(self->cfaktSound());
}

/* Reached only through our vtable; the manager is embedded in Game, so
 * nothing deletes one.  Reimplemented, not exercised. */
__declspec(dllexport) SoundManager *__attribute__((thiscall))
SoundMgr_ScalarDestructor(SoundManager *self, unsigned char flags)
{
    SoundMgr_Destruct(self);
    if (flags & 1)
        free(self);
    return self;
}

/* 0x4431f0.  With no logger passed, it makes its own "SoundManager.log" on
 * the game heap -- still alloc.h: the owned logger is freed through its
 * scalar dtor, GameLog_ScalarDeletingDtor, which frees with the game's
 * Free2 for every logger.  A failed allocation leaves logger_ NULL and
 * ownsLogger_ 1, as the original does. */
__declspec(dllexport) int __attribute__((thiscall))
SoundMgr_Init(SoundManager *self, int enable3d, HWND window,
              UINT bufferflags, short channels, int samplespersec,
              USHORT bitspersample, GameLogger *logger)
{
    SoundMgr_PurgeAssets(self);
    self->logger_ = logger;
    if (logger == NULL) {
        GameLogger *own = (GameLogger *)malloc(0x118);
        if (own != NULL)
            own = (GameLogger *)GameLog_Initialize(own, GS_SOUNDMGR_LOG_NAME, NULL);
        self->logger_     = own;
        self->ownsLogger_ = 1;
    }
    /* Ghidra types the argument `bool`; the listing tests the whole dword
     * (`test edi,edi`) and stores the whole dword below. */
    int ok = enable3d == 0
        ? CFaktSound_Initialize(self->cfaktSound(), window, bufferflags,
                                channels, samplespersec, bitspersample,
                                self->logger_)
        : CFaktSound_InitializeWith3DAudio(self->cfaktSound(), window,
                                bufferflags, channels, samplespersec,
                                bitspersample, self->logger_);
    if (ok == 0)
        return 0;
    self->dwPendingMode3D_ = enable3d;
    self->dwMode3D_        = enable3d;
    self->dwCreated_       = 1;
    return 1;
}

}
