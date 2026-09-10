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
    double d = *(const double *)(B + 0x170a54);
    return (unsigned int)(long long)d;
}

static void tick_sound(unsigned char *B)
{
    CStaticSoundbuffer *s = *(CStaticSoundbuffer **)(B + 0x13cc68);
    if (s != NULL)
        CStatic_TriggerPlayback(s, 0);
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_AnimateScoreTallyStages(void *self)
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

    now = ftol_low(B);
    el  = now - G32(0x14053f);
    st  = G8(0x14053e);

    if (st == 0) {
        n = G32(0x14051e);
        lim = LIM((n * 5 + 5) * 10);
        if (el >= lim) {
            G32(0x14053f) = now;
            G8(0x14053e) = 1;
            G32(0x1404dd) = n;
            G32(0x1404c1) = G32(0x140502);
        } else {
            tick_sound(B);
            G32(0x1404dd) = el / 50;
            G32(0x1404c1) = (el / 50) * 5;
        }
        goto totals;
    }
    if (st == 1) {
        n = G32(0x140522);
        lim = LIM((n * 5 + 5) * 10);
        if (el >= lim) {
            G32(0x14053f) = now;
            G8(0x14053e) = 2;
            G32(0x1404e1) = n;
            G32(0x1404c5) = G32(0x140506);
        } else {
            tick_sound(B);
            G32(0x1404e1) = el / 50;
            G32(0x1404c5) = (el / 50) * 10;
        }
        goto totals;
    }
    if (st == 2) {
        n = G32(0x14052a);
        lim = LIM((n * 5 + 5) * 20);
        if (el >= lim) {
            G32(0x14053f) = now;
            G8(0x14053e) = 3;
            G32(0x1404cd) = G32(0x14050e);
            G32(0x1404e9) = n;
        } else {
            tick_sound(B);
            G32(0x1404e9) = el / 100;
            G32(0x1404cd) = (el / 100) * 50;
        }
        /* no jump: falls into the stage-3 test with the same `el` */
    }

    st = G8(0x14053e);
    if (st == 3) {
        n = G32(0x140526);
        lim = LIM((n * 5 + 5) * 2);
        if (el >= lim) {
            G8(0x14053e) = 4;
            G32(0x14053f) = ftol_low(B);
            G32(0x1404e5) = n;
            G32(0x1404c9) = G32(0x14050a);
        } else {
            tick_sound(B);
            G32(0x1404e5) = el / 10;
            G32(0x1404c9) = (el / 10) * 2;
        }
    } else if (st == 4) {
        n = G32(0x14052e);
        lim = LIM((n * 5 + 5) * 20);
        if (el >= lim) {
            G8(0x14053e) = 5;
            G32(0x14053f) = ftol_low(B);
            G32(0x1404ed) = n;
            G32(0x1404d1) = G32(0x140512);
        } else {
            tick_sound(B);
            G32(0x1404ed) = el / 100;
            G32(0x1404d1) = (el / 100) * 5;
        }
    } else if (st == 5) {
        n = G32(0x140532);
        lim = LIM((n * 5 + 5) * 10);
        if (el >= lim) {
            G8(0x14053e) = 6;
            G32(0x14053f) = ftol_low(B);
            G32(0x1404f1) = n;
            G32(0x1404d5) = G32(0x140516);
            G32(0x517909) = 1;
        } else {
            tick_sound(B);
            G32(0x1404f1) = el / 50;
            G32(0x1404d5) = el / 50;
        }
    }

totals:
    {
        unsigned int t = G32(0x1404c9) + G32(0x1404cd) + G32(0x1404c1) +
                         G32(0x1404c5) + G32(0x1404d1) + G32(0x1404d5);
        G32(0x1404f5) = t;
        G32(0x1404f9) = G32(0x1404d9) + t;
        if (G8(0x175517) != 0x0d && hooks_GetAsyncKeyState(0x0d) != 0)
            G32(0x517909) = 1;
        return (t & 0xffffff00u) | 0xffu;
    }
#undef LIM
}
