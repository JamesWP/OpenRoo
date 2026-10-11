/* The game clock (clock.cpp): the seconds since start, as the game reads it.
 *
 * The clock is not read, it is advanced: every clock_advance() ends a frame.
 * It runs the frame hook (the state log, the recorder, the end of a replay)
 * and counts the frame, and recordings are numbered by that count.  So each
 * call is part of what a recording replays: the frame driver makes one a
 * frame, and a level entry one more.
 *
 * DETERMINISM: with KAROO_FIXED_DT set, each call advances the clock by
 * exactly that many seconds, which makes the simulation fixed-step. */

#pragma once

/* Called at every frame boundary, before the time advances; none by
 * default. */
typedef void (*ClockFrameHook)(void);
void clock_setFrameHook(ClockFrameHook fn);

/* Starts the clock now rather than at the first advance. */
void clock_start(void);

/* Ends a frame (the hook runs, the frame count goes up) and returns the
 * clock in seconds. */
double clock_advance(void);

/* What clock_advance() last returned, without advancing: the frame driver
 * times the frame against it. */
double clock_previous_seconds(void);

/* The frame number: the count of clock_advance() calls.  The state log and
 * recordings number frames by it. */
unsigned clock_frame(void);
