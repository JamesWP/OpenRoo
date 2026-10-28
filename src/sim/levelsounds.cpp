/* Everything is gated on the sound device being up.  The world's sound variant
 * is 0, 2 for Space and 1 for Candy; PRESERVED: Egypt is set to 0 a second
 * time, redundantly.  The player's two voice pools and eleven effect buffers
 * are reloaded, then every foe, falling tile, lift, platform and bridge gets its
 * sounds, and on a first attempt at a level with 3D sound each extra sound
 * object (.leo) starts looping at its position.  Every load passes a stack
 * copy of the name.  Returns 0 in the low byte. */

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

namespace audiodev { class Buffer; }
#include "voicepool.h"

static audiodev::Buffer *acq(Game *game, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return game->audio()->soundManager()->acquireStatic(name, 1);
}

static VoicePool *acq_pool(Game *game, int count, const SoundAssetName *asset)
{
    char name[256];
    strcpy(name, asset->name);
    return game->audio()->soundManager()->acquirePool(count, name, 1);
}

/* Resets the old buffer if there is one, then loads the named asset if it is
 * set.  The caller stores the result: the new buffer, or the reset one. */
static SoundVoice *reslot(Game *game, SoundVoice *cur,
                          const SoundAssetName *asset)
{
    if (cur != NULL)
        static_cast<audiodev::Buffer *>(cur)->reset();
    if (asset->enabled != 0)
        return acq(game, asset);
    return cur;
}

/* Gives every object in one list its moving-loop sound. */
template <class T>
static void attachLoopSound(Game *game, const EntityList<T> &list, SoundSource source, Sfx sfx,
                            const SoundAssetName *asset)
{
    for (const T &obj : list) {
        if (asset->enabled != 0)
            game->soundEvents()->attach({source, obj.id(), sfx}, acq(game, asset));
    }
}

  unsigned int  
Sim_InitLevelBasedSounds(Game *self)
{
    Game *game = self;
    Player *pl = game->world()->player();
    const char *world = game->world()->map()->mapName();

    g_logger.logMessage(2, "GAME: trying to init level-based sounds");
    if (self->audio()->soundCreated() != 0) {
        pl->setWorldSoundVariant(0);
        if (strcmp(world, "Egypt") == 0)
            pl->setWorldSoundVariant(0);
        if (strcmp(world, "Space") == 0)
            pl->setWorldSoundVariant(2);
        if (strcmp(world, "Candy") == 0)
            pl->setWorldSoundVariant(1);

        if (pl->pool9f() != NULL)
            static_cast<VoicePool *>(pl->pool9f())->wipe();
        if (game->audio()->themeSound(ThemeSound::Crystal)->enabled != 0)
            pl->setPool9f(acq_pool(game, 3, game->audio()->themeSound(ThemeSound::Crystal)));
        if (pl->poolCf() != NULL)
            static_cast<VoicePool *>(pl->poolCf())->wipe();
        if (game->audio()->themeSound(ThemeSound::MoveJJ)->enabled != 0)
            pl->setPoolCf(acq_pool(game, 10, game->audio()->themeSound(ThemeSound::MoveJJ)));

        pl->setSoundC3(reslot(game, pl->soundC3(), game->audio()->themeSound(ThemeSound::FallJJ)));
        pl->setSoundBf(reslot(game, pl->soundBf(), game->audio()->themeSound(ThemeSound::Glue)));
        pl->setSoundB3(reslot(game, pl->soundB3(), game->audio()->themeSound(ThemeSound::MoveJumpPad)));
        pl->setSoundC7(reslot(game, pl->soundC7(), game->audio()->themeSound(ThemeSound::MoveParagliding)));
        pl->setSoundA3(reslot(game, pl->soundA3(), game->audio()->themeSound(ThemeSound::MoveParaglidingStart)));
        pl->setSoundB7(reslot(game, pl->soundB7(), game->audio()->themeSound(ThemeSound::Teleporter)));
        pl->setSoundBb(reslot(game, pl->soundBb(), game->audio()->themeSound(ThemeSound::Teleporter)));
        pl->setSoundAb(reslot(game, pl->soundAb(), game->audio()->themeSound(ThemeSound::MoveIceSliding)));
        pl->setSoundAf(reslot(game, pl->soundAf(), game->audio()->themeSound(ThemeSound::MoveSliding)));
        pl->setSoundCb(reslot(game, pl->soundCb(), game->audio()->themeSound(ThemeSound::Switch)));
        pl->setSoundA7(reslot(game, pl->soundA7(), game->audio()->themeSound(ThemeSound::SplatJJ)));

        for (Foe &foe : game->world()->foes())
            Sim_AcquireFoeSounds(game, foe);

        for (const FallingTile &bt : game->world()->fallings()) {
            if (game->audio()->themeSound(ThemeSound::DestructStart)->enabled != 0)
                game->soundEvents()->attach({SoundSource::Falling, bt.id(), Sfx::TileFalls},
                                            acq(game, game->audio()->themeSound(ThemeSound::DestructStart)));
            if (game->audio()->themeSound(ThemeSound::DestructRegen)->enabled != 0)
                game->soundEvents()->attach({SoundSource::Falling, bt.id(), Sfx::TileRespawns},
                                            acq(game, game->audio()->themeSound(ThemeSound::DestructRegen)));
        }
        attachLoopSound(game, game->world()->lifts(), SoundSource::Lift, Sfx::LiftMoving,
                        game->audio()->themeSound(ThemeSound::Elevator));
        attachLoopSound(game, game->world()->platforms(), SoundSource::Platform,
                        Sfx::PlatformMoving, game->audio()->themeSound(ThemeSound::Platform));
        attachLoopSound(game, game->world()->bridges(), SoundSource::Bridge, Sfx::BridgeMoving,
                        game->audio()->themeSound(ThemeSound::Bridge));

        if (game->session()->restartCount() == 0 && game->config()->sound3D() != 0) {
            g_logger.logMessage(1, "GAME: try to play level-based LEO sounds");
            // The count is re-read every pass.
            ExtraObjects *xo = game->world()->extraObjects();
            for (unsigned short i = 0; i < xo->objectCount(); ++i) {
                ExtraObjectRecord *E = xo->record(i);
                if (E->kind != EXTRA_SOUND)
                    continue;
                const char *nm = E->file;
                g_logger.logMessage(1, "GAME: try to play LEO sound %s", nm);
                audiodev::Buffer *p = game->audio()->soundManager()->acquireStatic(nm, 1);
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
