/* The game clock (clock.cpp): the seconds since start, as the game reads it.
 * DETERMINISM: with KAROO_FIXED_DT set, each read advances the clock by
 * exactly that many seconds, which makes the simulation fixed-step. */

#pragma once

/* Called at every frame boundary (every clock read), before the time is
 * read; none by default. */
typedef void (*ClockFrameHook)(void);
void clock_setFrameHook(ClockFrameHook fn);

/* The clock in seconds.  Each call also ends a frame and runs the per-frame
 * test hooks: RenderGameFrame calls it once a frame, level entry once more. */
double clock_seconds(void);

/* The seconds clock_seconds() returned last time.  RenderGameFrame reads it
 * before calling again, to time the frame. */
double clock_previous_seconds(void);

/* Starts the clock now rather than at the first read. */
  void   hooks_ClockInit(void);

/* clock_seconds(), for the game's callers. */
  double   hooks_ClockSeconds(void);

/* The frame number: the count of clock_seconds() calls.  The state log and
 * recordings number frames by it. */
unsigned clock_frame(void);

