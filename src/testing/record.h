/* Input recording and replay.
 *
 * All player input reaches the game by two paths: the key array (one byte per
 * SDL scancode) the control dispatch reads once per tick (progctrl.cpp), and
 * single-key polls (input_key_down), through which the menus poll Enter, Escape
 * and the arrows.
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

/* The key array (inputdev::KEY_COUNT bytes, 0x80 while held).  Recording stores what the device returned; replay
 * overwrites the buffer with what was recorded and returns true. */
void record_keys(unsigned short game_state, const uint8_t *keys);
bool replay_keys(unsigned short *game_state, uint8_t *keys);

/* Single-key polls.  Recording notes the query and its answer; replay answers
 * from the recording. */
void record_async(int key, bool down);
bool replay_async(int key, bool *down);

/* True once the replay file is exhausted; the test harness ends the run here.
 */
bool record_replay_finished(void);

/* Whether a key (an inputdev::Key) is down: every key poll in the game goes
 * through here. */
bool input_key_down(int key);
