/* entitymath.cpp's exports -- the two pure entity helpers.  Declared here so
 * callers get them from the owner rather than redeclaring them
 * (COHESION_PLAN.md, template 10). */
#pragma once

/* CheckTileIsRamp -- tile kinds 5..8 are ramps. */
extern "C" __declspec(dllexport) int __attribute__((stdcall))
Sim_CheckTileIsRamp(unsigned char kind);

/* GetTurnedDirection 0x0043ad40 -- rotate a 1..4 facing by delta. */
extern "C" __declspec(dllexport) unsigned char __attribute__((stdcall))
Sim_GetTurnedDirection(unsigned char dir, unsigned char delta);
