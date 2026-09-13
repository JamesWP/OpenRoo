/* GAMETICK_PLAN.md Band B reopened — per-object sound attachment.
 *
 *   AcquireObjectSoundBuffersForIndex  0x0041cf50
 *     callers: GameTick (the runtime foe spawner) and InitLevelBasedSounds
 *
 * __thiscall(Game*, uint objectIndex), RET 4.  Transcribed from the LISTING.
 *
 * The SoundManager embedded at Game+0x13cba8 is NOT ours: its two acquire
 * methods are placeholders in soundmanager.h that call the originals --
 *   acquireStatic  AcquireSoundBuffer 0x00443660  (name, mode)        -> CStatic*
 *   acquirePool    AcquireVoicePool   0x00443810  (count, name, mode) -> VoicePool*
 * Replacing the sound manager is its own piece of work, not a GameTick one.
 *
 * Each acquisition is gated on the dword right after a 0x100-byte name
 * (name+0x100) and passes a stack COPY of the name, as the listing does.
 * The name blocks are Game fields (Band 3), so they stay offsets here:
 *
 *   +0x441ca VoicePool(3) -> pool9f       +0x429b6 -> soundB3
 *   +0x42ac2 -> soundB7, then re-tests the SAME guard and acquires the SAME
 *            name again into soundBb -- two buffers on one file (preserved)
 *   +0x42586 -> soundAb                   +0x42692 -> soundAf
 *   kind() == 2 : +0x457c6 -> soundC3, +0x42262 VoicePool(5) -> poolCf,
 *                 +0x44d4e -> soundA7
 *   otherwise   : +0x458d2 -> soundC3, +0x4247a VoicePool(5) -> poolCf,
 *                 +0x44e5a -> soundA7
 *   +0x42de6 -> soundCb
 *
 * The listing reloads the slot ([EBX+EDX*4+0x174804]) for the first five
 * stores, then holds the slot ADDRESS and dereferences it per store.  Both
 * read the same pointer, since the acquires never write the slot table;
 * here every store re-reads it through Game::foeSlot.
 *
 * Return: MOV AL,1 over whatever EAX last held -- the final acquire's result
 * when the +0x42ee6 guard was set, else that guard dword (0).
 *
 * Control: KAROO_SIM_FX=soundswap -- soundAb and soundAf swap names.  Sound
 * is outside every asserted field, so this is expected not to fail the
 * suite: the function's effects are audible, not simulated.
 */
#include <windows.h>
#include <string.h>
#include "log.h"
#include "game.h"
#include "foe.h"
#include "soundmanager.h"
#include "soundobj.h"

static int s_fx = -1;

/* A name block in Game, and the guard dword right after it. */
static const char *name_at(Game *g, unsigned int off)
{
    return (const char *)g + off;
}

static int guard(Game *g, unsigned int off)
{
    return *(const int *)(name_at(g, off) + 0x100);
}

static CStaticSoundbuffer *acq(Game *g, unsigned int nameOff)
{
    char name[256];
    strcpy(name, name_at(g, nameOff));
    return g->soundManager()->acquireStatic(name, 1);
}

static VoicePool *acq_pool(Game *g, int count, unsigned int nameOff)
{
    char name[256];
    strcpy(name, name_at(g, nameOff));
    return g->soundManager()->acquirePool(count, name, 1);
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_AcquireObjectSoundBuffersForIndex(Game *g, unsigned int objArg)
{
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

    if (guard(g, 0x441ca))
        g->foeSlot(idx)->setPool9f(acq_pool(g, 3, 0x441ca));
    if (guard(g, 0x429b6))
        g->foeSlot(idx)->setSoundB3(acq(g, 0x429b6));
    if (guard(g, 0x42ac2)) {
        g->foeSlot(idx)->setSoundB7(acq(g, 0x42ac2));
        if (guard(g, 0x42ac2))
            g->foeSlot(idx)->setSoundBb(acq(g, 0x42ac2));
    }
    if (guard(g, offAB))
        g->foeSlot(idx)->setSoundAb(acq(g, offAB));
    if (guard(g, offAF))
        g->foeSlot(idx)->setSoundAf(acq(g, offAF));

    if (g->foeSlot(idx)->kind() == 2) {
        if (guard(g, 0x457c6))
            g->foeSlot(idx)->setSoundC3(acq(g, 0x457c6));
        if (guard(g, 0x42262))
            g->foeSlot(idx)->setPoolCf(acq_pool(g, 5, 0x42262));
        if (guard(g, 0x44d4e))
            g->foeSlot(idx)->setSoundA7(acq(g, 0x44d4e));
    } else {
        if (guard(g, 0x458d2))
            g->foeSlot(idx)->setSoundC3(acq(g, 0x458d2));
        if (guard(g, 0x4247a))
            g->foeSlot(idx)->setPoolCf(acq_pool(g, 5, 0x4247a));
        if (guard(g, 0x44e5a))
            g->foeSlot(idx)->setSoundA7(acq(g, 0x44e5a));
    }

    last = (unsigned int)guard(g, 0x42de6);
    if (last != 0) {
        CStaticSoundbuffer *p = acq(g, 0x42de6);
        g->foeSlot(idx)->setSoundCb(p);
        last = (unsigned int)(unsigned long)p;
    }
    return (last & 0xffffff00u) | 1u;
}
