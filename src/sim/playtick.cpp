/* The play's per-tick simulation, called from Core_GameTick (core/gametick.cpp):
 * the level sounds and the reaction to the level's end.  The rules are
 * World's (entities/worldrules.cpp).
 *
 * PRESERVED:
 *   - the game-over branch of the restart skips the camera reset;
 *   - leaving game over with ENTER adds the level time to the total play time;
 *     ENTER after high-score name entry does not.
 */

#include "playtick.h"
#include "logger.h"
#include "game.h"
#include "levelsounds.h"
#include "fixedsounds.h"
#include "levelscore.h"

/* Acquires the fixed sound buffers, and loads the level sounds on the first tick they are ready. */
void Play_LoadSounds(Game *self)
{
    Sim_AcquireFixedSoundBuffersAndMaybeReport(self);
    if (self->audio()->levelSoundsReady() == 0 && self->levelPrepPending() == 0) {
        Sim_InitLevelBasedSounds(self);
        self->audio()->setLevelSoundsReady(1);
        if (self->state() == GameState::MainMenu && self->config()->musicOn() != 0)
            self->audio()->cdThemes()->replay();
    }
}

/* Opens the level-complete tally menu. */
void Play_OpenTallyMenu(Game *self)
{
    self->frontEnd()->menu()->rewind();
    self->frontEnd()->menu()->pop();
    self->frontEnd()->menu()->push(0x28);
    self->frontEnd()->menu()->setNode(0x28);
    self->frontEnd()->menu()->setLockStart(self->lastTickTime());
    self->frontEnd()->menu()->setLock(1);
    self->frontEnd()->menu()->setCursor(0);
}

/* Playing: the camera follows the player, then the World's rules for the
 * level's end; a completed level moves on to the next, or ends the game. */
void Play_CheckLevelEnd(Game *self)
{
    const Player *pl = self->world()->player();

    if ((unsigned int)pl->falling() == 0 && pl->moveState() == 0) {
        self->camera()->setFocus(0, pl->posU());
        self->camera()->setFocus(1, pl->posY());
        self->camera()->setFocus(2, pl->posV());
        self->camera()->setMode(0);
    }
    self->world()->updateLevelEnd(*self->soundEvents());
    if (!self->world()->completed())
        return;

    if ((unsigned int)self->session()->levelIndex() + 1 == (unsigned int)self->session()->levelCount()) {
        self->setState(GameState::GameOver);
        if (self->config()->musicOn() != 0)
            self->audio()->cdThemes()->play("gameover");
        Score_CalculateLevelScore(self, 3);
        g_logger.logMessage(1, "GAME: completed at level %d/%d",
                           (unsigned int)self->session()->levelIndex() + 1,
                           (unsigned int)self->session()->levelCount());
    } else {
        self->setState(GameState::LevelComplete);
        self->camera()->setMode(2);
        Play_OpenTallyMenu(self);
        Score_CalculateLevelScore(self, (char)static_cast<unsigned char>(self->state()));
        self->session()->setRestartCount(0);
    }
    self->session()->setTotalPlayTime((double)((double)(unsigned long long)self->world()->timeElapsed() +
                            (double)self->session()->totalPlayTime()));
}
