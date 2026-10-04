/* Game::GameTick: one simulation step, called once a frame by RenderGameFrame
 * with dt and the clock.  It ticks the bombs, the timed foe spawners, the
 * countdown, the player's bomb drop and switches, the foes, the lifts and
 * slides; then, by game state, the camera and exit while playing, the restart
 * or game-over tally on ENTER, and high-score entry.
 *
 * The x87 helpers keep a value on the FPU across a chain where the result
 * depends on it, rather than trusting the compiler's spills.
 *
 * PRESERVED:
 *   - lives drop at the restart after a death (ENTER), not at the death;
 *   - the bomb and foe loops re-read their counts every pass and do not step
 *     back after a removal, so the entry moved into a removed slot is skipped
 *     for one tick;
 *   - a bomb drop or foe bomb refused as too soon also leaves its request
 *     flag set;
 *   - the game-over branch of the restart skips the camera reset;
 *   - leaving game over with ENTER adds the level time to the total play time;
 *     ENTER after high-score name entry does not.
 *
 * KAROO_SIM_FX=tickorder, a negative control: the lift and slide loops run in
 * the other order.  Both change the tile map, so the order is observable. */

#include "inputdev.h"
#include <math.h>
#include <stdint.h>
#include "gametick.h"
#include "sysdev.h"
#include "windev.h"
#include <stdio.h>
#include <string.h>
#include "logger.h"
#include "game.h"
#include "scoretally.h"
#include "levelsounds.h"
#include "levelsetup.h"
#include "levelscore.h"
#include "levelparse.h"
#include "keypress.h"
#include "gridrestore.h"
#include "gamereset.h"
#include "fixedsounds.h"
#include "checkpoint.h"
#include "cheatcode.h"
#include "menutree.h"
#include "textentry.h"
#include "liftobject.h"
#include "slideobject.h"
#include "bridgeobject.h"
#include "breakabletile.h"
#include "bomb.h"
#include "foe.h"
#include "player.h"
#include "tilequery.h"
#include "soundobj.h"
#include "gamestr.h"
#include "gameglobals.h"

#include "record.h"
#include "progctrl.h"
#include "prof.h"
#include "cdm.h"
#include "audiodev.h"

#define KEY(k)  input_key_down(k)

static int s_fx = -1;

/* __ftol: truncates toward zero to an int64.  Out of range or NaN gives
 * 0x8000000000000000, as the x87 does. */
static long long ftol80(long double v)
{
    return (long long)v;
}

/* floor() of a double. */
static double crt_floor(double v)
{
    return floorl((long double)v);
}

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

/* The switch-triggered block, shared by the player and each foe. */

static void trigger_switch_tile(Game *game, unsigned char sw, int u, int v)
{
    BridgeObject *br = game->bridgeSlot(sw);
    Tile *t = game->map()->tile(u, v);
    if (br->phase() == 0)
        t->setBusy(1);
    else
        t->setBusy(0);
}

  unsigned int  
