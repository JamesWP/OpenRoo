/* GAMETICK_PLAN.md Band B reopened — per-object sound attachment.
 *
 *   AcquireObjectSoundBuffersForIndex  0x0041cf50
 *     callers: GameTick (the runtime foe spawner) and InitLevelBasedSounds
 *
 * __thiscall(Game*, uint objectIndex), RET 4.  Transcribed from the LISTING.
 *
 * The SoundManager embedded at Game+0x13cba8 is NOT ours: its two acquire
 * methods are kept as named callbacks, exactly as objectspawn.cpp keeps
 * AcquireSoundBuffer --
 *   AcquireSoundBuffer 0x00443660  thiscall(sm, name, mode)       -> CStatic*
 *   AcquireVoicePool   0x00443810  thiscall(sm, count, name, mode) -> VoicePool*
 * Replacing the sound manager is its own piece of work, not a GameTick one.
 *
 * Each acquisition is gated on the dword right after a 0x100-byte name
 * (name+0x100) and passes a stack COPY of the name, as the listing does:
 *
 *   +0x441ca VoicePool(3) -> obj+0x9f     +0x429b6 -> obj+0xb3
 *   +0x42ac2 -> obj+0xb7, then re-tests the SAME guard and acquires the SAME
 *            name again into obj+0xbb -- two buffers on one file (preserved)
 *   +0x42586 -> obj+0xab                  +0x42692 -> obj+0xaf
 *   obj+0x152 == 2 : +0x457c6 -> +0xc3, +0x42262 VoicePool(5) -> +0xcf,
 *                    +0x44d4e -> +0xa7
 *   otherwise      : +0x458d2 -> +0xc3, +0x4247a VoicePool(5) -> +0xcf,
 *                    +0x44e5a -> +0xa7
 *   +0x42de6 -> obj+0xcb
 *
 * The first five stores re-read the object slot every time (the listing
 * reloads [EBX+EDX*4+0x174804]); from the kind test onward the slot ADDRESS
 * is held and dereferenced per store.  Both forms read the same pointer,
 * since the acquires never write the slot table.
 *
 * Return: MOV AL,1 over whatever EAX last held -- the final acquire's result
 * when the +0x42ee6 guard was set, else that guard dword (0).
 *
 * Control: KAROO_SIM_FX=soundswap -- +0xab and +0xaf swap names.  Sound is
 * outside every asserted field, so this is expected not to fail the suite:
 * the function's effects are audible, not simulated.
 */
#include <windows.h>
#include <string.h>
#include "log.h"

struct CStaticSoundbuffer;
struct VoicePool;
typedef CStaticSoundbuffer *(__attribute__((thiscall)) *acquire_fn)(void *sm,
                                                                    const char *name,
                                                                    int mode);
typedef VoicePool *(__attribute__((thiscall)) *acquire_pool_fn)(void *sm, int count,
                                                                 const char *name,
                                                                 int mode);
#define ORIG_ACQUIRE_SOUND ((acquire_fn)     0x00443660)   /* named callback */
#define ORIG_ACQUIRE_POOL  ((acquire_pool_fn)0x00443810)   /* named callback */

#define G_SOUND_MGR 0x13cba8
#define G_FOE_SLOTS 0x174804

static int s_fx = -1;

static void *acq(unsigned char *B, unsigned int nameOff)
{
    char name[256];
    strcpy(name, (const char *)(B + nameOff));
    return ORIG_ACQUIRE_SOUND(B + G_SOUND_MGR, name, 1);
}

static void *acq_pool(unsigned char *B, int count, unsigned int nameOff)
{
    char name[256];
    strcpy(name, (const char *)(B + nameOff));
    return ORIG_ACQUIRE_POOL(B + G_SOUND_MGR, count, name, 1);
}

#define GUARD(off)  (*(int *)(B + (off) + 0x100))
#define SLOT()      (*(unsigned char **)(B + G_FOE_SLOTS + idx * 4))
#define PUT(o, v)   (*(void **)(SLOT() + (o)) = (v))

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_AcquireObjectSoundBuffersForIndex(void *self, unsigned int objArg)
{
    unsigned char *B = (unsigned char *)self;
    unsigned int idx = objArg & 0xff;
    unsigned int offAB = 0x42586, offAF = 0x42692;
    unsigned int last;

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "soundswap") == 0);
        if (s_fx)
            log_write("soundobj: KAROO_SIM_FX=soundswap -- +0xab/+0xaf swapped\n");
    }
    if (s_fx) {
        offAB = 0x42692;
        offAF = 0x42586;
    }

    if (GUARD(0x441ca))
        PUT(0x9f, acq_pool(B, 3, 0x441ca));
    if (GUARD(0x429b6))
        PUT(0xb3, acq(B, 0x429b6));
    if (GUARD(0x42ac2)) {
        PUT(0xb7, acq(B, 0x42ac2));
        if (GUARD(0x42ac2))
            PUT(0xbb, acq(B, 0x42ac2));
    }
    if (GUARD(offAB))
        PUT(0xab, acq(B, offAB));
    if (GUARD(offAF))
        PUT(0xaf, acq(B, offAF));

    if (SLOT()[0x152] == 2) {
        if (GUARD(0x457c6))
            PUT(0xc3, acq(B, 0x457c6));
        if (GUARD(0x42262))
            PUT(0xcf, acq_pool(B, 5, 0x42262));
        if (GUARD(0x44d4e))
            PUT(0xa7, acq(B, 0x44d4e));
    } else {
        if (GUARD(0x458d2))
            PUT(0xc3, acq(B, 0x458d2));
        if (GUARD(0x4247a))
            PUT(0xcf, acq_pool(B, 5, 0x4247a));
        if (GUARD(0x44e5a))
            PUT(0xa7, acq(B, 0x44e5a));
    }

    last = (unsigned int)GUARD(0x42de6);
    if (last != 0) {
        void *p = acq(B, 0x42de6);
        PUT(0xcb, p);
        last = (unsigned int)(unsigned long)p;
    }
    return (last & 0xffffff00u) | 1u;
}
