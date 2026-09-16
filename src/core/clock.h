#pragma once
#include <windows.h>

/* Fixed-timestep clock — see REPLAY_PLAN.md Stage A.
 *
 * Replaces the game's QueryPerformanceCounter wrapper at 0x00404040, which
 * returns *absolute elapsed seconds* (not a delta) on the x87 stack.  With
 * KAROO_FIXED_DT set, the clock advances by exactly that many seconds per
 * call, making the whole simulation a fixed-timestep one.
 */
double clock_seconds(void);

/* Frame index — one per clock_seconds() call, i.e. one per rendered frame.
 * The shared frame number for the hash log, the state log and recordings. */
unsigned clock_frame(void);

/* The game's time() at 0x0045169a, ours (patch.py routes its call sites here).
 * Seconds; also stored through `out` when non-NULL.  The particle samplers
 * seed rand() from it, exactly where the originals called 0x0045169a. */
extern "C" int __cdecl hooks_GameTime(int *out);
