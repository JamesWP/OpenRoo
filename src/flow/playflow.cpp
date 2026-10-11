/* The player's input around the play: the flythrough, the pause menu, the
 * controls while playing, and the ENTER that restarts, leaves the game-over
 * tally or ends name entry (Core_GameTick calls them).
 *
 * PRESERVED:
 *   - lives drop at the restart after a death (ENTER), not at the death;
 *   - the game-over branch of the restart skips the camera reset;
 *   - leaving game over with ENTER adds the level time to the total play time;
 *     ENTER after high-score name entry does not.
 */

#include "playflow.h"
#include "playtick.h"
#include <math.h>
#include <stdio.h>
#include "logger.h"
#include "game.h"
#include "prof.h"
#include "cdm.h"
#include "gamestate.h"
#include "progctrl.h"
#include "levelparse.h"
#include "levelsetup.h"
#include "levelscore.h"
#include "gamereset.h"
#include "gridrestore.h"
#include "checkpoint.h"
#include "cheatcode.h"
#include "keys.h"
#include "gamestr.h"


/* floor() of a double. */
static double crt_floor(double v)
{
    return floorl((long double)v);
}

/* The flythrough: its checkpoint is restored, and ENTER ends it and returns to play. */
void Flow_TickFlythrough(Game *self)
{
    Player *pl = self->world()->player();
    ScriptPlayer *sp = self->scriptPlayer();
    if (sp->loaded() != 0)
        Sim_RestoreCheckpointStateBlocks(self);
    if (sp->running() == 0 || sp->loaded() == 0)
        self->camera()->setMode(2);
    if (input::pressed(input::KEY_RETURN)) {
        sp->setRunning(0);
        sp->setSplineActive(0);
        sp->releaseStreams();
        sp->releaseStreams();
        self->setState(GameState::Playing);
        if (self->session()->restartCount() == 0) {
            if (self->config()->musicOn() != 0)
                self->audio()->cdThemes()->play(self->world()->map()->mapName());
            self->audio()->cdThemes()->setCurrentTrack((unsigned char)self->audio()->cdThemes()->findThemeIndex(self->world()->map()->mapName()));
        }
        self->camera()->setDistance(7.0f);
        self->camera()->setMode(0);
        pl->setField231(*self->world()->clock());
    }
}

/* ESC from play or the flythrough opens the pause menu. */
void Flow_OpenPauseMenu(Game *self)
{
    self->setStateBeforeMenu(self->state());
    self->frontEnd()->menu()->rewind();
    self->setState(GameState::PauseMenu);
    self->frontEnd()->menu()->setLockStart(self->lastTickTime());
    self->frontEnd()->menu()->setLock(1);
    input::swallow(input::KEY_ESCAPE);
}

/* The controls while playing: cheat entry, the timed spawners, the camera pitch, the
 * countdown's last seconds, and the progctrl dispatch once the player is at rest. */
void Flow_TickPlayingControls(Game *self, ProgableControl *progCtrl)
{
    Player *pl = self->world()->player();

    if (self->frontEnd()->cheatEntry()->active() != 0)
        Flow_HandleTypedCheatCode(self);

    Play_SpawnTimedFoes(self);

    self->config()->setActiveCameraPitch(self->config()->cameraPitch());
    if (pl->moveState() != 0) {
        self->camera()->setOverviewActive(0);
        self->camera()->setDistance(self->camera()->zoomDistance());
        if (pl->soundAf() != NULL) pl->soundAf()->stop();
        if (pl->soundAb() != NULL) pl->soundAb()->stop();
        if (pl->soundC7() != NULL) pl->soundC7()->stop();
    }

    if ((unsigned int)pl->gliding() != 0) {
        if (self->camera()->parkedCameraOption() == 0) {
            self->camera()->setParkedCameraOption((unsigned char)(self->config()->cameraTurnsWithPlayer() + 10));
            self->config()->setCameraTurnsWithPlayer(1);
        }
    } else if (self->camera()->parkedCameraOption() >= 10) {
        self->config()->setCameraTurnsWithPlayer((unsigned char)(self->camera()->parkedCameraOption() - 10));
        self->camera()->setParkedCameraOption(0);
    }

    // The countdown's last seconds: compared at 80 bits, stored at 64.
    {
        static const double k001 = 0.001;  // a double
        long double lim = (long double)(unsigned long long)(unsigned int)self->world()->timeLimit() * 1000.0L;
        long double rem80 = (lim - (long double)(unsigned long long)self->world()->timeElapsed()) *
                            (long double)k001;
        double rem64 = (double)rem80;
        if (rem80 > 11.0L || pl->moveState() == 3) {
            pl->setLastSecondsMark(10.0);
        } else if ((long double)pl->lastSecondsMark() > (long double)rem64) {
            if (self->audio()->fixedSounds()->lastSeconds != NULL)
                (self->audio()->fixedSounds()->lastSeconds)->play(false);
            pl->setLastSecondsMark(crt_floor(rem64));
        }
    }
    if (pl->moveState() == 0) {
        PROF_SCOPE("player");
        // Noted before the dispatch, so paused and cutscene modes are seen.
        gamestate_note_mode(static_cast<unsigned short>(static_cast<unsigned char>(self->state())));
        progCtrl->dispatch(static_cast<unsigned short>(static_cast<unsigned char>(self->state())));
    }
}

