/* GAMETICK_PLAN.md Band B reopened — the end-of-level score tally animation.
 *
 *   AnimateScoreTallyStages  0x0041a970   2 E8 sites (0x00415011, 0x00415021,
 *                                         both in GameTick; the return is
 *                                         never read)
 *
 * __fastcall, Game base in ECX, bare RET.  Callees: __ftol (inlined here),
 * TriggerPlayback (ours: CStatic_TriggerPlayback) and the GetAsyncKeyState
 * import (reached through hooks_GetAsyncKeyState, so replay input holds).
 *
 * Transcribed from the LISTING.  Points the decompile blurs:
 *
 *  - `now` is __ftol of the double at +0x170a54 (the dt accumulator), and
 *    elapsed = now - start as an UNSIGNED dword; every stage compares it
 *    unsigned (JC).
 *  - Stages 0 and 1 JMP to the totals; stage 2 does NOT -- both its paths
 *    fall into the stage-3 test, so a stage-2 completion runs stage 3 in the
 *    same call against the SAME elapsed value.  Preserved.
 *  - Stages 3..5 on completion re-evaluate __ftol(+0x170a54) for the new
 *    start rather than reusing `now` -- the same number, kept anyway.
 *  - Stage 5's count stores the same quotient into both fields (x1).
 *  - Stage 0..2 completions store the saved start from the FIRST __ftol.
 *  - The ENTER skip is debounced on +0x175517, not on a field of its own.
 *
 * Control: KAROO_SIM_FX=tallyfast -- every stage's duration is divided by
 * ten, so the tally finishes early.  A measured change: the frame at which
 * +0x517909 goes to 1 moves.
 */
#include <windows.h>
#include <string.h>
#include "static.h"
#include "log.h"
#include "game.h"
#include "scoretally.h"
#include "menutree.h"

extern "C" __declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);

static int s_fx = -1;

#define G32(o)  (*(unsigned int *)(B + (o)))
#define G8(o)   (*(unsigned char *)(B + (o)))

/* The CRT's __ftol: truncate toward zero into an __int64; the low dword is
 * what EAX carries. */
static unsigned int ftol_low(const unsigned char *B)
{
    double d = *((Game *)B)->clock();
    return (unsigned int)(long long)d;
}

