/* GAMETICK_PLAN.md Band C — Game::GameTick it(self).
 *
 *   Game::GameTick  0x00414df0   1 E8 site (0x00427007, RenderGameFrame)
 *
 * uint __thiscall(Game*, double dt, double now), RET 0x10.  Transcribed from
 * the LISTING (0x414df0-0x416414), not the decompile: the decompile drops
 * every __ftol argument, invents a fourth argument for two Log_Message calls,
 * and folds several fall-throughs that the listing keeps separate.
 *
 * Every callee is now either ours or a NAMED callback:
 *   ours      the whole of Bands A and B (entity ticks, spawns, removes, tile
 *             queries, level loaders, menu stack, score, text entry, cheat,
 *             keypress, sound attachment, CD music, high scores, input
 *             dispatch, the static sound buffers, the logger)
 *   callback  ReleaseScriptStreamBuffers 0x41e840 (scriptplayer.cpp and
 *             levelsetup.cpp already keep it), and -- through the callees --
 *             the SoundManager and the script-player tick.
 *   imports   GetAsyncKeyState (via hooks_GetAsyncKeyState, the replay path),
 *             PostQuitMessage.
 *   CRT       __ftol and floor, reproduced with the same x87 instructions.
 *
 * x87 fidelity.  The game runs with the FPU at 53-bit precision (control
 * word 0x27f), and this DLL shares the thread, so long-double arithmetic here
 * executes the same instructions under the same control word.  Where the
 * original keeps a value on the x87 stack across a compare (the countdown,
 * the contact distance, the camera sway) the helpers below do the same in
 * inline asm rather than trusting gcc's spill behaviour.
 *
 * Preserved behaviour worth naming:
 *  - lives are DEC'd during the RESTART (after ENTER), not at death (HOOKS.md)
 *  - the enemy and foe loops re-read their counts every iteration and do
 *    NOT step back after a removal, so the entry compacted into the removed
 *    slot is skipped for one tick
 *  - the bomb-drop and foe-bomb "too soon" tests skip the spawn AND leave the
 *    request flag set (the jump bypasses the clear)
 *  - the game-over branch of the restart path skips the camera-reset tail
 *  - the ENTER-to-leave-game-over path adds the level time to +0x170a44; the
 *    ENTER-after-name-entry path does not
 *
 * Control: KAROO_SIM_FX=tickorder -- the lift and slide tick loops run
 * in the opposite order (slides first).  Both mutate the tile map, so the
 * order is observable; this proves the per-frame dispatch is ours.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "game.h"
#include "gamelog.h"
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

struct CStaticSoundbuffer;
struct ProgableControl;
struct CDM;

extern "C" {
__declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);


__declspec(dllexport) void __attribute__((thiscall)) ProgCtrl_Dispatch(ProgableControl *s, unsigned short game_state);
__declspec(dllexport) void __attribute__((thiscall)) CDM_StopTrack(CDM *self);

__declspec(dllexport) int __attribute__((thiscall)) CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
__declspec(dllexport) void __attribute__((thiscall)) CStatic_HaltPlayback(CStaticSoundbuffer *self);
__declspec(dllexport) void __attribute__((thiscall)) CStatic_Set3DPosition(CStaticSoundbuffer *self, float x, float y, float z, DWORD dwApply);
}



#define KEY(k)  hooks_GetAsyncKeyState(k)



/* The level map and its tiles (levelmap.h). */

static int s_fx = -1;

/* ─── x87 helpers ───────────────────────────────────────────────────────── */

/* The CRT __ftol: chop the value on the x87 stack into an int64. */
static long long ftol80(long double v)
{
    unsigned short cw, chop;
    long long r;
    __asm__ volatile(
        "fnstcw %1\n\t"
        "movw %1, %%ax\n\t"
        "orw $0x0c00, %%ax\n\t"
        "movw %%ax, %2\n\t"
        "fldcw %2\n\t"
        "fistpll %0\n\t"
        "fldcw %1\n\t"
        : "=m"(r), "=m"(cw), "=m"(chop) : "t"(v) : "ax", "st");
    return r;
}

