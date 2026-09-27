/* The cheat codes, checked in this order once the entry has just finished:
 *   kaputo      every foe dies
 *   supa        completes the level (game over on the last level)
 *   jjmapnr<n>  loads level n by number
 *   jjmap <x>   loads level x by name
 *   mausuruh    one extra life
 *   sportsman   one more glide
 *   boommaker   ten more bombs
 *   notme       invulnerable
 * then the buffer is cleared and the entry re-armed.
 *
 * PRESERVED: jjmapnr and jjmap are prefix compares (7 and 5 bytes), and
 * "jjmapnr..." also matches jjmap; it misses only because the jjmapnr branch
 * clears the buffer first, and only when it was taken (strlen > 8).
 *
 * KAROO_SIM_FX=cheatlife is a negative control: mausuruh gives two lives.  No
 * recording types a cheat, so the suite is expected to pass; it waits for a
 * recording that does. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "game.h"
#include "gamelog.h"
#include "levelsetup.h"
#include "levelscore.h"
#include "levelparse.h"
#include "cheatcode.h"
#include "menutree.h"
#include "textentry.h"
#include "foe.h"
#include "player.h"
#include "tile.h"
#include "gamestr.h"
#include "gameglobals.h"

struct CDM;
void CDM_StopTrack(CDM *self);

static int s_fx = -1;

static int streq(const unsigned char *a, const char *b)
{
    return strcmp((const char *)a, b) == 0;
}

/* The tail both level loaders share: go to the loaded state with the intro
 * flythrough armed. */
static void enter_loaded_state(Game *game, FILE *fp)
{
    game->setCameraDistance(7.0f);
    game->setState(4);
    if (game->musicOn() != 0)
        CDM_StopTrack(&g_cdAudio);
    game->scriptPlayer()->setRunning(1);
    game->setCameraMode(1);
    game->setDebounce(0x0d);
    fclose(fp);
}

void Sim_HandleTypedCheatCode(Game *self)
{
    Player *pl = self->player();
    unsigned char *buf = self->cheatBuffer();
    unsigned char frame[256];
    char path[256];

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "cheatlife") == 0);
        if (s_fx)
            log_write("cheatcode: KAROO_SIM_FX=cheatlife -- mausuruh gives 2\n");
    }

    self->cheatEntry()->poll((unsigned int)(long long)*self->clock());
    if (self->cheatEntry()->active() != 0)
        return;

    if (streq(buf, "kaputo")) {
        Game *g = self;
        unsigned char i = 0;
        // The count is re-read every pass.
        if (g->foeCount() != 0) {
            do {
                unsigned char id = g->foeId(i);
                ++i;
                g->foeSlot(id)->setMoveState(4);
            } while (i < g->foeCount());
        }
    }

    if (streq(buf, "supa")) {
        if ((unsigned int)self->levelIndex() + 1 == (unsigned int)self->levelCount()) {
            self->setState(2);
            if (self->musicOn() != 0)
                self->cdThemes()->play(GS_GAME_GAMEOVER);
            Score_CalculateLevelScore(self, 0x28);
            self->setDebounce(0x0d);
            GameLog_LogMessage(&g_logger, 1, GS_GAME_COMPLETED_AT_LEVEL,
                               (unsigned int)self->levelIndex() + 1,
                               (unsigned int)self->levelCount());
        } else {
            self->menu()->setLockStart(self->lastTickTime());
            self->menu()->setLock(1);
            self->setState(3);
            if (self->musicOn() != 0)
                self->cdThemes()->play(GS_GAME_COMPLETED);
            self->setCameraMode(2);
            self->menu()->rewind();
            self->menu()->pop();
            self->menu()->push(0x28);
            self->menu()->setLockStart(self->lastTickTime());
            self->menu()->setNode(0x28);
            self->menu()->setCursor(0);
            self->menu()->setLock(1);
            Score_CalculateLevelScore(self, (char)self->state());
            self->setRestartCount(0);
            GameLog_LogMessage(&g_logger, 1, GS_CHEAT_C_SL);
        }
        self->setTotalPlayTime((double)(unsigned long long)self->timeElapsed() + self->totalPlayTime());
    }

    // jjmapnr: a 7-byte prefix, then load by number.
    memset(frame, 0, sizeof(frame));
    memcpy(frame, buf, 7);
    frame[7] = 0;
    if (streq(frame, "jjmapnr")) {
        size_t len = strlen((const char *)buf);
        if (len > 8) {
            char num[256];
            memcpy(num, buf + 8, len - 8);
            num[len - 8] = 0;
            unsigned char lvl = (unsigned char)(atoi(num) - 1);
            Sim_SetCurrentLevelName(self, lvl);
            if (lvl < self->levelCount()) {
                sprintf(path, GS_CHEAT_FMT_LVL_PATH, g_gameDir, self->levelName());
                GameLog_LogMessage(&g_logger, 3, GS_CHEAT_LC_BY_NUMBER, (unsigned int)lvl,
                                   self->levelName());
                self->setLevelIndex(lvl);
                FILE *fp = fopen(path, "r");
                if (fp != NULL) {
                    pl->setGemsCollected(0);
                    Sim_OpenLevelFile(self, self->levelIndex());
                    Sim_SetupLevelObjects(self);
                    enter_loaded_state(self, fp);
                }
            }
            buf[0] = 0;
        }
    }

    // jjmap: a 5-byte prefix, then load by name.
    memset(frame, 0, sizeof(frame));
    memcpy(frame, buf, 5);
    frame[5] = 0;
    if (streq(frame, "jjmap")) {
        size_t len = strlen((const char *)buf);
        if (len > 6) {
            memcpy(frame, buf + 6, len - 6);
            frame[len - 6] = 0;
            GameLog_LogMessage(&g_logger, 3, GS_CHEAT_LC, (const char *)frame);
            sprintf(path, GS_CHEAT_FMT_LVL_PATH, g_gameDir, (const char *)frame);
            FILE *fp = fopen(path, "r");
            if (fp != NULL) {
                pl->setGemsCollected(0);
                Sim_ParseLevelFiles(self, (const char *)frame);
                Sim_SetupLevelObjects(self);
                enter_loaded_state(self, fp);
            }
            buf[0] = 0;
        }
    }

    if (streq(buf, "mausuruh"))
        pl->setLives(pl->lives() + (s_fx ? 2 : 1));
    if (streq(buf, "sportsman"))
        pl->setGlides((unsigned char)(pl->glides() + 1));
    if (streq(buf, "boommaker"))
        pl->setFieldE8((unsigned char)(pl->fieldE8() + 10));

    if (streq(buf, "notme")) {
        if (pl->effectDActive() == 0)
            pl->appendEffect(0x0d);
        pl->setEffectDActive(1);
        pl->setKind(3);
        pl->curTile()->setOccupant(3);
        pl->setEffectDStart(*self->clock());
    }

    memset(buf, 0, 0x100);
    self->cheatEntry()->setCursor(0);
    self->cheatEntry()->setBuffer((char *)buf);
    self->cheatEntry()->setActive(1);
    self->cheatEntry()->setLastKey(0x0d);
}
