/* ExplodeDebris (explodedebris.h): the constructor, destructors, buffers, and
 * the effect itself.
 *
 * PRESERVED:
 *   - the release frees both scratch buffers and clears pVertexCopy,
 *     nVertexCount and bActive, but leaves pFaceRecords dangling, so a second
 *     release would free it twice.  None happens: the allocation overwrites
 *     both pointers first, and the destructor runs once;
 *   - the constructor seeds the Gaussian table with mu 2.0, sigma 1.0. */

#include <stdint.h>

#include "explodedebris.h"
#include "generators.h"
#include <algorithm>
#include "faktmesh.h"
#include <math.h>
#include "renderdevice.h"

void ExplodeDebris::release()
{
    RenderDevice::DestroyVertexBuffer(vb_);
    vb_ = nullptr;
    std::vector<MeshVertex>().swap(vertexCopy_);
    std::vector<std::array<float, 3>>().swap(faceRecords_);
    nVertexCount_ = 0;
    bActive_      = 0;
}

/* The "explode" keyword's buffers, sized from the mesh and zeroed. */
void ExplodeDebris::allocateExplodeBuffers(CFaktMesh *mesh)
{
    release();

    uint32_t n = mesh->vertexCount();
    vertexCopy_.assign(n, MeshVertex{});
    faceRecords_.assign(n / 3, std::array<float, 3>{});
    nVertexCount_ = (int)n;
}

/* The drop rate: the count, converted unsigned, times arg / 300. */
void ExplodeDebris::storeExplodeScaledCount(float scale)
{
    const float kOneOver300 = 1.0f / 300.0f;
    flExplodeScaledCount_ = (float)(uint32_t)nVertexCount_ * scale * kOneOver300;
}

/* The stores in a fixed order, then the table seed. */
ExplodeDebris::ExplodeDebris()
{
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
    return vertexCopy_[i].pos;
}

float *ExplodeDebris::debrisVelocity(int tri)
{
    return faceRecords_[tri].data();
}

int ExplodeDebris::begin(CFaktMesh *mesh,
                    unsigned short frame, const float *origin)
{
    if (frame >= mesh->frameCount())
        return 0;
    if ((uint32_t)nVertexCount_ != mesh->vertexCount())
        return 0;
    if (vertexCopy_.empty())
        return 0;

    // PRESERVED: the source offset is frame * count * 40 vertices, forty
    // times a frame's real size.  Frame 0 is right; any other frame
    // reads far past the mesh's vertices.
    uint32_t count = mesh->vertexCount();
    const MeshVertex *src = (const MeshVertex *)mesh->vertexData() + (uint32_t)frame * count * 40;
    std::copy_n(src, count, vertexCopy_.begin());

    for (uint32_t t = 0; t < (uint32_t)nVertexCount_ / 3; t++) {
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

long ExplodeDebris::draw(RenderDevice *dev)
{
    if (bActive_ == 0)
        return (long)0x800401f0;  // CO_E_NOTINITIALIZED

    if (!vb_ && !vertexCopy_.empty())
        vb_ = dev->CreateVertexBuffer(VertexFormat::Normal2, (uint32_t)vertexCopy_.size(),
                                      BufferUsage::Dynamic);
    if (vb_ && nLiveVertices_ > 0)
        dev->UpdateVertexBuffer(vb_, 0, vertexCopy_.data(), (uint32_t)nLiveVertices_);

    const BlendState saved = dev->blend();
    BlendState pass = saved;
    pass.src = BlendFactor::SrcAlpha;
    dev->SetBlend(pass);
    dev->DrawBuffer(Prim::TriangleList, vb_, 0, nLiveVertices_, DrawFlag::NoLight);
    pass.src = BlendFactor::DestAlpha;
    dev->SetBlend(pass);
    dev->DrawBuffer(Prim::TriangleList, vb_, 0, nLiveVertices_, DrawFlag::NoLight);
    dev->SetBlend(saved);
    return 0;
}
