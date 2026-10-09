/* The level teardown, Game::ClearGameState: resets the run's scalar state,
 * then destroys every object the level made.  The lifts, platforms, falling tiles
 * and bridges go through their classes' purges; foes and bombs are removed one
 * at a time through their ID lists until none is left.
 *
 * PRESERVED: the game-file check reads its byte before the field stores but
 * acts on it after them, so every store happens even when the game file is
 * bad, and the objects are still purged after windev::quit.
 *
 * KAROO_SIM_FX=keepobjects (a negative control, in each purge) leaves the
 * objects and their counts alive.  KAROO_RESET_DIAG=1 logs the first call, the
 * first foe and bomb removal, and a count every 500 calls. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>

#include "logger.h"
#include "windev.h"
#include "game.h"
#include "gamereset.h"
#include "liftobject.h"
#include "platformobject.h"
#include "bridgeobject.h"
#include "fallingtile.h"
#include "bomb.h"
#include "foe.h"
#include "player.h"
#include "gamestr.h"
#include "gameglobals.h"


typedef unsigned int __attribute__((aligned(1))) u32_ua;

static int s_diag           = 0;
static int s_init           = 0;

static unsigned s_calls        = 0;
static int s_logged_clear      = 0;
static int s_logged_foedrain   = 0;
static int s_logged_enemydrain = 0;

static void fx_init(void)
{
    char buf[64];
    uint32_t n;

    if (s_init)
        return;
    s_init = 1;

    n = sysdev::getEnv("KAROO_RESET_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

static void diag_tick(void)
{
    s_calls++;
    if (s_diag && (s_calls % 500) == 0)
        g_logger.write("gamereset: %u clear calls\n", s_calls);
}

  void  
Sim_ClearGameState(Game *self)
{
    unsigned char gamefile_ok;

    fx_init();
    diag_tick();

    if (s_diag && !s_logged_clear) {
        s_logged_clear = 1;
        g_logger.write("gamereset: first ClearGameState -- foes=%u enemies=%u "
                  "lift=%u platform=%u break=%u bridge=%u\n",
                  (unsigned)self->foeCount(), (unsigned)self->bombCount(),
                  (unsigned)self->liftCount(),
                  (unsigned)self->platformCount(),
                  (unsigned)self->fallingCount(),
                  (unsigned)self->bridgeCount());
    }

    // Read before the stores; acted on after them.
    gamefile_ok = self->levelCount();

    Player *pl = self->player();
    pl->setLives(2);  // lives
    self->setCameraMode(2);
    // Two doubles the game zeroes in two halves; nothing reads between, so
    // each is one store.
    pl->setField126(0.0);
    self->setTotalPlayTime(0.0);
    self->setLevelIndex(0);
    self->setRestartCount(0);
    pl->setGemsCollected(0);
    pl->setFacing(1);
    pl->setMoveDir(0);
    pl->setFalling(0);
    pl->setMoveState(0);
    pl->setStepDuration(200.0);  // bits 0x4069000000000000
    pl->setScore(0);

    if (gamefile_ok == 0) {
        g_logger.logMessage(4, "GAME: ** error ** game-file %s is not readable (maybe it does not exist\077) aborting game!!!",
                           self->gameFileName());
        windev::quit(1);
    }

    LiftObject::purgeAll(self->entityContext(), self->lifts());
    PlatformObject::purgeAll(self->entityContext(), self->platforms());
    FallingTile::purgeAll(self->entityContext(), self->fallings());
    BridgeObject::purgeAll(self->entityContext(), self->bridges());

    // The 256 switch counts.
    self->switchCells()->clearCounts();

    while (self->foeCount() != 0) {
        if (s_diag && !s_logged_foedrain) {
            s_logged_foedrain = 1;
            g_logger.write("gamereset: first foe drain -- count=%u id=%u\n",
                      (unsigned)self->foeCount(),
                      (unsigned)self->foeId(0));
        }
        Foe::remove(self, self->foeId(0));
    }

    while (self->bombCount() != 0) {
        if (s_diag && !s_logged_enemydrain) {
            s_logged_enemydrain = 1;
            g_logger.write("gamereset: first enemy drain -- count=%u id=%u\n",
                      (unsigned)self->bombCount(), (unsigned)self->bombId(0));
        }
        Bomb::remove(self, self->bombId(0));
    }

    self->setFallingCount(0);
    self->setFoeCount(0);
    self->setLiftCount(0);
    self->setPlatformCount(0);
    self->setBombCount(0);
}
