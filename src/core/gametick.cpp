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
 *   callback  ReleaseScriptStreamBuffers 0x41e840 (jjscript.cpp and
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
#include "liftobject.h"
#include "slideobject.h"
#include "bridgeobject.h"
#include "breakabletile.h"
#include "bomb.h"
#include "foe.h"
#include "tilequery.h"

struct CStaticSoundbuffer;
struct ProgableControl;
struct CDM;

extern "C" {
__declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);
__declspec(dllexport) void __cdecl GameLog_LogMessage(void *self, int level, const char *fmt, ...);

__declspec(dllexport) void __attribute__((thiscall)) Sim_AcquireFixedSoundBuffersAndMaybeReport(void *self);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_InitLevelBasedSounds(void *self);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_PlayCDStuf(void *self, const char *caption);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_PlayCDStuf_2(void *self);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_FindThemeIndexByThemeName(void *self, const char *name);
__declspec(dllexport) void __attribute__((thiscall)) Sim_RestoreCheckpointStateBlocks(void *self);
__declspec(dllexport) void __attribute__((thiscall)) Sim_HandleKeypress(void *self);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_AnimateScoreTallyStages(void *self);
__declspec(dllexport) void __attribute__((thiscall)) Sim_PollTextEntryKeys(void *self, unsigned int phase);
__declspec(dllexport) void __attribute__((thiscall)) Sim_HandleTypedCheatCode(void *self);

__declspec(dllexport) void __attribute__((thiscall)) Sim_UpdatePlayerTileEffects(void *self);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_AcquireObjectSoundBuffersForIndex(void *self, unsigned int objArg);

__declspec(dllexport) void __attribute__((thiscall)) Sim_PushMenuNodeOnStack(void *self, unsigned int nodeArg);
__declspec(dllexport) void __attribute__((thiscall)) Sim_PopMenuNodeFromStack(void *self);
__declspec(dllexport) void __attribute__((thiscall)) Sim_RewindMenuStackToRootNode(void *self);

__declspec(dllexport) void __attribute__((thiscall)) Sim_ClearGameState(void *self);
__declspec(dllexport) void __attribute__((thiscall)) Sim_ParseLevelFiles(void *self, const char *name);
__declspec(dllexport) void __attribute__((thiscall)) Sim_SetupLevelObjects(void *self);
__declspec(dllexport) void __attribute__((thiscall)) Sim_RestoreTileGridFromSnapshot(void *self);

__declspec(dllexport) void __attribute__((thiscall)) Score_CalculateLevelScore(void *self, char endReason);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_InsertScoreIntoHighScoreTable(void *self, unsigned int score, unsigned char levelId);
__declspec(dllexport) int __attribute__((thiscall)) HighScore_WriteFile(void *self, const char *name, char key);

__declspec(dllexport) void __attribute__((thiscall)) ProgCtrl_Dispatch(ProgableControl *s, unsigned short game_state);
__declspec(dllexport) void __attribute__((thiscall)) CDM_StopTrack(CDM *self);

__declspec(dllexport) int __attribute__((thiscall)) CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
__declspec(dllexport) void __attribute__((thiscall)) CStatic_HaltPlayback(CStaticSoundbuffer *self);
__declspec(dllexport) void __attribute__((thiscall)) CStatic_Set3DPosition(CStaticSoundbuffer *self, float x, float y, float z, DWORD dwApply);
}

typedef void (__attribute__((fastcall)) *relstream_fn)(void *self);
#define ORIG_RELEASE_SCRIPT ((relstream_fn)0x0041e840)   /* named callback */

#define PROGCTRL    ((ProgableControl *)0x0046c298)
#define CDAUDIO     ((CDM *)0x004dc640)
#define GAMELOGGER  ((void *)0x0046c4c0)
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


#define STATE   G8(0x2ab58c)
#define DEB     G8(0x175517)
#define MENU    (B + 0x175518)
#define ACC     GD(0x170a54)

