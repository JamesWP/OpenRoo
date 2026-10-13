/* The policies KAROO_BOT names (policy.h):
 *   idle    holds nothing;
 *   wander  walks forward and turns right whenever a step does not move it,
 *           a placeholder that exercises the harness (deaths, stuck);
 *   explore random single moves, logging the nav model's predictions
 *           against the game (explore.h);
 *   plan    the gem quota, then the exit, by the planner (executive.h);
 *   path:u,v;u,v;...
 *           walks the waypoints with the motor (motor.h), then stands.  Each
 *           waypoint is in line with the one before; the cells between are
 *           walked.  The first is normally the player's start. */
#pragma once

#include <memory>
#include <string>

#include "policy.h"

namespace bot {

/* The policy name names, or null if it names none. */
std::unique_ptr<Policy> makePolicy(const std::string &name);

}  // namespace bot
