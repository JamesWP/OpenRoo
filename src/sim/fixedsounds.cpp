#include <stdint.h>
#include "sysdev.h"
#include <stdio.h>
#include <string.h>
#include "logger.h"
#include "soundmanager.h"
#include "game.h"
#include "fixedsounds.h"
#include "menutree.h"
#include "player.h"

#include "audiodev.h"

/* Resets the old buffer if there is one, then loads a new one from "<game
 * dir>\waves\<name><suffix>.wav".  The caller stores the result back into the
 * slot. */
static audiodev::Buffer *bank(Game *game, SoundVoice *cur,
                                const char *fmt, const char *suffix)
{
    char path[256];
    if (cur != NULL)
        static_cast<audiodev::Buffer *>(cur)->reset();
    sprintf(path, fmt, game->gameDir(), suffix);
    return game->audio()->soundManager()->acquireStatic(path, 0);
}

  void  
Sim_AcquireFixedSoundBuffersAndMaybeReport(Game *self)
{
    SoundManager *sm = self->audio()->soundManager();
    char path[256];

    if (self->audio()->fixedSounds()->loaded != 0)
        return;

    if ((self->config()->save("openroo.ini") & 0xff) != 0)
        g_logger.logMessage(1, "GAME: config-values saved correctly");
    else
        g_logger.logMessage(3, "GAME: ** error ** while saving config-values (maybe write-protected or hd full\077) !!!");
    self->frontEnd()->menu()->setLockStart(self->world()->now());
    self->frontEnd()->menu()->setLock(1);
    if (self->config()->musicOn() != 0)
        self->audio()->cdThemes()->play("Main");
    self->audio()->cdThemes()->setCurrentTrack((unsigned char)self->audio()->cdThemes()->findThemeIndex("Main"));

    if (self->audio()->soundCreated() == 0) {
        g_logger.logMessage(1, "GAME: warning - SoundManager not created, no wave and CD-sound !!!");
        self->config()->setMusicOn(0);
        if (self->audio()->soundCreated() == 0) {
            self->audio()->fixedSounds()->loaded = 1;
            return;
        }
    }

    // Three banks, suffixes A, B and C; the crystal bank is the Game's, the
    // rest are the player's pickup sounds.
    for (unsigned int i = 0; i < 3; ++i) {
        char suffix[2] = { (char)('A' + i), 0 };
        Player *pl = self->world()->player();
        FixedSounds *fs = self->audio()->fixedSounds();
        fs->crystalBank[i] = bank(self, fs->crystalBank[i], "%s\\waves\\add02%s.wav", suffix);
        static const struct { Sfx sfx; const char *fmt; } pickups[] = {
            { Sfx::PickupFreeze,   "%s\\waves\\add08%s.wav" },
            { Sfx::PickupLife,     "%s\\waves\\add06%s.wav" },
            { Sfx::PickupTime,     "%s\\waves\\add07%s.wav" },
            { Sfx::PickupFast,     "%s\\waves\\add03%s.wav" },
            { Sfx::PickupSlow,     "%s\\waves\\add05%s.wav" },
            { Sfx::PickupBombs,    "%s\\waves\\add04%s.wav" },
            { Sfx::PickupReversed, "%s\\waves\\add10%s.wav" },
            { Sfx::PickupShield,   "%s\\waves\\add09%s.wav" },
            { Sfx::PickupGlider,   "%s\\waves\\add01%s.wav" },
        };
        for (const auto &p : pickups) {
            SoundKey key = pl->sound(p.sfx);
            key.variant  = (unsigned char)i;
            self->soundEvents()->attach(key, bank(self, self->soundEvents()->voice(key), p.fmt, suffix));
        }
    }

    ScriptPlayer *sp = self->scriptPlayer();
    self->world()->extraObjects()->setSoundLibrary(sm);
    sp->setSoundManager(sm);

    snprintf(path, sizeof(path), "%s\134waves\134TimeOut.wav", self->gameDir());
    self->audio()->fixedSounds()->timeOut = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), "%s\134waves\134LastSeconds.wav", self->gameDir());
    self->audio()->fixedSounds()->lastSeconds = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), "%s\134waves\134Count.wav", self->gameDir());
    self->audio()->fixedSounds()->count = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), "%s\134waves\134MenuUpDown.wav", self->gameDir());
    self->audio()->fixedSounds()->menuUpDown = sm->acquirePool(5, path, 0);
    snprintf(path, sizeof(path), "%s\134waves\134Switch.wav", self->gameDir());
    self->audio()->fixedSounds()->switchClick = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), "%s\134waves\134LevelCompleted.wav", self->gameDir());
    self->audio()->fixedSounds()->levelCompleted = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), "%s\134waves\134splat.wav", self->gameDir());
    self->soundEvents()->attach(self->world()->player()->sound(Sfx::Splat), sm->acquireStatic(path, 1));
    sm->setup(self->config()->sound3D());

    self->audio()->fixedSounds()->loaded = 1;
}