/* The controls outside play: the progctrl dispatch runs in mode 0. */
void Flow_TickIdleControls(Game *self, ProgableControl *progCtrl)
{
    Player *pl = self->world()->player();

    pl->setIdleStarted(0);
    pl->setLastActive(*self->world()->clock());
    gamestate_note_mode(0);
    progCtrl->dispatch(0);
    if (self->audio()->fixedSounds()->lastSeconds != NULL)
        (self->audio()->fixedSounds()->lastSeconds)->stop();
}

/* ENTER after a death: restarts the level, or ends it as game over or, on a bonus level, complete. */
void Flow_RestartAfterDeath(Game *self)
{
    Player *pl = self->world()->player();

    self->session()->setRestartCount((unsigned char)(self->session()->restartCount() + 1));
    int lives = pl->lives();
    int bonus = (int)self->world()->map()->bonus();
    int restart_tail = 1;
    if (lives > 0 && bonus == 0) {
        pl->setLives(lives - 1);  // lives drop at the restart
        Sim_RestoreTileGridFromSnapshot(self->world()->map());
        Sim_SetupLevelObjects(self);
    } else if (lives <= 0 && bonus == 0) {
        self->setState(GameState::GameOver);
        if (self->config()->musicOn() != 0)
            self->audio()->cdThemes()->play(GS_GAME_GAMEOVER);
        Score_CalculateLevelScore(self, (char)static_cast<unsigned char>(self->state()));
        input::swallow(input::KEY_RETURN);
        restart_tail = 0;  // game over: skip the camera reset
    } else {
        self->camera()->setMode(2);
        self->setState(GameState::LevelComplete);
        if (self->config()->musicOn() != 0)
            self->audio()->cdThemes()->play(GS_GAME_COMPLETED);
        Play_OpenTallyMenu(self);
        self->world()->setTimeElapsed((unsigned int)(self->world()->timeLimit() * 1000));
        Score_CalculateLevelScore(self, (char)static_cast<unsigned char>(self->state()));
        self->session()->setRestartCount(0);
        self->session()->setTotalPlayTime((double)((long double)(unsigned long long)self->world()->timeElapsed() +
                                (long double)self->session()->totalPlayTime()));
        g_logger.logMessage(2, "GAME: game completed %d %d",
                           (unsigned int)self->session()->levelIndex(), (unsigned int)self->session()->levelCount());
        if ((unsigned int)self->session()->levelIndex() == (unsigned int)self->session()->levelCount() - 1) {
            self->setState(GameState::GameOver);
            if (self->config()->musicOn() != 0)
                self->audio()->cdThemes()->play(GS_GAME_GAMEOVER);
        }
        self->session()->setRestartCount(0);
    }
    if (restart_tail) {
        self->camera()->setDistance(7.0f);
        self->camera()->setMode(0);
        input::swallow(input::KEY_RETURN);
    }
}

/* Back to the menu after a level: the final-level theme and quit path, or the next level's setup. */
static void return_to_menu(Game *self, bool lastLevel)
{
    self->setState(GameState::MainMenu);
    self->frontEnd()->menu()->rewind();
    const char *theme = NULL;
    if (lastLevel) {
        char name[256];
        sprintf(name, GS_GAME_FINAL_DIR, self->gameFileName());
        Sim_ParseLevelFiles(self, name);
        self->frontEnd()->menu()->push(0);
        self->frontEnd()->menu()->setNode(5);
        theme = GS_GAME_FINAL;
    } else {
        Sim_ClearGameState(self);
        Sim_ParseLevelFiles(self, self->menuLevelName());
        theme = GS_GAME_MAIN;
    }
    if (theme != NULL)
        self->audio()->cdThemes()->setCurrentTrack((unsigned char)self->audio()->cdThemes()->findThemeIndex(theme));
    Sim_SetupLevelObjects(self);
    self->scriptPlayer()->setRunning(1);
    input::swallow(input::KEY_RETURN);
}

/* ENTER on the game-over tally: enters a high score, or returns to the menu. */
void Flow_LeaveGameOver(Game *self)
{
    Player *pl = self->world()->player();

    unsigned int r = self->frontEnd()->highScores()->insert(
        (unsigned int)pl->score(), (unsigned char)(self->session()->levelIndex() + 1));
    if ((unsigned char)r < 0xff) {
        self->setState(GameState::NameEntry);
        if (self->config()->musicOn() != 0)
            self->audio()->cdAudio()->stop();
        self->frontEnd()->nameEntry()->setMaxLength(0x0f);
        self->frontEnd()->nameEntry()->setActive(1);
        self->frontEnd()->nameEntry()->setCursor(0);
        input::swallow(input::KEY_RETURN);
        self->frontEnd()->nameEntry()->setBuffer(
            self->frontEnd()->highScores()->record(self->frontEnd()->highScores()->lastRank())->name);
    } else {
        bool lastLevel = (unsigned int)self->session()->levelIndex() + 1 == (unsigned int)self->session()->levelCount() &&
                         pl->moveState() == 0;
        return_to_menu(self, lastLevel);
        self->session()->setTotalPlayTime((double)((long double)(unsigned long long)self->world()->timeElapsed() +
                                (long double)self->session()->totalPlayTime()));
    }
}

/* ENTER after high-score name entry: saves the table and returns to the menu. */
void Flow_LeaveNameEntry(Game *self)
{
    self->frontEnd()->highScores()->writeFile(self->gameDir(), GS_GAME_HSFILE, 0x4b);
    bool lastLevel = (unsigned int)self->session()->levelIndex() + 1 == (unsigned int)self->session()->levelCount();
    return_to_menu(self, lastLevel);
}
