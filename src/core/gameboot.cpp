/* The Game's start-up and shut-down: the work that builds and tears down the
 * whole game around the data Game holds (it reads files, sets up the first
 * level and the input, and removes the entities), so it is done here, above
 * everything Game itself must not depend on. */

#include "game.h"
#include <algorithm>
#include <iterator>
#include <stdio.h>
#include <string.h>
#include "inputdev.h"
#include "windev.h"
#include "gamestr.h"
#include "logger.h"
#include "cdm.h"
#include "cdthemes.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "voicepool.h"
#include "player.h"
#include "progctrl.h"
#include "gamereset.h"
#include "levelparse.h"
#include "levelsetup.h"
#include "liftobject.h"
#include "platformobject.h"
#include "fallingtile.h"
#include "bridgeobject.h"
#include "foe.h"
#include "bomb.h"
#include "gameboot.h"

/* The key bytes of the save-slot files ('7') and the high-score file ('K'). */
static const char SAVE_KEY      = 0x37;
static const char HIGHSCORE_KEY = 0x4b;

void Game_Boot(Game *g, const char *gameName, const char *gameDir)
{
    snprintf(g->gameDir_, sizeof(g->gameDir_), "%s", gameDir);
    g->field_0c_   = 0;
    g->tickCount_  = 0;
    g->field_04_   = 1.0;
    // The tally is read by the state dump before any level has been scored.
    g->tally_ = ScoreTally();
    g->switchCells_.clearCounts();
    // PRESERVED: only the first 256 of each 500-slot table.
    std::fill_n(g->bombs_.slot, 0x100, nullptr);
    std::fill_n(g->foes_.slot, 0x100, nullptr);
    g->fallings_.count = 0;
    g->foes_.count       = 0;
    g->lifts_.count      = 0;
    g->platforms_.count     = 0;
    g->bombs_.count      = 0;
    g->bridges_.count    = 0;
    Sim_ResetLevelObjectCounters(g);
    g->field_42252_      = 0;
    g->mapChanged_       = 1;
    g->initialised_      = 0;
    g->levelSoundsReady_ = 0;
    g->scriptPlayer_.clearStreams();
    std::fill(std::begin(g->foes_.ids), std::end(g->foes_.ids), 0);
    std::fill(std::begin(g->bombs_.ids), std::end(g->bombs_.ids), 0);
    g->field_48b14_ = 0;
    g->fixedSounds_.switchClick    = NULL;
    g->fixedSounds_.menuUpDown     = NULL;
    g->fixedSounds_.count          = NULL;
    g->fixedSounds_.lastSeconds    = NULL;
    g->fixedSounds_.timeOut        = NULL;
    g->fixedSounds_.crystalBank[0] = NULL;
    g->fixedSounds_.crystalBank[1] = NULL;
    g->fixedSounds_.crystalBank[2] = NULL;
    strcpy(g->menuLevelName_, GS_GAME_DEMO_LEVEL);
    g->field_13cc8c_ = 0;
    g->field_13cc88_ = 0;

    snprintf(g->gameFileName_, sizeof(g->gameFileName_), "%s", gameName);
    g->cdThemes_.readTrackThemeTable(g->gameDir_, g->gameFileName_);
    g->cdThemes_.listTrackLengths();
    if (g->cdThemes_.validateTrackLengths())
        g_logger.logMessage(2, "GAME: original CD is in the drive - OK");
    else
        g_logger.logMessage(3, "GAME: * warning * - the original CD is not in drive");
    g_logger.logMessage(1, "GAME: game-object created, game name:%s", g->gameFileName_);

    if (!g->loadGameFile(g->gameFileName_)) {
        g_logger.logMessage(4, "GAME: ** error ** game-file named as %s.gam could not be loaded - aborting!!!", g->gameFileName_);
        windev::quit(1);
        return;
    }

    g->debounce_ = inputdev::KEY_RETURN;
    g->menu_.setLastKey(inputdev::KEY_RETURN);
    g->saveSlots_.setCount(6);
    if (!g->saveSlots_.loadAllSlotFiles(g->gameDir_, g->gameFileName_, SAVE_KEY)) {
        g_logger.logMessage(3, "GAME: warning - no save-files for this game (maybe started a new game the first time\077), creating %d empty slots", (unsigned)g->saveSlots_.count());
        g->saveSlots_.initialiseEmpty();
        g->saveSlots_.writeAllSlotFiles(g->gameDir_, g->gameFileName_, SAVE_KEY);
    }
    g->field_173584_ = 0;
    if (!g->config_.loadValues(GS_CFG_FILE)) {
        g_logger.logMessage(3, "GAME: warning - no correct config-values (or no *.cfg-file), creating default-config");
        g->config_.fillDefaults();
    } else {
        g_logger.logMessage(1, "GAME: config-values loaded");
    }
    g_progCtrl.setJoyDeadzone(0, g->config_.joyDeadzone() * 100);
    g_progCtrl.setJoyDeadzone(4, g->config_.joyDeadzone() * 100);

    g->highScores_.setCount(10);
    if (!g->highScores_.readFile(g->gameDir_, g->gameFileName_, HIGHSCORE_KEY)) {
        g_logger.logMessage(3, "GAME: warning - no highscore-file for the game found! Creating a new one...");
        g->highScores_.fillDefaults();
        g->highScores_.writeFile(g->gameDir_, g->gameFileName_, HIGHSCORE_KEY);
    } else {
        g_logger.logMessage(1, "GAME: highscore-file loaded");
    }

    // PRESERVED: a demo check, for a nonzero field_0c_ with more than ten
    // levels.  field_0c_ was zeroed above, so it never fires.
    if (g->field_0c_ != 0 && g->levelCount_ > 10) {
        g_logger.logMessage(4, "GAME: ** error ** this is a DEMO, piracy alert, aborting!!!");
        windev::quit(1);
        return;
    }
    g_logger.logMessage(2, "GAME: this is a commercial version");
    audiodev::setEffectsVolume(g->effectsGain());
    g_cdAudio.setVolume(g->musicGain());
    g->state_ = 0;
    g_cdAudio.stop();
    g->menu_.buildDefaultGraph(g->saveSlots_.count());
    g->levelIndex_     = 0;
    g->camera()->setZoomDistance(g->config_.cameraDistanceSetting());
    g->camera()->setCameraDistance(g->config_.cameraDistanceSetting());
    Sim_ClearGameState(g);
    Sim_ParseLevelFiles(g, g->menuLevelName_);
    Sim_SetupLevelObjects(g);
    g->scriptPlayer_.setRunning(g->scriptPlayer_.loaded() != 0 ? 1 : 0);

    g->clock_            = 1000.0;
    g->field_173b1a_     = 0;
    g->player()->setGliding(0);
    g->textEntryActive_  = 0;
    g->camera()->setCameraMode(1);
    g->camera()->setOverviewActive(0);
    g->field_13cc84_     = 0;
    g->bridges_.count      = 0;
    g->fixedSounds_.loaded = 0;
    std::fill(std::begin(g->cheatBuffer_), std::end(g->cheatBuffer_), 0);
    g->cheatEntry_.setMaxLength(30);
    g->cheatEntry_.setCursor(0);
    g->cheatEntry_.setBuffer((char *)g->cheatBuffer_);
    g->cheatEntry_.setActive(1);
    g->cheatEntry_.setLastKey(inputdev::KEY_RETURN);
    g->camera()->setParkedCameraOption(0);
    g->initialised_        = 1;
}

