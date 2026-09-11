/* foepath.cpp's entry point -- the foe pathfinder (FoePath, 0x35 bytes,
 * hung off foe+0x13b).  Declared here so the foe gets it from the owner
 * rather than redeclaring it (COHESION_PLAN.md, template 10). */
#pragma once

/* FindFoePathBetweenCells 0x00401c20.  `self` is the FoePath. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_FindFoePathBetweenCells(void *self, int uFoe, int vFoe,
                            int uTarget, int vTarget);
