/* WrapperObject: the texture-coordinate arithmetic.  Nothing here feeds the
 * simulation; every value computed ends in a texture coordinate, so sin and
 * cos on a double differ from the original only in bits no gate records.
 *
 * PRESERVED: setMesh snapshots the UVs of every animation frame and flush
 * restores them all, but the sine wave and the scroll walk dwVertexCount
 * vertices, which is frame 0 only.  A multi-frame mesh animates frame 0 alone.
 *
 * Controls: KAROO_WRAP_FX=scrollback, =sineflip and =envflip each reverse one
 * effect's direction (the scroll, the warp, the environment map's v); all stay
 * on the render path.  KAROO_WRAP_DIAG=1 logs each entry point's first call
 * and a running census, since no gate observes texture coordinates. */

#include <windows.h>
#include <math.h>
#include <new>

#include "wrapperobject.h"
#include <stdlib.h>
#include "log.h"
#include "renderdevice.h"

KAROO_LAYOUT_CHECKS(WrapperObject)
{
    KAROO_LAYOUT_AT(vtable_,  0x00);
    KAROO_LAYOUT_AT(pBaseUV_, 0x04);
    KAROO_LAYOUT_AT(pMesh_,   0x08);
    KAROO_LAYOUT_AT(dirty_,   0x0c);
    KAROO_LAYOUT_SIZE(0x0d);
}

#define WRAP_ONE   1.0f
#define WRAP_ZERO  0.0f
#define WRAP_HALF  0.5f

/* The vertex format 0x212 (40 bytes): position, normal, then UV set 0 at 0x18.
 */
#define VTX_STRIDE 0x28
#define VTX_NORMAL 0x0c
#define VTX_UV0    0x18

static inline float *vtx_uv(CFaktMesh *mesh, unsigned int index)
{
    return (float *)((char *)mesh->pVertexData + index * VTX_STRIDE + VTX_UV0);
}

static inline const float *vtx_normal(CFaktMesh *mesh, unsigned int index)
{
    return (const float *)((char *)mesh->pVertexData + index * VTX_STRIDE
                           + VTX_NORMAL);
}

static inline unsigned int snapshot_count(CFaktMesh *mesh)
{
    return (unsigned int)mesh->wFrameCount * mesh->dwVertexCount;
}

enum WrapFx { WRAP_FX_OFF = 0, WRAP_FX_SCROLLBACK, WRAP_FX_SINEFLIP,
              WRAP_FX_ENVFLIP };

static int wrap_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        cached = WRAP_FX_OFF;
        if (GetEnvironmentVariableA("KAROO_WRAP_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "scrollback") == 0)     cached = WRAP_FX_SCROLLBACK;
            else if (lstrcmpiA(buf, "sineflip") == 0)  cached = WRAP_FX_SINEFLIP;
            else if (lstrcmpiA(buf, "envflip") == 0)   cached = WRAP_FX_ENVFLIP;
        }
        log_write("wrapper: FX mode = %d\n", cached);
    }
    return cached;
}

static bool wrap_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_WRAP_DIAG", buf, sizeof(buf)))
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

static LONG g_wrapCalls[WE_COUNT];
static LONG g_wrapVerts;

static void wrap_census(int entry, unsigned int verts)
{
    if (!wrap_diag())
        return;
    LONG n = InterlockedIncrement(&g_wrapCalls[entry]);
    InterlockedExchangeAdd(&g_wrapVerts, (LONG)verts);
    if (n == 1)
        log_write("wrapper: first %s\n", kWrapEntryName[entry]);
    else if ((n % 20000) == 0)
        log_write("wrapper: %s x%ld (uv writes so far %ld)\n",
                  kWrapEntryName[entry], n, g_wrapVerts);
}

void WrapperObject::construct()
{
    vtable_  = Wrapper_Vtable();
    pBaseUV_ = NULL;
    pMesh_   = NULL;
    dirty_   = 0;
    wrap_census(WE_CTOR, 0);
}

/* PRESERVED: frees without clearing pBaseUV_. */
void WrapperObject::dtorBody()
{
    vtable_ = Wrapper_Vtable();
    delete[] pBaseUV_;
    wrap_census(WE_DTOR, 0);
}

/* PRESERVED: a null mesh is ignored entirely: the old snapshot, the mesh and
 * the dirty flag are left as they were.  A failed allocation is not checked.
 */
void WrapperObject::setMesh(CFaktMesh *mesh)
{
    if (mesh == NULL)
        return;

    delete[] pBaseUV_;
    pBaseUV_ = NULL;
    pMesh_   = mesh;

    unsigned int count = snapshot_count(mesh);
    pBaseUV_ = new (std::nothrow) WrapperUV[count];
    dirty_   = 0;

    for (unsigned int i = 0; i < count; ++i) {
        const float *uv = vtx_uv(mesh, i);
        pBaseUV_[i].u = uv[0];
        pBaseUV_[i].v = uv[1];
    }
    wrap_census(WE_SETMESH, count);
}

