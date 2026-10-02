/* Input recording and replay.
 *
 * All player input reaches the game by two paths: the 256-byte DirectInput
 * scan-code array the control dispatch reads once per tick (progctrl.cpp), and
 * GetAsyncKeyState, through which the menus poll Enter, Escape and the arrows.
 * Recording captures both per frame; replay feeds them back in place of the
 * real devices.  KAROO_RECORD=<file> records, KAROO_REPLAY=<file> replays.
 *
 * DETERMINISM: a recording replays exactly only under the KAROO_FIXED_DT and
 * KAROO_SEED it was made with; both are stored in the header and a mismatch is
 * logged on replay. */
#pragma once
#include <stdint.h>

bool record_recording(void);
bool record_replaying(void);

/* Frame boundary: flush the recorded frame, or load the next replay frame.
 * Called by the clock once per frame, before the game ticks. */
void record_frame_boundary(void);

/* The scan-code array.  Recording stores what the device returned; replay
 * overwrites the buffer with what was recorded and returns true. */
void record_keys(unsigned short game_state, const uint8_t *keys);
bool replay_keys(unsigned short *game_state, uint8_t *keys);

/* GetAsyncKeyState.  Recording notes the query and its answer; replay answers
 * from the recording. */
void record_async(int vkey, short value);
bool replay_async(int vkey, short *value);

/* True once the replay file is exhausted; the test harness ends the run here.
 */
bool record_replay_finished(void);

/* The game's GetAsyncKeyState: every key poll in the game goes through here.
 */
  short hooks_GetAsyncKeyState(int vKey);
