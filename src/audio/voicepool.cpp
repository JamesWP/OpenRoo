#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include "static.h"
#include "voicepool.h"
#include <stdlib.h>
#include "log.h"

extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_HaltPlayback(CStaticSoundbuffer *self);

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
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolCycle(VoicePool *self, DWORD dwLoopFlags)
{
    ++g_nCycle; { static unsigned long seen; pool_first("Cycle", &seen); }
    if (self->pBufs == 0)
        return (int)0x887800AA;  // DSERR_UNINITIALIZED

    CStatic_HaltPlayback(&self->pBufs[self->dwCurrentIdx]);

    const int old = self->dwCurrentIdx;
    self->dwCurrentIdx = old + 1;  // stored before the trigger, unwrapped

    const int hr = CStatic_TriggerPlayback(&self->pBufs[old], dwLoopFlags);

    if (self->dwCurrentIdx >= self->dwVoiceCount)  // signed, and after
        self->dwCurrentIdx = 0;

    return hr;
}

/* PRESERVED: only voice 0's 3D buffer is checked before every voice's is used,
 * and the voices pointer is not checked at all.  Pools are all-3D or all-2D,
 * so neither fires. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_BroadcastPoolVoiceCoordinates(VoicePool *self,
                                  float x, float y, float z, DWORD dwApply)
{
    ++g_nBroadcast; { static unsigned long seen; pool_first("Broadcast", &seen); }
    if (self->pBufs->threeDBuffer == 0 || self->dwVoiceCount <= 0)
        return;

    int i = 0;
    do {
        self->pBufs[i].threeDBuffer->SetPosition(x, y, z, dwApply);
        i++;
    } while (i < self->dwVoiceCount);
}

extern "C" __declspec(dllexport) VoicePool * __attribute__((thiscall))
Sim_VoicePoolBlank(VoicePool *self)
{
    ++g_nBlank; { static unsigned long seen; pool_first("Blank", &seen); }
    self->pBufs        = 0;
    self->dwVoiceCount = 0;
    self->dwCurrentIdx = 0;
    self->logger       = 0;
    self->dwNestDepth  = 0;
    return self;
}

/* Resets every voice, destroys the array through the voices' vector destructor
 * (flag 3: an array, free the block) and clears the pool.  PRESERVED: the
 * nesting depth is not cleared; Fill3D's guard depends on it (see there).  The
 * count is re-read on every pass. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_VoicePoolWipe(VoicePool *self)
{
    ++g_nWipe; { static unsigned long seen; pool_first("Wipe", &seen); }
    pool_census();
    if (self->pBufs != 0) {
        for (int i = 0; i < self->dwVoiceCount; i++)  // the count is re-read on every pass
            CStatic_Reset(&self->pBufs[i]);

        if (self->pBufs != 0) {  // re-tested; harmless
            typedef void *(__attribute__((thiscall)) *vec_dtor_fn)(void *self, int flags);
            vec_dtor_fn dtor = *(vec_dtor_fn *)self->pBufs->vtable;
            dtor(self->pBufs, 3);
        }
        self->pBufs = 0;
    }
    self->logger       = 0;
    self->dwVoiceCount = 0;
    self->dwCurrentIdx = 0;

/* PRESERVED: the nesting depth is left alone. */
}

