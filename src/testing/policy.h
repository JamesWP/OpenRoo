#pragma once
#include <windows.h>

struct ProgableControl;

/* Autoplay policy — AI_PLAN.md Stage 4.
 *
 * Closes the loop: perception (worldstate.cpp) -> decision (here) -> the same
 * scancode array the replay path already drives.  No new input mechanism and
 * no new binary patches; this writes into the buffer progctrl.cpp was going to
 * hand the game anyway.
 *
 * Selected with KAROO_POLICY=<name>.  Unset, or "none", and this is a no-op —
 * which is what every existing recording in tests/ runs with.
 */
bool policy_active(void);

/* True once the policy owns the run: active, and past KAROO_POLICY_AFTER.
 * The replay path and the run-ending check both defer to this, so a recording
 * can supply a menu prefix and then get out of the way. */
bool policy_in_control(DWORD frame);

/* Overwrite `keys` with the policy's choice for this frame.  Returns false and
 * leaves the buffer alone when the policy is off, not in a level, or has no
 * valid observation — so the human at the keyboard still drives the menus. */
bool policy_keys(ProgableControl *s, unsigned short game_state, BYTE *keys);
