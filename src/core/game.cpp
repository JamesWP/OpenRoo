/* Game's lifecycle: the constructor, the destructor, the game-file loader and
 * the sound release.  Each member is built and torn down by its owner
 * (menutree.cpp, cdthemes.cpp, theme.cpp, extraobjects.cpp, soundmanager.cpp,
 * textentry.cpp, highscores.cpp, saveslots.cpp, player.cpp, scriptplayer.cpp,
 * config.cpp, levelmap.cpp), in a fixed order; teardown is its exact reverse.
 */

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "windev.h"
#include "gamestr.h"
#include "logger.h"
#include "gameglobals.h"
#include <stdlib.h>
#include "cdm.h"
#include "cdthemes.h"
#include "theme.h"
#include "extraobjects.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "voicepool.h"
#include "player.h"
#include "progctrl.h"
#include "gamereset.h"
#include "levelparse.h"
#include "levelsetup.h"
#include "liftobject.h"
#include "slideobject.h"
#include "breakabletile.h"
#include "bridgeobject.h"
#include "foe.h"
#include "bomb.h"

/* The key bytes of the save-slot files ('7') and the high-score file ('K'). */
static const char SAVE_KEY      = 0x37;
static const char HIGHSCORE_KEY = 0x4b;

Game::Game(const char *gameName)
{
    field_0c_   = 0;
    tickCount_  = 0;
    field_04_   = 1.0;
    // The tally is read by the state dump before any level has been scored.
    memset(&tally_, 0, sizeof(tally_));
    switchCells_.clearCounts();
    // PRESERVED: only the first 256 of each 500-slot table.
    memset(bombSlots_, 0, 0x100 * sizeof(bombSlots_[0]));
    memset(foeSlots_,  0, 0x100 * sizeof(foeSlots_[0]));
    breakableCount_ = 0;
    foeCount_       = 0;
    liftCount_      = 0;
    slideCount_     = 0;
    bombCount_      = 0;
    bridgeCount_    = 0;
    Sim_ResetLevelObjectCounters(this);
    field_42252_      = 0;
    mapChanged_       = 1;
    initialised_      = 0;
    levelSoundsReady_ = 0;
    scriptPlayer_.clearStreams();
    memset(foeIds_,  0, sizeof(foeIds_));
    memset(bombIds_, 0, sizeof(bombIds_));
    field_48b14_ = 0;
    fixedSounds_.switchClick    = NULL;
    fixedSounds_.menuUpDown     = NULL;
    fixedSounds_.count          = NULL;
    fixedSounds_.lastSeconds    = NULL;
    fixedSounds_.timeOut        = NULL;
    fixedSounds_.crystalBank[0] = NULL;
    fixedSounds_.crystalBank[1] = NULL;
    fixedSounds_.crystalBank[2] = NULL;
    strcpy(menuLevelName_, GS_GAME_DEMO_LEVEL);
    field_13cc8c_ = 0;
    field_13cc88_ = 0;

    // PRESERVED: the game name is used as sprintf's format.
    sprintf(gameFileName_, gameName);
    cdThemes_.readTrackThemeTable(gameFileName_);
    cdThemes_.listTrackLengths();
    if (cdThemes_.validateTrackLengths())
        g_logger.logMessage(2, "GAME: original CD is in the drive - OK");
    else
        g_logger.logMessage(3, "GAME: * warning * - the original CD is not in drive");
    g_logger.logMessage(1, "GAME: game-object created, game name:%s", gameFileName_);

    if (!loadGameFile(gameFileName_)) {
        g_logger.logMessage(4, "GAME: ** error ** game-file named as %s.gam could not be loaded - aborting!!!", gameFileName_);
        windev::quit(1);
        return;
    }

    debounce_ = 0x0d;
    menu_.setLastKey(0x0d);
    saveSlots_.setCount(6);
    if (!saveSlots_.loadAllSlotFiles(gameFileName_, SAVE_KEY)) {
        g_logger.logMessage(3, "GAME: warning - no save-files for this game (maybe started a new game the first time\077), creating %d empty slots", (unsigned)saveSlots_.count());
        saveSlots_.initialiseEmpty();
        saveSlots_.writeAllSlotFiles(gameFileName_, SAVE_KEY);
    }
    field_173584_ = 0;
    if (!config_.loadValues(GS_CFG_FILE)) {
        g_logger.logMessage(3, "GAME: warning - no correct config-values (or no *.cfg-file), creating default-config");
        config_.fillDefaults();
    } else {
        g_logger.logMessage(1, "GAME: config-values loaded");
    }
    g_progCtrl.setJoyDeadzone(0, config_.joyDeadzone() * 100);
    g_progCtrl.setJoyDeadzone(4, config_.joyDeadzone() * 100);

    highScores_.setCount(10);
    if (!highScores_.readFile(gameFileName_, HIGHSCORE_KEY)) {
        g_logger.logMessage(3, "GAME: warning - no highscore-file for the game found! Creating a new one...");
        highScores_.fillDefaults();
        highScores_.writeFile(gameFileName_, HIGHSCORE_KEY);
    } else {
        g_logger.logMessage(1, "GAME: highscore-file loaded");
    }

    // PRESERVED: a demo check, for a nonzero field_0c_ with more than ten
    // levels.  field_0c_ was zeroed above, so it never fires.
    if (field_0c_ != 0 && levelCount_ > 10) {
        g_logger.logMessage(4, "GAME: ** error ** this is a DEMO, piracy alert, aborting!!!");
        windev::quit(1);
        return;
    }
    g_logger.logMessage(2, "GAME: this is a commercial version");
    config_.setSavedWaveOutVolume(audiodev::masterVolume());
    audiodev::setMasterVolume(config_.waveOutVolume());
    config_.setSavedCdMixerVolume(g_cdAudio.getMixerDetails());
    g_cdAudio.setMixerVolume(config_.cdMixerVolume());
    state_ = 0;
    g_cdAudio.stop();
    menu_.buildDefaultGraph(saveSlots_.count());
    levelIndex_     = 0;
    zoomDistance_   = config_.cameraDistanceSetting();
    cameraDistance_ = config_.cameraDistanceSetting();
    Sim_ClearGameState(this);
    Sim_ParseLevelFiles(this, menuLevelName_);
    Sim_SetupLevelObjects(this);
    scriptPlayer_.setRunning(scriptPlayer_.loaded() != 0 ? 1 : 0);

    clock_            = 1000.0;
    field_173b1a_     = 0;
    player()->setGliding(0);
    textEntryActive_  = 0;
    cameraMode_       = 1;
    overviewActive_   = 0;
    field_13cc84_     = 0;
    bridgeCount_      = 0;
    fixedSounds_.loaded = 0;
    memset(cheatBuffer_, 0, 0x100);
    cheatEntry_.setMaxLength(30);
    cheatEntry_.setCursor(0);
    cheatEntry_.setBuffer((char *)cheatBuffer_);
    cheatEntry_.setActive(1);
    cheatEntry_.setLastKey(0x0d);
    parkedCameraOption_ = 0;
    initialised_        = 1;
}

