/* Each sound is loaded from a stack copy of its name, and only when its
 * "enabled" word is set. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include "logger.h"
#include "game.h"
#include "foe.h"
#include "soundmanager.h"
#include "voicepool.h"
#include "soundobj.h"

static audiodev::Buffer *acq(Game *g, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return g->audio()->soundManager()->acquireStatic(name, 1);
}

static VoicePool *acq_pool(Game *g, int count, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return g->audio()->soundManager()->acquirePool(count, name, 1);
}

  unsigned int  
Sim_AcquireObjectSoundBuffersForIndex(Game *g, unsigned int objArg)
{
    unsigned int idx = objArg & 0xff;
    const SoundAssetName *assetAB = g->audio()->themeSound(ThemeSound::MoveIceSliding);
    const SoundAssetName *assetAF = g->audio()->themeSound(ThemeSound::MoveSliding);
    unsigned int last;

    if (g->audio()->themeSound(ThemeSound::Crystal)->enabled)
        g->world()->foeSlot(idx)->setPool9f(acq_pool(g, 3, g->audio()->themeSound(ThemeSound::Crystal)));
    if (g->audio()->themeSound(ThemeSound::MoveJumpPad)->enabled)
        g->world()->foeSlot(idx)->setSoundB3(acq(g, g->audio()->themeSound(ThemeSound::MoveJumpPad)));
    if (g->audio()->themeSound(ThemeSound::Teleporter)->enabled) {
        g->world()->foeSlot(idx)->setSoundB7(acq(g, g->audio()->themeSound(ThemeSound::Teleporter)));
        // PRESERVED: the same guard is tested again and the same name loaded a
        // second time, so two buffers play one file.
        if (g->audio()->themeSound(ThemeSound::Teleporter)->enabled)
            g->world()->foeSlot(idx)->setSoundBb(acq(g, g->audio()->themeSound(ThemeSound::Teleporter)));
    }
    if (assetAB->enabled)
        g->world()->foeSlot(idx)->setSoundAb(acq(g, assetAB));
    if (assetAF->enabled)
        g->world()->foeSlot(idx)->setSoundAf(acq(g, assetAF));

    if (g->world()->foeSlot(idx)->kind() == 2) {
        if (g->audio()->themeSound(ThemeSound::FallCatcher)->enabled)
            g->world()->foeSlot(idx)->setSoundC3(acq(g, g->audio()->themeSound(ThemeSound::FallCatcher)));
        if (g->audio()->themeSound(ThemeSound::MoveCatcher)->enabled)
            g->world()->foeSlot(idx)->setPoolCf(acq_pool(g, 5, g->audio()->themeSound(ThemeSound::MoveCatcher)));
        if (g->audio()->themeSound(ThemeSound::SplatCatcher)->enabled)
            g->world()->foeSlot(idx)->setSoundA7(acq(g, g->audio()->themeSound(ThemeSound::SplatCatcher)));
    } else {
        if (g->audio()->themeSound(ThemeSound::FallThrower)->enabled)
            g->world()->foeSlot(idx)->setSoundC3(acq(g, g->audio()->themeSound(ThemeSound::FallThrower)));
        if (g->audio()->themeSound(ThemeSound::MoveThrower)->enabled)
            g->world()->foeSlot(idx)->setPoolCf(acq_pool(g, 5, g->audio()->themeSound(ThemeSound::MoveThrower)));
        if (g->audio()->themeSound(ThemeSound::SplatThrower)->enabled)
            g->world()->foeSlot(idx)->setSoundA7(acq(g, g->audio()->themeSound(ThemeSound::SplatThrower)));
    }

    // PRESERVED: the low byte is 1 over whatever the last acquire left: its
    // result if the final sound was set, else 0.
    last = (unsigned int)g->audio()->themeSound(ThemeSound::Switch)->enabled;
    if (last != 0) {
        audiodev::Buffer *p = acq(g, g->audio()->themeSound(ThemeSound::Switch));
        g->world()->foeSlot(idx)->setSoundCb(p);
        last = (unsigned int)(uintptr_t)p;
    }
    return (last & 0xffffff00u) | 1u;
}
