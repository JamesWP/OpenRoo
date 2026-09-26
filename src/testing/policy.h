/* The autoplay policy: plays a level by itself, for unattended test runs.
 *
 * Perception is worldstate.cpp's Observation, the route is plan.cpp's; the
 * policy turns the next step into key presses in the same scan-code array a
 * replay drives, so a policy run can be recorded and replayed like a
 * hand-played one.  Selected with KAROO_POLICY=<name> (nearest-crystal, or
 * probe to calibrate directions); unset or "none" and it does nothing. */

#pragma once
#include <windows.h>

struct ProgableControl;

/* True when KAROO_POLICY names a known policy. */
bool policy_active(void);

/* True once the policy owns the run: active, and past frame
 * KAROO_POLICY_AFTER.  Replay and the run-ending check defer to it, so a
 * recording can supply a menu prefix and then step aside. */
bool policy_in_control(DWORD frame);

/* Overwrites keys with the policy's choice for this frame.  Returns false and
 * leaves the buffer alone when the policy is off, not in a level, or has no
 * valid observation, so a person still drives the menus. */
bool policy_keys(ProgableControl *s, unsigned short game_state, BYTE *keys);

/* The menu side: drives the menu into a level at startup (KAROO_MENU_SLOT),
 * acknowledges intro, death and score screens with Enter, and quits (or, with
 * KAROO_POLICY_LOOP=1, reloads) when the run is over.  Called every frame,
 * including while no level is loaded. */
void policy_menu_tick(void);
