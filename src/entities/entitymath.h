/* Two pure helpers for entity movement (entitymath.cpp). */

#pragma once

/* Whether a tile kind is a stair tile: kinds 5 to 8. */
int
Sim_CheckTileIsStairs(unsigned char kind);

/* Turns a facing 1..4 by delta, wrapping within 1..4. */
unsigned char
Sim_GetTurnedDirection(unsigned char dir, unsigned char delta);