Game::~Game()
{
    g_logger.logMessage(1, "GAME: starting destructor");
    LiftObject::purgeAll(this);
    SlideObject::purgeAll(this);
    BreakableTile::purgeAll(this);
    BridgeObject::purgeAll(this);
    while (foeCount_ != 0)
        Foe::remove(this, foeIds_[0]);
    while (bombCount_ != 0)
        Bomb::remove(this, bombIds_[0]);
    breakableCount_ = 0;
    foeCount_       = 0;
    liftCount_      = 0;
    slideCount_     = 0;
    bombCount_      = 0;
    switchMax_      = 0;
    config_.setCameraDistanceSetting(zoomDistance_);
    if (highScores_.writeFile(gameFileName_, HIGHSCORE_KEY))
        g_logger.logMessage(1, "GAME: highscore-files saved");
    if (config_.save(GS_CFG_FILE))
        g_logger.logMessage(1, "GAME: config-values saved correctly");
    else
        g_logger.logMessage(3, "GAME: ** error ** while saving config-values (maybe write-protected or hd full\077) !!!");
    scriptPlayer_.releaseStreams();
    extraObjects_.releaseSounds();
    releaseAllSounds();
    g_logger.logMessage(1, "GAME: all sounds released successfully");
    soundManager()->purgeAssets();
    audiodev::setMasterVolume(config_.savedWaveOutVolume());
    g_cdAudio.setMixerVolume(config_.savedCdMixerVolume());

}

/* "<gamedir>\<name>.gam", opened in text mode ("r") even for the binary form.
 * FORMAT: binary is a 6,6,6,<levels> header, then every byte minus 5 into the
 * flat level-name table.  Text is one name per line, up to a line starting
 * '*', which is stored and counted, then uncounted.  PRESERVED: the EOF test
 * comes before the read, so the last, failing fread decodes the byte it left
 * behind a second time. */
