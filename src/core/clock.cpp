/* The game clock: the seconds since start, from QueryPerformanceCounter.
 * Every clock read is also a frame boundary, so the per-frame test hooks (the
 * determinism hash, the state and world logs, the autoplayer, the level
 * report, the menu driver and the recorder) run from here.
 *
 * The real clock keeps the game's quirks:
 *   - the first read returns 0.0 and only takes the tick baseline;
 *   - a counter that steps backwards (by less than 0x10000000) moves the
 *     baseline and adds no time;
 *   - the frequency is halved until it fits in 32 bits and is at most
 *     2000000, and the counter is shifted to match;
 *   - PRESERVED: a result identical 100001 reads in a row has 1.0 second added,
 *     a guard against a stalled counter.
 *
 * DETERMINISM: KAROO_FIXED_DT=<seconds> replaces the counter with a clock that
 * advances by exactly that much each read, so the game runs as fast as the
 * machine allows.  Recording and replay both need it. */

#include <time.h>
#include "clock.h"
#include "log.h"
#include "determinism.h"
#include "gamestate.h"
#include "worldstate.h"
#include "policy.h"
#include "menu.h"
#include "levelreport.h"
#include "launcher.h"
#include "record.h"
#include <stdlib.h>

static bool      g_started;  // set by the first read
static DWORD     g_last;     // the previous shifted tick
static double    g_accum;    // elapsed seconds

/* The previous read's result.  RenderGameFrame reads it before reading the
 * clock, and takes the frame's dt as the difference. */
static double g_prevClock;
static int       g_same;    // reads in a row with the same result
static BYTE      g_shift;   // the frequency's halvings
static double    g_period;  // seconds per shifted tick

/* > 0: the fixed step; 0: the real clock; -1: not yet read. */
static double    g_fixed_dt = -1.0;

/* Every LOG_EVERY reads, the virtual time is logged beside the wall time.
 * Under a fixed step the two drift apart, which shows the counter is not
 * being read. */
#define LOG_EVERY 600
static unsigned  g_calls;
static DWORD     g_wall0;

static void clock_init(void)
{
    LARGE_INTEGER freq;
    if (!QueryPerformanceFrequency(&freq))
        freq.QuadPart = 1000;

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

static bool g_replay_ended;

unsigned clock_frame(void) { return g_calls; }

double clock_seconds(void)
{
    if (g_fixed_dt < 0.0) clock_init();
    clock_log_progress();
    // The camera setup's read ends one extra, empty frame; that is the same
    // every run, so it does no harm to the hash.
    dethash_frame_end(g_accum);
    gamestate_tick();
    gamestate_deathdiff();
    worldstate_tick();
    policy_menu_tick();
    levelreport_tick();  // may set a menu goal, so it runs before menu_tick
    menu_tick();
    record_frame_boundary();

    // A replay ends on the recording's length, never on wall time.  The state
    // is dumped while the level is still live (teardown clears the score),
    // then the window is closed so the game shuts down normally.  An
    // autoplayer run outlives its recording, which only gets it into a level;
    // --auto-exit still bounds it.
    if (record_replaying() && record_replay_finished() && !g_replay_ended &&
        !policy_in_control(g_calls)) {
        g_replay_ended = true;
        gamestate_dump("replay-finished");
        launcher_end_run("replay finished");
    }

    if (g_fixed_dt > 0.0) {
        // The first read returns 0.0, as on the real clock.
        if (!g_started) { g_started = true; return g_accum; }
        g_accum += g_fixed_dt;
        g_prevClock = g_accum;  // RenderGameFrame's dt source
        return g_accum;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    DWORD cur = (DWORD)((ULONGLONG)now.QuadPart >> g_shift);

    if (!g_started) {
        g_last    = cur;
        g_started = true;
        return g_accum;
    }

    // A counter stepping backwards: move the baseline, add no time.
    if (cur <= g_last && (DWORD)(g_last - cur) < 0x10000000u) {
        g_last = cur;
        return g_accum;
    }

    DWORD delta = cur - g_last;
    g_last  = cur;
    g_accum = (double)delta * g_period + g_accum;

    if (g_accum == g_prevClock) {
        // The bump comes on the 100001st identical result.
        if (++g_same > 0x186a0) {
            g_accum += 1.0;
            g_same = 0;
        }
    } else {
        g_same = 0;
    }

    g_prevClock = g_accum;
    return g_accum;
}

/* DETERMINISM: time() seeds rand() for the level builder and the particle
 * samplers, and it is the only real-time input they have.  KAROO_SEED=<int>
 * makes it return that constant.  A replay needs both this and KAROO_FIXED_DT;
 * they fix independent sources. */

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
        return (int)time((time_t *)out);

    if (out) *out = g_seed;
    return g_seed;
}


/* time(), as the game calls it. */
int hooks_GameTime(int *out)
{
    return game_time(out);
}

/* Starts the clock now; a later call, or the first read, does nothing more. */
void hooks_ClockInit(void)
{
    if (g_fixed_dt < 0.0) clock_init();
}

/* clock_seconds(), for the game's callers. */
double hooks_ClockSeconds(void)
{
    return clock_seconds();
}


double clock_previous_seconds(void)
{
    return g_prevClock;
}
