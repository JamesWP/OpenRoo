/* Game::GameTick: one simulation step, called once a frame by RenderGameFrame
 * with dt and the clock.  It ticks the bombs, the timed foe spawners, the
 * countdown, the player's bomb drop and switches, the foes, the lifts and
 * platforms; then, by game state, the camera and exit while playing, the restart
 * or game-over tally on ENTER, and high-score entry.  The notes on what
 * those steps do are in sim/playtick.cpp and flow/playflow.cpp.
 *
 * The x87 helpers keep a value on the FPU across a chain where the result
 * depends on it, rather than trusting the compiler's spills.
 *
 * KAROO_SIM_FX=tickorder, a negative control: the lift and platform loops run in
 * the other order.  Both change the tile map, so the order is observable. */

#include "gametick.h"
#include "crtmath.h"
#include "inputdev.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "sysdev.h"
#include "windev.h"
#include "logger.h"
#include "game.h"
#include "quitforward.h"
#include "checkpoint.h"
#include "keypress.h"
#include "scoretally.h"
#include "prof.h"
#include "tiledebug.h"
#include "gamedebug.h"
#include "record.h"
#include "playtick.h"
#include "playflow.h"


static int s_fx = -1;

/* float(sin(now * 0.0025f) * 0.2 + base), evaluated in extended precision. */
static float camera_sway(double now, float base)
{
    static const float  k1 = 0.0024999999441206455f;  // 0.0025 as a float
    static const double k2 = 0.20000000298023224;     // 0.2 as a float, widened
    long double s = sinl((long double)now * (long double)k1);
    return (float)(s * (long double)k2 + (long double)base);
}

/* The completion percentage: a / (b * 0.001f) * 25, truncated to a byte.
 * b == 0 gives an infinity, which truncates to 0x8000000000000000, so the
 * byte is 0. */
static unsigned char completion_percent(unsigned int a, unsigned int b)
{
    static const float k001 = 0.0010000000474974513f;
    long double r = (long double)a / ((long double)b * (long double)k001)
                    * 25.0L;
    if (!(r > -9.2e18L && r < 9.2e18L))
        return 0;
    return (unsigned char)(long long)r;
}

/* Advances the clock by dt; the pause menu freezes the tick step at zero. */
static void advance_clock(Game *self, double dt, double now)
{
    self->setLastTickTime(now);
    if (self->state() == GameState::PauseMenu) {
        self->world()->tickStep()->value = 0.0;  // +0.0
    } else {
        self->world()->tickStep()->value = dt;
        (*self->world()->clock()) = (double)((long double)dt + (long double)(*self->world()->clock()));
    }
}

/* The level objects: the tile debug map, then the lifts, platforms, falling tiles and bridges. */
static void tick_level_objects(Game *self)
{
    Tile_DebugDrawMap(self);
    if (!s_fx) {
        self->world()->tickLifts();
        self->world()->tickPlatforms();
    } else {
        self->world()->tickPlatforms();
        self->world()->tickLifts();
    }
    self->world()->tickFallings();
    self->world()->tickBridges();
}

  unsigned int
