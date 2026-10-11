/* Single-key polls: the game's one way to ask whether a key is down.  The
 * recording and replay (testing/record.h) sit behind it. */
#pragma once

/* Whether a key (an inputdev::Key) is down: every key poll in the game goes
 * through here. */
bool input_key_down(int key);
