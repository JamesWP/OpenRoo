/* ExplodeDebris -- ctor, both destructors and the buffer release
 * (ENDGAME_PLAN.md E2).  explodedebris.h holds the layout, the name's evidence
 * and the vtable argument; this file holds the bodies and their quirks.
 *
 * ─── THE VTABLE IS OURS ──────────────────────────────────────────────────
 *
 * 0x0045d698 has ONE slot -- 0x00438050, and 0x0045d69c holds 0x3b5a740e,
 * which is not a code address -- and a byte scan of Karoo.exe.orig for the
 * literal finds exactly two occurrences, 0x00438021 and 0x00438072: the
 * constructor and the destructor body below.  Nothing else installs it, so
 * every ExplodeDebris carries our table and the game's is a UD2 tripwire.
 *
 * ─── Two quirks, both kept ───────────────────────────────────────────────
 *
 * 1. THE RELEASE DOES NOT NULL +0x08.  It frees pVertexCopy and
 *    pFaceRecords, then clears pVertexCopy, nVertexCount and bActive --
 *    and leaves pFaceRecords DANGLING.  A second release would therefore
 *    double-free it.  That cannot happen today (0x004380c0 overwrites both
 *    pointers before anything else looks at them, and the destructor runs
 *    once), which is exactly why the bug has never shown.  Reproduced, and
 *    named here rather than quietly fixed.
 *
 * 2. THE CONSTRUCTOR SEEDS THE GAUSSIAN TABLE WITH mu = 2.0, sigma = 1.0.
 *    Those are the two literals pushed at 0x00438015/0x0043801a
 *    (0x3f800000, 0x40000000) -- read off the listing, because the argument
 *    order of a two-float thiscall is precisely the thing CLAUDE.md says not
 *    to infer.  The push order puts sigma deeper, so the call is
 *    (this, mu = 2.0, sigma = 1.0).
 *
 * ─── The heap ────────────────────────────────────────────────────────────
 *
 * Both buffers are allocated by ExplodeDebris_AllocateExplodeBuffers (0x004380c0,
 * ours since ASSET Phase 5) and freed by ExplodeDebris_Release, so both sides are
 * now ours.  They still use the game's heap through alloc.h: the records that
 * carry a ExplodeDebris are copied wholesale into Game's quad-batch array, and
 * until every path that may free such a copy is ours, one heap for all of
 * them is the safe choice.  Moving to our own new[]/delete[] is a follow-up.
 */
#include <windows.h>

#include "explodedebris.h"
#include "generators.h"
#include <stdlib.h>
#include "faktmesh.h"
#include <string.h>
#include <math.h>
#include <d3d.h>

