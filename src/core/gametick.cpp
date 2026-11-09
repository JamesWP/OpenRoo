/* Game::GameTick: one simulation step, called once a frame by Core_UpdateFrame
 * with dt and the clock.  The World's clock moves first (its menus, the
 * flythrough and the tally run on the new clock, and may start a level);
 * then the controls note the presses, the World's rules run in one call
 * (World::update, entities/worldrules.cpp), and the game answers them: the
 * play time, the camera, the level's end, the restart or game-over tally on
 * ENTER, and high-score entry.  The notes on the game's own steps are in
 * sim/playtick.cpp and sequencer/playflow.cpp.
 *
 * The x87 helpers keep a value on the FPU across a chain where the result
 * depends on it, rather than trusting the compiler's spills. */

#include "gametick.h"
#include "keycodes.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "sysdev.h"
#include "windev.h"
#include "audiodev.h"
#include "logger.h"
#include "game.h"
#include "quitforward.h"
#include "checkpoint.h"
#include "keypress.h"
#include "scoretally.h"
#include "prof.h"
#include "tiledebug.h"
#include "entitydebug.h"
#include "cheats.h"
#include "gamedebug.h"
#include "keys.h"
#include "playtick.h"
#include "playflow.h"

/* float(sin(now * 0.0025f) * 0.2 + base), evaluated in extended precision. */
static float camera_sway(double now, float base)
{
    static const float  k1 = 0.0024999999441206455f;  // 0.0025 as a float
    static const double k2 = 0.20000000298023224;     // 0.2 as a float, widened
    double s = sin((double)now * (double)k1);
    return (float)(s * (double)k2 + (double)base);
}

/* The completion percentage: a / (b * 0.001f) * 25, truncated to a byte.
 * b == 0 gives an infinity, which truncates to 0x8000000000000000, so the
 * byte is 0. */
static unsigned char completion_percent(unsigned int a, unsigned int b)
{
    static const float k001 = 0.0010000000474974513f;
    double r = (double)a / ((double)b * (double)k001)
                    * 25.0;
    if (!(r > -9.2e18 && r < 9.2e18))
        return 0;
    return (unsigned char)(long long)r;
}

/* Advances the clock by dt; the pause menu freezes the tick step at zero. */
static void advance_clock(Game *self, double dt, double now)
{
    self->setLastTickTime(now);
    self->world()->beginTick(self->state() == GameState::PauseMenu ? 0.0 : dt);
}

  void
Core_GameTick(Game *self, double dt, double now, ProgableControl *progCtrl)
{
    static const bool copyWorld = sysdev::envFlag("KAROO_WORLD_COPY");
    if (copyWorld)
        self->moveToWorldCopy();
    Player *pl = self->world()->player();

    input::update();

    Play_LoadSounds(self);
    if (self->state() == GameState::Quitting && input::pressed(input::KEY_RETURN)) {
        g_logger.logMessage(1, "GAME: GameActions - JJ_GAME_END");
        windev::quit(1);
    }

    advance_clock(self, dt, now);
    audiodev::logMark(self->world()->now());

    if (self->state() == GameState::MainMenu || self->state() == GameState::PauseMenu) {
        PROF_SCOPE("menus");
        Sim_RestoreCheckpointStateBlocks(self);
        Flow_HandleKeypress(self, progCtrl);
    }

    if (self->state() == GameState::Flythrough)
        Flow_TickFlythrough(self);

    if (self->camera()->mode() == 0)
        self->camera()->setDistance(camera_sway(now, self->camera()->zoomDistance()));

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
        self->frontEnd()->nameEntry()->poll((unsigned int)(long long)(self->world()->now()));

    // Before the World's rules: the tile debug map's teleport, the pause
    // key, the HUD's vitality, and the controls, which note the presses.
    {
        unsigned char u, v;
        if (Tile_DebugDrawMap(self->world(), &u, &v))
            Cheat_TeleportPlayer(self, u, v);
    }
    if ((self->state() == GameState::Playing || self->state() == GameState::Flythrough) && input::pressed(input::KEY_ESCAPE))
        Flow_OpenPauseMenu(self);

    {
        unsigned char pct = completion_percent((unsigned int)pl->completionNumerator(), self->session()->playingTime());
        self->session()->setVitalityPercent(pct);
        if (pct > 100)
            self->session()->setVitalityPercent(100);
    }

    const bool playing = self->state() == GameState::Playing;
    if (playing)
        Flow_TickPlayingControls(self, progCtrl);
    else
        Flow_TickIdleControls(progCtrl);

    // The World's rules (World::update).
    const World::TickResult ticked =
        self->world()->update(self->takePresses(), playing, *self->soundEvents());

    // After them: the session's play time, the camera, the debug panels, and
    // the game's answer to the level's end.
    self->session()->setPlayingTime(self->session()->playingTime() + ticked.playedMs);
    if (pl->moveState() != 0)
        self->camera()->setMode(2);

    if ((unsigned int)pl->gliding() != 0) {
        self->camera()->setMode(0);
    } else if ((unsigned int)pl->falling() != 0) {
        double d = (double)(int)pl->fallStartH() - (double)(int)pl->heightCell();
        if (d > 2.0) {
            self->camera()->setMode(1);
            self->camera()->setFocus(0, pl->posU());
            self->camera()->setFocus(2, pl->posV());
        }
    }

    Entities_DebugDrawMap(self->world());
    Game_DebugPanel(self);
    Cheat_DebugPanel(self);

    if (playing && self->state() == GameState::Playing)
        Play_CheckLevelEnd(self);

    // ENTER after a death, or on the game-over tally.
    if (self->state() != GameState::GameOver) {
        if (input::pressed(input::KEY_RETURN) && pl->moveState() != 0 && self->state() == GameState::Playing)
            Flow_RestartAfterDeath(self);
    } else if (input::pressed(input::KEY_RETURN) && self->session()->tallyDone() != 0) {
        Flow_LeaveGameOver(self);
    }

    // ENTER after high-score name entry.
    if (self->state() == GameState::NameEntry && input::pressed(input::KEY_RETURN))
        Flow_LeaveNameEntry(self);

    if (pl->moveState() != 0 && pl->moveState() != 2)
        self->camera()->setMode(2);
    self->camera()->setOverviewActive(0);
    Core_ForwardQuit(self);
}
