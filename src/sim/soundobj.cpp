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

void Sim_AcquireFoeSounds(Game *g, Foe &foe)
{
    const SoundAssetName *assetAB = g->audio()->themeSound(ThemeSound::MoveIceSliding);
    const SoundAssetName *assetAF = g->audio()->themeSound(ThemeSound::MoveSliding);

    if (g->audio()->themeSound(ThemeSound::Crystal)->enabled)
        foe.setPool9f(acq_pool(g, 3, g->audio()->themeSound(ThemeSound::Crystal)));
    if (g->audio()->themeSound(ThemeSound::MoveJumpPad)->enabled)
        foe.setSoundB3(acq(g, g->audio()->themeSound(ThemeSound::MoveJumpPad)));
    if (g->audio()->themeSound(ThemeSound::Teleporter)->enabled) {
        foe.setSoundB7(acq(g, g->audio()->themeSound(ThemeSound::Teleporter)));
        // PRESERVED: the same guard is tested again and the same name loaded a
        // second time, so two buffers play one file.
        if (g->audio()->themeSound(ThemeSound::Teleporter)->enabled)
            foe.setSoundBb(acq(g, g->audio()->themeSound(ThemeSound::Teleporter)));
    }
    if (assetAB->enabled)
        foe.setSoundAb(acq(g, assetAB));
    if (assetAF->enabled)
        foe.setSoundAf(acq(g, assetAF));

    if (foe.isCatcher()) {
        if (g->audio()->themeSound(ThemeSound::FallCatcher)->enabled)
            foe.setSoundC3(acq(g, g->audio()->themeSound(ThemeSound::FallCatcher)));
        if (g->audio()->themeSound(ThemeSound::MoveCatcher)->enabled)
            foe.setPoolCf(acq_pool(g, 5, g->audio()->themeSound(ThemeSound::MoveCatcher)));
        if (g->audio()->themeSound(ThemeSound::SplatCatcher)->enabled)
            foe.setSoundA7(acq(g, g->audio()->themeSound(ThemeSound::SplatCatcher)));
    } else {
        if (g->audio()->themeSound(ThemeSound::FallThrower)->enabled)
            foe.setSoundC3(acq(g, g->audio()->themeSound(ThemeSound::FallThrower)));
        if (g->audio()->themeSound(ThemeSound::MoveThrower)->enabled)
            foe.setPoolCf(acq_pool(g, 5, g->audio()->themeSound(ThemeSound::MoveThrower)));
        if (g->audio()->themeSound(ThemeSound::SplatThrower)->enabled)
            foe.setSoundA7(acq(g, g->audio()->themeSound(ThemeSound::SplatThrower)));
    }

    if (g->audio()->themeSound(ThemeSound::Switch)->enabled)
        foe.setSoundCb(acq(g, g->audio()->themeSound(ThemeSound::Switch)));
}
