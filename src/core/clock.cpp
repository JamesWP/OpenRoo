/* Monotonic-clock reimplementation (replaces 0x00404040).
 *
 * The original wraps QueryPerformanceCounter and returns the *accumulated*
 * elapsed time in seconds as a float10 on the x87 stack — both call sites
 * (0x00426E19 in UpdatePlayerCamera, 0x00426F9C in RenderGameFrame) multiply
 * the result by 1000.0 (0x0045d3d0) to get milliseconds, and RenderGameFrame
 * derives its per-frame dt by subtracting the previous frame's value.  A
 * third reference is a PUSH 0x404040 at 0x0042658b that registers it as a
 * callback, so patch.py redirects that too.
 *
 * All of the original's state (0x46c434 shift, 0x46c438 period, 0x46c444 last
 * tick, 0x46c448 accumulator, 0x46c450 previous, 0x46c440 stall counter,
 * 0x4645a4 first-call flag) is read and written *only* by 0x404040 itself and
 * its initialiser 0x00403fa0 — confirmed with get_xrefs_to on each — so
 * keeping our own copy here is self-contained and the original can be
 * UD2-stubbed.
 *
 * Behaviour preserved from the decompile, quirks included:
 *   - the first call returns 0.0 and only latches the tick baseline;
 *   - a backwards tick (cur <= last, and the gap < 0x10000000) is swallowed:
 *     the baseline moves but no time is added;
 *   - the frequency is halved until it fits in 32 bits and is <= 2000000
 *     (0x0045d2f0), and the low 32 bits of the counter are shifted to match;
 *   - if the accumulated value comes out bit-identical 100001 calls in a row,
 *     1.0 second (0x0045d2e8) is added to unstick it.  That last one is an
 *     anti-stall hack for broken HALs; it is reproduced rather than removed.
 *
 * KAROO_FIXED_DT=<seconds> replaces the counter entirely with a virtual clock
 * that advances by exactly that much per call.  Recording and replay must both
 * use it (see REPLAY_PLAN.md); the game then runs as fast as the CPU allows.
 */
#include "clock.h"
#include "log.h"
#include "determinism.h"
#include "gamestate.h"
#include <stdlib.h>

static bool      g_started;      /* first-call flag           (was 0x4645a4) */
static DWORD     g_last;         /* previous shifted tick     (was 0x46c444) */
static double    g_accum;        /* elapsed seconds           (was 0x46c448) */
static double    g_prev;         /* previous returned value   (was 0x46c450) */
static int       g_same;         /* identical-result run      (was 0x46c440) */
static BYTE      g_shift;        /* frequency shift           (was 0x46c434) */
static double    g_period;       /* seconds per shifted tick  (was 0x46c438) */

/* Fixed timestep: >0 enables it, 0 = real clock.  -1 = not yet read. */
static double    g_fixed_dt = -1.0;

/* Progress logging: every LOG_EVERY calls, report the virtual time against the
 * wall clock.  Under a fixed timestep the two diverge — that divergence is the
 * evidence that the game is no longer reading the real counter. */
#define LOG_EVERY 600
static unsigned  g_calls;
static DWORD     g_wall0;

static void clock_init(void)
{
    LARGE_INTEGER freq;
    if (!QueryPerformanceFrequency(&freq))
        freq.QuadPart = 1000;

    /* Mirrors 0x00403fa0: halve until the frequency fits in 32 bits and is
     * no larger than 2000000.0, counting the shifts. */
    g_shift = 0;
    while (freq.HighPart != 0 || (double)freq.LowPart > 2000000.0) {
        freq.QuadPart = (LONGLONG)((ULONGLONG)freq.QuadPart >> 1);
        g_shift++;
    }
    g_period = freq.LowPart ? 1.0 / (double)freq.LowPart : 0.0;

    char buf[32];
    g_fixed_dt = 0.0;
    if (GetEnvironmentVariableA("KAROO_FIXED_DT", buf, sizeof(buf))) {
        double dt = atof(buf);
        if (dt > 0.0) g_fixed_dt = dt;
    }
    g_wall0 = GetTickCount();
    log_write("clock: shift=%u period=%.12f fixed_dt=%.9f (%s)\n",
              (unsigned)g_shift, g_period, g_fixed_dt,
              g_fixed_dt > 0.0 ? "FIXED TIMESTEP" : "real clock");
}

