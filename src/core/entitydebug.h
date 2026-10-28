/* The level's objects on the debug UI's Map window (entitydebug.cpp): each
 * bridge with its switch links, bomb, foe with its route, and the player last,
 * over the tiles' layer (tiledebug.h).  Drawn once a frame, after the tick,
 * from the World as the tick left it. */

#pragma once

class World;

/* Does nothing unless dbg::active(). */
void Entities_DebugDrawMap(const World *world);