/* floor() as the CRT does it: __frnd under the round-down control word
 * 0x173f (FUN_00451062; the rounding bits are all that matter here). */
static double crt_floor(double v)
{
    unsigned short cw, down;
    long double r;
    __asm__ volatile(
        "fnstcw %1\n\t"
        "movw %1, %%ax\n\t"
        "andw $0xf3ff, %%ax\n\t"
        "orw $0x0400, %%ax\n\t"
        "movw %%ax, %2\n\t"
        "fldcw %2\n\t"
        "frndint\n\t"
        "fldcw %1\n\t"
        : "=t"(r), "=m"(cw), "=m"(down) : "0"((long double)v) : "ax");
    return (double)r;
}

/* float(sin(now * 0.0025f) * 0.2 + base): FLD double, FMUL float, FSIN,
 * FMUL double, FADD float, FSTP float -- one x87 chain. */
static float camera_sway(double now, float base)
{
    static const float  k1 = 0.0024999999441206455f;   /* DAT_0045d3e8 */
    static const double k2 = 0.20000000298023224;      /* DAT_0045d3e0 */
    float out;
    __asm__ volatile(
        "fldl %1\n\t"
        "fmuls %2\n\t"
        "fsin\n\t"
        "fmull %3\n\t"
        "fadds %4\n\t"
        "fstps %0\n\t"
        : "=m"(out) : "m"(now), "m"(k1), "m"(k2), "m"(base) : "st");
    return out;
}

/* (double)u64(a) / ((double)u64(b) * 0.001f) * 25.0, then __ftol: the
 * completion percentage.  b == 0 gives an x87 infinity, whose FISTP is the
 * integer indefinite 0x8000000000000000 -- AL = 0 -- reproduced by keeping
 * the whole thing on the FPU. */
static unsigned char completion_percent(unsigned int a, unsigned int b)
{
    static const float  k001 = 0.0010000000474974513f;   /* DAT_0045d308 */
    static const double k25  = 25.0;                      /* DAT_0045d3d8 */
    unsigned long long qa = a, qb = b;
    unsigned short cw, chop;
    long long r;
    __asm__ volatile(
        "fildll %3\n\t"
        "fildll %4\n\t"
        "fmuls %5\n\t"
        "fdivrp\n\t"                 /* ST1/ST0, pop (MSVC's FDIVP)        */
        "fmull %6\n\t"
        "fnstcw %1\n\t"
        "movw %1, %%ax\n\t"
        "orw $0x0c00, %%ax\n\t"
        "movw %%ax, %2\n\t"
        "fldcw %2\n\t"
        "fistpll %0\n\t"
        "fldcw %1\n\t"
        : "=m"(r), "=m"(cw), "=m"(chop)
        : "m"(qa), "m"(qb), "m"(k001), "m"(k25) : "ax", "st", "st(1)");
    return (unsigned char)r;
}

/* ─── The switch-triggered block, shared by the player and each foe ─────── */

