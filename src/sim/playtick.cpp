/* The play's per-tick simulation, called from Core_GameTick (core/gametick.cpp):
 * the level sounds, the timed foe spawns, the player's bomb drop and switch,
 * the foes, and the level end.
 *
 * PRESERVED:
 *   - a bomb drop or foe bomb refused as too soon also leaves its request
 *     flag set;
 *   - the game-over branch of the restart skips the camera reset;
 *   - leaving game over with ENTER adds the level time to the total play time;
 *     ENTER after high-score name entry does not.
 */

#include "playtick.h"
#include "foetype.h"
#include "logger.h"
#include "game.h"
#include "prof.h"
#include "levelsounds.h"
#include "fixedsounds.h"
#include "levelscore.h"
#include "soundobj.h"
#include "tilequery.h"
#include "bridgeobject.h"
#include "foe.h"

/* The switch-triggered block, shared by the player and each foe. */

static void trigger_switch_tile(BridgeObject *br, Tile *t)
{
    t->setBusy(br->phase() == 0 ? 1 : 0);
}

/* The bridge a switch slot operates, if it is one (0xff is none) and the
 * bridge is at rest. */
static BridgeObject *idle_bridge(World *w, unsigned char sw)
{
    if (sw == 0xff)
        return nullptr;
    BridgeObject *br = w->bridge(sw);
    return (br != nullptr && br->armed() == 0) ? br : nullptr;
}

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

/* Spawns a foe from each timed spawner whose interval has elapsed. */
void Play_SpawnTimedFoes(Game *self)
{
    // The timed foe spawners (levelcensus.h).
    for (int i = 0; i < (int)self->world()->census()->timed; ++i) {
        TimedSpawner *E = self->world()->timedSpawner((unsigned)i);
        double since = (double)self->world()->now() - (double)E->lastSpawn;
        if (!(since > (double)E->interval))
            continue;
        signed char u = (signed char)E->u, v = (signed char)E->v;
        if (self->world()->map()->tile(u, v)->occupant() == 0 && self->world()->foes().size() < E->maxFoes) {
            Foe &foe = Foe::spawn(self->world()->entityContext(), self->world()->foes(), (unsigned char)u,
                                  (unsigned char)v, E->height, 2,
                                  (unsigned char)(E->field_0b + FOE_FACING_BASE));
            if (((int)self->session()->foesKilled() + 1) % 15 == 0)
                foe.setDropContents(7);
            else
                foe.setDropContents(1);
            Sim_AcquireFoeSounds(self, foe);
        }
        // The last-spawn time, from the clock.
        E->lastSpawn = self->world()->now();
    }
}

/* The player's bomb drop. */
void Play_DropPlayerBomb(Game *self)
{
    Player *pl = self->world()->player();

    if ((unsigned int)pl->bombDropRequest() != 0) {
        unsigned int timed = (unsigned int)pl->moveDir();
        int spawn = 1, offset = 0;
        if (timed != 0) {
            double since = (double)self->world()->now() - (double)pl->animStart();
            if (since < 50.0)
                offset = 1;
            else
                spawn = 0;  // too soon: the flag stays set
        }
        if (spawn) {
            if (offset)
                Bomb::spawn(self->world()->entityContext(), self->world()->bombs(), self->bombSounds(), (unsigned char)((unsigned char)pl->cellU() - (unsigned char)pl->stepU()),
                                    (unsigned char)((unsigned char)pl->cellV() - (unsigned char)pl->stepV()),
                                    (unsigned char)((unsigned char)pl->heightCell() - (unsigned char)pl->field141()),
                                    pl->facing());
            else
                Bomb::spawn(self->world()->entityContext(), self->world()->bombs(), self->bombSounds(), (unsigned char)pl->cellU(), (unsigned char)pl->cellV(), (unsigned char)pl->heightCell(),
                                    pl->facing());
            pl->setBombDropRequest(0);
        }
    }
}

/* A switch the player stepped on. */
void Play_PlayerSwitch(Game *self)
{
    Player *pl = self->world()->player();

    unsigned char sw = pl->switchSlot();
    if (BridgeObject *br = idle_bridge(self->world(), sw)) {
        g_logger.logMessage(1, "GAME: switch triggered %d", (unsigned int)sw);
        trigger_switch_tile(br, self->world()->map()->tile(pl->cellU(), pl->cellV()));
        br->arm(self->world()->now());
        br->playArmSound();
        Sim_MarkListedTilesBlockedByObject(self->world()->switchCells(), self->world()->map(), br->phase(), sw);
        pl->setSwitchSlot(0xff);
    }
}

