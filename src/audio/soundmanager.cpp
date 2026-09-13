/* SoundManager -- placeholder; see soundmanager.h.
 *
 * Both methods are __thiscall in the original, taking the buffer pointer and
 * the owner flag as two pushed dwords.  They are called through, at their
 * original addresses, until this file takes them over.
 */
#include "soundmanager.h"

typedef void (__attribute__((thiscall)) *release_fn)(SoundManager *sm,
                                                     void *buffer,
                                                     int bDestroyIfUnused);

#define ORIG_RELEASE_STATIC ((release_fn)0x004432f0)
#define ORIG_RELEASE_POOL   ((release_fn)0x00443400)

void SoundManager::releaseStaticForOwner(void *buffer, int bDestroyIfUnused)
{
    ORIG_RELEASE_STATIC(this, buffer, bDestroyIfUnused);
}

void SoundManager::releasePooledForOwner(void *buffer, int bDestroyIfUnused)
{
    ORIG_RELEASE_POOL(this, buffer, bDestroyIfUnused);
}

typedef CStaticSoundbuffer *(__attribute__((thiscall)) *acquire_fn)(SoundManager *sm,
                                                                    const char *name,
                                                                    int mode);
#define ORIG_ACQUIRE_STATIC ((acquire_fn)0x00443660)

CStaticSoundbuffer *SoundManager::acquireStatic(const char *name, int mode)
{
    return ORIG_ACQUIRE_STATIC(this, name, mode);
}

typedef VoicePool *(__attribute__((thiscall)) *acquire_pool_fn)(SoundManager *sm,
                                                                 int count,
                                                                 const char *name,
                                                                 int mode);
#define ORIG_ACQUIRE_POOL ((acquire_pool_fn)0x00443810)

VoicePool *SoundManager::acquirePool(int count, const char *name, int mode)
{
    return ORIG_ACQUIRE_POOL(this, count, name, mode);
}
