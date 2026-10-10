/* The tiles' part of the debug UI: the base layer of the Map window (the
 * floor, what each tile holds, slides and teleporter links), the tile
 * tooltip, and click-to-teleport.  Drawn from the game tick, before anything
 * that sits on the floor draws itself. */

#pragma once

#include <stdint.h>

class Game;

/* Opens the map for this frame.  Does nothing unless dbg::active(). */
void Tile_DebugDrawMap(Game *game);
