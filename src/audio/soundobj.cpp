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
 * The name blocks are Game fields, `Game::soundAsset<offset>()`, named by
 * offset until their meanings are decoded:
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

static CStaticSoundbuffer *acq(Game *g, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return g->soundManager()->acquireStatic(name, 1);
}

static VoicePool *acq_pool(Game *g, int count, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return g->soundManager()->acquirePool(count, name, 1);
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_AcquireObjectSoundBuffersForIndex(Game *g, unsigned int objArg)
{
    unsigned int idx = objArg & 0xff;
    const SoundAssetName *assetAB = g->soundAsset42586();
    const SoundAssetName *assetAF = g->soundAsset42692();
    unsigned int last;

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "soundswap") == 0);
        if (s_fx)
            log_write("soundobj: KAROO_SIM_FX=soundswap -- +0xab/+0xaf swapped\n");
    }
    if (s_fx) {
        assetAB = g->soundAsset42692();
        assetAF = g->soundAsset42586();
    }

    if (g->soundAsset441ca()->enabled)
        g->foeSlot(idx)->setPool9f(acq_pool(g, 3, g->soundAsset441ca()));
    if (g->soundAsset429b6()->enabled)
        g->foeSlot(idx)->setSoundB3(acq(g, g->soundAsset429b6()));
    if (g->soundAsset42ac2()->enabled) {
        g->foeSlot(idx)->setSoundB7(acq(g, g->soundAsset42ac2()));
        if (g->soundAsset42ac2()->enabled)
            g->foeSlot(idx)->setSoundBb(acq(g, g->soundAsset42ac2()));
    }
    if (assetAB->enabled)
        g->foeSlot(idx)->setSoundAb(acq(g, assetAB));
    if (assetAF->enabled)
        g->foeSlot(idx)->setSoundAf(acq(g, assetAF));

    if (g->foeSlot(idx)->kind() == 2) {
        if (g->soundAsset457c6()->enabled)
            g->foeSlot(idx)->setSoundC3(acq(g, g->soundAsset457c6()));
        if (g->soundAsset42262()->enabled)
            g->foeSlot(idx)->setPoolCf(acq_pool(g, 5, g->soundAsset42262()));
        if (g->soundAsset44d4e()->enabled)
            g->foeSlot(idx)->setSoundA7(acq(g, g->soundAsset44d4e()));
    } else {
        if (g->soundAsset458d2()->enabled)
            g->foeSlot(idx)->setSoundC3(acq(g, g->soundAsset458d2()));
        if (g->soundAsset4247a()->enabled)
            g->foeSlot(idx)->setPoolCf(acq_pool(g, 5, g->soundAsset4247a()));
        if (g->soundAsset44e5a()->enabled)
            g->foeSlot(idx)->setSoundA7(acq(g, g->soundAsset44e5a()));
    }

    last = (unsigned int)g->soundAsset42de6()->enabled;
    if (last != 0) {
        CStaticSoundbuffer *p = acq(g, g->soundAsset42de6());
        g->foeSlot(idx)->setSoundCb(p);
        last = (unsigned int)(unsigned long)p;
    }
    return (last & 0xffffff00u) | 1u;
}
