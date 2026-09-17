/* GAMETICK_PLAN.md Band B reopened — the one-shot fixed-sound load.
 *
 *   Game::AcquireFixedSoundBuffersAndMaybeReport  0x0041a280
 *     1 E8 site (0x00414DFC, the first thing GameTick does)
 *
 * __thiscall(Game*), bare RET.  Transcribed from the LISTING.  Guarded by
 * Game+0x13cc80 (sound.dwSoundsLoaded): runs once, then sets it.
 *
 *   Config_Save(Game+0x28ab2e, "Karoo.cfg")  -> log 1 "saved" / 3 "error"
 *   +0x175524/28 <- dt accumulator; +0x17552c = 1
 *   if CD on: PlayCDStuf("Main");   +0x2235a = FindThemeIndex("Main")
 *   no SoundManager (+0x13cc34 == 0): log the warning, CD off, skip to end
 *   three passes, suffix 'A'+i, base ESI = Game+0x175327 + 4i, ten buffers
 *   each Reset-if-set then AcquireSoundBuffer(sm, path, 0):
 *     ESI-0x386b3 add02 (= Game+0x13cc74+4i, the three banks GameTick picks
 *                        by rand()%3 on crystal completion)
 *     ESI+0x00 add08  +0x0c add06  +0x18 add07  +0x24 add03  +0x30 add05
 *     ESI+0x48 add04  +0x54 add10  +0x60 add09  +0x3c add01   (listing order)
 *   +0x196050 = 5, WORD +0x196054 = 5, +0x196044 = +0x13cbc8 (the
 *   IDirectSound), +0x196048 = 0, +0x48ba0 = +0x195b3f = &SoundManager
 *   GetAsyncKeyState(VK_L) three times; if the THIRD reads down,
 *     WriteLevelReport(this, "LevelReport.txt")
 *   fixed buffers: TimeOut +0x13cc5c, LastSeconds +0x13cc6c, Count +0x13cc68,
 *   MenuUpDown VoicePool(5) +0x13cc64, Switch +0x13cc60, LevelCompleted
 *   +0x13cc70, splat (mode 1) +0x175270;  SoundSetup(sm, mode_3d)
 *   +0x13cc80 = 1
 *
 * The VK_L polls go through hooks_GetAsyncKeyState, which is where
 * levelreport.cpp answers them under KAROO_LEVEL_REPORT=1 -- so
 * tools/levelreport.py passing is itself the proof this path is ours.
 *
 * Callbacks kept (the sound manager is not ours): AcquireSoundBuffer
 * 0x443660, AcquireVoicePool 0x443810, SoundSetup 0x4439d0.
 *
 * Control: KAROO_SIM_FX=reportkey -- the VK_L polls are skipped, so the
 * report never runs.  levelreport.py must then FAIL (no report written);
 * the replay suite, which never presses L, must still pass.
 */
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

struct CStaticSoundbuffer;

extern "C" __declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self);


static int s_fx = -1;

/* Reset-if-set, then acquire from a "%s\\waves\\...%s.wav" format.  The
 * caller stores the result back into the slot it passed. */
static CStaticSoundbuffer *bank(Game *game, CStaticSoundbuffer *cur,
                                unsigned int fmt, const char *suffix)
{
    char path[256];
    if (cur != NULL)
        CStatic_Reset(cur);
    sprintf(path, (const char *)fmt, GS_GAME_DIR, suffix);
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
        GameLog_LogMessage(GG_LOGGER, 1, GS_CFG_SAVE_OK);
    else
        GameLog_LogMessage(GG_LOGGER, 3, GS_CFG_SAVE_ERR);
    self->menu()->setLockStart(*self->clock());
    self->menu()->setLock(1);
    if (self->musicOn() != 0)
        self->cdThemes()->play(GS_GAME_MAIN);
    self->cdThemes()->setCurrentTrack((unsigned char)self->cdThemes()->findThemeIndex(GS_GAME_MAIN));

    if (self->soundCreated() == 0) {
        GameLog_LogMessage(GG_LOGGER, 1, GS_CFG_NO_SOUND);
        self->setMusicOn(0);
        if (self->soundCreated() == 0) {
            self->fixedSounds()->loaded = 1;
            return;
        }
    }

    for (unsigned int i = 0; i < 3; ++i) {
        char suffix[2] = { (char)('A' + i), 0 };
        /* ESI = Game+0x175327 + 4i is the Player's pickup bank +0x15e[i];
         * ESI-0x386b3 is Game+0x13cc74 + 4i, a Game field. */
        Player *pl = self->player();
        FixedSounds *fs = self->fixedSounds();
        fs->crystalBank[i] = bank(self, fs->crystalBank[i], 0x465be4, suffix);       /* add02 */
#define PBANK(k, fmt) pl->setPickupSound(Player::k, i, bank(self, pl->pickupSound(Player::k, i), fmt, suffix))
        PBANK(SND_15E, 0x465bcc);   /* add08 */
        PBANK(SND_16A, 0x465bb4);   /* add06 */
        PBANK(SND_176, 0x465b9c);   /* add07 */
        PBANK(SND_182, 0x465b84);   /* add03 */
        PBANK(SND_18E, 0x465b6c);   /* add05 */
        PBANK(SND_1A6, 0x465b54);   /* add04 */
        PBANK(SND_1B2, 0x465b3c);   /* add10 */
        PBANK(SND_1BE, 0x465b24);   /* add09 */
        PBANK(SND_19A, 0x465b0c);   /* add01 */
#undef PBANK
    }

    ScriptPlayer *sp = self->scriptPlayer();
    sp->streamWave()->nBuffer_seconds = 5;
    sp->streamWave()->wSegment_count = 5;
    sp->streamWave()->pDirectsound = sm->directSound();
    sp->streamWave()->dwFlags = 0;
    self->extraObjects()->setSoundManager(sm);
    sp->setSoundManager(sm);

    if (!s_fx) {
        hooks_GetAsyncKeyState(0x4c);
        hooks_GetAsyncKeyState(0x4c);
        if (hooks_GetAsyncKeyState(0x4c) != 0)
            Report_WriteLevelReport(self, GS_RPT_FILE);
    }

    sprintf(path, GS_WAV_TIME_OUT, GS_GAME_DIR);
    self->fixedSounds()->timeOut = sm->acquireStatic(path, 0);          /* TimeOut */
    sprintf(path, GS_WAV_LAST_SECONDS, GS_GAME_DIR);
    self->fixedSounds()->lastSeconds = sm->acquireStatic(path, 0);          /* LastSeconds */
    sprintf(path, GS_WAV_COUNT, GS_GAME_DIR);
    self->fixedSounds()->count = sm->acquireStatic(path, 0);          /* Count */
    sprintf(path, GS_WAV_MENU_UP_DOWN, GS_GAME_DIR);
    self->fixedSounds()->menuUpDown = sm->acquirePool(5, path, 0);        /* MenuUpDown */
    sprintf(path, GS_WAV_SWITCH, GS_GAME_DIR);
    self->fixedSounds()->switchClick = sm->acquireStatic(path, 0);          /* Switch */
    sprintf(path, GS_WAV_LEVEL_COMPLETED, GS_GAME_DIR);
    self->fixedSounds()->levelCompleted = sm->acquireStatic(path, 0);          /* LevelCompleted */
    sprintf(path, GS_WAV_SPLAT, GS_GAME_DIR);
    self->player()->setSoundA7(sm->acquireStatic(path, 1)); /* splat */
    sm->setup(self->sound3D());

    self->fixedSounds()->loaded = 1;
}
