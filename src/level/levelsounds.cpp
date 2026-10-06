/* Everything is gated on the sound device being up.  The world's sound variant
 * is 0, 2 for Space and 1 for Candy; PRESERVED: Egypt is set to 0 a second
 * time, redundantly.  The player's two voice pools and eleven effect buffers
 * are reloaded, then every foe, falling tile, lift, platform and bridge gets its
 * sounds, and on a first attempt at a level with 3D sound each extra sound
 * object (.leo) starts looping at its position.  Every load passes a stack
 * copy of the name.  Returns 0 in the low byte.
 *
 * KAROO_SIM_FX=worldcode is a negative control: Space and Candy swap sound
 * variants. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include "logger.h"
#include "soundmanager.h"
#include "game.h"
#include "levelsounds.h"
#include "player.h"
#include "soundobj.h"
#include "liftobject.h"
#include "platformobject.h"
#include "bridgeobject.h"
#include "fallingtile.h"
#include "gamestr.h"
#include "gameglobals.h"

namespace audiodev { class Buffer; }
#include "voicepool.h"

static int s_fx = -1;

static audiodev::Buffer *acq(Game *game, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return game->soundManager()->acquireStatic(name, 1);
}

static VoicePool *acq_pool(Game *game, int count, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return game->soundManager()->acquirePool(count, name, 1);
}

/* Resets the old buffer if there is one, then loads the named asset if it is
 * set.  The caller stores the result: the new buffer, or the reset one. */
static audiodev::Buffer *reslot(Game *game, audiodev::Buffer *cur,
                                  const SoundAssetName *asset)
{
    if (cur != NULL)
        cur->reset();
    if (asset->enabled != 0)
        return acq(game, asset);
    return cur;
}

/* Gives every object in one slot table its moving-loop sound.  The count and
 * the slot are re-read every pass. */
template <typename T>
static void attachLoopSound(Game *game,
                            unsigned char (Game::*count)() const,
                            T *(Game::*slot)(unsigned int) const,
                            const SoundAssetName *asset)
{
    for (unsigned short i = 0; i < (game->*count)(); ++i) {
        if (asset->enabled != 0)
            (game->*slot)(i)->setSound(acq(game, asset));
    }
}

  unsigned int  
Sim_InitLevelBasedSounds(Game *self)
{
    Game *game = self;
    Player *pl = game->player();
    const char *world = game->map()->mapName();

    if (s_fx < 0) {
        char e[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "worldcode") == 0);
        if (s_fx)
            g_logger.write("levelsounds: KAROO_SIM_FX=worldcode -- Space/Candy swapped\n");
    }

    g_logger.logMessage(2, "GAME: trying to init level-based sounds");
    if (self->soundCreated() != 0) {
        pl->setWorldSoundVariant(0);
        if (strcmp(world, GS_SND_EGYPT) == 0)
            pl->setWorldSoundVariant(0);
        if (strcmp(world, GS_SND_SPACE) == 0)
            pl->setWorldSoundVariant(s_fx ? 1 : 2);
        if (strcmp(world, GS_SND_CANDY) == 0)
            pl->setWorldSoundVariant(s_fx ? 2 : 1);

        if (pl->pool9f() != NULL)
            pl->pool9f()->wipe();
        if (game->soundAsset441ca()->enabled != 0)
            pl->setPool9f(acq_pool(game, 3, game->soundAsset441ca()));
        if (pl->poolCf() != NULL)
            pl->poolCf()->wipe();
        if (game->soundAsset4236e()->enabled != 0)
            pl->setPoolCf(acq_pool(game, 10, game->soundAsset4236e()));

        pl->setSoundC3(reslot(game, pl->soundC3(), game->soundAsset456ba()));
        pl->setSoundBf(reslot(game, pl->soundBf(), game->soundAsset42ef2()));
        pl->setSoundB3(reslot(game, pl->soundB3(), game->soundAsset429b6()));
        pl->setSoundC7(reslot(game, pl->soundC7(), game->soundAsset4279e()));
        pl->setSoundA3(reslot(game, pl->soundA3(), game->soundAsset428aa()));
        pl->setSoundB7(reslot(game, pl->soundB7(), game->soundAsset42ac2()));
        pl->setSoundBb(reslot(game, pl->soundBb(), game->soundAsset42ac2()));
        pl->setSoundAb(reslot(game, pl->soundAb(), game->soundAsset42586()));
        pl->setSoundAf(reslot(game, pl->soundAf(), game->soundAsset42692()));
        pl->setSoundCb(reslot(game, pl->soundCb(), game->soundAsset42de6()));
        pl->setSoundA7(reslot(game, pl->soundA7(), game->soundAsset44c42()));

        for (unsigned short i = 0; i < game->foeCount(); ++i)
            Sim_AcquireObjectSoundBuffersForIndex(game, game->foeId(i));

        for (unsigned short i = 0; i < game->fallingCount(); ++i) {
            if (game->soundAsset4310a()->enabled != 0) {
                audiodev::Buffer *p = acq(game, game->soundAsset4310a());
                game->fallingSlot(i)->setFallSound(p);
            }
            if (game->soundAsset43216()->enabled != 0) {
                audiodev::Buffer *p = acq(game, game->soundAsset43216());
                game->fallingSlot(i)->setRespawnSound(p);
            }
        }
        attachLoopSound(game, &Game::liftCount,   &Game::liftSlot,
                        game->soundAsset42bce());
        attachLoopSound(game, &Game::platformCount,  &Game::platformSlot,
                        game->soundAsset42cda());
        attachLoopSound(game, &Game::bridgeCount, &Game::bridgeSlot,
                        game->soundAsset42ffe());

        if (game->restartCount() == 0 && game->sound3D() != 0) {
            g_logger.logMessage(1, "GAME: try to play level-based LEO sounds");
            // The count is re-read every pass.
            ExtraObjects *xo = game->extraObjects();
            for (unsigned short i = 0; i < xo->objectCount(); ++i) {
                ExtraObjectRecord *E = xo->record(i);
                if (E->kind != EXTRA_SOUND)
                    continue;
                const char *nm = E->file;
                g_logger.logMessage(1, "GAME: try to play LEO sound %s", nm);
                audiodev::Buffer *p = game->soundManager()->acquireStatic(nm, 1);
                E->sound = p;
                if (p != NULL) {
                    p->setPosition(E->position[0], E->position[2],
                                          -E->position[1], 1);
                    E->sound->play(true);
                }
            }
        }
    }
    g_logger.logMessage(2, "GAME: level-based sounds initialized");
    return 0;
}