Sim_GameTick(Game *self, double dt, double now)
{
    Player *pl = self->player();

    if (s_fx < 0) {
        char e[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "tickorder") == 0);
        if (s_fx)
            g_logger.write("gametick: KAROO_SIM_FX=tickorder -- slides tick before lifts\n");
    }

    {
        PROF_SCOPE("sound buffers");
        Sim_AcquireFixedSoundBuffersAndMaybeReport(self);
        if (self->levelSoundsReady() == 0 && self->field_173584() == 0) {
            Sim_InitLevelBasedSounds(self);
            self->setLevelSoundsReady(1);
            if (self->stateRef() == 0 && self->musicOn() != 0)
                self->cdThemes()->replay();
        }
    }

    if (self->stateRef() == 7 && self->debounceRef() != inputdev::KEY_RETURN && KEY(inputdev::KEY_RETURN) != 0) {
        g_logger.logMessage(1, "GAME: GameActions - JJ_GAME_END");
        windev::quit(1);
    }

    self->setLastTickTime(now);
    if (self->stateRef() == 5) {
        self->tickStep()->value = 0.0;  // +0.0
    } else {
        self->tickStep()->value = dt;
        (*self->clock()) = (double)((long double)dt + (long double)(*self->clock()));
    }

    if (self->stateRef() == 0 || self->stateRef() == 5) {
        PROF_SCOPE("menu keypress");
        Sim_RestoreCheckpointStateBlocks(self);
        Sim_HandleKeypress(self);
    }

    if (self->stateRef() == 4) {
        ScriptPlayer *sp = self->scriptPlayer();
        if (sp->loaded() != 0)
            Sim_RestoreCheckpointStateBlocks(self);
        if (sp->running() == 0 || sp->loaded() == 0)
            self->setCameraMode(2);
        if (self->debounceRef() != inputdev::KEY_RETURN && KEY(inputdev::KEY_RETURN) != 0) {
            sp->setRunning(0);
            sp->setSplineActive(0);
            sp->releaseStreams();
            sp->releaseStreams();
            self->stateRef() = 1;
            if (self->restartCount() == 0) {
                if (self->musicOn() != 0)
                    self->cdThemes()->play(self->map()->mapName());
                self->cdThemes()->setCurrentTrack((unsigned char)self->cdThemes()->findThemeIndex(self->map()->mapName()));
            }
            self->setCameraDistance(7.0f);
            self->setCameraMode(0);
            pl->setField231(*self->clock());
        }
    }

    if (self->cameraMode() == 0)
        self->setCameraDistance(camera_sway(now, self->zoomDistance()));

    if (self->stateRef() == 3) {
        PROF_SCOPE("score tally");
        Sim_HandleKeypress(self);
        Sim_AnimateScoreTallyStages(self);
    }
    if (self->stateRef() == 2) {
        PROF_SCOPE("score tally");
        Sim_AnimateScoreTallyStages(self);
    }
    if (self->stateRef() == 6)
        self->nameEntry()->poll((unsigned int)ftol80((*self->clock())));

    Game *game = self;
    if (!s_fx) {
        {
            PROF_SCOPE("lifts");
            for (int i = 0; i < (int)game->liftCount(); ++i)
                game->liftSlot(i)->tick();
        }
        {
            PROF_SCOPE("slides");
            for (int i = 0; i < (int)game->slideCount(); ++i)
                game->slideSlot(i)->tick();
        }
    } else {
        {
            PROF_SCOPE("slides");
            for (int i = 0; i < (int)game->slideCount(); ++i)
                game->slideSlot(i)->tick();
        }
        {
            PROF_SCOPE("lifts");
            for (int i = 0; i < (int)game->liftCount(); ++i)
                game->liftSlot(i)->tick();
        }
    }
    {
        PROF_SCOPE("breakables");
        for (int i = 0; i < (int)game->breakableCount(); ++i)
            game->breakableSlot(i)->tick();
    }
    {
        PROF_SCOPE("bridges");
        for (int i = 0; i < (int)game->bridgeCount(); ++i)
            game->bridgeSlot(i)->tick();
    }

    if ((self->stateRef() == 1 || self->stateRef() == 4) && self->debounceRef() != inputdev::KEY_ESCAPE && KEY(inputdev::KEY_ESCAPE) != 0) {
        self->setStateBeforeMenu(self->stateRef());
        self->menu()->rewind();
        self->menu()->setLastKey(inputdev::KEY_ESCAPE);
        self->stateRef() = 5;
        self->menu()->setLockStart(self->lastTickTime());
        self->menu()->setLock(1);
        self->debounceRef() = inputdev::KEY_ESCAPE;
    }

    // Bombs: the count is re-read each pass; no step back after a removal.
    {
        PROF_SCOPE("bombs");
        for (int i = 0; i < (int)self->bombCount(); ++i) {
            self->bombSlot(self->bombId(i))->tick();
            unsigned char id = self->bombId(i);
            if (self->bombSlot(id)->removeRequested() != 0)
                Bomb::remove(self, id);
        }
    }

    {
        unsigned char pct = completion_percent((unsigned int)pl->completionNumerator(), self->field_170a65());
        self->setVitalityPercent(pct);
        if (pct > 100)
            self->setVitalityPercent(100);
    }

    {
        PROF_SCOPE("player tile effects");
        pl->updateTileEffects();
    }

    if (self->stateRef() == 1) {
        if (self->cheatEntry()->active() != 0)
            Sim_HandleTypedCheatCode(self);

        // The timed foe spawners (levelcensus.h).
        {
        PROF_SCOPE("timed spawners");
        for (int i = 0; i < (int)self->census()->timed; ++i) {
            TimedSpawner *E = self->timedSpawner((unsigned)i);
            long double since = (long double)(*self->clock()) - (long double)E->lastSpawn;
            if (!(since > (long double)E->interval))
                continue;
            signed char u = (signed char)E->u, v = (signed char)E->v;
            if (self->map()->tile(u, v)->occupant() == 0 &&self->foeCount() < E->maxFoes) {
                unsigned char id = Foe::spawn(self, (unsigned char)u,
                                              (unsigned char)v, E->height, 2,
                                              (unsigned char)(E->field_0b + 100));
                Foe *foe = self->foeSlot(id);
                if (((int)self->foesKilled() + 1) % 15 == 0)
                    foe->setDropContents(7);
                else
                    foe->setDropContents(1);
                Sim_AcquireObjectSoundBuffersForIndex(self, id);
            }
            // The last-spawn time, from the clock.
            E->lastSpawn = *self->clock();
        }

        }

        self->config()->setActiveCameraPitch(self->config()->cameraPitch());
        if (pl->moveState() != 0) {
            self->setOverviewActive(0);
            self->setCameraDistance(self->zoomDistance());
            if (pl->soundAf() != NULL) pl->soundAf()->stop();
            if (pl->soundAb() != NULL) pl->soundAb()->stop();
            if (pl->soundC7() != NULL) pl->soundC7()->stop();
        }

        if ((unsigned int)pl->gliding() != 0) {
            if (self->parkedCameraOption() == 0) {
                self->setParkedCameraOption((unsigned char)(self->cameraTurnsWithPlayer() + 10));
                self->setCameraTurnsWithPlayer(1);
            }
        } else if (self->parkedCameraOption() >= 10) {
            self->setCameraTurnsWithPlayer((unsigned char)(self->parkedCameraOption() - 10));
            self->setParkedCameraOption(0);
        }

        // The countdown's last seconds: compared at 80 bits, stored at 64.
        {
            static const double k001 = 0.001;  // a double
            long double lim = (long double)(unsigned long long)(unsigned int)self->timeLimit() * 1000.0L;
            long double rem80 = (lim - (long double)(unsigned long long)self->timeElapsed()) *
                                (long double)k001;
            double rem64 = (double)rem80;
            if (rem80 > 11.0L || pl->moveState() == 3) {
                pl->setLastSecondsMark(10.0);
            } else if ((long double)pl->lastSecondsMark() > (long double)rem64) {
                if (self->fixedSounds()->lastSeconds != NULL)
                    (self->fixedSounds()->lastSeconds)->play(false);
                pl->setLastSecondsMark(crt_floor(rem64));
            }
        }
        if (pl->moveState() == 0) {
            PROF_SCOPE("player program");
            g_progCtrl.dispatch((unsigned short)self->stateRef());
        }
    } else {
        pl->setIdleStarted(0);
        pl->setLastActive(*self->clock());
        g_progCtrl.dispatch(0);
        if (self->fixedSounds()->lastSeconds != NULL)
            (self->fixedSounds()->lastSeconds)->stop();
    }

    // The player's bomb drop.
    if ((unsigned int)pl->bombDropRequest() != 0) {
        unsigned int timed = (unsigned int)pl->moveDir();
        int spawn = 1, offset = 0;
        if (timed != 0) {
            long double since = (long double)(*self->clock()) - (long double)pl->animStart();
            if (since < 50.0L)
                offset = 1;
            else
                spawn = 0;  // too soon: the flag stays set
        }
        if (spawn) {
            if (offset)
                Bomb::spawn(self,(unsigned char)((unsigned char)pl->cellU() - (unsigned char)pl->stepU()),
                                    (unsigned char)((unsigned char)pl->cellV() - (unsigned char)pl->stepV()),
                                    (unsigned char)((unsigned char)pl->heightCell() - (unsigned char)pl->field141()),
                                    pl->facing());
            else
                Bomb::spawn(self,(unsigned char)pl->cellU(), (unsigned char)pl->cellV(), (unsigned char)pl->heightCell(),
                                    pl->facing());
            pl->setBombDropRequest(0);
        }
    }

    // A switch the player stepped on.
    {
        unsigned char sw = pl->switchSlot();
        if (sw < 0xff && self->bridgeSlot(sw)->armed() == 0) {
            g_logger.logMessage(1, "GAME: switch triggered %d", (unsigned int)sw);
            trigger_switch_tile(self, pl->switchSlot(), pl->cellU(), pl->cellV());
            BridgeObject *br = self->bridgeSlot(pl->switchSlot());
            br->arm(self->clock());
            br->playArmSound();
            Sim_MarkListedTilesBlockedByObject(self, pl->switchSlot());
            pl->setSwitchSlot(0xff);
        }
    }

    if (pl->moveState() != 0) {
        self->setCameraMode(2);
    } else if (self->stateRef() == 1) {
        self->setTimeElapsed(self->timeElapsed()
            + (unsigned int)ftol80(self->tickStep()->value));
        self->setField170a65(self->field_170a65()
            + (unsigned int)ftol80(self->tickStep()->value));
    }

    if ((unsigned int)pl->gliding() != 0) {
        self->setCameraMode(0);
    } else if ((unsigned int)pl->falling() != 0) {
        long double d = (long double)(int)pl->fallStartH() - (long double)(int)pl->heightCell();
        if (d > 2.0L) {
            self->setCameraMode(1);
            self->setCameraEye(0, pl->posU());
            self->setCameraEye(2, pl->posV());
        }
    }

    // The foes.  Their pieces are Foe methods (foe.cpp); the slot is re-read
    // for each piece.
    {
    PROF_SCOPE("foes");
    for (int i = 0; i < (int)game->foeCount(); ++i) {
        unsigned char id = game->foeId(i);
        Foe **slot = game->foeSlotRef(id);
        unsigned char tu, tv;

        {
            unsigned char sw = (*slot)->switchSlot();
            if (sw < 0xff && game->bridgeSlot(sw)->armed() == 0) {
                g_logger.logMessage(1, "GAME: switch triggered %d", (unsigned int)sw);
                trigger_switch_tile(self, (*slot)->switchSlot(), (*slot)->cellU(), (*slot)->cellV());
                game->bridgeSlot((*slot)->switchSlot())->arm(game->clock());
                Sim_MarkListedTilesBlockedByObject(self, (*slot)->switchSlot());
                (*slot)->clearSwitchSlot();
            }
        }

        int hold = ((unsigned int)pl->freezeActive() == 0 && self->stateRef() == 1) ? 0 : 1;
        if (self->stateRef() == 3)
            hold = 1;
        if (pl->moveState() != 0)
            hold = 1;
        {
            PROF_SCOPE("choose target");
            (*slot)->chooseTarget(game, hold, (unsigned char)pl->cellU(), (unsigned char)pl->cellV(),
                                  pl->markerCellU(), pl->markerCellV(), &tu, &tv);
        }
        {
            PROF_SCOPE("step");
            (*slot)->step(tu, tv);
        }
        {
            PROF_SCOPE("drop bomb");
            (*slot)->dropBomb(game);
        }
        {
            PROF_SCOPE("player contact");
            (*slot)->checkPlayerContact(pl->moveStateRef(),
                                        pl->posU(), pl->posY(), pl->posV());
        }
        if ((*slot)->finishDespawn(self->map())) {
            Foe::remove(game, id);
            self->setFoesKilled((unsigned char)(self->foesKilled() + 1));
        }
    }
    }

    // Playing: camera follow, time-out, exit.
    if (self->stateRef() == 1) {
        if ((unsigned int)pl->falling() == 0 && pl->moveState() == 0) {
            self->setCameraEye(0, pl->posU());
            self->setCameraEye(1, pl->posY());
            self->setCameraEye(2, pl->posV());
            self->setCameraMode(0);
        }
        if (pl->moveState() != 3) {
            int t = self->timeLimit() * 1000;
            if (t - (int)self->timeElapsed() <= 0) {
                void *snd = self->fixedSounds()->timeOut;
                self->setTimeElapsed((unsigned int)t);
                pl->setMoveState(3);
                if (snd != NULL)
                    ((audiodev::Buffer *)snd)->play(false);
            }
        }
        if (pl->gemsCollected() >= self->gemsRequired()) {
            if (self->field_173b1a() == 0 && self->stateRef() != 3) {
                int r = (int)ftol80((*self->clock()));
                void *snd = self->fixedSounds()->crystalBank[r % 3];
                if (snd != NULL)
                    ((audiodev::Buffer *)snd)->play(false);
                self->setField173b1a(1);
            }
            self->map()->tile((signed char)pl->markerCellU(), (signed char)pl->markerCellV())->setBusy(1);
            if ((unsigned char)pl->cellU() == pl->markerCellU() && (unsigned char)pl->cellV() == pl->markerCellV() &&
                (unsigned char)pl->heightCell() == pl->markerCellH() && (unsigned int)pl->falling() == 0 &&
                pl->moveState() == 0) {
                pl->setHeld(1);
                if ((unsigned int)pl->moveDir() == 0) {
                    if (self->fixedSounds()->levelCompleted != NULL)
                        (self->fixedSounds()->levelCompleted)->play(false);
                    if ((unsigned int)self->levelIndex() + 1 == (unsigned int)self->levelCount()) {
                        self->stateRef() = 2;
                        if (self->musicOn() != 0)
                            self->cdThemes()->play(GS_GAME_GAMEOVER);
                        Score_CalculateLevelScore(self, 3);
                        self->debounceRef() = inputdev::KEY_RETURN;
                        g_logger.logMessage(1, "GAME: completed at level %d/%d",
                                           (unsigned int)self->levelIndex() + 1,
                                           (unsigned int)self->levelCount());
                    } else {
                        self->stateRef() = 3;
                        self->setCameraMode(2);
                        self->menu()->rewind();
                        self->menu()->pop();
                        self->menu()->push(0x28);
                        self->menu()->setNode(0x28);
                        self->menu()->setLockStart(self->lastTickTime());
                        self->menu()->setLock(1);
                        self->menu()->setCursor(0);
                        Score_CalculateLevelScore(self, (char)self->stateRef());
                        self->setRestartCount(0);
                    }
                    self->setTotalPlayTime((double)((long double)(unsigned long long)self->timeElapsed() +
                                            (long double)self->totalPlayTime()));
                }
            }
        }
    }

    // ENTER after a death, or on the game-over tally.
    if (self->stateRef() != 2) {
        if (self->debounceRef() != inputdev::KEY_RETURN && KEY(inputdev::KEY_RETURN) != 0 && pl->moveState() != 0 && self->stateRef() == 1) {
            self->setRestartCount((unsigned char)(self->restartCount() + 1));
            int lives = pl->lives();
            int bonus = (int)self->map()->bonus();
            int restart_tail = 1;
            if (lives > 0 && bonus == 0) {
                pl->setLives(lives - 1);  // lives drop at the restart
                Sim_RestoreTileGridFromSnapshot(self);
                Sim_SetupLevelObjects(self);
            } else if (lives <= 0 && bonus == 0) {
                self->stateRef() = 2;
                if (self->musicOn() != 0)
                    self->cdThemes()->play(GS_GAME_GAMEOVER);
                Score_CalculateLevelScore(self, (char)self->stateRef());
                self->debounceRef() = inputdev::KEY_RETURN;
                restart_tail = 0;  // game over: skip the camera reset
            } else {
                self->setCameraMode(2);
                self->stateRef() = 3;
                if (self->musicOn() != 0)
                    self->cdThemes()->play(GS_GAME_COMPLETED);
                self->menu()->rewind();
                self->menu()->pop();
                self->menu()->push(0x28);
                self->menu()->setNode(0x28);
                self->menu()->setLockStart(self->lastTickTime());
                self->menu()->setLock(1);
                self->menu()->setCursor(0);
                self->setTimeElapsed((unsigned int)(self->timeLimit() * 1000));
                Score_CalculateLevelScore(self, (char)self->stateRef());
                self->setRestartCount(0);
                self->setTotalPlayTime((double)((long double)(unsigned long long)self->timeElapsed() +
                                        (long double)self->totalPlayTime()));
                g_logger.logMessage(2, "GAME: game completed %d %d",
                                   (unsigned int)self->levelIndex(), (unsigned int)self->levelCount());
                if ((unsigned int)self->levelIndex() == (unsigned int)self->levelCount() - 1) {
                    self->stateRef() = 2;
                    if (self->musicOn() != 0)
                        self->cdThemes()->play(GS_GAME_GAMEOVER);
                }
                self->setRestartCount(0);
            }
            if (restart_tail) {
                self->setCameraDistance(7.0f);
                self->setCameraMode(0);
                self->debounceRef() = inputdev::KEY_RETURN;
            }
        }
    } else if (self->debounceRef() != inputdev::KEY_RETURN && KEY(inputdev::KEY_RETURN) != 0 && self->tallyDone() != 0) {
        unsigned int r = self->highScores()->insert(
            (unsigned int)pl->score(), (unsigned char)(self->levelIndex() + 1));
        if ((unsigned char)r < 0xff) {
            self->stateRef() = 6;
            if (self->musicOn() != 0)
                g_cdAudio.stop();
            self->nameEntry()->setMaxLength(0x0f);
            self->nameEntry()->setActive(1);
            self->nameEntry()->setCursor(0);
            self->debounceRef() = inputdev::KEY_RETURN;
            self->nameEntry()->setLastKey(inputdev::KEY_RETURN);
            self->nameEntry()->setBuffer(
                self->highScores()->record(self->highScores()->lastRank())->name);
        } else {
            self->stateRef() = 0;
            self->menu()->rewind();
            const char *theme = NULL;
            int setup = 1;
            if ((unsigned int)self->levelIndex() + 1 == (unsigned int)self->levelCount() &&
                pl->moveState() == 0) {
                if (self->field_0c() == 0) {
                    char name[256];
                    sprintf(name, GS_GAME_FINAL_DIR, self->gameFileName());
                    Sim_ParseLevelFiles(self, name);
                    self->menu()->push(0);
                    self->menu()->setNode(5);
                    theme = GS_GAME_FINAL;
                } else {
                    self->stateRef() = 7;
                    if (self->musicOn() != 0)
                        g_cdAudio.stop();
                    self->debounceRef() = inputdev::KEY_RETURN;
                }
            } else {
                Sim_ClearGameState(self);
                Sim_ParseLevelFiles(self, self->menuLevelName());
                theme = GS_GAME_MAIN;
            }
            if (theme != NULL)
                self->cdThemes()->setCurrentTrack((unsigned char)self->cdThemes()->findThemeIndex(theme));
            if (setup) {
                Sim_SetupLevelObjects(self);
                self->scriptPlayer()->setRunning(1);
                self->debounceRef() = inputdev::KEY_RETURN;
                self->setTotalPlayTime((double)((long double)(unsigned long long)self->timeElapsed() +
                                        (long double)self->totalPlayTime()));
            }
        }
    }

    // ENTER after high-score name entry.
    if (self->stateRef() == 6 && self->debounceRef() != inputdev::KEY_RETURN && KEY(inputdev::KEY_RETURN) != 0) {
        self->highScores()->writeFile(GS_GAME_HSFILE, 0x4b);
        self->stateRef() = 0;
        self->menu()->rewind();
        const char *theme = NULL;
        if ((unsigned int)self->levelIndex() + 1 == (unsigned int)self->levelCount()) {
            if (self->field_0c() == 0) {
                char name[256];
                sprintf(name, GS_GAME_FINAL_DIR, self->gameFileName());
                Sim_ParseLevelFiles(self, name);
                self->menu()->push(0);
                self->menu()->setNode(5);
                theme = GS_GAME_FINAL;
            } else {
                self->stateRef() = 7;
                if (self->musicOn() != 0)
                    g_cdAudio.stop();
                self->debounceRef() = inputdev::KEY_RETURN;
            }
        } else {
            Sim_ClearGameState(self);
            Sim_ParseLevelFiles(self, self->menuLevelName());
            theme = GS_GAME_MAIN;
        }
        if (theme != NULL)
            self->cdThemes()->setCurrentTrack((unsigned char)self->cdThemes()->findThemeIndex(theme));
        Sim_SetupLevelObjects(self);
        self->scriptPlayer()->setRunning(1);
        self->debounceRef() = inputdev::KEY_RETURN;
    }

    if (pl->moveState() != 0 && pl->moveState() != 2)
        self->setCameraMode(2);
    if (KEY(self->debounceRef()) == 0)
        self->debounceRef() = 0;
    self->setOverviewActive(0);
    self->setField13cc90(0);
    self->setField48b14(self->field_48b14() + 1);
    self->setTickCount(self->tickCount() + 1);
    return self->tickCount() & 0xffffff00u;
}
