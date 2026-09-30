#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include "static.h"
#include <new>
#include "voicepool.h"
#include <stdlib.h>
#include "log.h"

/* KAROO_POOL_DIAG=1 counts calls to every pool function, logs each one's first
 * call, and totals the voices allocated, to tell "the gates never build a
 * pool" from "the gates build pools and assert nothing about them". */
static int pool_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_POOL_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static unsigned long g_nBlank, g_nWipe, g_nFill3D, g_nClone,
                     g_nGetVoiceAt, g_nFirstName, g_nCycle, g_nBroadcast,
                     g_nVoices, g_nCopyFail, g_nNestBail;

static void pool_first(const char *what, unsigned long *seen)
{
    if (!pool_diag() || *seen) return;
    *seen = 1;
    log_write("voicepool: first call to %s\n", what);
}

/* Reported from the pool-building calls, a handful per level load; Cycle and
 * Broadcast are the hot pair and are only counted. */
static void pool_census(void)
{
    if (!pool_diag()) return;
    log_write("voicepool: DIAG blank=%lu wipe=%lu fill3d=%lu clone=%lu "
              "getvoice=%lu firstname=%lu cycle=%lu broadcast=%lu "
              "voicesAllocated=%lu copyFallbacks=%lu nestBails=%lu\n",
              g_nBlank, g_nWipe, g_nFill3D, g_nClone, g_nGetVoiceAt,
              g_nFirstName, g_nCycle, g_nBroadcast, g_nVoices,
              g_nCopyFail, g_nNestBail);
}

/* KAROO_POOL_FX=onevoice is a negative control: Fill3D and Clone build one
 * voice, whatever count is asked for.  A one-voice pool cannot overlap a sound
 * with itself.  KAROO_POOL_DIAG's voice total reads it directly. */
enum PoolFx { POOL_FX_OFF = 0, POOL_FX_ONEVOICE = 1 };

static PoolFx pool_fx(void)
{
    static int cached = -1;
    if (cached >= 0) return (PoolFx)cached;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("KAROO_POOL_FX", buf, sizeof(buf));
    PoolFx fx = POOL_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "onevoice") == 0) fx = POOL_FX_ONEVOICE;
    }
    log_write("voicepool: FX mode = %s\n",
              fx == POOL_FX_ONEVOICE ? "onevoice" : "off");
    cached = (int)fx;
    return fx;
}

/* PRESERVED: it halts and re-triggers the same slot; the increment only
 * decides where the next call lands.  The cursor is stored unwrapped before
 * the trigger and wrapped after it, with a signed compare.  Only the voices
 * pointer is checked, so a zero-count pool still plays voice 0 once. */
int VoicePool::cycle(DWORD dwLoopFlags)
{
    ++g_nCycle; { static unsigned long seen; pool_first("Cycle", &seen); }
    if (pBufs_ == 0)
        return (int)0x887800AA;  // DSERR_UNINITIALIZED

    pBufs_[dwCurrentIdx_].haltPlayback();

    const int old = dwCurrentIdx_;
    dwCurrentIdx_ = old + 1;  // stored before the trigger, unwrapped

    const int hr = pBufs_[old].triggerPlayback(dwLoopFlags);

    if (dwCurrentIdx_ >= dwVoiceCount_)  // signed, and after
        dwCurrentIdx_ = 0;

    return hr;
}

/* PRESERVED: only voice 0's 3D buffer is checked before every voice's is used,
 * and the voices pointer is not checked at all.  Pools are all-3D or all-2D,
 * so neither fires. */
void VoicePool::broadcastCoordinates(float x, float y, float z, DWORD dwApply)
{
    ++g_nBroadcast; { static unsigned long seen; pool_first("Broadcast", &seen); }
    if (pBufs_->threeDBuffer() == 0 || dwVoiceCount_ <= 0)
        return;

    int i = 0;
    do {
        pBufs_[i].threeDBuffer()->SetPosition(x, y, z, dwApply);
        i++;
    } while (i < dwVoiceCount_);
}

VoicePool::VoicePool()
{
    ++g_nBlank; { static unsigned long seen; pool_first("Blank", &seen); }
    pBufs_        = 0;
    dwVoiceCount_ = 0;
    dwCurrentIdx_ = 0;
    logger_       = 0;
    dwNestDepth_  = 0;
}

VoicePool::~VoicePool()
{
    wipe();
}

/* Resets every voice, destroys the array through the voices' vector destructor
 * (flag 3: an array, free the block) and clears the pool.  PRESERVED: the
 * nesting depth is not cleared; Fill3D's guard depends on it (see there).  The
 * count is re-read on every pass. */