int Game::loadGameFile(const char *name)
{
    char path[0x80] = { 0 };
    char line[0x100];
    unsigned char hdr[4];

    levelCount_ = 0;
    sprintf(path, GS_GAME_FILE_PATH, g_gameDir, name);
    g_logger.logMessage(2, "GAME: load game-file: %s", path);
    FILE *fp = fopen(path, GS_MODE_READ);
    if (fp == NULL)
        return 0;
    fread(hdr, 4, 1, fp);

    char *table = &levelNameTable_[0][0];
    if (hdr[0] == 6 && hdr[1] == 6 && hdr[2] == 6) {
        g_logger.logMessage(2, "GAME: load game-file as binary,coded file");
        levelCount_ = hdr[3];
        unsigned int n = 0;
        char c = 0;
        while (!feof(fp)) {
            fread(&c, 1, 1, fp);
            c = (char)(c - 5);
            table[n & 0xffff] = c;
            n++;
        }
        g_logger.logMessage(2, "GAME: game-file loaded %d levels included %d bytes loaded",
                           (unsigned)levelCount_, n & 0xffff);
        for (unsigned short i = 0; i < levelCount_; i++)
            g_logger.logMessage(2, "GAME: game-file: %s", levelNameTable_[i]);
        fclose(fp);
        return 1;
    }

    g_logger.logMessage(2, "GAME: load game-file as text-file");
    unsigned int n = 0;
    fseek(fp, 0, SEEK_SET);
    line[0] = '\0';
    while (!feof(fp) && line[0] != '*') {
        line[0] = '\0';  // empty, in case fgets reads nothing
        fgets(line, 0x100, fp);
        // Chop the last character, newline or not.  An empty line at EOF would
        // chop the byte before the buffer; that write is skipped.
        size_t len = strlen(line);
        if (len > 0)
            line[len - 1] = '\0';
        strcpy(levelNameTable_[n & 0xffff], line);
        n++;
    }
    levelCount_ = (unsigned char)(n - 1);
    g_logger.logMessage(2, "GAME: game-file loaded %d levels included", (unsigned)levelCount_);
    fclose(fp);
    return 1;
}

/* Releases only while sound is up; soundsLoaded is cleared either way. */
void Game::releaseAllSounds()
{
    SoundManager *sm = soundManager();
    if (soundCreated() != 0) {
        Player *p = player();
        audiodev::Buffer *statics1[] = {
            fixedSounds_.timeOut, fixedSounds_.lastSeconds,
            fixedSounds_.count,   fixedSounds_.switchClick,
        };
        for (audiodev::Buffer *s : statics1)
            if (s) sm->releaseStaticForOwner(s, 1);
        if (fixedSounds_.menuUpDown)
            sm->releasePooledForOwner(fixedSounds_.menuUpDown, 1);
        if (p->soundC3()) sm->releaseStaticForOwner(p->soundC3(), 1);
        if (p->soundBf()) sm->releaseStaticForOwner(p->soundBf(), 1);
        if (p->poolCf())  sm->releasePooledForOwner(p->poolCf(), 1);
        if (fixedSounds_.levelCompleted)
            sm->releaseStaticForOwner(fixedSounds_.levelCompleted, 1);
        audiodev::Buffer *statics2[] = {
            p->soundC7(), p->soundA3(), p->soundB3(), p->soundB7(),
            p->soundBb(), p->soundAb(), p->soundAf(), p->soundCb(), p->soundA7(),
        };
        for (audiodev::Buffer *s : statics2)
            if (s) sm->releaseStaticForOwner(s, 1);
        if (p->pool9f())
            sm->releasePooledForOwner(p->pool9f(), 1);
        // The crystal banks and the pickup banks, one entry of each per pass.
        // PRESERVED: bank 5 is never released.
        static const int banks[] = { 0, 1, 2, 3, 4, 6, 7, 8 };
        for (int i = 0; i < 3; i++) {
            if (fixedSounds_.crystalBank[i])
                sm->releaseStaticForOwner(fixedSounds_.crystalBank[i], 1);
            for (int b : banks)
                if (p->pickupSound(b, i))
                    sm->releaseStaticForOwner(p->pickupSound(b, i), 1);
        }
    }
    fixedSounds_.loaded = 0;
}