/* Tile addressing: idx = v + u*100, pitch 0x7f (worldstate.cpp). */
#define TIDX(u, v)  (((int)(v) + (int)(u) * 100) * 0x7f)

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
    int idx = TIDX(u, v);
    if (br->phase() == 0)
        G32(0x2ab7a4 + idx) = 1;
    else
        G32(0x2ab7a4 + idx) = 0;
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_GameTick(void *self, double dt, double now)
{
    unsigned char *B = (unsigned char *)self;

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "tickorder") == 0);
        if (s_fx)
            log_write("gametick: KAROO_SIM_FX=tickorder -- slides tick before lifts\n");
    }

    Sim_AcquireFixedSoundBuffersAndMaybeReport(B);
    if (G32(0x42254) == 0 && G32(0x173584) == 0) {
        Sim_InitLevelBasedSounds(B);
        G32(0x42254) = 1;
        if (STATE == 0 && G32(0x2aa156) != 0)
            Sim_PlayCDStuf_2(B + 0x2223f);
    }

    if (STATE == 7 && DEB != 0x0d && KEY(0x0d) != 0) {
        GameLog_LogMessage(GAMELOGGER, 1, F_GAMEEND);
        PostQuitMessage(1);
    }

    *(double *)(B + 0x170a4c) = now;
    if (STATE == 5) {
        G32(0x170a5c) = 0;
        G32(0x170a60) = 0;
    } else {
        *(double *)(B + 0x170a5c) = dt;
        ACC = (double)((long double)dt + (long double)ACC);
    }

    if (STATE == 0 || STATE == 5) {
        Sim_RestoreCheckpointStateBlocks(B);
        Sim_HandleKeypress(B);
    }

    if (STATE == 4) {
        if (G32(0x1960e6) != 0)
            Sim_RestoreCheckpointStateBlocks(B);
        if (G32(0x1964e3) == 0 || G32(0x1960e6) == 0)
            G8(0x28ab2d) = 2;
        if (DEB != 0x0d && KEY(0x0d) != 0) {
            G32(0x1964e3) = 0;
            G32(0x196086) = 0;
            ORIG_RELEASE_SCRIPT(B + 0x195735);
            ORIG_RELEASE_SCRIPT(B + 0x195735);
            STATE = 1;
            if (G8(0x4220b) == 0) {
                if (G32(0x2aa156) != 0)
                    Sim_PlayCDStuf(B + 0x2223f, (const char *)(B + 0x2ab69d));
                G8(0x2235a) = (unsigned char)Sim_FindThemeIndexByThemeName(
                    B + 0x2223f, (const char *)(B + 0x2ab69d));
            }
            G32(0x28ab29) = 0x40e00000;
            G8(0x28ab2d) = 0;
            G32(0x1753fa) = G32(0x170a54);
            G32(0x1753fe) = G32(0x170a58);
        }
    }

    if (G8(0x28ab2d) == 0)
        GF(0x28ab29) = camera_sway(now, GF(0x13cca4));

    if (STATE == 3) {
        Sim_HandleKeypress(B);
        Sim_AnimateScoreTallyStages(B);
    }
    if (STATE == 2)
        Sim_AnimateScoreTallyStages(B);
    if (STATE == 6)
        Sim_PollTextEntryKeys(B + 0x170a6d, (unsigned int)ftol80(ACC));

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
        G8(0x48b13) = STATE;
        Sim_RewindMenuStackToRootNode(MENU);
        G8(0x175534) = 0x1b;
        STATE = 5;
        G32(0x175524) = G32(0x170a4c);
        G32(0x175528) = G32(0x170a50);
        G32(0x17552c) = 1;
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
        unsigned char pct = completion_percent(G32(0x1752a1), G32(0x170a65));
        G8(0x170a64) = pct;
        if (pct > 100)
            G8(0x170a64) = 100;
    }

    Sim_UpdatePlayerTileEffects(B + 0x1751c9);

    if (STATE == 1) {
        if (G32(0x13cdb7) != 0)
            Sim_HandleTypedCheatCode(B);

        /* the runtime foe spawners, stride 0x15 from Game+0x20251 */
        for (int i = 0; i < (int)G16(0x42207); ++i) {
            unsigned char *E = B + 0x20251 + i * 0x15;
            long double since = (long double)ACC - (long double)*(double *)(E - 0x11);
            if (!(since > (long double)*(double *)(E - 0x08)))
                continue;
            signed char u = (signed char)E[-0x14], v = (signed char)E[-0x13];
            if (G8(0x2ab732 + TIDX(u, v)) == 0 && G8(0x174fd4) < E[0]) {
                unsigned char id = Foe::spawn((Game *)B, (unsigned char)u,
                                              (unsigned char)v, E[-0x12], 2,
                                              (unsigned char)(E[-0x09] + 100));
                Foe *foe = ((Game *)B)->foeSlot(id);
                if (((int)G8(0x4224d) + 1) % 15 == 0)
                    foe->setDropContents(7);
                else
                    foe->setDropContents(1);
                Sim_AcquireObjectSoundBuffersForIndex(B, id);
            }
            *(unsigned int *)(E - 0x11) = G32(0x170a54);
            *(unsigned int *)(E - 0x0d) = G32(0x170a58);
        }

        G32(0x2ab576) = G32(0x2ab57a);
        if (G8(0x1752e8) != 0) {
            G32(0x13cca8) = 0;
            G32(0x28ab29) = G32(0x13cca4);
            if (GP(0x175278) != NULL) CStatic_HaltPlayback((CStaticSoundbuffer *)GP(0x175278));
            if (GP(0x175274) != NULL) CStatic_HaltPlayback((CStaticSoundbuffer *)GP(0x175274));
            if (GP(0x175290) != NULL) CStatic_HaltPlayback((CStaticSoundbuffer *)GP(0x175290));
        }

        if (G32(0x1752b3) != 0) {
            if (G8(0x3215d) == 0) {
                G8(0x3215d) = (unsigned char)(G8(0x2ab571) + 10);
                G8(0x2ab571) = 1;
            }
        } else if (G8(0x3215d) >= 10) {
            G8(0x2ab571) = (unsigned char)(G8(0x3215d) - 10);
            G8(0x3215d) = 0;
        }

        /* last-seconds countdown: compared at 80 bits, stored at 64 */
        {
            static const double k001 = 0.001;              /* DAT_0045d368, a DOUBLE */
            long double lim = (long double)(unsigned long long)G32(0x2ab591) * 1000.0L;
            long double rem80 = (lim - (long double)(unsigned long long)G32(0x2ab595)) *
                                (long double)k001;
            double rem64 = (double)rem80;
            if (rem80 > 11.0L || G8(0x1752e8) == 3) {
                GD(0x175393) = 10.0;
            } else if ((long double)GD(0x175393) > (long double)rem64) {
                if (GP(0x13cc6c) != NULL)
                    CStatic_TriggerPlayback((CStaticSoundbuffer *)GP(0x13cc6c), 0);
                GD(0x175393) = crt_floor(rem64);
            }
        }
        if (G8(0x1752e8) == 0)
            ProgCtrl_Dispatch(PROGCTRL, (unsigned short)STATE);
    } else {
        G32(0x175237) = 0;
        G32(0x17523b) = G32(0x170a54);
        G32(0x17523f) = G32(0x170a58);
        ProgCtrl_Dispatch(PROGCTRL, 0);
        if (GP(0x13cc6c) != NULL)
            CStatic_HaltPlayback((CStaticSoundbuffer *)GP(0x13cc6c));
    }

    /* the player's bomb drop */
    if (G32(0x1752ad) != 0) {
        unsigned int timed = G32(0x175317);
        int spawn = 1, offset = 0;
        if (timed != 0) {
            long double since = (long double)ACC - (long double)GD(0x17530f);
            if (since < 50.0L)
                offset = 1;
            else
                spawn = 0;                 /* too late: flag stays set */
        }
        if (spawn) {
            if (offset)
                Bomb::spawn((Game *)B,(unsigned char)(G8(0x1751fa) - G8(0x175308)),
                                    (unsigned char)(G8(0x1751fb) - G8(0x175309)),
                                    (unsigned char)(G8(0x1751fc) - G8(0x17530a)),
                                    G8(0x1751dd));
            else
                Bomb::spawn((Game *)B,G8(0x1751fa), G8(0x1751fb), G8(0x1751fc),
                                    G8(0x1751dd));
            G32(0x1752ad) = 0;
        }
    }

    /* a switch the player stepped on */
    {
        unsigned char sw = G8(0x1752a0);
        if (sw < 0xff && ((Game *)B)->bridgeSlot(sw)->armed() == 0) {
            GameLog_LogMessage(GAMELOGGER, 1, F_SWITCH, (unsigned int)sw);
            trigger_switch_tile(B, G8(0x1752a0), GS8(0x1751fa), GS8(0x1751fb));
            BridgeObject *br = ((Game *)B)->bridgeSlot(G8(0x1752a0));
            br->arm(((Game *)B)->clock());
            br->playArmSound();
            Sim_MarkListedTilesBlockedByObject(B, G8(0x1752a0));
            G8(0x1752a0) = 0xff;
        }
    }

    if (G8(0x1752e8) != 0) {
        G8(0x28ab2d) = 2;
    } else if (STATE == 1) {
        G32(0x2ab595) += (unsigned int)ftol80(*(double *)(B + 0x170a5c));
        G32(0x170a65) += (unsigned int)ftol80(*(double *)(B + 0x170a5c));
    }

    if (G32(0x1752b3) != 0) {
        G8(0x28ab2d) = 0;
    } else if (G32(0x1752e9) != 0) {
        long double d = (long double)(int)G8(0x1752da) - (long double)(int)GS8(0x1751fc);
        if (d > 2.0L) {
            G8(0x28ab2d) = 1;
            G32(0x2ab580) = G32(0x1751ee);
            G32(0x2ab588) = G32(0x1751f6);
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
                Sim_MarkListedTilesBlockedByObject(B, (*slot)->switchSlot());
                (*slot)->clearSwitchSlot();
            }
        }

        int hold = (G32(0x1753af) == 0 && STATE == 1) ? 0 : 1;
        if (STATE == 3)
            hold = 1;
        if (G8(0x1752e8) != 0)
            hold = 1;
        (*slot)->chooseTarget(game, hold, G8(0x1751fa), G8(0x1751fb),
                              G8(0x17530b), G8(0x17530c), &tu, &tv);

        (*slot)->step(tu, tv);
        (*slot)->dropBomb(game);
        (*slot)->checkPlayerContact(&G8(0x1752e8),
                                    GF(0x1751ee), GF(0x1751f2), GF(0x1751f6));
        if ((*slot)->finishDespawn(B + 0x3e181c)) {
            Foe::remove(game, id);
            G8(0x4224d) = (unsigned char)(G8(0x4224d) + 1);
        }
    }

    /* ─── playing: camera follow, time-out, exit ─── */
    if (STATE == 1) {
        if (G32(0x1752e9) == 0 && G8(0x1752e8) == 0) {
            G32(0x2ab580) = G32(0x1751ee);
            G32(0x2ab584) = G32(0x1751f2);
            G32(0x2ab588) = G32(0x1751f6);
            G8(0x28ab2d) = 0;
        }
        if (G8(0x1752e8) != 3) {
            int t = GI32(0x2ab591) * 1000;
            if (t - GI32(0x2ab595) <= 0) {
                void *snd = GP(0x13cc5c);
                GI32(0x2ab595) = t;
                G8(0x1752e8) = 3;
                if (snd != NULL)
                    CStatic_TriggerPlayback((CStaticSoundbuffer *)snd, 0);
            }
        }
        if (GI32(0x175406) >= GI32(0x2ab723)) {
            if (G32(0x173b1a) == 0 && STATE != 3) {
                int r = (int)ftol80(ACC);
                void *snd = GP(0x13cc74 + (r % 3) * 4);
                if (snd != NULL)
                    CStatic_TriggerPlayback((CStaticSoundbuffer *)snd, 0);
                G32(0x173b1a) = 1;
            }
            G32(0x2ab7a4 + TIDX(GS8(0x17530b), GS8(0x17530c))) = 1;
            if (G8(0x1751fa) == G8(0x17530b) && G8(0x1751fb) == G8(0x17530c) &&
                G8(0x1751fc) == G8(0x17530d) && G32(0x1752e9) == 0 &&
                G8(0x1752e8) == 0) {
                G32(0x1752b8) = 1;
                if (G32(0x175317) == 0) {
                    if (GP(0x13cc70) != NULL)
                        CStatic_TriggerPlayback((CStaticSoundbuffer *)GP(0x13cc70), 0);
                    if ((unsigned int)G8(0x173583) + 1 == (unsigned int)G8(0x4215e)) {
                        STATE = 2;
                        if (G32(0x2aa156) != 0)
                            Sim_PlayCDStuf(B + 0x2223f, S_GAMEOVER);
                        Score_CalculateLevelScore(B, 3);
                        DEB = 0x0d;
                        GameLog_LogMessage(GAMELOGGER, 1, F_COMPLETED,
                                           (unsigned int)G8(0x173583) + 1,
                                           (unsigned int)G8(0x4215e));
                    } else {
                        STATE = 3;
                        G8(0x28ab2d) = 2;
                        Sim_RewindMenuStackToRootNode(MENU);
                        Sim_PopMenuNodeFromStack(MENU);
                        Sim_PushMenuNodeOnStack(MENU, 0x28);
                        G8(0x195734) = 0x28;
                        G32(0x175524) = G32(0x170a4c);
                        G32(0x175528) = G32(0x170a50);
                        G32(0x17552c) = 1;
                        G8(0x175535) = 0;
                        Score_CalculateLevelScore(B, (char)STATE);
                        G8(0x4220b) = 0;
                    }
                    GD(0x170a44) = (double)((long double)(unsigned long long)G32(0x2ab595) +
                                            (long double)GD(0x170a44));
                }
            }
        }
    }

    /* ─── ENTER handling: after a death, or on the game-over tally ─── */
    if (STATE != 2) {
        if (DEB != 0x0d && KEY(0x0d) != 0 && G8(0x1752e8) != 0 && STATE == 1) {
            G8(0x4220b) = (unsigned char)(G8(0x4220b) + 1);
            int lives = GI32(0x175402);
            int bonus = GI32(0x2ab599);
            int restart_tail = 1;
            if (lives > 0 && bonus == 0) {
                GI32(0x175402) = lives - 1;              /* the DEC at 0x4160d6 */
                Sim_RestoreTileGridFromSnapshot(B);
                Sim_SetupLevelObjects(B);
            } else if (lives <= 0 && bonus == 0) {
                STATE = 2;
                if (G32(0x2aa156) != 0)
                    Sim_PlayCDStuf(B + 0x2223f, S_GAMEOVER);
                Score_CalculateLevelScore(B, (char)STATE);
                DEB = 0x0d;
                restart_tail = 0;                        /* JMP 0x4162b3 */
            } else {
                G8(0x28ab2d) = 2;
                STATE = 3;
                if (G32(0x2aa156) != 0)
                    Sim_PlayCDStuf(B + 0x2223f, S_COMPLETE);
                Sim_RewindMenuStackToRootNode(MENU);
                Sim_PopMenuNodeFromStack(MENU);
                Sim_PushMenuNodeOnStack(MENU, 0x28);
                G8(0x195734) = 0x28;
                G32(0x175524) = G32(0x170a4c);
                G32(0x175528) = G32(0x170a50);
                G32(0x17552c) = 1;
                G8(0x175535) = 0;
                GI32(0x2ab595) = GI32(0x2ab591) * 1000;
                Score_CalculateLevelScore(B, (char)STATE);
                G8(0x4220b) = 0;
                GD(0x170a44) = (double)((long double)(unsigned long long)G32(0x2ab595) +
                                        (long double)GD(0x170a44));
                GameLog_LogMessage(GAMELOGGER, 2, F_GAMEDONE,
                                   (unsigned int)G8(0x173583), (unsigned int)G8(0x4215e));
                if ((unsigned int)G8(0x173583) == (unsigned int)G8(0x4215e) - 1) {
                    STATE = 2;
                    if (G32(0x2aa156) != 0)
                        Sim_PlayCDStuf(B + 0x2223f, S_GAMEOVER);
                }
                G8(0x4220b) = 0;
            }
            if (restart_tail) {
                G32(0x28ab29) = 0x40e00000;
                G8(0x28ab2d) = 0;
                DEB = 0x0d;
            }
        }
    } else if (DEB != 0x0d && KEY(0x0d) != 0 && G32(0x517909) != 0) {
        unsigned int r = Sim_InsertScoreIntoHighScoreTable(
            B + 0x13cdbb, G32(0x1753f5), (unsigned char)(G8(0x173583) + 1));
        if ((unsigned char)r < 0xff) {
            STATE = 6;
            if (G32(0x2aa156) != 0)
                CDM_StopTrack(CDAUDIO);
            G8(0x170a77) = 0x0f;
            G32(0x170a78) = 1;
            G8(0x170a76) = 0;
            DEB = 0x0d;
            G8(0x170a75) = 0x0d;
            *(unsigned char **)(B + 0x170a71) = B + 0x13cdc0 + G8(0x13cdbf) * 0x37;
        } else {
            STATE = 0;
            Sim_RewindMenuStackToRootNode(MENU);
            const char *theme = NULL;
            int setup = 1;
            if ((unsigned int)G8(0x173583) + 1 == (unsigned int)G8(0x4215e) &&
                G8(0x1752e8) == 0) {
                if (G32(0x0c) == 0) {
                    char name[256];
                    sprintf(name, F_FINALDIR, (const char *)(B + 0x4215f));
                    Sim_ParseLevelFiles(B, name);
                    Sim_PushMenuNodeOnStack(MENU, 0);
                    G8(0x195734) = 5;
                    theme = S_FINAL;
                } else {
                    STATE = 7;
                    if (G32(0x2aa156) != 0)
                        CDM_StopTrack(CDAUDIO);
                    DEB = 0x0d;
                }
            } else {
                Sim_ClearGameState(B);
                Sim_ParseLevelFiles(B, (const char *)(B + 0x48b18));
                theme = S_MAIN;
            }
            if (theme != NULL)
                G8(0x2235a) = (unsigned char)Sim_FindThemeIndexByThemeName(B + 0x2223f, theme);
            if (setup) {
                Sim_SetupLevelObjects(B);
                G32(0x1964e3) = 1;
                DEB = 0x0d;
                GD(0x170a44) = (double)((long double)(unsigned long long)G32(0x2ab595) +
                                        (long double)GD(0x170a44));
            }
        }
    }

    /* ─── ENTER after high-score name entry ─── */
    if (STATE == 6 && DEB != 0x0d && KEY(0x0d) != 0) {
        HighScore_WriteFile(B + 0x13cdbb, S_HSFILE, 0x4b);
        STATE = 0;
        Sim_RewindMenuStackToRootNode(MENU);
        const char *theme = NULL;
        if ((unsigned int)G8(0x173583) + 1 == (unsigned int)G8(0x4215e)) {
            if (G32(0x0c) == 0) {
                char name[256];
                sprintf(name, F_FINALDIR, (const char *)(B + 0x4215f));
                Sim_ParseLevelFiles(B, name);
                Sim_PushMenuNodeOnStack(MENU, 0);
                G8(0x195734) = 5;
                theme = S_FINAL;
            } else {
                STATE = 7;
                if (G32(0x2aa156) != 0)
                    CDM_StopTrack(CDAUDIO);
                DEB = 0x0d;
            }
        } else {
            Sim_ClearGameState(B);
            Sim_ParseLevelFiles(B, (const char *)(B + 0x48b18));
            theme = S_MAIN;
        }
        if (theme != NULL)
            G8(0x2235a) = (unsigned char)Sim_FindThemeIndexByThemeName(B + 0x2223f, theme);
        Sim_SetupLevelObjects(B);
        G32(0x1964e3) = 1;
        DEB = 0x0d;
    }

    if (G8(0x1752e8) != 0 && G8(0x1752e8) != 2)
        G8(0x28ab2d) = 2;
    if (KEY(DEB) == 0)
        DEB = 0;
    G32(0x13cca8) = 0;
    G32(0x13cc90) = 0;
    G32(0x48b14) = G32(0x48b14) + 1;
    G32(0x18) = G32(0x18) + 1;
    return G32(0x18) & 0xffffff00u;
}