static void tick_sound(unsigned char *B)
{
    CStaticSoundbuffer *s = ((Game *)B)->fixedSounds()->count;
    if (s != NULL)
        CStatic_TriggerPlayback(s, 0);
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_AnimateScoreTallyStages(Game *self)
{
    unsigned char *B = (unsigned char *)self;
    unsigned int now, el, lim, n;
    unsigned char st;

    if (s_fx < 0) {
        char b[32];
        DWORD k = GetEnvironmentVariableA("KAROO_SIM_FX", b, sizeof(b));
        s_fx = (k > 0 && k < sizeof(b) && strcmp(b, "tallyfast") == 0);
        if (s_fx)
            log_write("scoretally: KAROO_SIM_FX=tallyfast -- stages x0.1\n");
    }
#define LIM(x)  (s_fx ? (x) / 10 : (x))

    ScoreTally *T = ((Game *)B)->tally();

    now = ftol_low(B);
    el  = now - (unsigned int)T->stageStart;
    st  = T->stage;

    if (st == 0) {
        n = (unsigned int)T->count[TALLY_GEMS];
        lim = LIM((n * 5 + 5) * 10);
        if (el >= lim) {
            T->stageStart = (int)now;
            T->stage = 1;
            T->shownCount[TALLY_GEMS] = (int)n;
            T->shownScore[TALLY_GEMS] = T->score[TALLY_GEMS];
        } else {
            tick_sound(B);
            T->shownCount[TALLY_GEMS] = (int)(el / 50);
            T->shownScore[TALLY_GEMS] = (int)((el / 50) * 5);
        }
        goto totals;
    }
    if (st == 1) {
        n = (unsigned int)T->count[TALLY_SURPLUS];
        lim = LIM((n * 5 + 5) * 10);
        if (el >= lim) {
            T->stageStart = (int)now;
            T->stage = 2;
            T->shownCount[TALLY_SURPLUS] = (int)n;
            T->shownScore[TALLY_SURPLUS] = T->score[TALLY_SURPLUS];
        } else {
            tick_sound(B);
            T->shownCount[TALLY_SURPLUS] = (int)(el / 50);
            T->shownScore[TALLY_SURPLUS] = (int)((el / 50) * 10);
        }
        goto totals;
    }
    if (st == 2) {
        n = (unsigned int)T->count[TALLY_FOES];
        lim = LIM((n * 5 + 5) * 20);
        if (el >= lim) {
            T->stageStart = (int)now;
            T->stage = 3;
            T->shownScore[TALLY_FOES] = T->score[TALLY_FOES];
            T->shownCount[TALLY_FOES] = (int)n;
        } else {
            tick_sound(B);
            T->shownCount[TALLY_FOES] = (int)(el / 100);
            T->shownScore[TALLY_FOES] = (int)((el / 100) * 50);
        }
        /* no jump: falls into the stage-3 test with the same `el` */
    }

    st = T->stage;
    if (st == 3) {
        n = (unsigned int)T->count[TALLY_TIME];
        lim = LIM((n * 5 + 5) * 2);
        if (el >= lim) {
            T->stage = 4;
            T->stageStart = (int)ftol_low(B);
            T->shownCount[TALLY_TIME] = (int)n;
            T->shownScore[TALLY_TIME] = T->score[TALLY_TIME];
        } else {
            tick_sound(B);
            T->shownCount[TALLY_TIME] = (int)(el / 10);
            T->shownScore[TALLY_TIME] = (int)((el / 10) * 2);
        }
    } else if (st == 4) {
        n = (unsigned int)T->count[TALLY_ALLITEMS];
        lim = LIM((n * 5 + 5) * 20);
        if (el >= lim) {
            T->stage = 5;
            T->stageStart = (int)ftol_low(B);
            T->shownCount[TALLY_ALLITEMS] = (int)n;
            T->shownScore[TALLY_ALLITEMS] = T->score[TALLY_ALLITEMS];
        } else {
            tick_sound(B);
            T->shownCount[TALLY_ALLITEMS] = (int)(el / 100);
            T->shownScore[TALLY_ALLITEMS] = (int)((el / 100) * 5);
        }
    } else if (st == 5) {
        n = (unsigned int)T->count[TALLY_VITALITY];
        lim = LIM((n * 5 + 5) * 10);
        if (el >= lim) {
            T->stage = 6;
            T->stageStart = (int)ftol_low(B);
            T->shownCount[TALLY_VITALITY] = (int)n;
            T->shownScore[TALLY_VITALITY] = T->score[TALLY_VITALITY];
            ((Game *)B)->setTallyDone(1);
        } else {
            tick_sound(B);
            T->shownCount[TALLY_VITALITY] = (int)(el / 50);
            T->shownScore[TALLY_VITALITY] = (int)(el / 50);
        }
    }

totals:
    {
        /* Summed unsigned, in the original's order. */
        unsigned int t = (unsigned int)T->shownScore[TALLY_TIME]
                       + (unsigned int)T->shownScore[TALLY_FOES]
                       + (unsigned int)T->shownScore[TALLY_GEMS]
                       + (unsigned int)T->shownScore[TALLY_SURPLUS]
                       + (unsigned int)T->shownScore[TALLY_ALLITEMS]
                       + (unsigned int)T->shownScore[TALLY_VITALITY];
        T->shownLevelTotal = (int)t;
        T->shownGrandTotal = (int)((unsigned int)T->shownBase + t);
        if (((Game *)B)->debounce() != 0x0d && hooks_GetAsyncKeyState(0x0d) != 0)
            ((Game *)B)->setTallyDone(1);
        return (t & 0xffffff00u) | 0xffu;
    }
#undef LIM
}
