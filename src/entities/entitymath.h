/* Two pure helpers for entity movement (entitymath.cpp). */

#pragma once

/* Whether a tile kind is a ramp: kinds 5 to 8. */
int __attribute__((stdcall))
Sim_CheckTileIsRamp(unsigned char kind);

/* Turns a facing 1..4 by delta, wrapping within 1..4. */
unsigned char __attribute__((stdcall))
Sim_GetTurnedDirection(unsigned char dir, unsigned char delta);
