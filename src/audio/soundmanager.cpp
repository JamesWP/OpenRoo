/* Static buffers and voice pools copy from an entry's master buffer; if that
 * fails, from its spare, a second load of the same file in software.
 *
 * Copy and Clone return the source on success, so every success test here is
 * `result == src`.  The master sits at offset 0 of the entry, so the master's
 * address is the entry's.  Every copy passes 1: no reload from the file,
 * because the manager wants the failure so it can try the spare.
 *
 * PRESERVED:
 *   - Both acquires test the allocation, then carry a NULL onward: the
 *     loader dereferences it one call deeper.
 *   - The pool release's "not found" message reads voice 0 unchecked, so an
 *     empty pool faults while reporting.
 *   - Both releases try the 3D list when the plain list does not have the
 *     buffer or its entry refuses it.
 *   - Setup commits the new mode even when nothing reloaded, or reloads
 *     failed.
 *   - Setup reloads only the 3D list.
 *   - AcquirePool computes the 3D flag twice from the same two values.
 *   - In Setup the copy source moves from the master to the spare on the
 *     first failure, and stays there for the rest of the duplicates and all
 *     of the entry's voice pools. */

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

/* The source file named in error lines, with __LINE__. */
static const char SRCFILE[] =
    "src/audio/soundmanager.cpp";

/* KAROO_SNDMGR_FX=nosharemaster is a negative control: the first acquirer
 * never gets the master, only a duplicate like everyone else.  It only costs
 * one extra duplicate per sound and moves no geometry, so both gates can host
 * it.  KAROO_SNDMGR_DIAG's masterGrants goes to 0 and clones rises by as many;
 * KAROO_DSB_DIAG's masterLent falls to 0. */
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

/* KAROO_SNDMGR_DIAG=1 logs each function's first call and a census of
 * acquires, releases, loads and setups. */
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
static unsigned long g_acqSpare;  // an acquire the spare had to carry

static void sndmgr_first(const char *fn, unsigned long *pSeen)
{
    if (!sndmgr_diag() || *pSeen != 0)
        return;
    *pSeen = 1;
    log_write("soundmgr: first call to %s\n", fn);
}

/* Dumped from the releases, setup and the acquire hits, so a run that never
 * releases still reports. */
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

/* The spare's flags: drop DSBCAPS_LOCHARDWARE, add DSBCAPS_LOCSOFTWARE. */
static inline unsigned long spare_flags(unsigned long dwFlags)
{
    return (dwFlags & 0xfffffffbUL) | 0x8UL;
}

/* Destroys an entry and frees it. */
static void destroy_entry(doublesoundbuff *entry)
{
    ++g_entriesDestroyed;
    Dsb_Destruct(entry);
    operator delete(entry);
}


/* entry is either an entry or the address of an entry's spare buffer, which
 * works because the master is at offset 0. */
int SoundMgr_LoadEntryMaster(SoundManager *self, void *entry,
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

/* The buffer's own file name is the lookup key. */
void SoundMgr_ReleaseStaticForOwner(SoundManager *self, CStaticSoundbuffer *buf,
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
            continue;  // this entry did not own it: try the other list
        if (i == 0) ++g_relStaticPlain; else ++g_relStatic3D;
        if (!bDestroyIfUnused)
            return;
        if (!Dsb_IsFullyReleased(entry))
            return;
        NamedList_Remove(lists[i], e);
        if (entry == NULL)  // cannot be NULL
            return;
        destroy_entry(entry);
        sndmgr_census();
        return;
    }

    ++g_relStaticLost;
    sndmgr_census();
    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3, SRCFILE, __LINE__,
        "Could not release StaticSoundbuffer '%s', because the buffer was "
        "not found !", buf->filename);
}

/* The key is the pool's voice 0's file name, fetched again for each list. */
void SoundMgr_ReleasePoolForOwner(SoundManager *self, VoicePool *pool,
                                  int bDestroyIfUnused)
{
    ++g_relPool;
    { static unsigned long seen; sndmgr_first("ReleasePoolForOwner", &seen); }

    NamedEntryList *lists[2] = { &self->entriesPlain_, &self->entries3D_ };
    for (int i = 0; i < 2; i++) {
        // Fetched per list.
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
    // PRESERVED: voice 0 is used unchecked, so reporting on an empty pool
    // faults.
    CStaticSoundbuffer *voice0 = Sim_VoicePoolGetVoiceAt(pool, 0);
    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3, SRCFILE, __LINE__,
        "Could not release MultiStaticSoundbuffer '%s', because the buffer "
        "was not found !", voice0->filename);
}

/* Finds or creates the entry, then hands out its master once, or a fresh
 * duplicate added to its borrower list. */
CStaticSoundbuffer *
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
        // PRESERVED: a failed allocation is passed on, and faults in the
        // loader.
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
    // Round again; the lookup now hits.
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
            ++g_acqSpare;  // the master copy failed; the spare carried it
        LinkedList_Append(entry->clones(), clone);
        sndmgr_census();
        return clone;
    }

    ++g_cloneFail;
    if (clone != NULL) {
        // Deleted through its own vtable, with the free flag.
        typedef void (*dtor_fn)(void *, int);
        (*(dtor_fn *)clone->vtable)(clone, 1);
    }
    return NULL;
}

