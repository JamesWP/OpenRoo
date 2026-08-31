#pragma once
#include <windows.h>

/* Stage B game-state reader — REPLAY_PLAN.md.
 *
 * Reads the assertion surface off GameGlobal and logs it whenever it changes,
 * so a play session confirms (or refutes) the field interpretations before any
 * test asserts on them. */
bool gamestate_enabled(void);

/* game_state as handed to ProgableControl::DispatchInputActions. */
void gamestate_note_mode(unsigned short mode);

/* Called once per frame; logs only on change. */
void gamestate_tick(void);

/* Snapshot/diff the whole Game object across a death (KAROO_DEATH_DIFF=1). */
void gamestate_deathdiff(void);

/* Stage E — write the current game state to KAROO_STATE_DUMP as JSON.
 *
 * The plan called for a KAROO_ASSERT=file.json that the DLL compares against
 * and turns into an exit code.  That is not buildable as specified: Stage A
 * already established that `launch.sh --skip-launcher` exits non-zero regardless of
 * what the game returns, so a DLL-set exit code cannot survive to the caller.
 * The DLL therefore only *reports* — one JSON object, no parser in the DLL —
 * and tools/replaytest.py does the comparing.  That also keeps the expected
 * values in the test manifest next to the recording, where they can be read.
 *
 * `reason` is recorded in the dump so a partial run cannot be mistaken for a
 * completed one. */
void gamestate_dump(const char *reason);
