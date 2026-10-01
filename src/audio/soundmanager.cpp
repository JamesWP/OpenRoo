/* Static buffers and voice pools duplicate an entry's master buffer; if that
 * fails, its spare, a second load of the same file.
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

#include <new>
#include "soundmanager.h"
#include "doublesoundbuff.h"
#include "namedlist.h"
#include "audiodev.h"
#include "voicepool.h"
#include "linkedlist.h"
#include "gamelog.h"
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

/* Destroys an entry and frees it. */
static void destroy_entry(doublesoundbuff *entry)
{
    ++g_entriesDestroyed;
    delete entry;
}

 

/* Loads filename into buf: an entry's master or its spare. */
int SoundManager::loadEntryMaster(audiodev::Buffer *buf,
                         const char *filename, int bDo3D)
{
    ++g_loadMaster;
    { static unsigned long seen; sndmgr_first("LoadEntryMaster", &seen); }

    if (bDo3D) {
        ++g_loadMaster3D;
        int ok = buf->load(device_, filename, true);
        if (ok)
            buf->set3DEnabled(dwPendingMode3D_ != 0);
        else
            ++g_loadFail;
        return ok;
    }

    int ok = buf->load(device_, filename, false);
    if (!ok)
        ++g_loadFail;
    return ok;
}

/* The buffer's own file name is the lookup key. */
void SoundManager::releaseStaticForOwner(audiodev::Buffer *buf,
                               int bDestroyIfUnused)
{
    ++g_relStatic;
    { static unsigned long seen; sndmgr_first("ReleaseStaticForOwner", &seen); }

    NamedEntryList *lists[2] = { &entriesPlain_, &entries3D_ };
    for (int i = 0; i < 2; i++) {
        NamedEntry *e = lists[i]->find(buf->filename());
        if (e == NULL)
            continue;
        doublesoundbuff *entry = (doublesoundbuff *)e->payload();
        if (!entry->releaseStatic(buf))
            continue;  // this entry did not own it: try the other list
        if (i == 0) ++g_relStaticPlain; else ++g_relStatic3D;
        if (!bDestroyIfUnused)
            return;
        if (!entry->isFullyReleased())
            return;
        lists[i]->remove(e);
        if (entry == NULL)  // cannot be NULL
            return;
        destroy_entry(entry);
        sndmgr_census();
        return;
    }

    ++g_relStaticLost;
    sndmgr_census();
    ((GameLogger *)logger_)->logSourceLocation(3, SRCFILE, __LINE__,
        "Could not release StaticSoundbuffer '%s', because the buffer was "
        "not found !", buf->filename());
}