/* PRESERVED: pMesh_ is left set, so a later flush would read the freed
 * snapshot; only the owning container's destructor calls this. */
void WrapperObject::releaseSnapshot()
{
    delete[] pBaseUV_;
    pBaseUV_ = NULL;
    wrap_census(WE_RELEASE, 0);
}

void WrapperObject::flush()
{
    if (pMesh_ == NULL || dirty_ == 0)
        return;

    unsigned int count = snapshot_count(pMesh_);
    for (unsigned int i = 0; i < count; ++i) {
        float *uv = vtx_uv(pMesh_, i);
        uv[0] = pBaseUV_[i].u;
        uv[1] = pBaseUV_[i].v;
    }
    dirty_ = 0;
    wrap_census(WE_FLUSH, count);
}

/* The tick count is widened unsigned. */
void WrapperObject::applySineWave(unsigned int ticks, float rate,
                                  float amplitude, float skew)
{
    double angle = (double)ticks * (double)rate;
    float  s     = (float)sin(angle);
    float  c     = (float)cos(angle);

    if (wrap_fx() == WRAP_FX_SINEFLIP)
        s = -s;

    if (pMesh_ != NULL && pMesh_->dwVertexCount != 0) {
        float cosTerm = c * amplitude;
        unsigned int count = pMesh_->dwVertexCount;

        for (unsigned int i = 0; i < count; ++i) {
            float *uv = vtx_uv(pMesh_, i);
            float  su = pBaseUV_[i].u;
            float  sv = pBaseUV_[i].v;

            uv[0] = ((su + su - WRAP_ONE) * skew + amplitude) * s + su;
            uv[1] = (cosTerm + sv) - (sv + sv - WRAP_ONE) * skew * s;

            count = pMesh_->dwVertexCount;
        }
        wrap_census(WE_SINE, count);
    }
    dirty_ = 1;
}

void WrapperObject::scrollUVs(unsigned int ticks, int axisU, float speed)
{
    if (pMesh_ != NULL) {
        //     // The delta is rounded to float once; each vertex gets a float add.
        float delta = (float)((double)ticks * (double)speed);
        if (wrap_fx() == WRAP_FX_SCROLLBACK)
            delta = -delta;

        unsigned int count = pMesh_->dwVertexCount;
        unsigned int axis  = (axisU != 0) ? 0u : 1u;

        for (unsigned int i = 0; i < count; ++i) {
            float *uv = vtx_uv(pMesh_, i);
            uv[axis] += delta;
            count = pMesh_->dwVertexCount;
        }
        if (count != 0)
            wrap_census(WE_SCROLL, count);
    }
    dirty_ = 1;
}

void WrapperObject::updateObjectTransform(RenderDevice *dev,
                                          unsigned short frame)
{
    //     // PRESERVED: an out-of-range frame returns without setting dirty_; every
    //     // other exit sets it.
    if (frame >= pMesh_->wFrameCount)
        return;

    Mat4 view, world;
    dev->GetTransform(Transform::View,  &view);
    dev->GetTransform(Transform::World, &world);

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

    unsigned int count = pMesh_->dwVertexCount;
    if (count != 0) {
        bool flip = (wrap_fx() == WRAP_FX_ENVFLIP);

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
            count = pMesh_->dwVertexCount;
        }
        wrap_census(WE_ENVMAP, count);
    }
    dirty_ = 1;
}

extern "C" {

static void *const g_WrapperVtable[1] = { (void *)&Wrapper_ScalarDtor };

__declspec(dllexport) void *Wrapper_Vtable(void)
{
    return (void *)g_WrapperVtable;
}

__declspec(dllexport) WrapperObject *__attribute__((thiscall))
Wrapper_Construct(WrapperObject *self)
{
    self->construct();
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_DtorBody(WrapperObject *self)
{
    self->dtorBody();
}

/* PRESERVED: frees the object itself, although no WrapperObject is ever
 * allocated alone; unreachable. */
__declspec(dllexport) void *__attribute__((thiscall))
Wrapper_ScalarDtor(WrapperObject *self, unsigned int flags)
{
    Wrapper_DtorBody(self);
    if (flags & 1)
        free(self);
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_SetMesh(WrapperObject *self, CFaktMesh *mesh)
{
    self->setMesh(mesh);
}

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_ReleaseSnapshot(WrapperObject *self)
{
    self->releaseSnapshot();
}

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_Flush(WrapperObject *self)
{
    self->flush();
}

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_ApplySineWave(WrapperObject *self, unsigned int ticks, float rate,
                      float amplitude, float skew)
{
    self->applySineWave(ticks, rate, amplitude, skew);
}

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_ScrollUVs(WrapperObject *self, unsigned int ticks, int axisU,
                  float speed)
{
    self->scrollUVs(ticks, axisU, speed);
}

__declspec(dllexport) void __attribute__((thiscall))
Wrapper_UpdateObjectTransform(WrapperObject *self, RenderDevice *dev,
                              unsigned short frame)
{
    self->updateObjectTransform(dev, frame);
}

}