/* The same find-or-create, then a pool of nVoices duplicates.  There is no
 * master shortcut: a pool is always built. */
VoicePool *
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

        // PRESERVED: the same flag computed a second time.
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
    VoicePool *raw  = (VoicePool *)malloc(sizeof(VoicePool));
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

/* Switches the 3D listener mode and reloads everything in the 3D list. */
int SoundMgr_Setup(SoundManager *self, int mode3d)
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
            n = n->pNext;  // advanced before the body
            ++g_setupReloaded;

            if (!CStatic_CreateAndLoad(entry->master(), self->directSound(),
                                       mode3d))
                GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                    SRCFILE, __LINE__,
                    "CSoundManager::Set3D_LoadNew(...) switch 3D of "
                    "OrgSoundBuffer failed");

            if (entry->spareBuf.soundbuffer != NULL
                && !CStatic_CreateAndLoad(entry->spare(),
                                          self->directSound(), mode3d))
                GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                    SRCFILE, __LINE__,
                    "CSoundManager::Set3D_LoadNew(...) switch 3D of "
                    "SecOrgSoundBuffer failed");

            // The copy source, carried across both loops (see the top of the
            // file).
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
                        SRCFILE, __LINE__,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                    continue;  // already on the spare: nothing left to try
                }
                if (entry->spareBuf.soundbuffer == NULL
                    && !SoundMgr_LoadEntryMaster(self, entry->spare(),
                            src->filename, spare_flags(src->dwDsFlags),
                            mode3d))
                    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                        SRCFILE, __LINE__,
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
                        SRCFILE, __LINE__,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                    continue;
                }
                if (entry->spareBuf.soundbuffer == NULL
                    && !SoundMgr_LoadEntryMaster(self, entry->spare(),
                            src->filename, spare_flags(src->dwDsFlags),
                            mode3d))
                    GameLog_LogSourceLocation((GameLogger *)self->logger_, 3,
                        SRCFILE, __LINE__,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                src = entry->spare();
                ++g_setupSpare;
                Sim_VoicePoolClone(pool, nVoices, self->directSound(), src, 1);
            }
        }

        // PRESERVED: committed even if the list was empty or reloads failed.
        self->dwMode3D_        = (unsigned long)mode3d;
        self->dwPendingMode3D_ = (unsigned long)mode3d;
    }

    sndmgr_census();
    return 1;
}


/* The methods callers use. */
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

/* The one-slot vtable: the deleting destructor. */
static void *const g_SoundMgrVtable[1] = { (void *)&SoundMgr_ScalarDestructor };

/* Destroys every entry in one list, then empties it.  The next pointer is read
 * before the entry is destroyed. */
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


SoundManager *SoundMgr_Construct(SoundManager *self)
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
    self->dwDefaultDsFlags_ = 2;  // DSBCAPS_STATIC
    return self;
}

/* Also the reset: Init runs it first, and the Game's teardown on its own.  An
 * owned logger is deleted through its vtable with the free flag. */
void SoundMgr_PurgeAssets(SoundManager *self)
{
    purge_list(&self->entriesPlain_);
    purge_list(&self->entries3D_);
    CFaktSound_ReleaseComRefs(self->cfaktSound());
    if (self->ownsLogger_ != 0 && self->logger_ != NULL) {
        typedef void *(*ScalarDtor)(void *, int);
        (*(ScalarDtor *)*(void **)self->logger_)(self->logger_, 1);
    }
    self->logger_           = NULL;
    self->ownsLogger_       = 0;
    self->dwMode3D_         = 0;
    self->dwPendingMode3D_  = 0;
    self->dwCreated_        = 0;
    self->dwDefaultDsFlags_ = 2;
}

void SoundMgr_Destruct(SoundManager *self)
{
    self->vtable_ = (void *)g_SoundMgrVtable;
    SoundMgr_PurgeAssets(self);
    NamedList_DtorBody(&self->entries3D_);
    NamedList_DtorBody(&self->entriesPlain_);
    CFaktSound_ClearState(self->cfaktSound());
}

/* Reached only through the vtable; the manager is embedded in the Game, so
 * nothing deletes one. */
SoundManager *SoundMgr_ScalarDestructor(SoundManager *self, unsigned char flags)
{
    SoundMgr_Destruct(self);
    if (flags & 1)
        free(self);
    return self;
}

/* PRESERVED: a failed logger allocation leaves logger_ NULL and ownsLogger_ 1.
 */
int SoundMgr_Init(SoundManager *self, int enable3d, HWND window,
                  UINT bufferflags, short channels, int samplespersec,
                  USHORT bitspersample, GameLogger *logger)
{
    SoundMgr_PurgeAssets(self);
    self->logger_ = logger;
    if (logger == NULL) {
        GameLogger *own = (GameLogger *)malloc(sizeof(GameLogger));
        if (own != NULL)
            own = (GameLogger *)GameLog_Initialize(own, GS_SOUNDMGR_LOG_NAME, NULL);
        self->logger_     = own;
        self->ownsLogger_ = 1;
    }
    // enable3d is tested and stored as a whole word.
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

