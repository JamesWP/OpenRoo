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

/* Resets the player's old buffer if there is one, then loads the named asset
 * in its place if it is set; otherwise the reset one stays. */
static void reslot(Game *game, const SoundKey &key, ThemeSound sound)
{
    const SoundAssetName *asset = game->audio()->themeSound(sound);
    if (SoundVoice *cur = game->soundEvents()->voice(key))
        static_cast<audiodev::Buffer *>(cur)->reset();
    if (asset->enabled != 0)
        game->soundEvents()->attach(key, acq(game, asset));
}

/* The same for a pool, which is wiped. */
static void reslotPool(Game *game, const SoundKey &key, int count, ThemeSound sound)
{
    const SoundAssetName *asset = game->audio()->themeSound(sound);
    if (SoundVoice *cur = game->soundEvents()->voice(key))
        static_cast<VoicePool *>(cur)->wipe();
    if (asset->enabled != 0)
        game->soundEvents()->attach(key, acq_pool(game, count, asset));
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

        reslotPool(game, pl->sound(Sfx::Crystal), 3, ThemeSound::Crystal);
        reslotPool(game, pl->sound(Sfx::Step), 10, ThemeSound::MoveJJ);

        reslot(game, pl->sound(Sfx::Fall), ThemeSound::FallJJ);
        reslot(game, pl->sound(Sfx::Glue), ThemeSound::Glue);
        reslot(game, pl->sound(Sfx::JumpPad), ThemeSound::MoveJumpPad);
        reslot(game, pl->sound(Sfx::Glide), ThemeSound::MoveParagliding);
        reslot(game, pl->sound(Sfx::GlideStart), ThemeSound::MoveParaglidingStart);
        reslot(game, pl->sound(Sfx::Teleport), ThemeSound::Teleporter);
        reslot(game, pl->sound(Sfx::Teleported), ThemeSound::Teleporter);
        reslot(game, pl->sound(Sfx::Ice), ThemeSound::MoveIceSliding);
        reslot(game, pl->sound(Sfx::Slide), ThemeSound::MoveSliding);
        reslot(game, pl->sound(Sfx::Switch), ThemeSound::Switch);
        reslot(game, pl->sound(Sfx::Splat), ThemeSound::SplatJJ);

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
