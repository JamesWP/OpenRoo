/* The level teardown, Game::ClearGameState: resets the run's scalar state,
 * then destroys every object the level made.  The lifts, platforms, falling tiles
 * and bridges go through their classes' purges; foes and bombs are removed one
 * at a time through their ID lists until none is left.
 *
 * PRESERVED: the game-file check reads its byte before the field stores but
 * acts on it after them, so every store happens even when the game file is
 * bad, and the objects are still purged after the quit request.
 *
 * KAROO_RESET_DIAG=1 logs the first call, the
 * first foe and bomb removal, and a count every 500 calls. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>

#include "logger.h"
#include "game.h"
#include "gamereset.h"
#include "player.h"


#ifdef _MSC_VER
typedef unsigned int u32_ua;  // x86 reads unaligned
#else
typedef unsigned int __attribute__((aligned(1))) u32_ua;
#endif

static int s_diag           = 0;
static int s_init           = 0;

static unsigned s_calls        = 0;
static int s_logged_clear      = 0;

static void diag_init(void)
{

    if (s_init)
        return;
    s_init = 1;

    s_diag = sysdev::envFlag("KAROO_RESET_DIAG");
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

    diag_init();
    diag_tick();

    if (s_diag && !s_logged_clear) {
        s_logged_clear = 1;
        g_logger.write("gamereset: first ClearGameState -- foes=%u enemies=%u "
                  "lift=%u platform=%u break=%u bridge=%u\n",
                  (unsigned)self->world()->foes().size(), (unsigned)self->world()->bombs().size(),
                  (unsigned)self->world()->lifts().size(),
                  (unsigned)self->world()->platforms().size(),
                  (unsigned)self->world()->fallings().size(),
                  (unsigned)self->world()->bridges().size());
    }

    // Read before the stores; acted on after them.
    gamefile_ok = self->session()->levelCount();

    Player *pl = self->world()->player();
    pl->setLives(2);  // lives
    self->camera()->setMode(2);
    // Two doubles the game zeroes in two halves; nothing reads between, so
    // each is one store.
    pl->setField126(0.0);
    self->session()->setTotalPlayTime(0.0);
    self->session()->setLevelIndex(0);
    self->session()->setRestartCount(0);
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
        self->requestQuit(1);
    }

    self->world()->clearObjects();
}
