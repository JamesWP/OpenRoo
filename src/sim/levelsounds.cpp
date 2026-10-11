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

namespace audiodev { class Buffer; }
#include "voicepool.h"

static int s_fx = -1;

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

/* Gives every object in one slot table its moving-loop sound.  The body only
 * sets a sound, so the table is not changed while it is walked. */
template <class T, unsigned N>
static void attachLoopSound(Game *game, EntitySlots<T, N> &table,
                            const SoundAssetName *asset)
{
    for (T *obj : table) {
        if (asset->enabled != 0)
            obj->setSound(acq(game, asset));
    }
}

  unsigned int  
Sim_InitLevelBasedSounds(Game *self)
{
    Game *game = self;
    Player *pl = game->world()->player();
    const char *world = game->world()->map()->mapName();

    if (s_fx < 0) {
        char e[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "worldcode") == 0);
        if (s_fx)
            g_logger.write("levelsounds: KAROO_SIM_FX=worldcode -- Space/Candy swapped\n");
    }

    g_logger.logMessage(2, "GAME: trying to init level-based sounds");
    if (self->audio()->soundCreated() != 0) {
        pl->setWorldSoundVariant(0);
        if (strcmp(world, GS_SND_EGYPT) == 0)
            pl->setWorldSoundVariant(0);
        if (strcmp(world, GS_SND_SPACE) == 0)
            pl->setWorldSoundVariant(s_fx ? 1 : 2);
        if (strcmp(world, GS_SND_CANDY) == 0)
            pl->setWorldSoundVariant(s_fx ? 2 : 1);

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

        for (unsigned short i = 0; i < game->world()->foeCount(); ++i)
            Sim_AcquireObjectSoundBuffersForIndex(game, game->world()->foeId(i));

        for (FallingTile *bt : game->world()->fallings()) {
            if (game->audio()->themeSound(ThemeSound::DestructStart)->enabled != 0) {
                audiodev::Buffer *p = acq(game, game->audio()->themeSound(ThemeSound::DestructStart));
                bt->setFallSound(p);
            }
            if (game->audio()->themeSound(ThemeSound::DestructRegen)->enabled != 0) {
                audiodev::Buffer *p = acq(game, game->audio()->themeSound(ThemeSound::DestructRegen));
                bt->setRespawnSound(p);
            }
        }
        attachLoopSound(game, game->world()->lifts(),
                        game->audio()->themeSound(ThemeSound::Elevator));
        attachLoopSound(game, game->world()->platforms(),
                        game->audio()->themeSound(ThemeSound::Platform));
        attachLoopSound(game, game->world()->bridges(),
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
