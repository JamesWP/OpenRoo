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

#include "gamedir.h"
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
#include "dbg.h"
#include "menutree.h"
#include "textentry.h"
#include "foe.h"
#include "player.h"
#include "tile.h"
#include "gamestr.h"
#include "gameglobals.h"
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

/* The tail both level loaders share: go to the loaded state with the intro
 * flythrough armed. */
static void enter_loaded_state(Game *game)
{
    game->setCameraDistance(7.0f);
    game->setState(4);
    if (game->musicOn() != 0)
        g_cdAudio.stop();
    game->scriptPlayer()->setRunning(1);
    game->setCameraMode(1);
    game->setDebounce(inputdev::KEY_RETURN);
}

void Cheat_KillFoes(Game *g)
{
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

void Cheat_AddLife(Game *g, int n)
{
    g->player()->setLives(g->player()->lives() + n);
}

void Cheat_AddGlide(Game *g)
{
    g->player()->setGlides((unsigned char)(g->player()->glides() + 1));
}

void Cheat_AddBombs(Game *g)
{
    g->player()->setBombsCarried((unsigned char)(g->player()->bombsCarried() + 10));
}

void Cheat_Invulnerable(Game *g)
{
    Player *pl = g->player();
    if (pl->effectDActive() == 0)
        pl->appendEffect(0x0d);
    pl->setEffectDActive(1);
    pl->setKind(3);
    pl->curTile()->setOccupant(3);
    pl->setEffectDStart(*g->clock());
}

void Cheat_FreezeFoes(Game *g)
{
    Player *pl = g->player();
    if (pl->freezeActive() == 0)
        pl->appendEffect(8);
    pl->setFreezeStart(*g->clock());
    pl->setFreezeActive(1);
}

void Cheat_TeleportPlayer(Game *g, unsigned char u, unsigned char v)
{
    Player *pl = g->player();
    const unsigned char h = g->map()->tile(u, v)->height();
    pl->setMoveDir(0);
    pl->setPendingMove(0);
    pl->setCell(u, v, h);
    pl->setPos((float)u, (float)h, (float)v);
}

bool Cheat_LoadLevel(Game *self, unsigned char lvl)
{
    char path[384];
    Sim_SetCurrentLevelName(self, lvl);
    if (lvl >= self->levelCount())
        return false;
    snprintf(path, sizeof(path), GS_CHEAT_FMT_LVL_PATH, gameDir(), self->levelName());
    g_logger.logMessage(3, "GAME: lc by number %d name:%s", (unsigned int)lvl, self->levelName());
    self->setLevelIndex(lvl);
    if (!std::ifstream(sysdev::nativePath(path)))
        return false;
    self->player()->setGemsCollected(0);
    Sim_OpenLevelFile(self, self->levelIndex());
    Sim_SetupLevelObjects(self);
    enter_loaded_state(self);
    return true;
}

  void  
Sim_HandleTypedCheatCode(Game *self)
{
    Player *pl = self->player();
    unsigned char *buf = self->cheatBuffer();
    unsigned char frame[256];
    char path[384];

    if (s_fx < 0) {
        char e[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "cheatlife") == 0);
        if (s_fx)
            g_logger.write("cheatcode: KAROO_SIM_FX=cheatlife -- mausuruh gives 2\n");
    }

    self->cheatEntry()->poll((unsigned int)(long long)*self->clock());
    if (self->cheatEntry()->active() != 0)
        return;

    if (streq(buf, "kaputo")) {
        Cheat_KillFoes(self);
    }

    if (streq(buf, "supa")) {
        if ((unsigned int)self->levelIndex() + 1 == (unsigned int)self->levelCount()) {
            self->setState(2);
            if (self->musicOn() != 0)
                self->cdThemes()->play(GS_GAME_GAMEOVER);
            Score_CalculateLevelScore(self, 0x28);
            self->setDebounce(inputdev::KEY_RETURN);
            g_logger.logMessage(1, "GAME: completed at level %d/%d",
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
            g_logger.logMessage(1, "GAME: c - sl");
        }
        self->setTotalPlayTime((double)(unsigned long long)self->timeElapsed() + self->totalPlayTime());
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
            snprintf(path, sizeof(path), GS_CHEAT_FMT_LVL_PATH, gameDir(), (const char *)frame);
            if (std::ifstream(sysdev::nativePath(path))) {
                pl->setGemsCollected(0);
                Sim_ParseLevelFiles(self, (const char *)frame);
                Sim_SetupLevelObjects(self);
                enter_loaded_state(self);
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
    self->cheatEntry()->setCursor(0);
    self->cheatEntry()->setBuffer((char *)buf);
    self->cheatEntry()->setActive(1);
    self->cheatEntry()->setLastKey(inputdev::KEY_RETURN);
}

void Cheat_DebugPanel(Game *g)
{
    // Not saved with the window: the cheats change the game.
    static bool holdFrozen;
    dbg::Section s("Cheats", true);
    if (!s)
        return;
    if (ImGui::Button("+life"))      Cheat_AddLife(g, 1);
    ImGui::SameLine();
    if (ImGui::Button("+10 bombs"))  Cheat_AddBombs(g);
    ImGui::SameLine();
    if (ImGui::Button("+glide"))     Cheat_AddGlide(g);
    if (ImGui::Button("invulnerable")) Cheat_Invulnerable(g);
    ImGui::SameLine();
    if (ImGui::Button("kill foes"))  Cheat_KillFoes(g);
    ImGui::Checkbox("hold foes frozen", &holdFrozen);
    if (holdFrozen)
        Cheat_FreezeFoes(g);
}
