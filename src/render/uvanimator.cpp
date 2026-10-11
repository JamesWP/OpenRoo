/* UVAnimator: the texture-coordinate arithmetic.  Nothing here feeds the
 * simulation; every value computed ends in a texture coordinate, so sin and
 * cos on a double differ from the original only in bits no gate records.
 *
 * PRESERVED: setMesh snapshots the UVs of every animation frame and flush
 * restores them all, but the sine wave and the scroll walk dwVertexCount
 * vertices, which is frame 0 only.  A multi-frame mesh animates frame 0 alone.
 *
 * Controls: KAROO_UVANIM_FX=scrollback, =sineflip and =envflip each reverse one
 * effect's direction (the scroll, the warp, the environment map's v); all stay
 * on the render path.  KAROO_UVANIM_DIAG=1 logs each entry point's first call
 * and a running census, since no gate observes texture coordinates. */

#include <strings.h>
#include <atomic>
#include "sysdev.h"
#include <math.h>
#include <new>

#include "uvanimator.h"
#include <stdlib.h>
#include "logger.h"
#include "renderdevice.h"

#define WRAP_ONE   1.0f
#define WRAP_ZERO  0.0f
#define WRAP_HALF  0.5f

static inline float *vtx_uv(AnimatedMesh *mesh, unsigned int index)
{
    return ((MeshVertex *)mesh->vertexData())[index].uv0;
}

static inline const float *vtx_normal(AnimatedMesh *mesh, unsigned int index)
{
    return ((MeshVertex *)mesh->vertexData())[index].normal;
}

static inline unsigned int snapshot_count(AnimatedMesh *mesh)
{
    return (unsigned int)mesh->frameCount() * mesh->vertexCount();
}

enum WrapFx { UVANIM_FX_OFF = 0, UVANIM_FX_SCROLLBACK, UVANIM_FX_SINEFLIP,
              UVANIM_FX_ENVFLIP };

static int uvanim_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        cached = UVANIM_FX_OFF;
        if (sysdev::getEnv("KAROO_UVANIM_FX", buf, sizeof(buf))) {
            if (strcasecmp(buf, "scrollback") == 0)     cached = UVANIM_FX_SCROLLBACK;
            else if (strcasecmp(buf, "sineflip") == 0)  cached = UVANIM_FX_SINEFLIP;
            else if (strcasecmp(buf, "envflip") == 0)   cached = UVANIM_FX_ENVFLIP;
        }
        g_logger.write("uvanimator: FX mode = %d\n", cached);
    }
    return cached;
}

static bool wrap_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (sysdev::getEnv("KAROO_UVANIM_DIAG", buf, sizeof(buf)))
            cached = (buf[0] != '0');
    }
    return cached != 0;
}

enum WrapEntry { WE_SETMESH = 0, WE_RELEASE, WE_FLUSH, WE_SINE, WE_SCROLL,
                 WE_ENVMAP, WE_CTOR, WE_DTOR, WE_COUNT };

static const char *const kWrapEntryName[WE_COUNT] = {
    "setMesh", "releaseSnapshot", "flush", "applySineWave",
    "scrollUVs", "updateObjectTransform", "ctor", "dtor"
};

static std::atomic<long> g_uvanimCalls[WE_COUNT];
static std::atomic<long> g_uvanimVerts;

static void uvanim_census(int entry, unsigned int verts)
{
    if (!wrap_diag())
        return;
    long n = ++g_uvanimCalls[entry];
    g_uvanimVerts += (long)verts;
    if (n == 1)
        g_logger.write("uvanimator: first %s\n", kWrapEntryName[entry]);
    else if ((n % 20000) == 0)
        g_logger.write("uvanimator: %s x%ld (uv writes so far %ld)\n",
                  kWrapEntryName[entry], n, g_uvanimVerts.load());
}

UVAnimator::UVAnimator()
{
    pBaseUV_ = NULL;
    pMesh_   = NULL;
    dirty_   = 0;
    uvanim_census(WE_CTOR, 0);
}

/* PRESERVED: frees without clearing pBaseUV_. */
UVAnimator::~UVAnimator()
{
    delete[] pBaseUV_;
    uvanim_census(WE_DTOR, 0);
}

/* PRESERVED: a null mesh is ignored entirely: the old snapshot, the mesh and
 * the dirty flag are left as they were.  A failed allocation is not checked.
 */
void UVAnimator::setMesh(AnimatedMesh *mesh)
{
    if (mesh == NULL)
        return;

    delete[] pBaseUV_;
    pBaseUV_ = NULL;
    pMesh_   = mesh;

    unsigned int count = snapshot_count(mesh);
    pBaseUV_ = new (std::nothrow) AnimatedUV[count];
    dirty_   = 0;
    vertsDirty_ = 0;

    for (unsigned int i = 0; i < count; ++i) {
        const float *uv = vtx_uv(mesh, i);
        pBaseUV_[i].u = uv[0];
        pBaseUV_[i].v = uv[1];
    }
    uvanim_census(WE_SETMESH, count);
}

