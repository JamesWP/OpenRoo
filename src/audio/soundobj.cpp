/* Each sound is loaded from a stack copy of its name, and only when its
 * "enabled" word is set.
 *
 * KAROO_SIM_FX=soundswap is a negative control: two of the foe's sounds swap
 * names.  Sound is outside every asserted field, so the suite is expected to
 * pass: the effect is audible, not simulated. */

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
        // PRESERVED: the same guard is tested again and the same name loaded a
        // second time, so two buffers play one file.
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

    // PRESERVED: the low byte is 1 over whatever the last acquire left: its
    // result if the final sound was set, else 0.
    last = (unsigned int)g->soundAsset42de6()->enabled;
    if (last != 0) {
        CStaticSoundbuffer *p = acq(g, g->soundAsset42de6());
        g->foeSlot(idx)->setSoundCb(p);
        last = (unsigned int)(unsigned long)p;
    }
    return (last & 0xffffff00u) | 1u;
}