/* PRESERVED: the software-buffer retry never runs.  The function guards
 * re-entry with the nesting depth; its last resort wipes and calls itself with
 * DSBCAPS_LOCSOFTWARE, but the depth is still raised, so the inner call trips
 * the guard and fails.  A pool that fails in hardware fails outright; running
 * the retry would allocate buffers the game never did. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolFill3D(VoicePool *self, int count, IDirectSound *pDS,
                    DWORD dwDsFlags, const char *filename, void *logger)
{
    ++g_nFill3D; { static unsigned long seen; pool_first("Fill3D", &seen); }
    pool_census();
    if (pool_fx() == POOL_FX_ONEVOICE && count > 1)
        count = 1;  // KAROO_POOL_FX=onevoice

    Sim_VoicePoolWipe(self);

    if (++self->dwNestDepth > 1) {  // re-entrancy guard
        ++g_nNestBail;
        goto fail;
    }

    if (count < 1 || logger == 0)
        goto fail;

    self->logger       = logger;
    self->dwVoiceCount = count;

    {
        // count * 0x18 + 4: the leading word is the count the vector
        // destructor reads back.
        void *block = malloc((unsigned)(count * 0x18 + 4));
        CStaticSoundbuffer *bufs = 0;
        if (block != 0) {
            *(int *)block = count;
            bufs = (CStaticSoundbuffer *)((char *)block + 4);
            for (int i = 0; i < count; i++)  // voice construction cannot fail
                CStatic_Init(&bufs[i]);
            g_nVoices += (unsigned long)count;
        }
        self->pBufs = bufs;
    }

    // Voice 0 comes off disk.  A failed allocation reaches here as a NULL
    // this, as in the game.
    if (!CStatic_CreateAndLoad3DSoundFile(&self->pBufs[0], pDS, dwDsFlags,
                                          filename, logger)) {
        Sim_VoicePoolWipe(self);
        goto fail;
    }

    {
        CStaticSoundbuffer *src = self->pBufs;          // voice 0, the template
        for (int i = 1; i < self->dwVoiceCount; i++) {  // the count is re-read on every pass
            if (CStatic_Copy(&self->pBufs[i], pDS, src, 1) != 0)
                continue;
            ++g_nCopyFail;
            if (CStatic_CreateAndLoad3DSoundFile(&self->pBufs[i], pDS,
                                                 dwDsFlags, filename, logger))
                continue;

            // The software retry: it cannot succeed (see above).
            Sim_VoicePoolWipe(self);
            {
                int r = Sim_VoicePoolFill3D(self, count, pDS,
                                            dwDsFlags | DSBCAPS_LOCSOFTWARE,
                                            filename, logger);
                self->dwNestDepth--;
                return r;
            }
        }
    }

    self->dwNestDepth--;
    return 1;

fail:
    self->dwNestDepth--;
    return 0;
}

/* PRESERVED: success returns src, the caller's pointer, on the duplicate path
 * and the voices array after a reload; callers only test for NULL.  A
 * duplicate succeeds when Copy returns src.  noFallback gives up on the first
 * failed duplicate instead of reloading.  The reload asks for software
 * buffers, and as an outer call it can succeed. */
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
Sim_VoicePoolClone(VoicePool *self, int count, IDirectSound *pDS,
                   CStaticSoundbuffer *src, int noFallback)
{
    ++g_nClone; { static unsigned long seen; pool_first("Clone", &seen); }
    pool_census();
    if (pool_fx() == POOL_FX_ONEVOICE && count > 1)
        count = 1;  // KAROO_POOL_FX=onevoice

    if (count < 1)
        return 0;

    Sim_VoicePoolWipe(self);

    self->dwVoiceCount = count;
    self->logger       = src->logger;

    {
        void *block = malloc((unsigned)(count * 0x18 + 4));
        CStaticSoundbuffer *bufs = 0;
        if (block != 0) {
            *(int *)block = count;
            bufs = (CStaticSoundbuffer *)((char *)block + 4);
            for (int i = 0; i < count; i++)
                CStatic_Init(&bufs[i]);
            g_nVoices += (unsigned long)count;
        }
        self->pBufs = bufs;
    }

    for (int i = 0; i < self->dwVoiceCount; i++) {  // the count is re-read on every pass
        if (CStatic_Copy(&self->pBufs[i], pDS, src, 0) == (void *)src)
            continue;
        ++g_nCopyFail;

        Sim_VoicePoolWipe(self);
        if (noFallback != 0)
            return 0;

        if (Sim_VoicePoolFill3D(self, count, pDS,
                                src->dwDsFlags | DSBCAPS_LOCSOFTWARE,
                                src->filename, src->logger) == 0)
            return 0;
        return self->pBufs;
    }

    return src;  // the caller's own pointer
}

/* Signed compares.  The voices pointer is not checked. */
extern "C" __declspec(dllexport) CStaticSoundbuffer * __attribute__((thiscall))
Sim_VoicePoolGetVoiceAt(VoicePool *self, int index)
{
    ++g_nGetVoiceAt; { static unsigned long seen; pool_first("GetVoiceAt", &seen); }
    if (index < 0 || index >= self->dwVoiceCount)
        return 0;
    return &self->pBufs[index];
}

extern "C" __declspec(dllexport) char * __attribute__((thiscall))
Sim_VoicePoolFirstFilename(VoicePool *self)
{
    ++g_nFirstName; { static unsigned long seen; pool_first("FirstFilename", &seen); }
    CStaticSoundbuffer *voice = Sim_VoicePoolGetVoiceAt(self, 0);
    if (voice == 0)
        return 0;
    return voice->filename;
}
