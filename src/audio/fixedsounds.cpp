/* The level report (reportwriter.cpp) is triggered from here: holding L as the
 * sounds load writes it.  KAROO_LEVEL_REPORT=1 answers those polls
 * (levelreport.cpp).
 *
 * KAROO_SIM_FX=reportkey is a negative control: L is not polled, so the report
 * never runs.  levelreport.py must fail and the replay suite, which never
 * presses L, must pass. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "soundmanager.h"
#include "game.h"
#include "reportwriter.h"
#include "gamelog.h"
#include "fixedsounds.h"
#include "menutree.h"
#include "player.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "record.h"

struct CStaticSoundbuffer;

extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self);

static int s_fx = -1;

/* Resets the old buffer if there is one, then loads a new one from "<game
 * dir>\waves\<name><suffix>.wav".  The caller stores the result back into the
 * slot. */
static CStaticSoundbuffer *bank(Game *game, CStaticSoundbuffer *cur,
                                const char *fmt, const char *suffix)
{
    char path[256];
    if (cur != NULL)
        CStatic_Reset(cur);
    sprintf(path, fmt, g_gameDir, suffix);
    return game->soundManager()->acquireStatic(path, 0);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_AcquireFixedSoundBuffersAndMaybeReport(Game *self)
{
    SoundManager *sm = self->soundManager();
    char path[256];

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "reportkey") == 0);
        if (s_fx)
            log_write("fixedsounds: KAROO_SIM_FX=reportkey -- VK_L not polled\n");
    }

    if (self->fixedSounds()->loaded != 0)
        return;

    if ((Config_Save(self->config(), GS_CFG_FILE) & 0xff) != 0)
        GameLog_LogMessage(&g_logger, 1, GS_CFG_SAVE_OK);
    else
        GameLog_LogMessage(&g_logger, 3, GS_CFG_SAVE_ERR);
    self->menu()->setLockStart(*self->clock());
    self->menu()->setLock(1);
    if (self->musicOn() != 0)
        self->cdThemes()->play(GS_GAME_MAIN);
    self->cdThemes()->setCurrentTrack((unsigned char)self->cdThemes()->findThemeIndex(GS_GAME_MAIN));

    if (self->soundCreated() == 0) {
        GameLog_LogMessage(&g_logger, 1, GS_CFG_NO_SOUND);
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
    sp->streamWave()->nBuffer_seconds = 5;
    sp->streamWave()->wSegment_count = 5;
    sp->streamWave()->pDirectsound = sm->directSound();
    sp->streamWave()->dwFlags = 0;
    self->extraObjects()->setSoundManager(sm);
    sp->setSoundManager(sm);

    // DETERMINISM: three polls of L; only the third answer counts.  All three
    // are part of the recorded key stream.
    if (!s_fx) {
        hooks_GetAsyncKeyState(0x4c);
        hooks_GetAsyncKeyState(0x4c);
        if (hooks_GetAsyncKeyState(0x4c) != 0)
            Report_WriteLevelReport(self, GS_RPT_FILE);
    }

    sprintf(path, GS_WAV_TIME_OUT, g_gameDir);
    self->fixedSounds()->timeOut = sm->acquireStatic(path, 0);
    sprintf(path, GS_WAV_LAST_SECONDS, g_gameDir);
    self->fixedSounds()->lastSeconds = sm->acquireStatic(path, 0);
    sprintf(path, GS_WAV_COUNT, g_gameDir);
    self->fixedSounds()->count = sm->acquireStatic(path, 0);
    sprintf(path, GS_WAV_MENU_UP_DOWN, g_gameDir);
    self->fixedSounds()->menuUpDown = sm->acquirePool(5, path, 0);
    sprintf(path, GS_WAV_SWITCH, g_gameDir);
    self->fixedSounds()->switchClick = sm->acquireStatic(path, 0);
    sprintf(path, GS_WAV_LEVEL_COMPLETED, g_gameDir);
    self->fixedSounds()->levelCompleted = sm->acquireStatic(path, 0);
    sprintf(path, GS_WAV_SPLAT, g_gameDir);
    self->player()->setSoundA7(sm->acquireStatic(path, 1));
    sm->setup(self->sound3D());

    self->fixedSounds()->loaded = 1;
}
