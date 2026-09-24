/* ExplodeDebris -- the theme `explode` effect: a model shattered into its
 * triangles, which fly outward and thin out.  Ghidra's library matcher
 * mis-named the class `CvtSyms`; this file then called it `ShadowMesh`, an
 * inference from its neighbour DrawObjectShadows that the code does not
 * support -- nothing here projects or flattens anything.  Renamed
 * 2026-09-24 once 0x004381d0 / 0x004383e0 / 0x004384d0 were read.
 *
 *   0x00438010  ctor       vtable 0x0045d698, seven zeroes, then the
 *                          Gaussian table at +0x14 (0x00438170) with
 *                          mu = 2.0, sigma = 1.0
 *   0x00438050  scalar deleting dtor -- vtable slot 0, no code reference
 *   0x00438070  dtor body  re-install the table, tail-jump to the release
 *   0x00438080  release    free the two scratch buffers
 *   0x004380c0  allocate   the theme loader's `explode`: size the buffers
 *   0x004381a0  drop rate  the `explode` keyword's third argument
 *   0x004381d0  begin      copy one mesh frame, give each triangle a velocity
 *   0x004383e0  advance    move the live vertices, drop whole triangles
 *   0x004384d0  draw       the live triangles, twice (SRCALPHA, DESTALPHA)
 *
 * Begin is called from RenderGameFrame when an object explodes; advance and
 * draw from RenderSceneObjects (the object itself) and DrawObjectShadows
 * (its planar shadow, which is the debris flattened like any other model).
 *
 * ─── The layout ──────────────────────────────────────────────────────────
 *
 *   +0x00  vtable, 0x0045d698 -- ONE slot (0x0045d69c is not a code address)
 *   +0x04  the vertex copy,  nVertexCount * 0x28 (FVF 0x212) -- game heap
 *   +0x08  per-triangle velocity, (nVertexCount / 3) * 0xc -- game heap
 *   +0x0c  nVertexCount as it was when the buffers were built
 *   +0x10  active: set by begin; zeroed by ctor and release
 *   +0x14  30 floats, the Gaussian speed table; +0x8c its cursor
 *   +0x90  live vertex count: begin sets it, advance drops it by 3s
 *   +0x94  drop accumulator (fractional triangles owed)
 *   +0x98  drop rate: (float)(uint)nVertexCount * arg / 300
 *
 * The 30-float table and its cursor tile exactly: 0x14 + 30*4 == 0x8c.
 * The size, 0x9c, comes from the container (theme.h): the object sits at
 * ThemeLevelObject+0x15 and the next field the loader writes is at +0xb1.
 *
 * This struct absorbs what generators.h used to call `GaussianFieldHost`,
 * which modelled the same object from 0x00438170's fifteen instructions
 * alone.  One object, one definition (COHESION_PLAN.md).
 *
 * ─── The heap stays the game's ───────────────────────────────────────────
 *
 * The two scratch buffers are allocated by ExplodeDebris_AllocateExplodeBuffers
 * (0x004380c0, ours) with the game's `operator new`, and freed with
 * `FactAlloc::Free2`.  Why both sides still use the game heap is in the .cpp.
 */
#pragma once

#include <windows.h>
#include "layout.h"

struct ExplodeDebris {
    static const int ORIGIN = 0;

    void         *vtable;          /* +0x00  our one-slot table          */
    void         *pVertexCopy;     /* +0x04  dwVertexCount * 0x28        */
    void         *pFaceRecords;    /* +0x08  (dwVertexCount / 3) * 0xc   */
    int           nVertexCount;    /* +0x0c                              */
    DWORD         bActive;         /* +0x10  set by begin                */
    float         samples[30];     /* +0x14  refilled whole, never part  */
    DWORD         cursor;          /* +0x8c  reset by the same call      */
    int           nLiveVertices;   /* +0x90  begin sets, advance drops   */
    float         flDropAccum;     /* +0x94  fractional triangles owed   */
    float         flExplodeScaledCount; /* +0x98  see above              */

    KAROO_LAYOUT_REGISTER(ExplodeDebris);
};

KAROO_LAYOUT_CHECKS(ExplodeDebris)
{
    KAROO_LAYOUT_AT(pVertexCopy,  0x04);
    KAROO_LAYOUT_AT(pFaceRecords, 0x08);
    KAROO_LAYOUT_AT(nVertexCount, 0x0c);
    KAROO_LAYOUT_AT(samples,      0x14);
    KAROO_LAYOUT_AT(cursor,       0x8c);
    KAROO_LAYOUT_AT(nLiveVertices, 0x90);
    KAROO_LAYOUT_AT(flDropAccum,  0x94);
    KAROO_LAYOUT_AT(flExplodeScaledCount, 0x98);
    KAROO_LAYOUT_SIZE(0x9c);
}

/* ─── Exports -- patch.py CALL_PATCHES / JMP_PATCHES redirect here ────── */
extern "C" {

/* Our one-slot table, installed by the ctor and the dtor body. */
__declspec(dllexport) void *ExplodeDebris_Vtable(void);

__declspec(dllexport) ExplodeDebris *__attribute__((thiscall))
ExplodeDebris_Construct(ExplodeDebris *self);

/* 0x00438050 -- vtable slot 0.  Returns `this`; bit 0 of `flags` frees. */
__declspec(dllexport) void *__attribute__((thiscall))
ExplodeDebris_ScalarDtor(ExplodeDebris *self, unsigned int flags);

__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_DtorBody(ExplodeDebris *self);

/* 0x00438080 -- free the two scratch buffers.  See the .cpp for the field it
 * does NOT clear. */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_Release(ExplodeDebris *self);

/* 0x004380c0 / 0x004381a0 -- the theme loader's `explode` setup. */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_AllocateExplodeBuffers(ExplodeDebris *self, struct CFaktMesh *mesh);
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_StoreExplodeScaledCount(ExplodeDebris *self, float scale);

/* 0x004381d0 -- begin: copy one frame, give every triangle a velocity.
 * Returns 1, or 0 if the frame, the vertex count or the buffer is wrong. */
__declspec(dllexport) int __attribute__((thiscall))
ExplodeDebris_Begin(ExplodeDebris *self, struct CFaktMesh *mesh,
                    unsigned short frame, const float *origin);
/* 0x004383e0 -- advance by dt: move, then drop whole triangles. */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_Advance(ExplodeDebris *self, float dt);
/* 0x004384d0 -- draw the live triangles twice; 0x800401f0 if inactive. */
__declspec(dllexport) HRESULT __attribute__((thiscall))
ExplodeDebris_Draw(ExplodeDebris *self, struct IDirect3DDevice3 *dev);

} // extern "C"