extern "C" {

static void *const g_ExplodeDebrisVtable[1] = { (void *)&ExplodeDebris_ScalarDtor };

__declspec(dllexport) void *ExplodeDebris_Vtable(void)
{
    return (void *)g_ExplodeDebrisVtable;
}

/* 0x00438080.  Guarded frees, then three of the four stores -- see quirk 1. */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_Release(ExplodeDebris *self)
{
    if (self->pVertexCopy)
        free(self->pVertexCopy);
    if (self->pFaceRecords)
        free(self->pFaceRecords);
    self->pVertexCopy  = NULL;
    self->nVertexCount = 0;
    self->bActive      = 0;
    /* pFaceRecords is deliberately NOT cleared (quirk 1). */
}

/* 0x004380c0.  The `explode` keyword's buffers, sized from the mesh.
 *
 * BUGS KEPT: neither allocation is checked before the zeroing that follows
 * the second one; the vertex buffer is zeroed twice, the second time as
 * (n*5 & 0x1fffffff)*2 dwords, which equals n*10 only while n*5 fits in 29
 * bits.  The first zeroing is guarded, the second is not. */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_AllocateExplodeBuffers(ExplodeDebris *self, CFaktMesh *mesh)
{
    ExplodeDebris_Release(self);

    DWORD n = mesh->dwVertexCount;
    void *verts = malloc(n * 0x28);
    if (verts != NULL && (int)n > 0)
        memset(verts, 0, n * 0x28);
    self->pVertexCopy  = verts;
    self->pFaceRecords = malloc((n / 3) * 0xc);
    self->nVertexCount = (int)mesh->dwVertexCount;

    memset(self->pVertexCopy, 0, (((DWORD)self->nVertexCount * 5) & 0x1fffffffu) * 2 * 4);
}

/* 0x004381a0.  DAT_0045d69c is 0x3b5a740e = 1/300 as a float.  The count
 * is converted UNSIGNED.  What +0x98 means is not known: no reader of it was
 * found. */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_StoreExplodeScaledCount(ExplodeDebris *self, float scale)
{
    const float kOneOver300 = 1.0f / 300.0f;
    self->flExplodeScaledCount = (float)(DWORD)self->nVertexCount * scale * kOneOver300;
}

/* 0x00438010.  Stores in the original's order, then the table seed. */
__declspec(dllexport) ExplodeDebris *__attribute__((thiscall))
ExplodeDebris_Construct(ExplodeDebris *self)
{
    self->vtable       = ExplodeDebris_Vtable();
    self->pVertexCopy  = NULL;
    self->pFaceRecords = NULL;
    self->nVertexCount = 0;
    self->bActive      = 0;
    self->nLiveVertices = 0;
    self->flDropAccum  = 0;

    Gen_FillGaussianField(self, 2.0f, 1.0f);
    return self;
}

/* 0x00438070.  Re-install the table, then tail into the release. */
__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_DtorBody(ExplodeDebris *self)
{
    self->vtable = ExplodeDebris_Vtable();
    ExplodeDebris_Release(self);
}

/* 0x00438050 -- vtable slot 0.
 *
 * Unverified by test, and said plainly: xref.py finds no reference of any
 * kind to 0x00438050, our table's slot 0 is the only way in, and every
 * ExplodeDebris is embedded in a larger object, so bit 0 is never set.  Same
 * position as LevelObjBase_ScalarDtor, Wrapper_ScalarDtor and
 * FaktMesh_ScalarDtor. */
__declspec(dllexport) void *__attribute__((thiscall))
ExplodeDebris_ScalarDtor(ExplodeDebris *self, unsigned int flags)
{
    ExplodeDebris_DtorBody(self);
    if (flags & 1)
        free(self);
    return self;
}

/* ─── The effect: 0x4381d0 begin, 0x4383e0 advance, 0x4384d0 draw ─────────
 *
 * Vertices are FVF 0x212 (0x28 bytes, position first); the velocity table
 * holds one float[3] per triangle.  Written as plain float C -- the
 * original's x87 keeps the length and the last quotient in extended
 * precision, a difference in the last bits of a debris velocity that
 * nothing downstream compares. */
static float *debris_vertex(ExplodeDebris *self, int i)
{
    return (float *)((BYTE *)self->pVertexCopy + i * 0x28);
}

static float *debris_velocity(ExplodeDebris *self, int tri)
{
    return (float *)((BYTE *)self->pFaceRecords + tri * 0xc);
}

__declspec(dllexport) int __attribute__((thiscall))
ExplodeDebris_Begin(ExplodeDebris *self, CFaktMesh *mesh,
                    unsigned short frame, const float *origin)
{
    if (frame >= mesh->wFrameCount)
        return 0;
    if ((DWORD)self->nVertexCount != mesh->dwVertexCount)
        return 0;
    if (self->pVertexCopy == NULL)
        return 0;

    /* BUG KEPT: the source offset is frame * count * 0x640, forty times a
     * frame's real size (count * 0x28).  Frame 0 is right; any other frame
     * reads far past the mesh's vertex data. */
    DWORD count = mesh->dwVertexCount;
    memcpy(self->pVertexCopy,
           (BYTE *)mesh->pVertexData + (DWORD)frame * count * 0x640,
           count * 0x28);

    for (DWORD t = 0; t < (DWORD)self->nVertexCount / 3; t++) {
        float *r = debris_velocity(self, t);
        const float *v0 = debris_vertex(self, t * 3);
        const float *v1 = v0 + 10, *v2 = v0 + 20;
        for (int k = 0; k < 3; k++) r[k] = v0[k] - origin[k];
        for (int k = 0; k < 3; k++) r[k] = (v1[k] - origin[k]) + r[k];
        for (int k = 0; k < 3; k++) r[k] = (v2[k] - origin[k]) + r[k];

        float speed = self->samples[self->cursor];
        float len = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        self->cursor++;
        for (int k = 0; k < 3; k++)
            r[k] = speed * (r[k] / len);   /* len 0 gives NaN, as the original */
        if (self->cursor >= 30)
            self->cursor = 0;
    }

    self->bActive       = 1;
    self->nLiveVertices = self->nVertexCount;
    self->flDropAccum   = 0.0f;
    return 1;
}

__declspec(dllexport) void __attribute__((thiscall))
ExplodeDebris_Advance(ExplodeDebris *self, float dt)
{
    if (self->bActive == 0 || self->nLiveVertices <= 0)
        return;

    for (int i = 0; i < self->nLiveVertices; i++) {
        float *v = debris_vertex(self, i);
        const float *r = debris_velocity(self, i / 3);
        for (int k = 0; k < 3; k++)
            v[k] = dt * r[k] + v[k];
    }

    /* Whole triangles owed: floor, then the CRT's truncating __ftol. */
    self->flDropAccum = dt * self->flExplodeScaledCount + self->flDropAccum;
    int n = (int)floor((double)self->flDropAccum);
    self->nLiveVertices -= n * 3;
    self->flDropAccum = self->flDropAccum - (float)n;
    if (self->nLiveVertices < 0)
        self->nLiveVertices = 0;
}

__declspec(dllexport) HRESULT __attribute__((thiscall))
ExplodeDebris_Draw(ExplodeDebris *self, IDirect3DDevice3 *dev)
{
    if (self->bActive == 0)
        return (HRESULT)0x800401f0;       /* CO_E_NOTINITIALIZED */

    DWORD saved;
    dev->GetRenderState(D3DRENDERSTATE_SRCBLEND, &saved);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0x212, self->pVertexCopy,
                       self->nLiveVertices, D3DDP_DONOTLIGHT);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_DESTALPHA);
    dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0x212, self->pVertexCopy,
                       self->nLiveVertices, D3DDP_DONOTLIGHT);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, saved);
    return 0;
}

} // extern "C"
