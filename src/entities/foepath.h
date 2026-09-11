/* foepath.cpp's entry point -- the foe pathfinder (FoePath, 0x35 bytes,
 * hung off foe+0x13b).  Declared here so the foe gets it from the owner
 * rather than redeclaring it (COHESION_PLAN.md, template 10). */
#pragma once

/* PLACEHOLDER: the FoePath destructor body 0x00401c00, __fastcall (ECX =
 * the FoePath): ReleasePathSearchNodeLists (ours) then FactAlloc::Free of
 * its +0x12 buffer.  Called through, not rewritten: the still-original
 * Player dtor (0x0041FA48) calls it too.  The caller frees the FoePath
 * itself afterwards (Free2). */
inline void FoePath_Destroy(void *self)
{
    typedef void (__attribute__((fastcall)) *fn)(void *);
    ((fn)0x00401c00)(self);
}

/* FindFoePathBetweenCells 0x00401c20.  `self` is the FoePath. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_FindFoePathBetweenCells(void *self, int uFoe, int vFoe,
                            int uTarget, int vTarget);
