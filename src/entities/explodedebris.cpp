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

/* Guarded frees, then three of the four stores. */
void ExplodeDebris::release()
{
    if (pVertexCopy_)
        free(pVertexCopy_);
    if (pFaceRecords_)
        free(pFaceRecords_);
    pVertexCopy_  = NULL;
    nVertexCount_ = 0;
    bActive_      = 0;

/* pFaceRecords is not cleared: PRESERVED. */
}

/* The "explode" keyword's buffers, sized from the mesh.  PRESERVED: neither
 * allocation is checked before the zeroing after the second; the vertex buffer
 * is zeroed twice, the second time as (n*5 & 0x1fffffff)*2 dwords, which is
 * n*10 only while n*5 fits in 29 bits. */
void ExplodeDebris::allocateExplodeBuffers(CFaktMesh *mesh)
{
    release();

    DWORD n = mesh->vertexCount();
    MeshVertex *verts = (MeshVertex *)malloc(n * sizeof(MeshVertex));
    if (verts != NULL && (int)n > 0)
        memset(verts, 0, n * 0x28);
    pVertexCopy_  = verts;
    pFaceRecords_ = (float (*)[3])malloc((n / 3) * sizeof(float[3]));
    nVertexCount_ = (int)mesh->vertexCount();

    memset(pVertexCopy_, 0, (((DWORD)nVertexCount_ * 5) & 0x1fffffffu) * 2 * 4);
}

/* The drop rate: the count, converted unsigned, times arg / 300. */
void ExplodeDebris::storeExplodeScaledCount(float scale)
{
    const float kOneOver300 = 1.0f / 300.0f;
    flExplodeScaledCount_ = (float)(DWORD)nVertexCount_ * scale * kOneOver300;
}

/* The stores in a fixed order, then the table seed. */
ExplodeDebris::ExplodeDebris()
{
    pVertexCopy_  = NULL;
    pFaceRecords_ = NULL;
    nVertexCount_ = 0;
    bActive_      = 0;
    nLiveVertices_ = 0;
    flDropAccum_  = 0;

    Gen_FillGaussianField(this, 2.0f, 1.0f);
}

ExplodeDebris::~ExplodeDebris()
{
    release();
}

/* The effect.  Vertices are MeshVertex (position first); the
 * velocity table holds one float[3] per triangle.  Plain float C: extended
 * precision would change only the last bits of a debris velocity, which
 * nothing compares. */
float *ExplodeDebris::debrisVertex(int i)
{
    return pVertexCopy_[i].pos;
}

float *ExplodeDebris::debrisVelocity(int tri)
{
    return pFaceRecords_[tri];
}

int ExplodeDebris::begin(CFaktMesh *mesh,
                    unsigned short frame, const float *origin)
{
    if (frame >= mesh->frameCount())
        return 0;
    if ((DWORD)nVertexCount_ != mesh->vertexCount())
        return 0;
    if (pVertexCopy_ == NULL)
        return 0;

    // PRESERVED: the source offset is frame * count * 40 vertices, forty
    // times a frame's real size.  Frame 0 is right; any other frame
    // reads far past the mesh's vertices.
    DWORD count = mesh->vertexCount();
    memcpy(pVertexCopy_,
           (MeshVertex *)mesh->vertexData() + (DWORD)frame * count * 40,
           count * sizeof(MeshVertex));

    for (DWORD t = 0; t < (DWORD)nVertexCount_ / 3; t++) {
        float *r = debrisVelocity(t);
        const float *v0 = debrisVertex(t * 3);
        const float *v1 = debrisVertex(t * 3+1);
        const float *v2 = debrisVertex(t * 3+2);
        for (int k = 0; k < 3; k++) r[k] = v0[k] - origin[k];
        for (int k = 0; k < 3; k++) r[k] = (v1[k] - origin[k]) + r[k];
        for (int k = 0; k < 3; k++) r[k] = (v2[k] - origin[k]) + r[k];

        float speed = samples_[cursor_];
        float len = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        cursor_++;
        for (int k = 0; k < 3; k++)
            r[k] = speed * (r[k] / len);  // PRESERVED: len 0 gives NaN
        if (cursor_ >= 30)
            cursor_ = 0;
    }

    bActive_       = 1;
    nLiveVertices_ = nVertexCount_;
    flDropAccum_   = 0.0f;
    return 1;
}

void ExplodeDebris::advance(float dt)
{
    if (bActive_ == 0 || nLiveVertices_ <= 0)
        return;

    for (int i = 0; i < nLiveVertices_; i++) {
        float *v = debrisVertex(i);
        const float *r = debrisVelocity(i / 3);
        for (int k = 0; k < 3; k++)
            v[k] = dt * r[k] + v[k];
    }

    // Whole triangles owed: floor, then truncate.
    flDropAccum_ = dt * flExplodeScaledCount_ + flDropAccum_;
    int n = (int)floor((double)flDropAccum_);
    nLiveVertices_ -= n * 3;
    flDropAccum_ = flDropAccum_ - (float)n;
    if (nLiveVertices_ < 0)
        nLiveVertices_ = 0;
}

HRESULT ExplodeDebris::draw(RenderDevice *dev)
{
    if (bActive_ == 0)
        return (HRESULT)0x800401f0;  // CO_E_NOTINITIALIZED

    DWORD saved;
    saved = dev->GetRenderState(RS::SrcBlend);
    dev->SetRenderState(RS::SrcBlend, Blend::SrcAlpha);
    dev->Draw(Prim::TriangleList, VertexFormat::Normal2, pVertexCopy_,
                       nLiveVertices_, DrawFlag::NoLight);
    dev->SetRenderState(RS::SrcBlend, Blend::DestAlpha);
    dev->Draw(Prim::TriangleList, VertexFormat::Normal2, pVertexCopy_,
                       nLiveVertices_, DrawFlag::NoLight);
    dev->SetRenderState(RS::SrcBlend, saved);
    return 0;
}
