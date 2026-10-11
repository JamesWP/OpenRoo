/* Each sound is loaded from a stack copy of its name, and only when its
 * "enabled" word is set.
 *
 * KAROO_SIM_FX=soundswap is a negative control: two of the foe's sounds swap
 * names.  Sound is outside every asserted field, so the suite is expected to
 * pass: the effect is audible, not simulated. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include "logger.h"
#include "game.h"
#include "foe.h"
#include "soundmanager.h"
#include "voicepool.h"
#include "soundobj.h"

static int s_fx = -1;

static audiodev::Buffer *acq(Game *g, const SoundAssetName *asset)
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

  unsigned int  
Sim_AcquireObjectSoundBuffersForIndex(Game *g, unsigned int objArg)
{
    unsigned int idx = objArg & 0xff;
    const SoundAssetName *assetAB = g->themeSound(ThemeSound::MoveIceSliding);
    const SoundAssetName *assetAF = g->themeSound(ThemeSound::MoveSliding);
    unsigned int last;

    if (s_fx < 0) {
        char e[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "soundswap") == 0);
        if (s_fx)
            g_logger.write("soundobj: KAROO_SIM_FX=soundswap -- +0xab/+0xaf swapped\n");
    }
    if (s_fx) {
        assetAB = g->themeSound(ThemeSound::MoveSliding);
        assetAF = g->themeSound(ThemeSound::MoveIceSliding);
    }

    if (g->themeSound(ThemeSound::Crystal)->enabled)
        g->world()->foeSlot(idx)->setPool9f(acq_pool(g, 3, g->themeSound(ThemeSound::Crystal)));
    if (g->themeSound(ThemeSound::MoveJumpPad)->enabled)
        g->world()->foeSlot(idx)->setSoundB3(acq(g, g->themeSound(ThemeSound::MoveJumpPad)));
    if (g->themeSound(ThemeSound::Teleporter)->enabled) {
        g->world()->foeSlot(idx)->setSoundB7(acq(g, g->themeSound(ThemeSound::Teleporter)));
        // PRESERVED: the same guard is tested again and the same name loaded a
        // second time, so two buffers play one file.
        if (g->themeSound(ThemeSound::Teleporter)->enabled)
            g->world()->foeSlot(idx)->setSoundBb(acq(g, g->themeSound(ThemeSound::Teleporter)));
    }
    if (assetAB->enabled)
        g->world()->foeSlot(idx)->setSoundAb(acq(g, assetAB));
    if (assetAF->enabled)
        g->world()->foeSlot(idx)->setSoundAf(acq(g, assetAF));

    if (g->world()->foeSlot(idx)->kind() == 2) {
        if (g->themeSound(ThemeSound::FallCatcher)->enabled)
            g->world()->foeSlot(idx)->setSoundC3(acq(g, g->themeSound(ThemeSound::FallCatcher)));
        if (g->themeSound(ThemeSound::MoveCatcher)->enabled)
            g->world()->foeSlot(idx)->setPoolCf(acq_pool(g, 5, g->themeSound(ThemeSound::MoveCatcher)));
        if (g->themeSound(ThemeSound::SplatCatcher)->enabled)
            g->world()->foeSlot(idx)->setSoundA7(acq(g, g->themeSound(ThemeSound::SplatCatcher)));
    } else {
        if (g->themeSound(ThemeSound::FallThrower)->enabled)
            g->world()->foeSlot(idx)->setSoundC3(acq(g, g->themeSound(ThemeSound::FallThrower)));
        if (g->themeSound(ThemeSound::MoveThrower)->enabled)
            g->world()->foeSlot(idx)->setPoolCf(acq_pool(g, 5, g->themeSound(ThemeSound::MoveThrower)));
        if (g->themeSound(ThemeSound::SplatThrower)->enabled)
            g->world()->foeSlot(idx)->setSoundA7(acq(g, g->themeSound(ThemeSound::SplatThrower)));
    }

    // PRESERVED: the low byte is 1 over whatever the last acquire left: its
    // result if the final sound was set, else 0.
    last = (unsigned int)g->themeSound(ThemeSound::Switch)->enabled;
    if (last != 0) {
        audiodev::Buffer *p = acq(g, g->themeSound(ThemeSound::Switch));
        g->world()->foeSlot(idx)->setSoundCb(p);
        last = (unsigned int)(uintptr_t)p;
    }
    return (last & 0xffffff00u) | 1u;
}
