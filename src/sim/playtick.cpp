#include "playtick.h"
#include "crtmath.h"
#include "inputdev.h"
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
#include "gamestr.h"

/* The switch-triggered block, shared by the player and each foe. */

static void trigger_switch_tile(Game *game, unsigned char sw, int u, int v)
{
    BridgeObject *br = game->world()->bridgeSlot(sw);
    Tile *t = game->world()->map()->tile(u, v);
    if (br->phase() == 0)
        t->setBusy(1);
    else
        t->setBusy(0);
}

/* Acquires the fixed sound buffers, and loads the level sounds on the first tick they are ready. */
void Play_LoadSounds(Game *self)
{
    Sim_AcquireFixedSoundBuffersAndMaybeReport(self);
    if (self->levelSoundsReady() == 0 && self->field_173584() == 0) {
        Sim_InitLevelBasedSounds(self);
        self->setLevelSoundsReady(1);
        if (self->state() == GameState::MainMenu && self->musicOn() != 0)
            self->cdThemes()->replay();
    }
}

/* Spawns a foe from each timed spawner whose interval has elapsed. */
void Play_SpawnTimedFoes(Game *self)
{
    // The timed foe spawners (levelcensus.h).
    for (int i = 0; i < (int)self->world()->census()->timed; ++i) {
        TimedSpawner *E = self->world()->timedSpawner((unsigned)i);
        long double since = (long double)(*self->world()->clock()) - (long double)E->lastSpawn;
        if (!(since > (long double)E->interval))
            continue;
        signed char u = (signed char)E->u, v = (signed char)E->v;
        if (self->world()->map()->tile(u, v)->occupant() == 0 &&self->world()->foeCount() < E->maxFoes) {
            unsigned char id = Foe::spawn(self->world()->entityContext(), self->world()->foes(), (unsigned char)u,
                                          (unsigned char)v, E->height, 2,
                                          (unsigned char)(E->field_0b + 100));
            Foe *foe = self->world()->foeSlot(id);
            if (((int)self->session()->foesKilled() + 1) % 15 == 0)
                foe->setDropContents(7);
            else
                foe->setDropContents(1);
            Sim_AcquireObjectSoundBuffersForIndex(self, id);
        }
        // The last-spawn time, from the clock.
        E->lastSpawn = *self->world()->clock();
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
            long double since = (long double)(*self->world()->clock()) - (long double)pl->animStart();
            if (since < 50.0L)
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
    if (sw < 0xff && self->world()->bridgeSlot(sw)->armed() == 0) {
        g_logger.logMessage(1, "GAME: switch triggered %d", (unsigned int)sw);
        trigger_switch_tile(self, pl->switchSlot(), pl->cellU(), pl->cellV());
        BridgeObject *br = self->world()->bridgeSlot(pl->switchSlot());
        br->arm(self->world()->clock());
        br->playArmSound();
        Sim_MarkListedTilesBlockedByObject(self->world()->switchCells(), self->world()->map(),
                                               self->world()->bridgeSlot(pl->switchSlot() & 0xff)->phase(),
                                               pl->switchSlot());
        pl->setSwitchSlot(0xff);
    }
}

/* The foes.  Their pieces are Foe methods (foe.cpp); the slot is re-read
 * for each piece. */
void Play_TickFoes(Game *self)
{
    Player *pl = self->world()->player();

    if (self->world()->foeCount() != 0) {
        PROF_SCOPE("foes");
        for (int i = 0; i < (int)self->world()->foeCount(); ++i) {
            unsigned char id = self->world()->foeId(i);
            Foe **slot = self->world()->foeSlotRef(id);
            unsigned char tu, tv;

            {
                unsigned char sw = (*slot)->switchSlot();
                if (sw < 0xff && self->world()->bridgeSlot(sw)->armed() == 0) {
                    g_logger.logMessage(1, "GAME: switch triggered %d", (unsigned int)sw);
                    trigger_switch_tile(self, (*slot)->switchSlot(), (*slot)->cellU(), (*slot)->cellV());
                    self->world()->bridgeSlot((*slot)->switchSlot())->arm(self->world()->clock());
                    Sim_MarkListedTilesBlockedByObject(self->world()->switchCells(), self->world()->map(),
                                                           self->world()->bridgeSlot((*slot)->switchSlot() & 0xff)->phase(),
                                                           (*slot)->switchSlot());
                    (*slot)->clearSwitchSlot();
                }
            }

            int hold = ((unsigned int)pl->freezeActive() == 0 && self->state() == GameState::Playing) ? 0 : 1;
            if (self->state() == GameState::LevelComplete)
                hold = 1;
            if (pl->moveState() != 0)
                hold = 1;
            {
                PROF_SCOPE("choose target");
                (*slot)->chooseTarget(self->world()->entityContext(), self->world()->foes(), hold, (unsigned char)pl->cellU(), (unsigned char)pl->cellV(),
                                      pl->markerCellU(), pl->markerCellV(), &tu, &tv);
            }
            {
                PROF_SCOPE("step");
                (*slot)->step(tu, tv);
            }
            (*slot)->debugDraw(id);
            (*slot)->dropBomb(self->world()->entityContext(), self->world()->bombs(), self->bombSounds());
            (*slot)->checkPlayerContact(pl->moveStateRef(),
                                        pl->posU(), pl->posY(), pl->posV());
            if ((*slot)->finishDespawn(self->world()->map())) {
                Foe::remove(self->world()->entityContext(), self->world()->foes(), id);
                self->session()->setFoesKilled((unsigned char)(self->session()->foesKilled() + 1));
            }
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
        self->camera()->setCameraEye(0, pl->posU());
        self->camera()->setCameraEye(1, pl->posY());
        self->camera()->setCameraEye(2, pl->posV());
        self->camera()->setCameraMode(0);
    }
    if (pl->moveState() != 3) {
        int t = self->world()->timeLimit() * 1000;
        if (t - (int)self->world()->timeElapsed() <= 0) {
            void *snd = self->fixedSounds()->timeOut;
            self->world()->setTimeElapsed((unsigned int)t);
            pl->setMoveState(3);
            if (snd != NULL)
                ((audiodev::Buffer *)snd)->play(false);
        }
    }
    if (pl->gemsCollected() >= self->world()->gemsRequired()) {
        if (self->field_173b1a() == 0 && self->state() != GameState::LevelComplete) {
            int r = (int)ftol80((*self->world()->clock()));
            void *snd = self->fixedSounds()->crystalBank[r % 3];
            if (snd != NULL)
                ((audiodev::Buffer *)snd)->play(false);
            self->setField173b1a(1);
        }
        self->world()->map()->tile((signed char)pl->markerCellU(), (signed char)pl->markerCellV())->setBusy(1);
        if ((unsigned char)pl->cellU() == pl->markerCellU() && (unsigned char)pl->cellV() == pl->markerCellV() &&
            (unsigned char)pl->heightCell() == pl->markerCellH() && (unsigned int)pl->falling() == 0 &&
            pl->moveState() == 0) {
            pl->setHeld(1);
            if ((unsigned int)pl->moveDir() == 0) {
                if (self->fixedSounds()->levelCompleted != NULL)
                    (self->fixedSounds()->levelCompleted)->play(false);
                if ((unsigned int)self->session()->levelIndex() + 1 == (unsigned int)self->session()->levelCount()) {
                    self->setState(GameState::GameOver);
                    if (self->musicOn() != 0)
                        self->cdThemes()->play(GS_GAME_GAMEOVER);
                    Score_CalculateLevelScore(self, 3);
                    self->frontEnd()->debounceRef() = inputdev::KEY_RETURN;
                    g_logger.logMessage(1, "GAME: completed at level %d/%d",
                                       (unsigned int)self->session()->levelIndex() + 1,
                                       (unsigned int)self->session()->levelCount());
                } else {
                    self->setState(GameState::LevelComplete);
                    self->camera()->setCameraMode(2);
                    Play_OpenTallyMenu(self);
                    Score_CalculateLevelScore(self, (char)static_cast<unsigned char>(self->state()));
                    self->session()->setRestartCount(0);
                }
                self->session()->setTotalPlayTime((double)((long double)(unsigned long long)self->world()->timeElapsed() +
                                        (long double)self->session()->totalPlayTime()));
            }
        }
    }
}
