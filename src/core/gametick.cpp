/* GAMETICK_PLAN.md Band C — Game::GameTick itself.
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


#define PROGCTRL    ((ProgableControl *)0x0046c298)
#define CDAUDIO     ((CDM *)0x004dc640)
#define GAMELOGGER  ((GameLogger *)0x0046c4c0)
#define F_GAMEEND   ((const char *)0x0046559c)   /* "GAME: GameActions - JJ_GAME_END" */
#define F_SWITCH    ((const char *)0x00465580)   /* "GAME: switch triggered %d" */
#define F_COMPLETED ((const char *)0x00465554)   /* "GAME: completed at level %d/%d" */
#define F_GAMEDONE  ((const char *)0x0046552c)   /* "GAME: game completed %d %d" */
#define S_GAMEOVER  ((const char *)0x00465574)
#define S_COMPLETE  ((const char *)0x00465548)
#define S_MAIN      ((const char *)0x00465510)
#define S_FINAL     ((const char *)0x00465518)
#define F_FINALDIR  ((const char *)0x00465520)   /* "Final\\%s" */
#define S_HSFILE    ((const char *)0x0046550c)   /* "jj" */

#define G8(o)   (*(unsigned char *)(B + (o)))
#define GS8(o)  (*(signed char *)(B + (o)))
#define G16(o)  (*(unsigned short *)(B + (o)))
#define G32(o)  (*(unsigned int *)(B + (o)))
#define GI32(o) (*(int *)(B + (o)))
#define GP(o)   (*(void **)(B + (o)))
#define GD(o)   (*(double *)(B + (o)))
#define GF(o)   (*(float *)(B + (o)))
#define KEY(k)  hooks_GetAsyncKeyState(k)


#define STATE   (((Game *)B)->stateRef())
#define DEB     (((Game *)B)->debounceRef())
#define MENU    (((Game *)B)->menu())
#define ACC     (*((Game *)B)->clock())

/* The level map and its tiles (levelmap.h). */
#define MAP     (((Game *)B)->map())

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

