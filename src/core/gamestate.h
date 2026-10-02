/* The game-state reader (gamestate.cpp).  It reads the state the replay tests
 * assert on, logs it when it changes, and dumps it as JSON for replaytest.py
 * to compare. */

#pragma once

/* Whether KAROO_STATE_LOG is set. */
bool gamestate_enabled(void);

/* Records the game mode as handed to the input dispatch. */
void gamestate_note_mode(unsigned short mode);

/* The last game mode seen; 0 is "not in a level".  worldstate.cpp checks it so
 * that a menu frame never dumps a stale or torn-down grid. */
unsigned short gamestate_mode(void);

/* Called once a frame; logs only on a change. */
void gamestate_tick(void);

/* Snapshots the whole Game and diffs it across a death (KAROO_DEATH_DIFF=1).
 */
void gamestate_deathdiff(void);

/* Writes the current state to KAROO_STATE_DUMP as one JSON object.  The game
 * cannot return an exit code through launch.sh, so the comparing is done by
 * tools/replaytest.py, against the values in the test manifest.  reason is
 * written into the dump so a partial run cannot pass for a complete one. */
void gamestate_dump(const char *reason);