void Game_Shutdown(Game *g)
{
    g_logger.logMessage(1, "GAME: starting destructor");
    LiftObject::purgeAll(g->entityContext(), g->lifts());
    PlatformObject::purgeAll(g->entityContext(), g->platforms());
    FallingTile::purgeAll(g->entityContext(), g->fallings());
    BridgeObject::purgeAll(g->entityContext(), g->bridges());
    while (g->foes_.count != 0)
        Foe::remove(g->entityContext(), g->foes_, g->foes_.ids[0]);
    while (g->bombs_.count != 0)
        Bomb::remove(g->entityContext(), g->bombs_, g->bombs_.ids[0]);
    g->fallings_.count = 0;
    g->foes_.count       = 0;
    g->lifts_.count      = 0;
    g->platforms_.count     = 0;
    g->bombs_.count      = 0;
    g->switchMax_      = 0;
    g->config_.setCameraDistanceSetting(g->camera()->zoomDistance());
    if (g->highScores_.writeFile(g->gameDir_, g->gameFileName_, HIGHSCORE_KEY))
        g_logger.logMessage(1, "GAME: highscore-files saved");
    if (g->config_.save(GS_CFG_FILE))
        g_logger.logMessage(1, "GAME: config-values saved correctly");
    else
        g_logger.logMessage(3, "GAME: ** error ** while saving config-values (maybe write-protected or hd full\077) !!!");
    g->scriptPlayer_.releaseStreams();
    g->extraObjects_.releaseSounds();
    g->releaseAllSounds();
    g_logger.logMessage(1, "GAME: all sounds released successfully");
    g->soundManager()->purgeAssets();

}
