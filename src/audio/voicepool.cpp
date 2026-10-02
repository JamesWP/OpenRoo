#include <windows.h>
#include <stdint.h>
#include "sysdev.h"
#include <new>
#include "voicepool.h"
#include <stdlib.h>
#include "logger.h"

/* KAROO_POOL_DIAG=1 counts calls to every pool function, logs each one's first
 * call, and totals the voices allocated, to tell "the gates never build a
 * pool" from "the gates build pools and assert nothing about them". */
static int pool_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        uint32_t n = sysdev::getEnv("KAROO_POOL_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static unsigned long g_nBlank, g_nWipe, g_nClone,
                     g_nGetVoiceAt, g_nFirstName, g_nCycle, g_nBroadcast,
                     g_nVoices, g_nCopyFail;

static void pool_first(const char *what, unsigned long *seen)
{
    if (!pool_diag() || *seen) return;
    *seen = 1;
    g_logger.write("voicepool: first call to %s\n", what);
}

/* Reported from the pool-building calls, a handful per level load; Cycle and
 * Broadcast are the hot pair and are only counted. */
static void pool_census(void)
{
    if (!pool_diag()) return;
    g_logger.write("voicepool: DIAG blank=%lu wipe=%lu clone=%lu "
              "getvoice=%lu firstname=%lu cycle=%lu broadcast=%lu "
              "voicesAllocated=%lu copyFailures=%lu\n",
              g_nBlank, g_nWipe, g_nClone, g_nGetVoiceAt,
              g_nFirstName, g_nCycle, g_nBroadcast, g_nVoices,
              g_nCopyFail);
}

/* KAROO_POOL_FX=onevoice is a negative control: Clone builds one
 * voice, whatever count is asked for.  A one-voice pool cannot overlap a sound
 * with itself.  KAROO_POOL_DIAG's voice total reads it directly. */
enum PoolFx { POOL_FX_OFF = 0, POOL_FX_ONEVOICE = 1 };

static PoolFx pool_fx(void)
{
    static int cached = -1;
    if (cached >= 0) return (PoolFx)cached;
    char buf[32];
    uint32_t n = sysdev::getEnv("KAROO_POOL_FX", buf, sizeof(buf));
    PoolFx fx = POOL_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "onevoice") == 0) fx = POOL_FX_ONEVOICE;
    }
    g_logger.write("voicepool: FX mode = %s\n",
              fx == POOL_FX_ONEVOICE ? "onevoice" : "off");
    cached = (int)fx;
    return fx;
}

/* PRESERVED: it halts and re-triggers the same slot; the increment only
 * decides where the next call lands.  The cursor is stored unwrapped before
 * the trigger and wrapped after it, with a signed compare.  Only the voices
 * pointer is checked, so a zero-count pool still plays voice 0 once. */
void VoicePool::cycle(bool loop)
{
    ++g_nCycle; { static unsigned long seen; pool_first("Cycle", &seen); }
    if (bufs_ == 0)
        return;

    bufs_[currentIdx_].stop();

    const int old = currentIdx_;
    currentIdx_ = old + 1;  // stored before the trigger, unwrapped

    bufs_[old].play(loop);

    if (currentIdx_ >= voiceCount_)  // signed, and after
        currentIdx_ = 0;
}

/* PRESERVED: only voice 0 is checked for 3D before every voice is positioned,
 * and the voices pointer is not checked at all.  Pools are all-3D or all-2D,
 * so neither fires. */
void VoicePool::broadcastCoordinates(float x, float y, float z, bool immediate)
{
    ++g_nBroadcast; { static unsigned long seen; pool_first("Broadcast", &seen); }
    if (!bufs_->is3D() || voiceCount_ <= 0)
        return;

    for (int i = 0; i < voiceCount_; i++)
        bufs_[i].setPosition(x, y, z, immediate);
}

VoicePool::VoicePool()
{
    ++g_nBlank; { static unsigned long seen; pool_first("Blank", &seen); }
    bufs_       = 0;
    voiceCount_ = 0;
    currentIdx_ = 0;
}

VoicePool::~VoicePool()
{
    wipe();
}

/* Resets every voice, destroys the array and clears the pool. */
void VoicePool::wipe()
{
    ++g_nWipe; { static unsigned long seen; pool_first("Wipe", &seen); }
    pool_census();
    if (bufs_ != 0) {
        for (int i = 0; i < voiceCount_; i++)
            bufs_[i].reset();

        delete[] bufs_;
        bufs_ = 0;
    }
    voiceCount_ = 0;
    currentIdx_ = 0;
}

/* Gives up on the first voice the platform will not duplicate. */
bool VoicePool::clone(int count, audiodev::Device &dev,
                      const audiodev::Buffer &src)
{
    ++g_nClone; { static unsigned long seen; pool_first("Clone", &seen); }
    pool_census();
    if (pool_fx() == POOL_FX_ONEVOICE && count > 1)
        count = 1;  // KAROO_POOL_FX=onevoice

    if (count < 1)
        return false;

    wipe();

    voiceCount_ = count;

    {
        audiodev::Buffer *bufs = new (std::nothrow) audiodev::Buffer[count];
        if (bufs != 0)
            g_nVoices += (unsigned long)count;
        bufs_ = bufs;
    }

    for (int i = 0; i < voiceCount_; i++) {  // the count is re-read on every pass
        if (bufs_[i].duplicate(dev, src))
            continue;
        ++g_nCopyFail;
        wipe();
        return false;
    }

    return true;
}

/* Signed compares.  The voices pointer is not checked. */
audiodev::Buffer *VoicePool::voiceAt(int index)
{
    ++g_nGetVoiceAt; { static unsigned long seen; pool_first("GetVoiceAt", &seen); }
    if (index < 0 || index >= voiceCount_)
        return 0;
    return &bufs_[index];
}

const char *VoicePool::firstFilename()
{
    ++g_nFirstName; { static unsigned long seen; pool_first("FirstFilename", &seen); }
    audiodev::Buffer *voice = voiceAt(0);
    if (voice == 0)
        return 0;
    return voice->filename();
}
