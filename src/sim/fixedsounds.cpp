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

/* A level-wide sound from "<game dir>\\waves\\<name><suffix>.wav". */
static void define(Game *game, SoundSource source, Sfx sfx, unsigned char variant,
                   const char *fmt, const char *suffix, bool positional = false)
{
    char path[256];
    snprintf(path, sizeof(path), fmt, game->gameDir(), suffix);
    game->soundEvents()->define(source, sfx, variant, path, positional, 1);
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
        define(self, SoundSource::Level, Sfx::GemsDone, (unsigned char)i, "%s\\waves\\add02%s.wav",
               suffix);
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
        for (const auto &p : pickups)
            define(self, SoundSource::Player, p.sfx, (unsigned char)i, p.fmt, suffix);
    }

    ScriptPlayer *sp = self->scriptPlayer();
    self->world()->extraObjects()->setSoundLibrary(sm);
    sp->setSoundManager(sm);

    define(self, SoundSource::Level, Sfx::TimeOut, 0, "%s\134waves\134TimeOut%s.wav", "");
    snprintf(path, sizeof(path), "%s\134waves\134LastSeconds.wav", self->gameDir());
    self->audio()->fixedSounds()->lastSeconds = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), "%s\134waves\134Count.wav", self->gameDir());
    self->audio()->fixedSounds()->count = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), "%s\134waves\134MenuUpDown.wav", self->gameDir());
    self->audio()->fixedSounds()->menuUpDown = sm->acquirePool(5, path, 0);
    snprintf(path, sizeof(path), "%s\134waves\134Switch.wav", self->gameDir());
    self->audio()->fixedSounds()->switchClick = sm->acquireStatic(path, 0);
    define(self, SoundSource::Level, Sfx::LevelComplete, 0, "%s\134waves\134LevelCompleted%s.wav", "");
    define(self, SoundSource::Player, Sfx::Splat, 0, "%s\134waves\134splat%s.wav", "", true);
    sm->setup(self->config()->sound3D());

    self->audio()->fixedSounds()->loaded = 1;
}