/* PRESERVED: pMesh_ is left set, so a later flush would read the freed
 * snapshot; only the owning container's destructor calls this. */
void UVAnimator::releaseSnapshot()
{
    delete[] pBaseUV_;
    pBaseUV_ = NULL;
    uvanim_census(WE_RELEASE, 0);
}

/* Puts every frame's UVs back to the snapshot's. */
void UVAnimator::restoreVertices()
{
    unsigned int count = snapshot_count(pMesh_);
    for (unsigned int i = 0; i < count; ++i) {
        float *uv = vtx_uv(pMesh_, i);
        uv[0] = pBaseUV_[i].u;
        uv[1] = pBaseUV_[i].v;
    }
    pMesh_->touchVertices(0, count);
    vertsDirty_ = 0;
    uvanim_census(WE_FLUSH, count);
}

void UVAnimator::flush()
{
    if (pMesh_ == NULL || dirty_ == 0)
        return;

    if (vertsDirty_)
        restoreVertices();
    pMesh_->setUVTransform(UVTransform());
    dirty_ = 0;
}

/* The tick count is widened unsigned.  The warp is, per coordinate, an affine
 * function of the snapshot's:
 *   u' = ((2u - 1) * skew + amplitude) * s + u
 *   v' = cos * amplitude + v - (2v - 1) * skew * s
 * so it is a scale and an offset, whatever the vertex. */
void UVAnimator::applySineWave(unsigned int ticks, float rate,
                                  float amplitude, float skew)
{
    double angle = (double)ticks * (double)rate;
    float  s     = (float)sin(angle);
    float  c     = (float)cos(angle);

    if (uvanim_fx() == UVANIM_FX_SINEFLIP)
        s = -s;

    if (pMesh_ != NULL && pMesh_->vertexCount() != 0) {
        if (vertsDirty_)
            restoreVertices();
        UVTransform t;
        t.scaleU  = 2.0f * skew * s + WRAP_ONE;
        t.scaleV  = WRAP_ONE - 2.0f * skew * s;
        t.offsetU = (amplitude - skew) * s;
        t.offsetV = c * amplitude + skew * s;
        pMesh_->setUVTransform(t);
        uvanim_census(WE_SINE, pMesh_->vertexCount());
    }
    dirty_ = 1;
}

/* The scroll adds to whatever the coordinates already are, every call. */
void UVAnimator::scrollUVs(unsigned int ticks, int axisU, float speed)
{
    if (pMesh_ != NULL) {
        float delta = (float)((double)ticks * (double)speed);
        if (uvanim_fx() == UVANIM_FX_SCROLLBACK)
            delta = -delta;

        UVTransform t = pMesh_->uvTransform();
        (axisU != 0 ? t.offsetU : t.offsetV) += delta;
        pMesh_->setUVTransform(t);

        const unsigned int count = pMesh_->vertexCount();
        if (count != 0)
            uvanim_census(WE_SCROLL, count);
    }
    dirty_ = 1;
}

void UVAnimator::updateObjectTransform(RenderDevice *dev,
                                          unsigned short frame)
{
    //     // PRESERVED: an out-of-range frame returns without setting dirty_; every
    //     // other exit sets it.
    if (frame >= pMesh_->frameCount())
        return;

    const Mat4 &view = dev->view();
    const Mat4 &world = dev->world();

    const float *w = world.m;
    const float *v = view.m;
    float m[16];
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += w[row * 4 + k] * v[k * 4 + col];
            m[row * 4 + col] = sum;
        }

    unsigned int count = pMesh_->vertexCount();
    if (count != 0) {
        // The vertices are written absolutely, so a scroll or warp in force
        // goes.
        pMesh_->setUVTransform(UVTransform());
        vertsDirty_ = 1;
        bool flip = (uvanim_fx() == UVANIM_FX_ENVFLIP);

        for (unsigned int i = 0; i < count; ++i) {
            unsigned int index = (unsigned int)frame * count + i;
            const float *n  = vtx_normal(pMesh_, index);
            float        nx = n[0], ny = n[1], nz = n[2];

            //             // The facing test writes when dot <= 0 or NaN.
            float dot = m[2 + 8] * nz + m[2 + 4] * ny + m[2] * nx;
            if (!(dot > WRAP_ZERO)) {
                float *uv = vtx_uv(pMesh_, index);
                uv[0] = (m[0 + 8] * nz + m[0 + 4] * ny + m[0] * nx + WRAP_ONE)
                        * WRAP_HALF;
                float vv = (WRAP_ONE
                            - (m[1 + 8] * nz + m[1 + 4] * ny + m[1] * nx))
                           * WRAP_HALF;
                uv[1] = flip ? -vv : vv;
            }
            count = pMesh_->vertexCount();
        }
        pMesh_->touchVertices(frame * count, count);
        uvanim_census(WE_ENVMAP, count);
    }
    dirty_ = 1;
}
