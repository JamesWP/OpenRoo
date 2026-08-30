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