void VoicePool::wipe()
{
    ++g_nWipe; { static unsigned long seen; pool_first("Wipe", &seen); }
    pool_census();
    if (pBufs_ != 0) {
        for (int i = 0; i < dwVoiceCount_; i++)  // the count is re-read on every pass
            pBufs_[i].reset();

        delete[] pBufs_;
        pBufs_ = 0;
    }
    logger_       = 0;
    dwVoiceCount_ = 0;
    dwCurrentIdx_ = 0;

/* PRESERVED: the nesting depth is left alone. */
}

/* PRESERVED: the software-buffer retry never runs.  The function guards
 * re-entry with the nesting depth; its last resort wipes and calls itself with
 * DSBCAPS_LOCSOFTWARE, but the depth is still raised, so the inner call trips
 * the guard and fails.  A pool that fails in hardware fails outright; running
 * the retry would allocate buffers the game never did. */
int VoicePool::fill3D(int count, IDirectSound *pDS,
                    DWORD dwDsFlags, const char *filename, void *logger)
{
    ++g_nFill3D; { static unsigned long seen; pool_first("Fill3D", &seen); }
    pool_census();
    if (pool_fx() == POOL_FX_ONEVOICE && count > 1)
        count = 1;  // KAROO_POOL_FX=onevoice

    wipe();

    if (++dwNestDepth_ > 1) {  // re-entrancy guard
        ++g_nNestBail;
        goto fail;
    }

    if (count < 1 || logger == 0)
        goto fail;

    logger_       = logger;
    dwVoiceCount_ = count;

    {
        CStaticSoundbuffer *bufs = new (std::nothrow) CStaticSoundbuffer[count];
        if (bufs != 0)
            g_nVoices += (unsigned long)count;
        pBufs_ = bufs;
    }

    // Voice 0 comes off disk.  A failed allocation reaches here as a NULL
    // this, as in the game.
    if (!pBufs_[0].createAndLoad3DSoundFile(pDS, dwDsFlags,
                                          filename, logger)) {
        wipe();
        goto fail;
    }

    {
        CStaticSoundbuffer *src = pBufs_;          // voice 0, the template
        for (int i = 1; i < dwVoiceCount_; i++) {  // the count is re-read on every pass
            if (pBufs_[i].copy(pDS, src, 1) != 0)
                continue;
            ++g_nCopyFail;
            if (pBufs_[i].createAndLoad3DSoundFile(pDS,
                                                 dwDsFlags, filename, logger))
                continue;

            // The software retry: it cannot succeed (see above).
            wipe();
            {
                int r = fill3D(count, pDS,
                                            dwDsFlags | DSBCAPS_LOCSOFTWARE,
                                            filename, logger);
                dwNestDepth_--;
                return r;
            }
        }
    }

    dwNestDepth_--;
    return 1;

fail:
    dwNestDepth_--;
    return 0;
}

/* PRESERVED: success returns src, the caller's pointer, on the duplicate path
 * and the voices array after a reload; callers only test for NULL.  A
 * duplicate succeeds when Copy returns src.  noFallback gives up on the first
 * failed duplicate instead of reloading.  The reload asks for software
 * buffers, and as an outer call it can succeed. */
void *VoicePool::clone(int count, IDirectSound *pDS,
                   CStaticSoundbuffer *src, int noFallback)
{
    ++g_nClone; { static unsigned long seen; pool_first("Clone", &seen); }
    pool_census();
    if (pool_fx() == POOL_FX_ONEVOICE && count > 1)
        count = 1;  // KAROO_POOL_FX=onevoice

    if (count < 1)
        return 0;

    wipe();

    dwVoiceCount_ = count;
    logger_       = src->logger();

    {
        CStaticSoundbuffer *bufs = new (std::nothrow) CStaticSoundbuffer[count];
        if (bufs != 0)
            g_nVoices += (unsigned long)count;
        pBufs_ = bufs;
    }

    for (int i = 0; i < dwVoiceCount_; i++) {  // the count is re-read on every pass
        if (pBufs_[i].copy(pDS, src, 0) == (void *)src)
            continue;
        ++g_nCopyFail;

        wipe();
        if (noFallback != 0)
            return 0;

        if (fill3D(count, pDS,
                                src->dsFlags() | DSBCAPS_LOCSOFTWARE,
                                src->filename(), src->logger()) == 0)
            return 0;
        return pBufs_;
    }

    return src;  // the caller's own pointer
}

/* Signed compares.  The voices pointer is not checked. */
CStaticSoundbuffer *VoicePool::voiceAt(int index)
{
    ++g_nGetVoiceAt; { static unsigned long seen; pool_first("GetVoiceAt", &seen); }
    if (index < 0 || index >= dwVoiceCount_)
        return 0;
    return &pBufs_[index];
}

char *VoicePool::firstFilename()
{
    ++g_nFirstName; { static unsigned long seen; pool_first("FirstFilename", &seen); }
    CStaticSoundbuffer *voice = voiceAt(0);
    if (voice == 0)
        return 0;
    return voice->filename();
}