static void trigger_switch_tile(Game *game, unsigned char sw, int u, int v)
{
    BridgeObject *br = game->bridgeSlot(sw);
    Tile *t = game->map()->tile(u, v);
    if (br->phase() == 0)
        t->setBusy(1);
    else
        t->setBusy(0);
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_GameTick(Game *self, double dt, double now)
{
    Player *pl = self->player();

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "tickorder") == 0);
        if (s_fx)
            log_write("gametick: KAROO_SIM_FX=tickorder -- slides tick before lifts\n");
    }

    Sim_AcquireFixedSoundBuffersAndMaybeReport(self);
    if (self->levelSoundsReady() == 0 && self->field_173584() == 0) {
        Sim_InitLevelBasedSounds(self);
        self->setLevelSoundsReady(1);
        if (self->stateRef() == 0 && self->musicOn() != 0)
            self->cdThemes()->replay();
    }

    if (self->stateRef() == 7 && self->debounceRef() != 0x0d && KEY(0x0d) != 0) {
        GameLog_LogMessage(GG_LOGGER, 1, GS_GAME_JJ_GAME_END);
        PostQuitMessage(1);
    }

    self->setLastTickTime(now);
    if (self->stateRef() == 5) {
        self->tickStep()->value = 0.0;    /* two zero dwords: +0.0 */
    } else {
        self->tickStep()->value = dt;
        (*self->clock()) = (double)((long double)dt + (long double)(*self->clock()));
    }

    if (self->stateRef() == 0 || self->stateRef() == 5) {
        Sim_RestoreCheckpointStateBlocks(self);
        Sim_HandleKeypress(self);
    }

    if (self->stateRef() == 4) {
        ScriptPlayer *sp = self->scriptPlayer();
        if (sp->loaded() != 0)
            Sim_RestoreCheckpointStateBlocks(self);
        if (sp->running() == 0 || sp->loaded() == 0)
            self->setCameraMode(2);
        if (self->debounceRef() != 0x0d && KEY(0x0d) != 0) {
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
        self->setCameraDistance(camera_sway(now, self->field_13cca4()));

    if (self->stateRef() == 3) {
        Sim_HandleKeypress(self);
        Sim_AnimateScoreTallyStages(self);
    }
    if (self->stateRef() == 2)
        Sim_AnimateScoreTallyStages(self);
    if (self->stateRef() == 6)
        self->nameEntry()->poll((unsigned int)ftol80((*self->clock())));

    Game *game = self;
    if (!s_fx) {
        for (int i = 0; i < (int)game->liftCount(); ++i)
            game->liftSlot(i)->tick();
        for (int i = 0; i < (int)game->slideCount(); ++i)
            game->slideSlot(i)->tick();
    } else {
        for (int i = 0; i < (int)game->slideCount(); ++i)
            game->slideSlot(i)->tick();
        for (int i = 0; i < (int)game->liftCount(); ++i)
            game->liftSlot(i)->tick();
    }
    for (int i = 0; i < (int)game->breakableCount(); ++i)
        game->breakableSlot(i)->tick();
    for (int i = 0; i < (int)game->bridgeCount(); ++i)
        game->bridgeSlot(i)->tick();

    if ((self->stateRef() == 1 || self->stateRef() == 4) && self->debounceRef() != 0x1b && KEY(0x1b) != 0) {
        self->setStateBeforeMenu(self->stateRef());
        Sim_RewindMenuStackToRootNode(self->menu());
        self->menu()->setLastKey(0x1b);
        self->stateRef() = 5;
        self->menu()->setLockStart(self->lastTickTime());
        self->menu()->setLock(1);
        self->debounceRef() = 0x1b;
    }

    /* enemies: count re-read each pass; no step-back after a removal */
    for (int i = 0; i < (int)self->bombCount(); ++i) {
        self->bombSlot(self->bombId(i))->tick();
        unsigned char id = self->bombId(i);
        if (self->bombSlot(id)->removeRequested() != 0)
            Bomb::remove(self, id);
    }

    {
        unsigned char pct = completion_percent((unsigned int)pl->completionNumerator(), self->field_170a65());
        self->setVitalityPercent(pct);
        if (pct > 100)
            self->setVitalityPercent(100);
    }

    pl->updateTileEffects();

    if (self->stateRef() == 1) {
        if (self->cheatEntry()->active() != 0)
            Sim_HandleTypedCheatCode(self);

        /* the runtime foe spawners (levelcensus.h); the original walks a
         * pointer at each record's +0x14, Game+0x20251 + i*0x15 */
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
            /* The spawner's last-spawn time (read as a double above), set
             * from the clock -- two dword MOVs in the original, one double. */
            E->lastSpawn = *self->clock();
        }

        self->config()->setField20a48Bits(self->config()->field20a4cBits());
        if (pl->moveState() != 0) {
            self->setField13cca8(0);
            self->setCameraDistance(self->field_13cca4());
            if (pl->soundAf() != NULL) CStatic_HaltPlayback(pl->soundAf());
            if (pl->soundAb() != NULL) CStatic_HaltPlayback(pl->soundAb());
            if (pl->soundC7() != NULL) CStatic_HaltPlayback(pl->soundC7());
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

        /* last-seconds countdown: compared at 80 bits, stored at 64 */
        {
            static const double k001 = 0.001;              /* DAT_0045d368, a DOUBLE */
            long double lim = (long double)(unsigned long long)(unsigned int)self->timeLimit() * 1000.0L;
            long double rem80 = (lim - (long double)(unsigned long long)self->timeElapsed()) *
                                (long double)k001;
            double rem64 = (double)rem80;
            if (rem80 > 11.0L || pl->moveState() == 3) {
                pl->setLastSecondsMark(10.0);
            } else if ((long double)pl->lastSecondsMark() > (long double)rem64) {
                if (self->fixedSounds()->lastSeconds != NULL)
                    CStatic_TriggerPlayback(self->fixedSounds()->lastSeconds, 0);
                pl->setLastSecondsMark(crt_floor(rem64));
            }
        }
        if (pl->moveState() == 0)
            ProgCtrl_Dispatch(GG_PROGCTRL, (unsigned short)self->stateRef());
    } else {
        pl->setIdleStarted(0);
        pl->setLastActive(*self->clock());
        ProgCtrl_Dispatch(GG_PROGCTRL, 0);
        if (self->fixedSounds()->lastSeconds != NULL)
            CStatic_HaltPlayback(self->fixedSounds()->lastSeconds);
    }

    /* the player's bomb drop */
    if ((unsigned int)pl->bombDropRequest() != 0) {
        unsigned int timed = (unsigned int)pl->moveDir();
        int spawn = 1, offset = 0;
        if (timed != 0) {
            long double since = (long double)(*self->clock()) - (long double)pl->animStart();
            if (since < 50.0L)
                offset = 1;
            else
                spawn = 0;                 /* too late: flag stays set */
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

    /* a switch the player stepped on */
    {
        unsigned char sw = pl->switchSlot();
        if (sw < 0xff && self->bridgeSlot(sw)->armed() == 0) {
            GameLog_LogMessage(GG_LOGGER, 1, GS_GAME_SWITCH_TRIGGERED, (unsigned int)sw);
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

    /* ─── the foe loop ───
     * The foe-side pieces are Foe methods (foe.cpp); the slot is re-read
     * through its address for each, as the original re-reads it. */
    for (int i = 0; i < (int)game->foeCount(); ++i) {
        unsigned char id = game->foeId(i);
        Foe **slot = game->foeSlotRef(id);
        unsigned char tu, tv;

        {
            unsigned char sw = (*slot)->switchSlot();
            if (sw < 0xff && game->bridgeSlot(sw)->armed() == 0) {
                GameLog_LogMessage(GG_LOGGER, 1, GS_GAME_SWITCH_TRIGGERED, (unsigned int)sw);
                trigger_switch_tile(self, (*slot)->switchSlot(), (*slot)->cellU(), (*slot)->cellV());
                game->bridgeSlot((*slot)->switchSlot())->arm(game->clock());
                Sim_MarkListedTilesBlockedByObject(self, (*slot)->switchSlot());
                (*slot)->clearSwitchSlot();
            }
        }

        int hold = ((unsigned int)pl->effect8Active() == 0 && self->stateRef() == 1) ? 0 : 1;
        if (self->stateRef() == 3)
            hold = 1;
        if (pl->moveState() != 0)
            hold = 1;
        (*slot)->chooseTarget(game, hold, (unsigned char)pl->cellU(), (unsigned char)pl->cellV(),
                              pl->markerCellU(), pl->markerCellV(), &tu, &tv);

        (*slot)->step(tu, tv);
        (*slot)->dropBomb(game);
        (*slot)->checkPlayerContact(pl->moveStateRef(),
                                    pl->posU(), pl->posY(), pl->posV());
        if ((*slot)->finishDespawn(self->map())) {
            Foe::remove(game, id);
            self->setFoesKilled((unsigned char)(self->foesKilled() + 1));
        }
    }

    /* ─── playing: camera follow, time-out, exit ─── */
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
                    CStatic_TriggerPlayback((CStaticSoundbuffer *)snd, 0);
            }
        }
        if (pl->gemsCollected() >= self->gemsRequired()) {
            if (self->field_173b1a() == 0 && self->stateRef() != 3) {
                int r = (int)ftol80((*self->clock()));
                void *snd = self->fixedSounds()->crystalBank[r % 3];
                if (snd != NULL)
                    CStatic_TriggerPlayback((CStaticSoundbuffer *)snd, 0);
                self->setField173b1a(1);
            }
            self->map()->tile((signed char)pl->markerCellU(), (signed char)pl->markerCellV())->setBusy(1);
            if ((unsigned char)pl->cellU() == pl->markerCellU() && (unsigned char)pl->cellV() == pl->markerCellV() &&
                (unsigned char)pl->heightCell() == pl->markerCellH() && (unsigned int)pl->falling() == 0 &&
                pl->moveState() == 0) {
                pl->setHeld(1);
                if ((unsigned int)pl->moveDir() == 0) {
                    if (self->fixedSounds()->levelCompleted != NULL)
                        CStatic_TriggerPlayback(self->fixedSounds()->levelCompleted, 0);
                    if ((unsigned int)self->levelIndex() + 1 == (unsigned int)self->levelCount()) {
                        self->stateRef() = 2;
                        if (self->musicOn() != 0)
                            self->cdThemes()->play(GS_GAME_GAMEOVER);
                        Score_CalculateLevelScore(self, 3);
                        self->debounceRef() = 0x0d;
                        GameLog_LogMessage(GG_LOGGER, 1, GS_GAME_COMPLETED_AT_LEVEL,
                                           (unsigned int)self->levelIndex() + 1,
                                           (unsigned int)self->levelCount());
                    } else {
                        self->stateRef() = 3;
                        self->setCameraMode(2);
                        Sim_RewindMenuStackToRootNode(self->menu());
                        Sim_PopMenuNodeFromStack(self->menu());
                        Sim_PushMenuNodeOnStack(self->menu(), 0x28);
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

    /* ─── ENTER handling: after a death, or on the game-over tally ─── */
    if (self->stateRef() != 2) {
        if (self->debounceRef() != 0x0d && KEY(0x0d) != 0 && pl->moveState() != 0 && self->stateRef() == 1) {
            self->setRestartCount((unsigned char)(self->restartCount() + 1));
            int lives = pl->lives();
            int bonus = (int)self->map()->bonus();
            int restart_tail = 1;
            if (lives > 0 && bonus == 0) {
                pl->setLives(lives - 1);              /* the DEC at 0x4160d6 */
                Sim_RestoreTileGridFromSnapshot(self);
                Sim_SetupLevelObjects(self);
            } else if (lives <= 0 && bonus == 0) {
                self->stateRef() = 2;
                if (self->musicOn() != 0)
                    self->cdThemes()->play(GS_GAME_GAMEOVER);
                Score_CalculateLevelScore(self, (char)self->stateRef());
                self->debounceRef() = 0x0d;
                restart_tail = 0;                        /* JMP 0x4162b3 */
            } else {
                self->setCameraMode(2);
                self->stateRef() = 3;
                if (self->musicOn() != 0)
                    self->cdThemes()->play(GS_GAME_COMPLETED);
                Sim_RewindMenuStackToRootNode(self->menu());
                Sim_PopMenuNodeFromStack(self->menu());
                Sim_PushMenuNodeOnStack(self->menu(), 0x28);
                self->menu()->setNode(0x28);
                self->menu()->setLockStart(self->lastTickTime());
                self->menu()->setLock(1);
                self->menu()->setCursor(0);
                self->setTimeElapsed((unsigned int)(self->timeLimit() * 1000));
                Score_CalculateLevelScore(self, (char)self->stateRef());
                self->setRestartCount(0);
                self->setTotalPlayTime((double)((long double)(unsigned long long)self->timeElapsed() +
                                        (long double)self->totalPlayTime()));
                GameLog_LogMessage(GG_LOGGER, 2, GS_GAME_DONE_LOG,
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
                self->debounceRef() = 0x0d;
            }
        }
    } else if (self->debounceRef() != 0x0d && KEY(0x0d) != 0 && self->tallyDone() != 0) {
        unsigned int r = self->highScores()->insert(
            (unsigned int)pl->score(), (unsigned char)(self->levelIndex() + 1));
        if ((unsigned char)r < 0xff) {
            self->stateRef() = 6;
            if (self->musicOn() != 0)
                CDM_StopTrack(GG_CDAUDIO);
            self->nameEntry()->setMaxLength(0x0f);
            self->nameEntry()->setActive(1);
            self->nameEntry()->setCursor(0);
            self->debounceRef() = 0x0d;
            self->nameEntry()->setLastKey(0x0d);
            self->nameEntry()->setBuffer(
                self->highScores()->record(self->highScores()->lastRank())->name);
        } else {
            self->stateRef() = 0;
            Sim_RewindMenuStackToRootNode(self->menu());
            const char *theme = NULL;
            int setup = 1;
            if ((unsigned int)self->levelIndex() + 1 == (unsigned int)self->levelCount() &&
                pl->moveState() == 0) {
                if (self->field_0c() == 0) {
                    char name[256];
                    sprintf(name, GS_GAME_FINAL_DIR, self->gameFileName());
                    Sim_ParseLevelFiles(self, name);
                    Sim_PushMenuNodeOnStack(self->menu(), 0);
                    self->menu()->setNode(5);
                    theme = GS_GAME_FINAL;
                } else {
                    self->stateRef() = 7;
                    if (self->musicOn() != 0)
                        CDM_StopTrack(GG_CDAUDIO);
                    self->debounceRef() = 0x0d;
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
                self->debounceRef() = 0x0d;
                self->setTotalPlayTime((double)((long double)(unsigned long long)self->timeElapsed() +
                                        (long double)self->totalPlayTime()));
            }
        }
    }

    /* ─── ENTER after high-score name entry ─── */
    if (self->stateRef() == 6 && self->debounceRef() != 0x0d && KEY(0x0d) != 0) {
        self->highScores()->writeFile(GS_GAME_HSFILE, 0x4b);
        self->stateRef() = 0;
        Sim_RewindMenuStackToRootNode(self->menu());
        const char *theme = NULL;
        if ((unsigned int)self->levelIndex() + 1 == (unsigned int)self->levelCount()) {
            if (self->field_0c() == 0) {
                char name[256];
                sprintf(name, GS_GAME_FINAL_DIR, self->gameFileName());
                Sim_ParseLevelFiles(self, name);
                Sim_PushMenuNodeOnStack(self->menu(), 0);
                self->menu()->setNode(5);
                theme = GS_GAME_FINAL;
            } else {
                self->stateRef() = 7;
                if (self->musicOn() != 0)
                    CDM_StopTrack(GG_CDAUDIO);
                self->debounceRef() = 0x0d;
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
        self->debounceRef() = 0x0d;
    }

    if (pl->moveState() != 0 && pl->moveState() != 2)
        self->setCameraMode(2);
    if (KEY(self->debounceRef()) == 0)
        self->debounceRef() = 0;
    self->setField13cca8(0);
    self->setField13cc90(0);
    self->setField48b14(self->field_48b14() + 1);
    self->setTickCount(self->tickCount() + 1);
    return self->tickCount() & 0xffffff00u;
}