/* The key is the pool's voice 0's file name, fetched again for each list. */
void SoundManager::releasePooledForOwner(VoicePool *pool,
                             int bDestroyIfUnused)
{
    ++g_relPool;
    { static unsigned long seen; sndmgr_first("ReleasePoolForOwner", &seen); }

    NamedEntryList *lists[2] = { &entriesPlain_, &entries3D_ };
    for (int i = 0; i < 2; i++) {
        // Fetched per list.
        const char *name = pool->firstFilename();
        NamedEntry *e = lists[i]->find(name);
        if (e == NULL)
            continue;
        doublesoundbuff *entry = (doublesoundbuff *)e->payload();
        if (!entry->releasePool(pool))
            continue;
        if (i == 0) ++g_relPoolPlain; else ++g_relPool3D;
        if (!bDestroyIfUnused)
            return;
        if (!entry->isFullyReleased())
            return;
        lists[i]->remove(e);
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
    audiodev::Buffer *voice0 = pool->voiceAt(0);
    ((GameLogger *)logger_)->logSourceLocation(3, SRCFILE, __LINE__,
        "Could not release MultiStaticSoundbuffer '%s', because the buffer "
        "was not found !", voice0->filename());
}

/* Finds or creates the entry, then hands out its master once, or a fresh
 * duplicate added to its borrower list. */
audiodev::Buffer *SoundManager::acquireStatic(const char *name, int bWant3D)
{
    ++g_acqStatic;
    { static unsigned long seen; sndmgr_first("AcquireStatic", &seen); }

    if (!dwCreated_)
        return NULL;

    NamedEntry     *e;
    NamedEntryList *list;
    int             bDo3D;

    for (;;) {
        bDo3D = 0;
        if (bWant3D == 0) {
            list = &entriesPlain_;
        } else {
            list = &entries3D_;
            if (dwMode3D_)
                bDo3D = 1;
        }

        e = list->find(name);
        if (e != NULL)
            break;

        ++g_acqStaticNew;
        doublesoundbuff *fresh = new doublesoundbuff();
        // PRESERVED: a failed allocation is passed on, and faults in the
        // loader.
        if (!loadEntryMaster(fresh->master(), name, bDo3D)) {
            if (fresh == NULL)
                return NULL;
            destroy_entry(fresh);
            return NULL;
        }
        list->insert(name, fresh);
        if (!dwCreated_)
            return NULL;
    // Round again; the lookup now hits.
    }

    ++g_acqStaticHit;
    doublesoundbuff *entry = (doublesoundbuff *)e->payload();

    if (entry->masterTaken() == 0 && sndmgr_fx() != SM_FX_NOSHAREMASTER) {
        entry->setMasterTaken(1);
        ++g_masterGrants;
        return entry->master();
    }

    audiodev::Buffer *clone = new (std::nothrow) audiodev::Buffer();

    bool r = clone->duplicate(device_, *entry->master());
    if (r
        || ((entry->spare()->isLoaded()
             || loadEntryMaster(entry->spare(), name, bDo3D))
            && clone->duplicate(device_, *entry->spare()))) {
        ++g_clones;
        if (!r)
            ++g_acqSpare;  // the master copy failed; the spare carried it
        entry->clones()->append(clone);
        sndmgr_census();
        return clone;
    }

    ++g_cloneFail;
    delete clone;
    return NULL;
}

/* The same find-or-create, then a pool of nVoices duplicates.  There is no
 * master shortcut: a pool is always built. */
VoicePool *SoundManager::acquirePool(int nVoices, const char *name,
                     int bWant3D)
{
    ++g_acqPool;
    { static unsigned long seen; sndmgr_first("AcquirePool", &seen); }

    if (!dwCreated_)
        return NULL;

    NamedEntry     *e;
    NamedEntryList *list;
    int             bDo3D;

    for (;;) {
        bDo3D = 0;
        if (bWant3D == 0) {
            list = &entriesPlain_;
        } else {
            list = &entries3D_;
            if (dwMode3D_)
                bDo3D = 1;
        }

        e = list->find(name);
        if (e != NULL)
            break;

        ++g_acqPoolNew;
        doublesoundbuff *fresh = new doublesoundbuff();

        // PRESERVED: the same flag computed a second time.
        int bDo3DAgain = 0;
        if (bWant3D != 0 && dwMode3D_ != 0)
            bDo3DAgain = 1;

        if (!loadEntryMaster(fresh->master(), name, bDo3DAgain)) {
            if (fresh == NULL)
                return NULL;
            ++g_entriesDestroyed;
            delete fresh;
            return NULL;
        }
        list->insert(name, fresh);
        if (!dwCreated_)
            return NULL;
    }

    ++g_acqPoolHit;
    doublesoundbuff *entry = (doublesoundbuff *)e->payload();

    VoicePool *pool = new (std::nothrow) VoicePool();

    if (pool->clone(nVoices, device_, *entry->master())
        || ((entry->spare()->isLoaded()
             || loadEntryMaster(entry->spare(), name, bDo3D))
            && pool->clone(nVoices, device_, *entry->spare()))) {
        ++g_pools;
        entry->pools()->append(pool);
        sndmgr_census();
        return pool;
    }

    ++g_poolFail;
    if (pool == NULL)
        return NULL;
    delete pool;
    return NULL;
}

/* Switches the 3D listener mode and reloads everything in the 3D list. */
int SoundManager::setup(int mode3d)
{
    ++g_setup;
    { static unsigned long seen; sndmgr_first("Setup", &seen); }

    if (!dwCreated_)
        return 0;

    if ((unsigned long)mode3d != dwMode3D_) {
        ++g_setupModeChange;
        if (!device_.set3DEnabled(mode3d != 0))
            return 0;

        for (NamedEntry *n = entries3D_.head(); n != NULL; ) {
            doublesoundbuff *entry = (doublesoundbuff *)n->payload();
            n = n->next();  // advanced before the body
            ++g_setupReloaded;

            if (!entry->master()->reload3D(device_, mode3d != 0))
                ((GameLogger *)logger_)->logSourceLocation(3,
                    SRCFILE, __LINE__,
                    "CSoundManager::Set3D_LoadNew(...) switch 3D of "
                    "OrgSoundBuffer failed");

            if (entry->spare()->isLoaded()
                && !entry->spare()->reload3D(device_, mode3d != 0))
                ((GameLogger *)logger_)->logSourceLocation(3,
                    SRCFILE, __LINE__,
                    "CSoundManager::Set3D_LoadNew(...) switch 3D of "
                    "SecOrgSoundBuffer failed");

            // The copy source, carried across both loops (see the top of the
            // file).
            audiodev::Buffer *src = entry->master();

            for (LinkedListNode *c = entry->clones()->head(); c != NULL; ) {
                audiodev::Buffer *clone = (audiodev::Buffer *)c->value();
                c = c->next();

                clone->reset();
                if (clone->duplicate(device_, *src))
                    continue;

                if (src == entry->spare()) {
                    ((GameLogger *)logger_)->logSourceLocation(3,
                        SRCFILE, __LINE__,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                    continue;  // already on the spare: nothing left to try
                }
                if (!entry->spare()->isLoaded()
                    && !loadEntryMaster(entry->spare(),
                            src->filename(), mode3d))
                    ((GameLogger *)logger_)->logSourceLocation(3,
                        SRCFILE, __LINE__,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                src = entry->spare();
                ++g_setupSpare;
                clone->duplicate(device_, *src);
            }

            for (LinkedListNode *p = entry->pools()->head(); p != NULL; ) {
                VoicePool *pool = (VoicePool *)p->value();
                p = p->next();

                int nVoices = (int)pool->voiceCount();
                pool->wipe();
                if (pool->clone(nVoices, device_, *src))
                    continue;

                if (src == entry->spare()) {
                    ((GameLogger *)logger_)->logSourceLocation(3,
                        SRCFILE, __LINE__,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                    continue;
                }
                if (!entry->spare()->isLoaded()
                    && !loadEntryMaster(entry->spare(),
                            src->filename(), mode3d))
                    ((GameLogger *)logger_)->logSourceLocation(3,
                        SRCFILE, __LINE__,
                        "CSoundManager::Set3D_LoadNew(...) Create of "
                        "SecOrgSoundBuffer failed");
                src = entry->spare();
                ++g_setupSpare;
                pool->clone(nVoices, device_, *src);
            }
        }

        // PRESERVED: committed even if the list was empty or reloads failed.
        dwMode3D_        = (unsigned long)mode3d;
        dwPendingMode3D_ = (unsigned long)mode3d;
    }

    sndmgr_census();
    return 1;
}

/* Destroys every entry in one list, then empties it.  The next pointer is read
 * before the entry is destroyed. */
static void purge_list(NamedEntryList *list)
{
    for (NamedEntry *e = list->head(); e != NULL; ) {
        doublesoundbuff *payload = (doublesoundbuff *)e->payload();
        e = e->next();
        if (payload != NULL) {
            payload->clear();
            delete payload;
        }
    }
    list->clear();
}

 

SoundManager::SoundManager()
{
    logger_           = NULL;
    ownsLogger_       = 0;
    dwMode3D_         = 0;
    dwPendingMode3D_  = 0;
    dwCreated_        = 0;
}

/* Also the reset: Init runs it first, and the Game's teardown on its own.  An
 * owned logger is deleted. */
void SoundManager::purgeAssets()
{
    purge_list(&entriesPlain_);
    purge_list(&entries3D_);
    device_.destroy();
    if (ownsLogger_ != 0 && logger_ != NULL) {
        delete (GameLogger *)logger_;
    }
    logger_           = NULL;
    ownsLogger_       = 0;
    dwMode3D_         = 0;
    dwPendingMode3D_  = 0;
    dwCreated_        = 0;
}

SoundManager::~SoundManager()
{
    purgeAssets();
}


/* PRESERVED: a failed logger allocation leaves logger_ NULL and ownsLogger_ 1.
 */
int SoundManager::init(int enable3d, void *window, int channels,
              int samplespersec, int bitspersample, GameLogger *logger)
{
    purgeAssets();
    logger_ = logger;
    if (logger == NULL) {
        logger_     = new (std::nothrow) GameLogger(GS_SOUNDMGR_LOG_NAME, NULL);
        ownsLogger_ = 1;
    }
    audiodev::DeviceConfig config;
    config.window        = window;
    config.enable3D      = enable3d != 0;
    config.channels      = channels;
    config.sampleRate    = samplespersec;
    config.bitsPerSample = bitspersample;
    // enable3d is stored as a whole word.
    if (!device_.create(config))
        return 0;
    dwPendingMode3D_ = enable3d;
    dwMode3D_        = enable3d;
    dwCreated_       = 1;
    return 1;
}
