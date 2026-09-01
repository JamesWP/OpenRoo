#pragma once
#include <windows.h>
#include "worldstate.h"

/* Route planning for the policy — AI_PLAN.md Stage 6.
 *
 * Two things live here that the one-shot "walk to the nearest pickup" search
 * could not express:
 *
 *   - foe avoidance, which needs a second notion of cost rather than a second
 *     notion of passability, and
 *   - collection *order*, which is a travelling-salesman problem: greedily
 *     taking the nearest pickup each time is what made the policy criss-cross
 *     a level instead of sweeping it.
 */

/* Squares a foe could be standing on next tick: its own cell and the four it
 * could step to.  Filled by plan_mark_danger(); indexed like the grid. */
bool plan_is_dangerous(const Observation *o, int u, int v);

/* Next cell to step to on the way to the current objective, or false when
 * there is nothing to head for.  `seek_exit` targets the level exit instead of
 * a pickup.  Routes around danger when it can and through it when it must —
 * standing still next to a foe is not safer than moving. */
bool plan_next_step(const Observation *o, int pu, int pv, int *nu, int *nv,
                    bool seek_exit);

/* Drop any cached tour — call when the level changes. */
void plan_reset(void);
