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

#include "inputdev.h"
#include <stdint.h>
#include "sysdev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logger.h"
#include "game.h"
#include "levelsetup.h"
#include "levelscore.h"
#include "levelparse.h"
#include "cheatcode.h"
#include "cheats.h"
#include "menutree.h"
#include "textentry.h"
#include "player.h"
#include "gamestr.h"
#include "cdm.h"
#include <algorithm>
#include <iterator>
#include <fstream>

class CDM;

static int s_fx = -1;

static int streq(const unsigned char *a, const char *b)
{
    return strcmp((const char *)a, b) == 0;
}

  void  
Flow_HandleTypedCheatCode(Game *self)
{
    Player *pl = self->world()->player();
    unsigned char *buf = self->frontEnd()->cheatBuffer();
    unsigned char frame[256];
    char path[384];

    if (s_fx < 0) {
        char e[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "cheatlife") == 0);
        if (s_fx)
            g_logger.write("cheatcode: KAROO_SIM_FX=cheatlife -- mausuruh gives 2\n");
    }

    self->frontEnd()->cheatEntry()->poll((unsigned int)(long long)*self->world()->clock());
    if (self->frontEnd()->cheatEntry()->active() != 0)
        return;

    if (streq(buf, "kaputo")) {
        Cheat_KillFoes(self);
    }

    if (streq(buf, "supa")) {
        if ((unsigned int)self->session()->levelIndex() + 1 == (unsigned int)self->session()->levelCount()) {
            self->setState(GameState::GameOver);
            if (self->musicOn() != 0)
                self->cdThemes()->play(GS_GAME_GAMEOVER);
            Score_CalculateLevelScore(self, 0x28);
            self->frontEnd()->setDebounce(inputdev::KEY_RETURN);
            g_logger.logMessage(1, "GAME: completed at level %d/%d",
                               (unsigned int)self->session()->levelIndex() + 1,
                               (unsigned int)self->session()->levelCount());
        } else {
            self->frontEnd()->menu()->setLockStart(self->lastTickTime());
            self->frontEnd()->menu()->setLock(1);
            self->setState(GameState::LevelComplete);
            if (self->musicOn() != 0)
                self->cdThemes()->play(GS_GAME_COMPLETED);
            self->camera()->setCameraMode(2);
            self->frontEnd()->menu()->rewind();
            self->frontEnd()->menu()->pop();
            self->frontEnd()->menu()->push(0x28);
            self->frontEnd()->menu()->setLockStart(self->lastTickTime());
            self->frontEnd()->menu()->setNode(0x28);
            self->frontEnd()->menu()->setCursor(0);
            self->frontEnd()->menu()->setLock(1);
            Score_CalculateLevelScore(self, (char)static_cast<unsigned char>(self->state()));
            self->session()->setRestartCount(0);
            g_logger.logMessage(1, "GAME: c - sl");
        }
        self->session()->setTotalPlayTime((double)(unsigned long long)self->world()->timeElapsed() + self->session()->totalPlayTime());
    }

    // jjmapnr: a 7-byte prefix, then load by number.
    std::fill(std::begin(frame), std::end(frame), 0);
    std::copy_n(buf, 7, frame);
    frame[7] = 0;
    if (streq(frame, "jjmapnr")) {
        size_t len = strlen((const char *)buf);
        if (len > 8) {
            char num[256];
            std::copy_n(buf + 8, len - 8, num);
            num[len - 8] = 0;
            unsigned char lvl = (unsigned char)(atoi(num) - 1);
            Cheat_LoadLevel(self, lvl);
            buf[0] = 0;
        }
    }

    // jjmap: a 5-byte prefix, then load by name.
    std::fill(std::begin(frame), std::end(frame), 0);
    std::copy_n(buf, 5, frame);
    frame[5] = 0;
    if (streq(frame, "jjmap")) {
        size_t len = strlen((const char *)buf);
        if (len > 6) {
            std::copy_n(buf + 6, len - 6, frame);
            frame[len - 6] = 0;
            g_logger.logMessage(3, "GAME: lc %s", (const char *)frame);
            snprintf(path, sizeof(path), GS_CHEAT_FMT_LVL_PATH, self->gameDir(), (const char *)frame);
            if (std::ifstream(sysdev::nativePath(path))) {
                pl->setGemsCollected(0);
                Sim_ParseLevelFiles(self, (const char *)frame);
                Sim_SetupLevelObjects(self);
                Cheat_EnterLoadedState(self);
            }
            buf[0] = 0;
        }
    }

    if (streq(buf, "mausuruh"))
        Cheat_AddLife(self, s_fx ? 2 : 1);
    if (streq(buf, "sportsman"))
        Cheat_AddGlide(self);
    if (streq(buf, "boommaker"))
        Cheat_AddBombs(self);

    if (streq(buf, "notme"))
        Cheat_Invulnerable(self);

    std::fill_n(buf, 0x100, 0);
    self->frontEnd()->cheatEntry()->setCursor(0);
    self->frontEnd()->cheatEntry()->setBuffer((char *)buf);
    self->frontEnd()->cheatEntry()->setActive(1);
    self->frontEnd()->cheatEntry()->setLastKey(inputdev::KEY_RETURN);
}