static void clock_log_progress(void)
{
    if (++g_calls % LOG_EVERY) return;
    log_write("clock: call %u  virtual=%.3fs  wall=%.3fs\n",
              g_calls, g_accum, (double)(GetTickCount() - g_wall0) / 1000.0);
}

double clock_seconds(void)
{
    if (g_fixed_dt < 0.0) clock_init();
    clock_log_progress();
    /* One call per rendered frame from RenderGameFrame, so this is the frame
     * boundary for the Stage A2 checksum.  UpdatePlayerCamera's setup-time call
     * closes one extra (empty) frame; that is deterministic, so it is left. */
    dethash_frame_end(g_accum);
    gamestate_tick();
    gamestate_deathdiff();

    if (g_fixed_dt > 0.0) {
        /* Virtual clock.  First call returns 0.0, as the original does. */
        if (!g_started) { g_started = true; return g_accum; }
        g_accum += g_fixed_dt;
        return g_accum;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    DWORD cur = (DWORD)((ULONGLONG)now.QuadPart >> g_shift);

    if (!g_started) {
        g_last    = cur;
        g_started = true;
        return g_accum;                       /* 0.0 */
    }

    /* Backwards / stalled counter: move the baseline, add no time. */
    if (cur <= g_last && (DWORD)(g_last - cur) < 0x10000000u) {
        g_last = cur;
        return g_accum;
    }

    DWORD delta = cur - g_last;
    g_last  = cur;
    g_accum = (double)delta * g_period + g_accum;

    if (g_accum == g_prev) {
        /* Original: INC; CMP 0x186a0; JLE keep — so the bump fires on the
         * 100001st identical result, and only then is the counter reset. */
        if (++g_same > 0x186a0) {
            g_accum += 1.0;
            g_same = 0;
        }
    } else {
        g_same = 0;
    }

    g_prev = g_accum;
    return g_accum;
}

/* ── Deterministic seed (REPLAY_PLAN.md Stage A2) ──────────────────────────
 *
 * Stage A2's checksum showed two runs diverging on the very first emitted
 * particle, with emission *counts* identical and only pos/vel/life differing.
 * That traced to 0x00448FB0, the generator table builder, which draws from
 * rand() — MSVC's LCG at 0x0045167C (state 0x00469F38), unnamed in the binary,
 * which is why the plan's search_functions("rand") missed it.
 *
 * The LCG itself is deterministic.  The leak is the seed: 0x0045169A is time()
 * (GetLocalTime/GetSystemTime folded to epoch seconds), and it feeds srand at
 * five sites — SetupLevelObjects (0x0041672B), FUN_004479F0 (0x004479FA),
 * FUN_00448E80 (0x00448EC5, one-shot), CloneTypeTable (0x00449F22) and
 * FUN_0044B920 (0x0044B9AB).  All five xrefs of time() are srand seeding and
 * nothing else, so intercepting the one function pins every one of them.
 *
 * KAROO_SEED=<int> returns that constant instead of the wall clock.  Replay
 * needs it *and* KAROO_FIXED_DT — they fix independent sources.
 *
 * Unset, we call the original at 0x0045169A through.  It is deliberately NOT
 * UD2-stubbed: calling through keeps the real behaviour bit-exact, including
 * the timezone/DST globals it caches at 0x004E0910..0x004E0924, which a
 * reimplementation would leave stale.
 */
#define GAME_TIME_ORIGINAL ((int (__cdecl *)(int *))0x0045169A)

static int  g_seed      = 0;
static bool g_seed_set  = false;
static bool g_seed_read = false;

static int game_time(int *out)
{
    if (!g_seed_read) {
        char buf[32];
        g_seed_read = true;
        if (GetEnvironmentVariableA("KAROO_SEED", buf, sizeof(buf)) && buf[0]) {
            g_seed     = atoi(buf);
            g_seed_set = true;
        }
        log_write("clock: seed = %s (%d)\n",
                  g_seed_set ? "FIXED" : "wall clock", g_seed);
    }

    if (!g_seed_set)
        return GAME_TIME_ORIGINAL(out);

    if (out) *out = g_seed;
    return g_seed;
}

extern "C" {

/* Replaces 0x0045169A (time()) at all five srand call sites. */
__declspec(dllexport) int __cdecl hooks_GameTime(int *out)
{
    return game_time(out);
}

/* Replaces 0x00404040.  __cdecl, no arguments, double returned in st(0) —
 * exactly what both call sites expect (they FMUL the result straight away). */
__declspec(dllexport) double __cdecl hooks_ClockSeconds(void)
{
    return clock_seconds();
}

} // extern "C"
