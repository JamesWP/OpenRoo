/* ExplodeDebris: the theme "explode" effect.  A model is shattered into its
 * triangles, which fly outward and thin out.  RenderGameFrame begins it when
 * an object explodes; RenderSceneObjects advances and draws it, and
 * DrawObjectShadows draws its shadow, the debris flattened like any model.
 *
 * The object sits at ThemeLevelObject+0x15, 0x9c bytes up to the next field
 * the theme loader writes.  The 30-float speed table and its cursor tile
 * exactly: 0x14 + 30 * 4 is 0x8c. */

#pragma once

#include <windows.h>
#include "layout.h"
class RenderDevice;

struct ExplodeDebris {
    static const int ORIGIN = 0;

    void         *vtable;                // +0x00  the one-slot vtable
    void         *pVertexCopy;           // +0x04  nVertexCount * 0x28 (FVF 0x212)
    void         *pFaceRecords;  // +0x08  a velocity per triangle, (nVertexCount / 3) * 0xc
    int           nVertexCount;          // +0x0c  as it was when the buffers were built
    DWORD         bActive;               // +0x10  set by begin; cleared by the ctor and release
    float         samples[30];  // +0x14  Gaussian speeds, mu 2.0, sigma 1.0; refilled whole
    DWORD         cursor;                // +0x8c  reset by the same refill
    int           nLiveVertices;         // +0x90  begin sets it; advance drops it by threes
    float         flDropAccum;           // +0x94  fractional triangles owed
    float         flExplodeScaledCount;  // +0x98  the drop rate: nVertexCount * arg / 300

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

extern "C" {

/* The one-slot vtable, installed by the ctor and the dtor body. */
__declspec(dllexport) void *ExplodeDebris_Vtable(void);

__declspec(dllexport) ExplodeDebris *__attribute__((thiscall))
ExplodeDebris_Construct(ExplodeDebris *self);

/* Vtable slot 0.  Returns self; bit 0 of flags frees. */
__declspec(dllexport) void *__attribute__((thiscall))
ExplodeDebris_ScalarDtor(ExplodeDebris *self, unsigned int flags);

__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_DtorBody(ExplodeDebris *self);

/* Frees the two scratch buffers.  See the .cpp for the field it does not
 * clear. */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_Release(ExplodeDebris *self);

/* The theme loader's "explode": size the buffers, and store the drop rate (the
 * keyword's third argument). */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_AllocateExplodeBuffers(ExplodeDebris *self, struct CFaktMesh *mesh);
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_StoreExplodeScaledCount(ExplodeDebris *self, float scale);

/* Copies one mesh frame and gives every triangle a velocity.  Returns 1, or 0
 * if the frame, the vertex count or the buffer is wrong. */
__declspec(dllexport) int __attribute__((thiscall))
ExplodeDebris_Begin(ExplodeDebris *self, struct CFaktMesh *mesh,
                    unsigned short frame, const float *origin);

/* Advances by dt: moves the live vertices, then drops whole triangles. */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_Advance(ExplodeDebris *self, float dt);

/* Draws the live triangles twice (SRCALPHA, then DESTALPHA); 0x800401f0 if
 * inactive. */
__declspec(dllexport) HRESULT __attribute__((thiscall))
ExplodeDebris_Draw(ExplodeDebris *self, RenderDevice *dev);

}