Core_GameTick(Game *self, double dt, double now, ProgableControl *progCtrl)
{
    Player *pl = self->world()->player();

    if (s_fx < 0) {
        char e[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "tickorder") == 0);
        if (s_fx)
            g_logger.write("gametick: KAROO_SIM_FX=tickorder -- platforms tick before lifts\n");
    }

    Play_LoadSounds(self);
    if (self->state() == GameState::Quitting && self->frontEnd()->debounceRef() != inputdev::KEY_RETURN && input_key_down(inputdev::KEY_RETURN) != 0) {
        g_logger.logMessage(1, "GAME: GameActions - JJ_GAME_END");
        windev::quit(1);
    }

    advance_clock(self, dt, now);

    if (self->state() == GameState::MainMenu || self->state() == GameState::PauseMenu) {
        PROF_SCOPE("menus");
        Sim_RestoreCheckpointStateBlocks(self);
        Flow_HandleKeypress(self, progCtrl);
    }

    if (self->state() == GameState::Flythrough)
        Flow_TickFlythrough(self);

    if (self->camera()->cameraMode() == 0)
        self->camera()->setCameraDistance(camera_sway(now, self->camera()->zoomDistance()));

    if (self->state() == GameState::LevelComplete) {
        PROF_SCOPE("menus");
        Flow_HandleKeypress(self, progCtrl);
        Flow_AnimateScoreTallyStages(self);
    }
    if (self->state() == GameState::GameOver) {
        PROF_SCOPE("menus");
        Flow_AnimateScoreTallyStages(self);
    }
    if (self->state() == GameState::NameEntry)
        self->frontEnd()->nameEntry()->poll((unsigned int)ftol80((*self->world()->clock())));

    tick_level_objects(self);

    if ((self->state() == GameState::Playing || self->state() == GameState::Flythrough) && self->frontEnd()->debounceRef() != inputdev::KEY_ESCAPE && input_key_down(inputdev::KEY_ESCAPE) != 0)
        Flow_OpenPauseMenu(self);

    self->world()->tickBombs();

    {
        unsigned char pct = completion_percent((unsigned int)pl->completionNumerator(), self->session()->field_170a65());
        self->session()->setVitalityPercent(pct);
        if (pct > 100)
            self->session()->setVitalityPercent(100);
    }

    {
        PROF_SCOPE("player");
        pl->updateTileEffects();
    }

    if (self->state() == GameState::Playing)
        Flow_TickPlayingControls(self, progCtrl);
    else
        Flow_TickIdleControls(self, progCtrl);

    Play_DropPlayerBomb(self);
    Play_PlayerSwitch(self);

    if (pl->moveState() != 0) {
        self->camera()->setCameraMode(2);
    } else if (self->state() == GameState::Playing) {
        self->world()->setTimeElapsed(self->world()->timeElapsed()
            + (unsigned int)ftol80(self->world()->tickStep()->value));
        self->session()->setField170a65(self->session()->field_170a65()
            + (unsigned int)ftol80(self->world()->tickStep()->value));
    }

    if ((unsigned int)pl->gliding() != 0) {
        self->camera()->setCameraMode(0);
    } else if ((unsigned int)pl->falling() != 0) {
        long double d = (long double)(int)pl->fallStartH() - (long double)(int)pl->heightCell();
        if (d > 2.0L) {
            self->camera()->setCameraMode(1);
            self->camera()->setCameraEye(0, pl->posU());
            self->camera()->setCameraEye(2, pl->posV());
        }
    }

    Play_TickFoes(self);

    pl->debugDraw();  // last, so it sits over the foes
    Game_DebugPanel(self);
    Cheat_DebugPanel(self);

    if (self->state() == GameState::Playing)
        Play_CheckLevelEnd(self);

    // ENTER after a death, or on the game-over tally.
    if (self->state() != GameState::GameOver) {
        if (self->frontEnd()->debounceRef() != inputdev::KEY_RETURN && input_key_down(inputdev::KEY_RETURN) != 0 && pl->moveState() != 0 && self->state() == GameState::Playing)
            Flow_RestartAfterDeath(self);
    } else if (self->frontEnd()->debounceRef() != inputdev::KEY_RETURN && input_key_down(inputdev::KEY_RETURN) != 0 && self->session()->tallyDone() != 0) {
        Flow_LeaveGameOver(self);
    }

    // ENTER after high-score name entry.
    if (self->state() == GameState::NameEntry && self->frontEnd()->debounceRef() != inputdev::KEY_RETURN && input_key_down(inputdev::KEY_RETURN) != 0)
        Flow_LeaveNameEntry(self);

    if (pl->moveState() != 0 && pl->moveState() != 2)
        self->camera()->setCameraMode(2);
    if (input_key_down(self->frontEnd()->debounceRef()) == 0)
        self->frontEnd()->debounceRef() = 0;
    self->camera()->setOverviewActive(0);
    self->camera()->setField13cc90(0);
    self->setField48b14(self->field_48b14() + 1);
    self->setTickCount(self->tickCount() + 1);
    Core_ForwardQuit(self);
    return self->tickCount() & 0xffffff00u;
}
