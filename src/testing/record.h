/* Input recording and replay.
 *
 * All player input reaches the game as the key array input::update takes once
 * per tick (src/input/keys.h).  Recording captures it per frame; replay feeds
 * it back in place of the real keyboard.
 * KAROO_RECORD=<file> records, KAROO_REPLAY=<file> replays.
 *
 * DETERMINISM: a recording replays exactly only under the KAROO_FIXED_DT and
 * KAROO_SEED it was made with; both are stored in the header and a mismatch is
 * logged on replay. */
#pragma once
#include <stdint.h>
#include "keysource.h"

bool record_recording(void);
bool record_replaying(void);

/* Frame boundary: flush the recorded frame, or load the next replay frame.
 * Called by the clock once per frame, before the game ticks. */
void record_frame_boundary(void);

/* The replay and the recorder, as a key source and an observer (input/keys.h
 * adds them).  Null unless KAROO_REPLAY or KAROO_RECORD is set.  The replay
 * is the one source the game takes keys from; the recorder sees the keys the
 * game was given, from whatever sources. */
input::KeySource   *replay_source(void);
input::KeyObserver *record_observer(void);

/* True once the replay file is exhausted; the test harness ends the run here.
 */
bool record_replay_finished(void);

/* KAROO_INPUT_DEBUG=1: the replay logs its first key records. */
bool input_debug(void);
