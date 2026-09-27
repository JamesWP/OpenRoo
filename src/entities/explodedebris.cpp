/* ExplodeDebris (explodedebris.h): the constructor, destructors, buffers, and
 * the effect itself.
 *
 * PRESERVED:
 *   - the release frees both scratch buffers and clears pVertexCopy,
 *     nVertexCount and bActive, but leaves pFaceRecords dangling, so a second
 *     release would free it twice.  None happens: the allocation overwrites
 *     both pointers first, and the destructor runs once;
 *   - the constructor seeds the Gaussian table with mu 2.0, sigma 1.0. */

#include <windows.h>

#include "explodedebris.h"
#include "generators.h"
#include <stdlib.h>
#include "faktmesh.h"
#include <string.h>
#include <math.h>
#include "renderdevice.h"


static void *const g_ExplodeDebrisVtable[1] = { (void *)&ExplodeDebris_ScalarDtor };

void *ExplodeDebris_Vtable(void)
{
    return (void *)g_ExplodeDebrisVtable;
}

/* Guarded frees, then three of the four stores. */
void ExplodeDebris_Release(ExplodeDebris *self)
{
    if (self->pVertexCopy)
        free(self->pVertexCopy);
    if (self->pFaceRecords)
        free(self->pFaceRecords);
    self->pVertexCopy  = NULL;
    self->nVertexCount = 0;
    self->bActive      = 0;

/* pFaceRecords is not cleared: PRESERVED. */
}

/* The "explode" keyword's buffers, sized from the mesh.  PRESERVED: neither
 * allocation is checked before the zeroing after the second; the vertex buffer
 * is zeroed twice, the second time as (n*5 & 0x1fffffff)*2 dwords, which is
 * n*10 only while n*5 fits in 29 bits. */
void ExplodeDebris_AllocateExplodeBuffers(ExplodeDebris *self, CFaktMesh *mesh)
{
    ExplodeDebris_Release(self);

    DWORD n = mesh->dwVertexCount;
    MeshVertex *verts = (MeshVertex *)malloc(n * sizeof(MeshVertex));
    if (verts != NULL && (int)n > 0)
        memset(verts, 0, n * sizeof(MeshVertex));
    self->pVertexCopy  = verts;
    self->pFaceRecords = (float (*)[3])malloc((n / 3) * sizeof(float[3]));
    self->nVertexCount = (int)mesh->dwVertexCount;

    memset(self->pVertexCopy, 0, (((DWORD)self->nVertexCount * 5) & 0x1fffffffu) * 2 * 4);
}

/* The drop rate: the count, converted unsigned, times arg / 300. */
void ExplodeDebris_StoreExplodeScaledCount(ExplodeDebris *self, float scale)
{
    const float kOneOver300 = 1.0f / 300.0f;
    self->flExplodeScaledCount = (float)(DWORD)self->nVertexCount * scale * kOneOver300;
}

/* The stores in a fixed order, then the table seed. */
ExplodeDebris *ExplodeDebris_Construct(ExplodeDebris *self)
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

/* Re-install the vtable, then release. */
void ExplodeDebris_DtorBody(ExplodeDebris *self)
{
    self->vtable = ExplodeDebris_Vtable();
    ExplodeDebris_Release(self);
}

/* Vtable slot 0.  Every ExplodeDebris is embedded in a larger object, so bit 0
 * is never set and this path is not exercised. */
void *ExplodeDebris_ScalarDtor(ExplodeDebris *self, unsigned int flags)
{
    ExplodeDebris_DtorBody(self);
    if (flags & 1)
        free(self);
    return self;
}

/* The effect.  Vertices are MeshVertex (position first); the
 * velocity table holds one float[3] per triangle.  Plain float C: extended
 * precision would change only the last bits of a debris velocity, which
 * nothing compares. */
static float *debris_vertex(ExplodeDebris *self, int i)
{
    return self->pVertexCopy[i].pos;
}

static float *debris_velocity(ExplodeDebris *self, int tri)
{
    return self->pFaceRecords[tri];
}

int ExplodeDebris_Begin(ExplodeDebris *self, CFaktMesh *mesh,
                        unsigned short frame, const float *origin)
{
    if (frame >= mesh->wFrameCount)
        return 0;
    if ((DWORD)self->nVertexCount != mesh->dwVertexCount)
        return 0;
    if (self->pVertexCopy == NULL)
        return 0;

    // PRESERVED: the source offset is frame * count * 40 vertices, forty
    // times a frame's real size.  Frame 0 is right; any other frame
    // reads far past the mesh's vertices.
    DWORD count = mesh->dwVertexCount;
    memcpy(self->pVertexCopy,
           (MeshVertex *)mesh->pVertexData + (DWORD)frame * count * 40,
           count * sizeof(MeshVertex));

    for (DWORD t = 0; t < (DWORD)self->nVertexCount / 3; t++) {
        float *r = debris_velocity(self, t);
        const float *v0 = debris_vertex(self, t * 3);
        const float *v1 = debris_vertex(self, t * 3 + 1);
        const float *v2 = debris_vertex(self, t * 3 + 2);
        for (int k = 0; k < 3; k++) r[k] = v0[k] - origin[k];
        for (int k = 0; k < 3; k++) r[k] = (v1[k] - origin[k]) + r[k];
        for (int k = 0; k < 3; k++) r[k] = (v2[k] - origin[k]) + r[k];

        float speed = self->samples[self->cursor];
        float len = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        self->cursor++;
        for (int k = 0; k < 3; k++)
            r[k] = speed * (r[k] / len);  // PRESERVED: len 0 gives NaN
        if (self->cursor >= 30)
            self->cursor = 0;
    }

    self->bActive       = 1;
    self->nLiveVertices = self->nVertexCount;
    self->flDropAccum   = 0.0f;
    return 1;
}

void ExplodeDebris_Advance(ExplodeDebris *self, float dt)
{
    if (self->bActive == 0 || self->nLiveVertices <= 0)
        return;

    for (int i = 0; i < self->nLiveVertices; i++) {
        float *v = debris_vertex(self, i);
        const float *r = debris_velocity(self, i / 3);
        for (int k = 0; k < 3; k++)
            v[k] = dt * r[k] + v[k];
    }

    // Whole triangles owed: floor, then truncate.
    self->flDropAccum = dt * self->flExplodeScaledCount + self->flDropAccum;
    int n = (int)floor((double)self->flDropAccum);
    self->nLiveVertices -= n * 3;
    self->flDropAccum = self->flDropAccum - (float)n;
    if (self->nLiveVertices < 0)
        self->nLiveVertices = 0;
}

HRESULT ExplodeDebris_Draw(ExplodeDebris *self, RenderDevice *dev)
{
    if (self->bActive == 0)
        return (HRESULT)0x800401f0;  // CO_E_NOTINITIALIZED

    DWORD saved;
    saved = dev->GetRenderState(RS::SrcBlend);
    dev->SetRenderState(RS::SrcBlend, Blend::SrcAlpha);
    dev->Draw(Prim::TriangleList, VertexFormat::Normal2, self->pVertexCopy,
                       self->nLiveVertices, DrawFlag::NoLight);
    dev->SetRenderState(RS::SrcBlend, Blend::DestAlpha);
    dev->Draw(Prim::TriangleList, VertexFormat::Normal2, self->pVertexCopy,
                       self->nLiveVertices, DrawFlag::NoLight);
    dev->SetRenderState(RS::SrcBlend, saved);
    return 0;
}

