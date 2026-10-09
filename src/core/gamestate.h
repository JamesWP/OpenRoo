/* The game-state reader (gamestate.cpp).  It reads the state the replay tests
 * assert on, logs it when it changes, and dumps it as JSON for replaytest.py
 * to compare. */

#pragma once

/* Whether KAROO_STATE_LOG is set. */
bool gamestate_enabled(void);

/* Records the game mode as handed to the input dispatch. */
void gamestate_note_mode(unsigned short mode);

/* Called once a frame; logs only on a change. */
void gamestate_tick(void);

/* Writes the current state to KAROO_STATE_DUMP as one JSON object.  The game
 * cannot return an exit code through launch.sh, so the comparing is done by
 * tools/replaytest.py, against the values in the test manifest.  reason is
 * written into the dump so a partial run cannot pass for a complete one. */
void gamestate_dump(const char *reason);
