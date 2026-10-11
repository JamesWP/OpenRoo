/* The cheats' actions: what each code does, shared by the typed codes
 * (cheatcode.cpp) and the debug UI (gamedebug.cpp).  They change the game and
 * never read the keyboard. */

#include <stdio.h>
#include "sysdev.h"
#include "logger.h"
#include "game.h"
#include "levelsetup.h"
#include "levelparse.h"
#include "cheats.h"
#include "foe.h"
#include "player.h"
#include "tile.h"
#include "gamestr.h"
#include "cdm.h"
#include <fstream>

/* The tail both level loaders share: go to the loaded state with the intro
 * flythrough armed. */
void Cheat_EnterLoadedState(Game *game)
{
    game->camera()->setDistance(7.0f);
    game->setState(GameState::Flythrough);
    if (game->config()->musicOn() != 0)
        game->audio()->cdAudio()->stop();
    game->scriptPlayer()->setRunning(1);
    game->camera()->setMode(1);
}

void Cheat_KillFoes(Game *g)
{
    // Only the move state is set: no foe is added or removed.
    for (Foe *f : g->world()->foes())
        f->setMoveState(4);
}

void Cheat_AddLife(Game *g, int n)
{
    g->world()->player()->setLives(g->world()->player()->lives() + n);
}

void Cheat_AddGlide(Game *g)
{
    g->world()->player()->setGlides((unsigned char)(g->world()->player()->glides() + 1));
}

void Cheat_AddBombs(Game *g)
{
    g->world()->player()->setBombsCarried((unsigned char)(g->world()->player()->bombsCarried() + 10));
}

void Cheat_Invulnerable(Game *g)
{
    Player *pl = g->world()->player();
    if (pl->effectDActive() == 0)
        pl->appendEffect(0x0d);
    pl->setEffectDActive(1);
    pl->setKind(3);
    pl->curTile()->setOccupant(3);
    pl->setEffectDStart(*g->world()->clock());
}

void Cheat_FreezeFoes(Game *g)
{
    Player *pl = g->world()->player();
    if (pl->freezeActive() == 0)
        pl->appendEffect(8);
    pl->setFreezeStart(*g->world()->clock());
    pl->setFreezeActive(1);
}

void Cheat_TeleportPlayer(Game *g, unsigned char u, unsigned char v)
{
    Player *pl = g->world()->player();
    const unsigned char h = g->world()->map()->tile(u, v)->height();
    pl->setMoveDir(0);
    pl->setPendingMove(0);
    pl->setCell(u, v, h);
    pl->setPos((float)u, (float)h, (float)v);
}

bool Cheat_LoadLevel(Game *self, unsigned char lvl)
{
    char path[384];
    Sim_SetCurrentLevelName(self, lvl);
    if (lvl >= self->session()->levelCount())
        return false;
    snprintf(path, sizeof(path), GS_CHEAT_FMT_LVL_PATH, self->gameDir(), self->world()->levelName());
    g_logger.logMessage(3, "GAME: lc by number %d name:%s", (unsigned int)lvl, self->world()->levelName());
    self->session()->setLevelIndex(lvl);
    if (!std::ifstream(sysdev::nativePath(path)))
        return false;
    self->world()->player()->setGemsCollected(0);
    Sim_OpenLevelFile(self, self->session()->levelIndex());
    Sim_SetupLevelObjects(self);
    Cheat_EnterLoadedState(self);
    return true;
}
