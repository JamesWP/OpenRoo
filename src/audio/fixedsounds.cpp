#include "gamedir.h"
#include "inputdev.h"
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
#include "gamestr.h"
#include "gameglobals.h"
#include "record.h"

#include "audiodev.h"

/* Resets the old buffer if there is one, then loads a new one from "<game
 * dir>\waves\<name><suffix>.wav".  The caller stores the result back into the
 * slot. */
static audiodev::Buffer *bank(Game *game, audiodev::Buffer *cur,
                                const char *fmt, const char *suffix)
{
    char path[256];
    if (cur != NULL)
        cur->reset();
    sprintf(path, fmt, gameDir(), suffix);
    return game->soundManager()->acquireStatic(path, 0);
}

  void  
Sim_AcquireFixedSoundBuffersAndMaybeReport(Game *self)
{
    SoundManager *sm = self->soundManager();
    char path[256];

    if (self->fixedSounds()->loaded != 0)
        return;

    if ((self->config()->save(GS_CFG_FILE) & 0xff) != 0)
        g_logger.logMessage(1, "GAME: config-values saved correctly");
    else
        g_logger.logMessage(3, "GAME: ** error ** while saving config-values (maybe write-protected or hd full\077) !!!");
    self->menu()->setLockStart(*self->clock());
    self->menu()->setLock(1);
    if (self->musicOn() != 0)
        self->cdThemes()->play(GS_GAME_MAIN);
    self->cdThemes()->setCurrentTrack((unsigned char)self->cdThemes()->findThemeIndex(GS_GAME_MAIN));

    if (self->soundCreated() == 0) {
        g_logger.logMessage(1, "GAME: warning - SoundManager not created, no wave and CD-sound !!!");
        self->setMusicOn(0);
        if (self->soundCreated() == 0) {
            self->fixedSounds()->loaded = 1;
            return;
        }
    }

    // Three banks, suffixes A, B and C; the crystal bank is the Game's, the
    // rest are the player's pickup sounds.
    for (unsigned int i = 0; i < 3; ++i) {
        char suffix[2] = { (char)('A' + i), 0 };
        Player *pl = self->player();
        FixedSounds *fs = self->fixedSounds();
        fs->crystalBank[i] = bank(self, fs->crystalBank[i], "%s\\waves\\add02%s.wav", suffix);
#define PBANK(k, fmt) pl->setPickupSound(Player::k, i, bank(self, pl->pickupSound(Player::k, i), fmt, suffix))
        PBANK(SND_15E, "%s\\waves\\add08%s.wav");
        PBANK(SND_16A, "%s\\waves\\add06%s.wav");
        PBANK(SND_176, "%s\\waves\\add07%s.wav");
        PBANK(SND_182, "%s\\waves\\add03%s.wav");
        PBANK(SND_18E, "%s\\waves\\add05%s.wav");
        PBANK(SND_1A6, "%s\\waves\\add04%s.wav");
        PBANK(SND_1B2, "%s\\waves\\add10%s.wav");
        PBANK(SND_1BE, "%s\\waves\\add09%s.wav");
        PBANK(SND_19A, "%s\\waves\\add01%s.wav");
#undef PBANK
    }

    ScriptPlayer *sp = self->scriptPlayer();
    self->extraObjects()->setSoundManager(sm);
    sp->setSoundManager(sm);

    snprintf(path, sizeof(path), GS_WAV_TIME_OUT, gameDir());
    self->fixedSounds()->timeOut = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), GS_WAV_LAST_SECONDS, gameDir());
    self->fixedSounds()->lastSeconds = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), GS_WAV_COUNT, gameDir());
    self->fixedSounds()->count = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), GS_WAV_MENU_UP_DOWN, gameDir());
    self->fixedSounds()->menuUpDown = sm->acquirePool(5, path, 0);
    snprintf(path, sizeof(path), GS_WAV_SWITCH, gameDir());
    self->fixedSounds()->switchClick = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), GS_WAV_LEVEL_COMPLETED, gameDir());
    self->fixedSounds()->levelCompleted = sm->acquireStatic(path, 0);
    snprintf(path, sizeof(path), GS_WAV_SPLAT, gameDir());
    self->player()->setSoundA7(sm->acquireStatic(path, 1));
    sm->setup(self->sound3D());

    self->fixedSounds()->loaded = 1;
}
