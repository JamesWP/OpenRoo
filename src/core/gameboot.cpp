/* The Game's start-up and shut-down: the work that builds and tears down the
 * whole game around the data Game holds (it reads files, sets up the first
 * level and the input, and removes the entities), so it is done here, above
 * everything Game itself must not depend on. */

#include "game.h"
#include <algorithm>
#include <iterator>
#include <stdio.h>
#include <string.h>
#include "windev.h"
#include "logger.h"
#include "cdm.h"
#include "cdthemes.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "voicepool.h"
#include "player.h"
#include "gamereset.h"
#include "levelparse.h"
#include "levelsetup.h"
#include "gameboot.h"
#include "quitforward.h"

/* The key bytes of the save-slot files ('7') and the high-score file ('K'). */
static const char SAVE_KEY      = 0x37;
static const char HIGHSCORE_KEY = 0x4b;

void Game_Boot(Game *g, const char *gameName, const char *gameDir)
{
    snprintf(g->gameDir_, sizeof(g->gameDir_), "%s", gameDir);
    // The tally is read by the state dump before any level has been scored.
    *g->session()->tally() = ScoreTally();
    g->world()->switchCells()->clearCounts();
    Sim_ResetLevelObjectCounters(g);
    g->world()->setCrystalsInLevel(0);
    g->mapChanged_       = 1;
    g->initialised_      = 0;
    g->audio()->setLevelSoundsReady(0);
    g->scriptPlayer_.clearStreams();
    g->audio()->fixedSounds()->switchClick    = NULL;
    g->audio()->fixedSounds()->menuUpDown     = NULL;
    g->audio()->fixedSounds()->count          = NULL;
    g->audio()->fixedSounds()->lastSeconds    = NULL;
    g->audio()->fixedSounds()->timeOut        = NULL;
    g->audio()->fixedSounds()->crystalBank[0] = NULL;
    g->audio()->fixedSounds()->crystalBank[1] = NULL;
    g->audio()->fixedSounds()->crystalBank[2] = NULL;
    strcpy(g->menuLevelName_, "DemoLevelForest");
    g->frontEnd()->setCreditsRestart(0);
    g->frontEnd()->setCreditsLatch(0);

    snprintf(g->gameFileName_, sizeof(g->gameFileName_), "%s", gameName);
    g->audio()->cdThemes()->readTrackThemeTable(g->gameDir_, g->gameFileName_);
    g->audio()->cdThemes()->listTrackLengths();
    if (g->audio()->cdThemes()->validateTrackLengths())
        g_logger.logMessage(2, "GAME: original CD is in the drive - OK");
    else
        g_logger.logMessage(3, "GAME: * warning * - the original CD is not in drive");
    g_logger.logMessage(1, "GAME: game-object created, game name:%s", g->gameFileName_);

    if (!g->session()->loadGameFile(g->gameDir_, g->gameFileName_)) {
        g_logger.logMessage(4, "GAME: ** error ** game-file named as %s.gam could not be loaded - aborting!!!", g->gameFileName_);
        windev::quit(1);
        return;
    }

    g->frontEnd()->saveSlots()->setCount(6);
    if (!g->frontEnd()->saveSlots()->loadAllSlotFiles(g->gameDir_, g->gameFileName_, SAVE_KEY)) {
        g_logger.logMessage(3, "GAME: warning - no save-files for this game (maybe started a new game the first time\077), creating %d empty slots", (unsigned)g->frontEnd()->saveSlots()->count());
        g->frontEnd()->saveSlots()->initialiseEmpty();
        g->frontEnd()->saveSlots()->writeAllSlotFiles(g->gameDir_, g->gameFileName_, SAVE_KEY);
    }
    g->levelPrepPending_ = 0;
    if (!g->config_.loadValues("openroo.ini")) {
        g_logger.logMessage(3, "GAME: warning - no correct config-values (or no *.cfg-file), creating default-config");
        g->config_.fillDefaults();
    } else {
        g_logger.logMessage(1, "GAME: config-values loaded");
    }

    g->frontEnd()->highScores()->setCount(10);
    if (!g->frontEnd()->highScores()->readFile(g->gameDir_, g->gameFileName_, HIGHSCORE_KEY)) {
        g_logger.logMessage(3, "GAME: warning - no highscore-file for the game found! Creating a new one...");
        g->frontEnd()->highScores()->fillDefaults();
        g->frontEnd()->highScores()->writeFile(g->gameDir_, g->gameFileName_, HIGHSCORE_KEY);
    } else {
        g_logger.logMessage(1, "GAME: highscore-file loaded");
    }

    g_logger.logMessage(2, "GAME: this is a commercial version");
    audiodev::setEffectsVolume(g->config()->effectsGain());
    g->audio()->cdAudio()->setVolume(g->config()->musicGain());
    g->state_ = GameState::MainMenu;
    g->audio()->cdAudio()->stop();
    g->frontEnd()->menu()->buildDefaultGraph(g->frontEnd()->saveSlots()->count());
    g->session()->setLevelIndex(0);
    g->camera()->setZoomDistance(g->config_.cameraDistanceSetting());
    g->camera()->setDistance(g->config_.cameraDistanceSetting());
    Sim_ClearGameState(g);
    Sim_ParseLevelFiles(g, g->menuLevelName_);
    Sim_SetupLevelObjects(g);
    g->scriptPlayer_.setRunning(g->scriptPlayer_.loaded() != 0 ? 1 : 0);

    g->world()->setNow(1000.0);
    g->gemsGoalReached_     = 0;
    g->world()->player()->setGliding(0);
    g->frontEnd()->setTextEntryActive(0);
    g->camera()->setMode(1);
    g->camera()->setOverviewActive(0);
    g->audio()->fixedSounds()->loaded = 0;
    std::fill_n(g->frontEnd()->cheatBuffer(), 0x100, 0);
    g->frontEnd()->cheatEntry()->setMaxLength(30);
    g->frontEnd()->cheatEntry()->setCursor(0);
    g->frontEnd()->cheatEntry()->setBuffer((char *)g->frontEnd()->cheatBuffer());
    g->frontEnd()->cheatEntry()->setActive(1);
    g->camera()->setParkedCameraOption(0);
    g->initialised_        = 1;
    Core_ForwardQuit(g);
}

void Game_Shutdown(Game *g)
{
    g_logger.logMessage(1, "GAME: starting destructor");
    g->world()->clearObjects();
    g->world()->setSwitchMax(0);
    g->config_.setCameraDistanceSetting(g->camera()->zoomDistance());
    if (g->frontEnd()->highScores()->writeFile(g->gameDir_, g->gameFileName_, HIGHSCORE_KEY))
        g_logger.logMessage(1, "GAME: highscore-files saved");
    if (g->config_.save("openroo.ini"))
        g_logger.logMessage(1, "GAME: config-values saved correctly");
    else
        g_logger.logMessage(3, "GAME: ** error ** while saving config-values (maybe write-protected or hd full\077) !!!");
    g->scriptPlayer_.releaseStreams();
    g->world()->extraObjects()->releaseSounds();
    g->releaseAllSounds();
    g_logger.logMessage(1, "GAME: all sounds released successfully");
    g->audio()->soundManager()->purgeAssets();

}
