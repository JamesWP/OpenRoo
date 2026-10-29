/* Everything is gated on the sound device being up.  The world's sound variant
 * is 0, 2 for Space and 1 for Candy; PRESERVED: Egypt is set to 0 a second
 * time, redundantly.  The player's, the foes', the bombs' and the level
 * objects' sounds are defined from the theme (soundevents.h), and on a first attempt at a level with 3D sound each extra sound
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

namespace audiodev { class Buffer; }

/* A source kind's sfx plays the theme's sound, if the theme has it set;
 * `voices` are loaded now, enough for that many to play at once. */
static void define(Game *game, SoundSource source, Sfx sfx, ThemeSound sound, unsigned voices)
{
    const SoundAssetName *asset = game->audio()->themeSound(sound);
    if (asset->enabled != 0)
        game->soundEvents()->define(source, sfx, 0, asset->name, true, voices);
    else
        game->soundEvents()->undefine(source, sfx);
}

/* What a foe of either kind, and a bomb, share with the player. */
static void define_mover(Game *game, SoundSource source, unsigned voices)
{
    define(game, source, Sfx::JumpPad, ThemeSound::MoveJumpPad, voices);
    define(game, source, Sfx::Teleport, ThemeSound::Teleporter, voices);
    define(game, source, Sfx::Teleported, ThemeSound::Teleporter, voices);
}

/* A catcher's or a thrower's sounds. */
static void define_foe(Game *game, SoundSource source, ThemeSound fall, ThemeSound step,
                       ThemeSound splat)
{
    const unsigned voices = 4;
    define_mover(game, source, voices);
    define(game, source, Sfx::Crystal, ThemeSound::Crystal, 3);
    define(game, source, Sfx::Ice, ThemeSound::MoveIceSliding, voices);
    define(game, source, Sfx::Slide, ThemeSound::MoveSliding, voices);
    define(game, source, Sfx::Fall, fall, voices);
    define(game, source, Sfx::Step, step, 8);
    define(game, source, Sfx::Splat, splat, voices);
    define(game, source, Sfx::Switch, ThemeSound::Switch, voices);
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

        const SoundSource P = SoundSource::Player;
        define_mover(game, P, 1);
        define(game, P, Sfx::Crystal, ThemeSound::Crystal, 3);
        define(game, P, Sfx::Step, ThemeSound::MoveJJ, 10);
        define(game, P, Sfx::Fall, ThemeSound::FallJJ, 1);
        define(game, P, Sfx::Glue, ThemeSound::Glue, 1);
        define(game, P, Sfx::Glide, ThemeSound::MoveParagliding, 1);
        define(game, P, Sfx::GlideStart, ThemeSound::MoveParaglidingStart, 1);
        define(game, P, Sfx::Ice, ThemeSound::MoveIceSliding, 1);
        define(game, P, Sfx::Slide, ThemeSound::MoveSliding, 1);
        define(game, P, Sfx::Switch, ThemeSound::Switch, 1);
        define(game, P, Sfx::Splat, ThemeSound::SplatJJ, 1);

        define_foe(game, SoundSource::Catcher, ThemeSound::FallCatcher, ThemeSound::MoveCatcher,
                   ThemeSound::SplatCatcher);
        define_foe(game, SoundSource::Thrower, ThemeSound::FallThrower, ThemeSound::MoveThrower,
                   ThemeSound::SplatThrower);

        const SoundSource B = SoundSource::Bomb;
        define_mover(game, B, 2);
        define(game, B, Sfx::BombBlast, ThemeSound::ExplosionBomb, 4);
        define(game, B, Sfx::Fall, ThemeSound::FallCatcher, 2);
        define(game, B, Sfx::BombRoll, ThemeSound::BombTick, 4);

        define(game, SoundSource::Falling, Sfx::TileFalls, ThemeSound::DestructStart, 4);
        define(game, SoundSource::Falling, Sfx::TileRespawns, ThemeSound::DestructRegen, 4);
        define(game, SoundSource::Lift, Sfx::LiftMoving, ThemeSound::Elevator, 1);
        define(game, SoundSource::Platform, Sfx::PlatformMoving, ThemeSound::Platform, 1);
        define(game, SoundSource::Bridge, Sfx::BridgeMoving, ThemeSound::Bridge, 1);

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
