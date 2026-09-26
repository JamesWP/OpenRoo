#pragma once
#include <windows.h>

/* Input recording and replay — REPLAY_PLAN.md Stages C and D.
 *
 * All gameplay input reaches the game through exactly two paths, and both are
 * ours already:
 *   1. ProgableControl::DispatchInputActions — a 256-byte DirectInput scancode
 *      array, read once per frame (src/input/progctrl.cpp).
 *   2. GetAsyncKeyState — 16 call sites, all through one IAT slot
 *      (0x0045D1B0), carrying ENTER/ESC decisions the dispatcher never sees.
 *
 * Recording captures both per frame; replay feeds them back in place of the
 * real devices.  A recording is only meaningful alongside the Stage A/A2
 * determinism fixes, so KAROO_FIXED_DT and KAROO_SEED are written into the
 * header and checked on replay.
 */

bool record_recording(void);
bool record_replaying(void);

/* Frame boundary: flush the recorded frame, or load the next replay frame.
 * Called from clock_seconds(), before the game ticks. */
void record_frame_boundary(void);

/* Scancode array.  Recording stores what the device returned; replay
 * overwrites the buffer with what was recorded and returns true. */
void record_keys(unsigned short game_state, const BYTE *keys);
bool replay_keys(unsigned short *game_state, BYTE *keys);

/* GetAsyncKeyState.  Recording notes the query and its answer; replay answers
 * from the recording. */
void record_async(int vkey, SHORT value);
bool replay_async(int vkey, SHORT *value);

/* True once the replay file is exhausted — the harness ends the run here. */
bool record_replay_finished(void);

/* The GetAsyncKeyState replacement itself (record.cpp).  patch.py redirects
 * every one of the game's 16 call sites here, and our own replacements of
 * those callers call it directly -- declared once, by its owner, rather than
 * in each of the five files that poll keys (COHESION_PLAN.md template 5). */
extern "C" __declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);
