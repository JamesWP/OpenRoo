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
/* The seconds clock_seconds() returned last time -- RenderGameFrame reads it
 * before calling again, to time the frame. */
double clock_previous_seconds(void);

/* The clock's initialiser 0x00403fa0 (one CALL site, 0x0042d4de): the eager
 * form of clock_seconds()'s lazy first-call init. */
extern "C" void __cdecl hooks_ClockInit(void);
/* Replaces 0x00404040: the seconds clock, as the game reads it. */
extern "C" __declspec(dllexport) double __cdecl hooks_ClockSeconds(void);

/* Frame index — one per clock_seconds() call, i.e. one per rendered frame.
 * The shared frame number for the hash log, the state log and recordings. */
unsigned clock_frame(void);

/* The game's time() at 0x0045169a, ours (patch.py routes its call sites here).
 * Seconds; also stored through `out` when non-NULL.  The particle samplers
 * seed rand() from it, exactly where the originals called 0x0045169a. */
extern "C" int __cdecl hooks_GameTime(int *out);
