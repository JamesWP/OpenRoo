/* Route planning for the autoplay policy: which pickup to take next, in what
 * order, and the next cell to step to on the way, keeping clear of foes where
 * it can. */

#pragma once
#include "worldstate.h"

/* True if a foe could be on the cell next tick (its own cell and the four it
 * could step to, unless frozen), or the cell is a falling tile. */
bool plan_is_dangerous(const Observation *o, int u, int v);

/* The next cell to step to towards the current objective, or false when there
 * is nothing to head for.  seek_exit targets the level exit instead of a
 * pickup.  Routes around danger when it can and through it when it must;
 * standing still beside a foe is not safer than moving. */
bool plan_next_step(const Observation *o, int pu, int pv, int *nu, int *nv,
                    bool seek_exit);

/* Drops the cached tour; call when the level changes. */
void plan_reset(void);