static void trigger_switch_tile(unsigned char *B, unsigned char sw, int u, int v)
{
    BridgeObject *br = ((Game *)B)->bridgeSlot(sw);
    Tile *t = MAP->tile(u, v);
    if (br->phase() == 0)
        t->setField217(1);
    else
        t->setField217(0);
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_GameTick(Game *self, double dt, double now)
{
    unsigned char *B = (unsigned char *)self;
    Player *pl = ((Game *)B)->player();

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "tickorder") == 0);
        if (s_fx)
            log_write("gametick: KAROO_SIM_FX=tickorder -- slides tick before lifts\n");
    }

    Sim_AcquireFixedSoundBuffersAndMaybeReport((Game *)B);
    if (((Game *)B)->levelSoundsReady() == 0 && ((Game *)B)->field_173584() == 0) {
        Sim_InitLevelBasedSounds((Game *)B);
        ((Game *)B)->setLevelSoundsReady(1);
        if (STATE == 0 && ((Game *)B)->musicOn() != 0)
            ((Game *)B)->cdThemes()->replay();
    }

    if (STATE == 7 && DEB != 0x0d && KEY(0x0d) != 0) {
        GameLog_LogMessage(GAMELOGGER, 1, F_GAMEEND);
        PostQuitMessage(1);
    }

    ((Game *)B)->setLastTickTime(now);
    if (STATE == 5) {
        ((Game *)B)->tickStep()->value = 0.0;    /* two zero dwords: +0.0 */
    } else {
        ((Game *)B)->tickStep()->value = dt;
        ACC = (double)((long double)dt + (long double)ACC);
    }

    if (STATE == 0 || STATE == 5) {
        Sim_RestoreCheckpointStateBlocks((Game *)B);
        Sim_HandleKeypress((Game *)B);
    }

    if (STATE == 4) {
        ScriptPlayer *sp = ((Game *)B)->scriptPlayer();
        if (sp->loaded() != 0)
            Sim_RestoreCheckpointStateBlocks((Game *)B);
        if (sp->running() == 0 || sp->loaded() == 0)
            ((Game *)B)->setCameraMode(2);
        if (DEB != 0x0d && KEY(0x0d) != 0) {
            sp->setRunning(0);
            sp->setSplineActive(0);
            sp->releaseStreams();
            sp->releaseStreams();
            STATE = 1;
            if (((Game *)B)->restartCount() == 0) {
                if (((Game *)B)->musicOn() != 0)
                    ((Game *)B)->cdThemes()->play(MAP->mapName());
                ((Game *)B)->cdThemes()->setCurrentTrack((unsigned char)((Game *)B)->cdThemes()->findThemeIndex(MAP->mapName()));
            }
            ((Game *)B)->setCameraDistance(7.0f);
            ((Game *)B)->setCameraMode(0);
            pl->setField231(*((Game *)B)->clock());
        }
    }

    if (((Game *)B)->cameraMode() == 0)
        ((Game *)B)->setCameraDistance(camera_sway(now, ((Game *)B)->field_13cca4()));

    if (STATE == 3) {
        Sim_HandleKeypress((Game *)B);
        Sim_AnimateScoreTallyStages((Game *)B);
    }
    if (STATE == 2)
        Sim_AnimateScoreTallyStages((Game *)B);
    if (STATE == 6)
        ((Game *)B)->nameEntry()->poll((unsigned int)ftol80(ACC));

    Game *game = (Game *)B;
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

    if ((STATE == 1 || STATE == 4) && DEB != 0x1b && KEY(0x1b) != 0) {
        ((Game *)B)->setStateBeforeMenu(STATE);
        Sim_RewindMenuStackToRootNode(MENU);
        ((Game *)B)->menu()->setLastKey(0x1b);
        STATE = 5;
        ((Game *)B)->menu()->setLockStart(((Game *)B)->lastTickTime());
        ((Game *)B)->menu()->setLock(1);
        DEB = 0x1b;
    }

    /* enemies: count re-read each pass; no step-back after a removal */
    for (int i = 0; i < (int)((Game *)B)->bombCount(); ++i) {
        ((Game *)B)->bombSlot(((Game *)B)->bombId(i))->tick();
        unsigned char id = ((Game *)B)->bombId(i);
        if (((Game *)B)->bombSlot(id)->removeRequested() != 0)
            Bomb::remove((Game *)B, id);
    }

    {
        unsigned char pct = completion_percent((unsigned int)pl->fieldD8(), ((Game *)B)->field_170a65());
        ((Game *)B)->setVitalityPercent(pct);
        if (pct > 100)
            ((Game *)B)->setVitalityPercent(100);
    }

    pl->updateTileEffects();

    if (STATE == 1) {
        if (((Game *)B)->cheatEntry()->active() != 0)
            Sim_HandleTypedCheatCode((Game *)B);

        /* the runtime foe spawners (levelcensus.h); the original walks a
         * pointer at each record's +0x14, Game+0x20251 + i*0x15 */
        for (int i = 0; i < (int)((Game *)B)->census()->timed; ++i) {
            TimedSpawner *E = ((Game *)B)->timedSpawner((unsigned)i);
            long double since = (long double)ACC - (long double)E->lastSpawn;
            if (!(since > (long double)E->interval))
                continue;
            signed char u = (signed char)E->u, v = (signed char)E->v;
            if (MAP->tile(u, v)->field1a5() == 0 &&((Game *)B)->foeCount() < E->maxFoes) {
                unsigned char id = Foe::spawn((Game *)B, (unsigned char)u,
                                              (unsigned char)v, E->height, 2,
                                              (unsigned char)(E->field_0b + 100));
                Foe *foe = ((Game *)B)->foeSlot(id);
                if (((int)((Game *)B)->foesKilled() + 1) % 15 == 0)
                    foe->setDropContents(7);
                else
                    foe->setDropContents(1);
                Sim_AcquireObjectSoundBuffersForIndex((Game *)B, id);
            }
            /* The spawner's last-spawn time (read as a double above), set
             * from the clock -- two dword MOVs in the original, one double. */
            E->lastSpawn = *((Game *)B)->clock();
        }

        ((Game *)B)->config()->setField20a48Bits(((Game *)B)->config()->field20a4cBits());
        if (pl->moveState() != 0) {
            ((Game *)B)->setField13cca8(0);
            ((Game *)B)->setCameraDistance(((Game *)B)->field_13cca4());
            if (pl->soundAf() != NULL) CStatic_HaltPlayback(pl->soundAf());
            if (pl->soundAb() != NULL) CStatic_HaltPlayback(pl->soundAb());
            if (pl->soundC7() != NULL) CStatic_HaltPlayback(pl->soundC7());
        }

        if ((unsigned int)pl->fieldEa() != 0) {
            if (((Game *)B)->parkedCameraOption() == 0) {
                ((Game *)B)->setParkedCameraOption((unsigned char)(((Game *)B)->cameraTurnsWithPlayer() + 10));
                ((Game *)B)->setCameraTurnsWithPlayer(1);
            }
        } else if (((Game *)B)->parkedCameraOption() >= 10) {
            ((Game *)B)->setCameraTurnsWithPlayer((unsigned char)(((Game *)B)->parkedCameraOption() - 10));
            ((Game *)B)->setParkedCameraOption(0);
        }

        /* last-seconds countdown: compared at 80 bits, stored at 64 */
        {
            static const double k001 = 0.001;              /* DAT_0045d368, a DOUBLE */
            long double lim = (long double)(unsigned long long)(unsigned int)((Game *)B)->timeLimit() * 1000.0L;
            long double rem80 = (lim - (long double)(unsigned long long)((Game *)B)->timeElapsed()) *
                                (long double)k001;
            double rem64 = (double)rem80;
            if (rem80 > 11.0L || pl->moveState() == 3) {
                pl->setField1ca(10.0);
            } else if ((long double)pl->field1ca() > (long double)rem64) {
                if (((Game *)B)->fixedSounds()->lastSeconds != NULL)
                    CStatic_TriggerPlayback(((Game *)B)->fixedSounds()->lastSeconds, 0);
                pl->setField1ca(crt_floor(rem64));
            }
        }
        if (pl->moveState() == 0)
            ProgCtrl_Dispatch(PROGCTRL, (unsigned short)STATE);
    } else {
        pl->setField6e(0);
        pl->setField72(*((Game *)B)->clock());
        ProgCtrl_Dispatch(PROGCTRL, 0);
        if (((Game *)B)->fixedSounds()->lastSeconds != NULL)
            CStatic_HaltPlayback(((Game *)B)->fixedSounds()->lastSeconds);
    }

    /* the player's bomb drop */
    if ((unsigned int)pl->fieldE4() != 0) {
        unsigned int timed = (unsigned int)pl->field14e();
        int spawn = 1, offset = 0;
        if (timed != 0) {
            long double since = (long double)ACC - (long double)pl->field146();
            if (since < 50.0L)
                offset = 1;
            else
                spawn = 0;                 /* too late: flag stays set */
        }
        if (spawn) {
            if (offset)
                Bomb::spawn((Game *)B,(unsigned char)((unsigned char)pl->cellU() - (unsigned char)pl->field13f()),
                                    (unsigned char)((unsigned char)pl->cellV() - (unsigned char)pl->field140()),
                                    (unsigned char)((unsigned char)pl->heightCell() - (unsigned char)pl->field141()),
                                    pl->facing());
            else
                Bomb::spawn((Game *)B,(unsigned char)pl->cellU(), (unsigned char)pl->cellV(), (unsigned char)pl->heightCell(),
                                    pl->facing());
            pl->setFieldE4(0);
        }
    }

    /* a switch the player stepped on */
    {
        unsigned char sw = pl->switchSlot();
        if (sw < 0xff && ((Game *)B)->bridgeSlot(sw)->armed() == 0) {
            GameLog_LogMessage(GAMELOGGER, 1, F_SWITCH, (unsigned int)sw);
            trigger_switch_tile(B, pl->switchSlot(), pl->cellU(), pl->cellV());
            BridgeObject *br = ((Game *)B)->bridgeSlot(pl->switchSlot());
            br->arm(((Game *)B)->clock());
            br->playArmSound();
            Sim_MarkListedTilesBlockedByObject((Game *)B, pl->switchSlot());
            pl->setSwitchSlot(0xff);
        }
    }

    if (pl->moveState() != 0) {
        ((Game *)B)->setCameraMode(2);
    } else if (STATE == 1) {
        ((Game *)B)->setTimeElapsed(((Game *)B)->timeElapsed()
            + (unsigned int)ftol80(((Game *)B)->tickStep()->value));
        ((Game *)B)->setField170a65(((Game *)B)->field_170a65()
            + (unsigned int)ftol80(((Game *)B)->tickStep()->value));
    }

    if ((unsigned int)pl->fieldEa() != 0) {
        ((Game *)B)->setCameraMode(0);
    } else if ((unsigned int)pl->field120() != 0) {
        long double d = (long double)(int)pl->field111() - (long double)(int)pl->heightCell();
        if (d > 2.0L) {
            ((Game *)B)->setCameraMode(1);
            ((Game *)B)->setCameraEye(0, pl->posU());
            ((Game *)B)->setCameraEye(2, pl->posV());
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
                GameLog_LogMessage(GAMELOGGER, 1, F_SWITCH, (unsigned int)sw);
                trigger_switch_tile(B, (*slot)->switchSlot(), (*slot)->cellU(), (*slot)->cellV());
                game->bridgeSlot((*slot)->switchSlot())->arm(game->clock());
                Sim_MarkListedTilesBlockedByObject((Game *)B, (*slot)->switchSlot());
                (*slot)->clearSwitchSlot();
            }
        }

        int hold = ((unsigned int)pl->field1e6() == 0 && STATE == 1) ? 0 : 1;
        if (STATE == 3)
            hold = 1;
        if (pl->moveState() != 0)
            hold = 1;
        (*slot)->chooseTarget(game, hold, (unsigned char)pl->cellU(), (unsigned char)pl->cellV(),
                              pl->field142(), pl->field143(), &tu, &tv);

        (*slot)->step(tu, tv);
        (*slot)->dropBomb(game);
        (*slot)->checkPlayerContact(pl->moveStateRef(),
                                    pl->posU(), pl->posY(), pl->posV());
        if ((*slot)->finishDespawn(MAP)) {
            Foe::remove(game, id);
            ((Game *)B)->setFoesKilled((unsigned char)(((Game *)B)->foesKilled() + 1));
        }
    }

    /* ─── playing: camera follow, time-out, exit ─── */
    if (STATE == 1) {
        if ((unsigned int)pl->field120() == 0 && pl->moveState() == 0) {
            ((Game *)B)->setCameraEye(0, pl->posU());
            ((Game *)B)->setCameraEye(1, pl->posY());
            ((Game *)B)->setCameraEye(2, pl->posV());
            ((Game *)B)->setCameraMode(0);
        }
        if (pl->moveState() != 3) {
            int t = ((Game *)B)->timeLimit() * 1000;
            if (t - (int)((Game *)B)->timeElapsed() <= 0) {
                void *snd = ((Game *)B)->fixedSounds()->timeOut;
                ((Game *)B)->setTimeElapsed((unsigned int)t);
                pl->setMoveState(3);
                if (snd != NULL)
                    CStatic_TriggerPlayback((CStaticSoundbuffer *)snd, 0);
            }
        }
        if (pl->gemsCollected() >= ((Game *)B)->gemsRequired()) {
            if (((Game *)B)->field_173b1a() == 0 && STATE != 3) {
                int r = (int)ftol80(ACC);
                void *snd = ((Game *)B)->fixedSounds()->crystalBank[r % 3];
                if (snd != NULL)
                    CStatic_TriggerPlayback((CStaticSoundbuffer *)snd, 0);
                ((Game *)B)->setField173b1a(1);
            }
            MAP->tile((signed char)pl->field142(), (signed char)pl->field143())->setField217(1);
            if ((unsigned char)pl->cellU() == pl->field142() && (unsigned char)pl->cellV() == pl->field143() &&
                (unsigned char)pl->heightCell() == pl->field144() && (unsigned int)pl->field120() == 0 &&
                pl->moveState() == 0) {
                pl->setFieldEf(1);
                if ((unsigned int)pl->field14e() == 0) {
                    if (((Game *)B)->fixedSounds()->levelCompleted != NULL)
                        CStatic_TriggerPlayback(((Game *)B)->fixedSounds()->levelCompleted, 0);
                    if ((unsigned int)((Game *)B)->levelIndex() + 1 == (unsigned int)((Game *)B)->levelCount()) {
                        STATE = 2;
                        if (((Game *)B)->musicOn() != 0)
                            ((Game *)B)->cdThemes()->play(S_GAMEOVER);
                        Score_CalculateLevelScore((Game *)B, 3);
                        DEB = 0x0d;
                        GameLog_LogMessage(GAMELOGGER, 1, F_COMPLETED,
                                           (unsigned int)((Game *)B)->levelIndex() + 1,
                                           (unsigned int)((Game *)B)->levelCount());
                    } else {
                        STATE = 3;
                        ((Game *)B)->setCameraMode(2);
                        Sim_RewindMenuStackToRootNode(MENU);
                        Sim_PopMenuNodeFromStack(MENU);
                        Sim_PushMenuNodeOnStack(MENU, 0x28);
                        ((Game *)B)->menu()->setNode(0x28);
                        ((Game *)B)->menu()->setLockStart(((Game *)B)->lastTickTime());
                        ((Game *)B)->menu()->setLock(1);
                        ((Game *)B)->menu()->setCursor(0);
                        Score_CalculateLevelScore((Game *)B, (char)STATE);
                        ((Game *)B)->setRestartCount(0);
                    }
                    ((Game *)B)->setTotalPlayTime((double)((long double)(unsigned long long)((Game *)B)->timeElapsed() +
                                            (long double)((Game *)B)->totalPlayTime()));
                }
            }
        }
    }

    /* ─── ENTER handling: after a death, or on the game-over tally ─── */
    if (STATE != 2) {
        if (DEB != 0x0d && KEY(0x0d) != 0 && pl->moveState() != 0 && STATE == 1) {
            ((Game *)B)->setRestartCount((unsigned char)(((Game *)B)->restartCount() + 1));
            int lives = pl->field239();
            int bonus = (int)MAP->bonus();
            int restart_tail = 1;
            if (lives > 0 && bonus == 0) {
                pl->setField239(lives - 1);              /* the DEC at 0x4160d6 */
                Sim_RestoreTileGridFromSnapshot((Game *)B);
                Sim_SetupLevelObjects((Game *)B);
            } else if (lives <= 0 && bonus == 0) {
                STATE = 2;
                if (((Game *)B)->musicOn() != 0)
                    ((Game *)B)->cdThemes()->play(S_GAMEOVER);
                Score_CalculateLevelScore((Game *)B, (char)STATE);
                DEB = 0x0d;
                restart_tail = 0;                        /* JMP 0x4162b3 */
            } else {
                ((Game *)B)->setCameraMode(2);
                STATE = 3;
                if (((Game *)B)->musicOn() != 0)
                    ((Game *)B)->cdThemes()->play(S_COMPLETE);
                Sim_RewindMenuStackToRootNode(MENU);
                Sim_PopMenuNodeFromStack(MENU);
                Sim_PushMenuNodeOnStack(MENU, 0x28);
                ((Game *)B)->menu()->setNode(0x28);
                ((Game *)B)->menu()->setLockStart(((Game *)B)->lastTickTime());
                ((Game *)B)->menu()->setLock(1);
                ((Game *)B)->menu()->setCursor(0);
                ((Game *)B)->setTimeElapsed((unsigned int)(((Game *)B)->timeLimit() * 1000));
                Score_CalculateLevelScore((Game *)B, (char)STATE);
                ((Game *)B)->setRestartCount(0);
                ((Game *)B)->setTotalPlayTime((double)((long double)(unsigned long long)((Game *)B)->timeElapsed() +
                                        (long double)((Game *)B)->totalPlayTime()));
                GameLog_LogMessage(GAMELOGGER, 2, F_GAMEDONE,
                                   (unsigned int)((Game *)B)->levelIndex(), (unsigned int)((Game *)B)->levelCount());
                if ((unsigned int)((Game *)B)->levelIndex() == (unsigned int)((Game *)B)->levelCount() - 1) {
                    STATE = 2;
                    if (((Game *)B)->musicOn() != 0)
                        ((Game *)B)->cdThemes()->play(S_GAMEOVER);
                }
                ((Game *)B)->setRestartCount(0);
            }
            if (restart_tail) {
                ((Game *)B)->setCameraDistance(7.0f);
                ((Game *)B)->setCameraMode(0);
                DEB = 0x0d;
            }
        }
    } else if (DEB != 0x0d && KEY(0x0d) != 0 && ((Game *)B)->tallyDone() != 0) {
        unsigned int r = ((Game *)B)->highScores()->insert(
            (unsigned int)pl->field22c(), (unsigned char)(((Game *)B)->levelIndex() + 1));
        if ((unsigned char)r < 0xff) {
            STATE = 6;
            if (((Game *)B)->musicOn() != 0)
                CDM_StopTrack(CDAUDIO);
            ((Game *)B)->nameEntry()->setMaxLength(0x0f);
            ((Game *)B)->nameEntry()->setActive(1);
            ((Game *)B)->nameEntry()->setCursor(0);
            DEB = 0x0d;
            ((Game *)B)->nameEntry()->setLastKey(0x0d);
            ((Game *)B)->nameEntry()->setBuffer(
                ((Game *)B)->highScores()->record(((Game *)B)->highScores()->lastRank())->name);
        } else {
            STATE = 0;
            Sim_RewindMenuStackToRootNode(MENU);
            const char *theme = NULL;
            int setup = 1;
            if ((unsigned int)((Game *)B)->levelIndex() + 1 == (unsigned int)((Game *)B)->levelCount() &&
                pl->moveState() == 0) {
                if (((Game *)B)->field_0c() == 0) {
                    char name[256];
                    sprintf(name, F_FINALDIR, ((Game *)B)->gameFileName());
                    Sim_ParseLevelFiles((Game *)B, name);
                    Sim_PushMenuNodeOnStack(MENU, 0);
                    ((Game *)B)->menu()->setNode(5);
                    theme = S_FINAL;
                } else {
                    STATE = 7;
                    if (((Game *)B)->musicOn() != 0)
                        CDM_StopTrack(CDAUDIO);
                    DEB = 0x0d;
                }
            } else {
                Sim_ClearGameState((Game *)B);
                Sim_ParseLevelFiles((Game *)B, ((Game *)B)->menuLevelName());
                theme = S_MAIN;
            }
            if (theme != NULL)
                ((Game *)B)->cdThemes()->setCurrentTrack((unsigned char)((Game *)B)->cdThemes()->findThemeIndex(theme));
            if (setup) {
                Sim_SetupLevelObjects((Game *)B);
                ((Game *)B)->scriptPlayer()->setRunning(1);
                DEB = 0x0d;
                ((Game *)B)->setTotalPlayTime((double)((long double)(unsigned long long)((Game *)B)->timeElapsed() +
                                        (long double)((Game *)B)->totalPlayTime()));
            }
        }
    }

    /* ─── ENTER after high-score name entry ─── */
    if (STATE == 6 && DEB != 0x0d && KEY(0x0d) != 0) {
        ((Game *)B)->highScores()->writeFile(S_HSFILE, 0x4b);
        STATE = 0;
        Sim_RewindMenuStackToRootNode(MENU);
        const char *theme = NULL;
        if ((unsigned int)((Game *)B)->levelIndex() + 1 == (unsigned int)((Game *)B)->levelCount()) {
            if (((Game *)B)->field_0c() == 0) {
                char name[256];
                sprintf(name, F_FINALDIR, ((Game *)B)->gameFileName());
                Sim_ParseLevelFiles((Game *)B, name);
                Sim_PushMenuNodeOnStack(MENU, 0);
                ((Game *)B)->menu()->setNode(5);
                theme = S_FINAL;
            } else {
                STATE = 7;
                if (((Game *)B)->musicOn() != 0)
                    CDM_StopTrack(CDAUDIO);
                DEB = 0x0d;
            }
        } else {
            Sim_ClearGameState((Game *)B);
            Sim_ParseLevelFiles((Game *)B, ((Game *)B)->menuLevelName());
            theme = S_MAIN;
        }
        if (theme != NULL)
            ((Game *)B)->cdThemes()->setCurrentTrack((unsigned char)((Game *)B)->cdThemes()->findThemeIndex(theme));
        Sim_SetupLevelObjects((Game *)B);
        ((Game *)B)->scriptPlayer()->setRunning(1);
        DEB = 0x0d;
    }

    if (pl->moveState() != 0 && pl->moveState() != 2)
        ((Game *)B)->setCameraMode(2);
    if (KEY(DEB) == 0)
        DEB = 0;
    ((Game *)B)->setField13cca8(0);
    ((Game *)B)->setField13cc90(0);
    ((Game *)B)->setField48b14(((Game *)B)->field_48b14() + 1);
    ((Game *)B)->setTickCount(((Game *)B)->tickCount() + 1);
    return ((Game *)B)->tickCount() & 0xffffff00u;
}