/* The foes.  Their pieces are Foe methods (foe.cpp). */
void Play_TickFoes(Game *self)
{
    Player *pl = self->world()->player();
    EntityList<Foe> &foes = self->world()->foes();

    if (foes.empty())
        return;
    PROF_SCOPE("foes");
    for (std::size_t i = 0; i < foes.size();) {
        Foe &foe = foes[i];
        unsigned char tu, tv;

        unsigned char sw = foe.switchSlot();
        if (BridgeObject *br = idle_bridge(self->world(), sw)) {
            g_logger.logMessage(1, "GAME: switch triggered %d", (unsigned int)sw);
            trigger_switch_tile(br, self->world()->map()->tile(foe.cellU(), foe.cellV()));
            br->arm(self->world()->now());
            Sim_MarkListedTilesBlockedByObject(self->world()->switchCells(), self->world()->map(), br->phase(), sw);
            foe.clearSwitchSlot();
        }

        int hold = ((unsigned int)pl->freezeActive() == 0 && self->state() == GameState::Playing) ? 0 : 1;
        if (self->state() == GameState::LevelComplete)
            hold = 1;
        if (pl->moveState() != 0)
            hold = 1;
        {
            PROF_SCOPE("choose target");
            foe.chooseTarget(self->world()->entityContext(), foes, hold, (unsigned char)pl->cellU(),
                             (unsigned char)pl->cellV(), pl->markerCellU(), pl->markerCellV(), &tu, &tv);
        }
        {
            PROF_SCOPE("step");
            foe.step(tu, tv);
        }
        foe.debugDraw((unsigned)i);
        foe.dropBomb(self->world()->entityContext(), self->world()->bombs(), self->bombSounds());
        foe.checkPlayerContact(pl->moveStateRef(), pl->posU(), pl->posY(), pl->posV());
        if (foe.finishDespawn(self->world()->map())) {
            Foe::remove(self->world()->entityContext(), foes, i);  // the next moves into i
            self->session()->setFoesKilled((unsigned char)(self->session()->foesKilled() + 1));
        } else {
            ++i;
        }
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

/* Playing: camera follow, time-out, exit. */
void Play_CheckLevelEnd(Game *self)
{
    Player *pl = self->world()->player();

    if ((unsigned int)pl->falling() == 0 && pl->moveState() == 0) {
        self->camera()->setFocus(0, pl->posU());
        self->camera()->setFocus(1, pl->posY());
        self->camera()->setFocus(2, pl->posV());
        self->camera()->setMode(0);
    }
    if (pl->moveState() != 3) {
        int t = self->world()->timeLimit() * 1000;
        if (t - (int)self->world()->timeElapsed() <= 0) {
            void *snd = self->audio()->fixedSounds()->timeOut;
            self->world()->setTimeElapsed((unsigned int)t);
            pl->setMoveState(3);
            if (snd != NULL)
                ((audiodev::Buffer *)snd)->play(false);
        }
    }
    if (pl->gemsCollected() >= self->world()->gemsRequired()) {
        if (self->gemsGoalReached() == 0 && self->state() != GameState::LevelComplete) {
            int r = (int)(long long)(self->world()->now());
            void *snd = self->audio()->fixedSounds()->crystalBank[r % 3];
            if (snd != NULL)
                ((audiodev::Buffer *)snd)->play(false);
            self->setGemsGoalReached(1);
        }
        self->world()->map()->tile((signed char)pl->markerCellU(), (signed char)pl->markerCellV())->setBusy(1);
        if ((unsigned char)pl->cellU() == pl->markerCellU() && (unsigned char)pl->cellV() == pl->markerCellV() &&
            (unsigned char)pl->heightCell() == pl->markerCellH() && (unsigned int)pl->falling() == 0 &&
            pl->moveState() == 0) {
            pl->setHeld(1);
            if ((unsigned int)pl->moveDir() == 0) {
                if (self->audio()->fixedSounds()->levelCompleted != NULL)
                    (self->audio()->fixedSounds()->levelCompleted)->play(false);
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
        }
    }
}
