/* WrapperObject -- the per-mesh texture-coordinate animator (ENDGAME_PLAN E2).
 *
 * Nine originals, 0x0043f0c0..0x0043f390, the whole class.  wrapperobject.h
 * holds the layout, the vtable argument and what drives each method; this
 * file holds the arithmetic and the reasons it is written the way it is.
 *
 * ─── The snapshot, and the asymmetry that is the class's real bug ────────
 *
 * setMesh snapshots the UVs of EVERY animation frame
 * (wFrameCount * dwVertexCount pairs) and flush restores every one of them.
 * The two animating modes that write UVs -- the sine wave and the scroll --
 * only ever walk `dwVertexCount` vertices, which is frame 0.  So an animated
 * mesh with more than one frame animates frame 0 alone, and the flush
 * rewrites the other frames with values nothing ever changed.  That is the
 * original's behaviour on both sides of the asymmetry, and it is preserved:
 * the loop bounds here are deliberately different from each other.
 *
 * (updateObjectTransform is the exception -- it is the one method that takes
 * a frame index, and it writes that frame.  It still walks dwVertexCount
 * vertices, because that is one frame's worth.)
 *
 * ─── Exactness points, all read off the listing ──────────────────────────
 *
 * 1. THE TICK COUNTER IS AN UNSIGNED WIDENING.  Both animating modes load it
 *    with `FILD qword` off a scratch pair whose high dword was just zeroed
 *    (`MOV [ESP+8],ESI` with ESI == 0), so the caller's dword is unsigned no
 *    matter what the caller thought.  Written as a cast from unsigned int.
 *
 * 2. THE SCROLL DELTA IS ROUNDED TO float BEFORE THE LOOP.  0x0043f318
 *    stores it (`FSTP float ptr [ESP+0x14]`) and every iteration reloads it,
 *    so it is a float add per vertex, not an 80-bit one.  A local `float`
 *    reproduces that; making it double would not.
 *
 * 3. THE ENVIRONMENT MAP'S FACING TEST IS NaN-ASYMMETRIC.  `FCOMP; FNSTSW;
 *    TEST AH,0x41; JZ skip` writes the vertex when the dot product is less
 *    than, equal to, OR UNORDERED WITH zero, so it is written `!(dot > 0)`
 *    rather than `dot <= 0`.  Same shape as the bridge tick's phase-end
 *    compare.
 *
 * 4. THE FRAME GUARD IS AN UNSIGNED 16-BIT COMPARE AND RETURNS EARLY.  An
 *    out-of-range frame leaves the dirty flag ALONE; every other exit from
 *    updateObjectTransform sets it, including the zero-vertex one.
 *
 * 5. releaseSnapshot AND THE DTOR BODY DIFFER.  The dtor body frees without
 *    NULLing (nothing runs afterwards); releaseSnapshot frees and NULLs.
 *    Neither clears pMesh_, so an object whose snapshot has been released
 *    still reports a mesh -- and flush would then read through a freed
 *    pointer.  It cannot happen, because the only caller of releaseSnapshot
 *    is the owning container's destructor, but the shape is the original's.
 *
 * 6. setMesh IGNORES A NULL MESH COMPLETELY.  It does not free the old
 *    snapshot, does not clear the mesh pointer, does not clear the dirty
 *    flag.  `if (mesh == NULL) return;` is the whole function for that case.
 *
 * ─── Floating point ──────────────────────────────────────────────────────
 *
 * The original's sine and cosine are x87 FSIN/FCOS on an 80-bit angle; this
 * is `sin()`/`cos()` on a double and a cast back (CLAUDE.md: write simple C,
 * the lost bits do not matter).  Nothing here feeds the simulation -- every
 * value computed in this file ends up in a texture coordinate, and no gate
 * and no save file has ever recorded one.  The summation ORDER of the two
 * dot products in updateObjectTransform is kept as the listing has it (the
 * row-2 term first, then row-1, then row-0) because that costs nothing.
 *
 * ─── The heap ────────────────────────────────────────────────────────────
 *
 * The snapshot array was the game's `operator new` / `FactAlloc::Free2`.
 * Both sides of its lifetime are now ours -- setMesh is the only allocator
 * and the dtor body and releaseSnapshot are the only frees -- so by
 * CLAUDE.md's rule it moves to plain `new[]`/`delete[]`.  alloc.h survives
 * here for ONE reference only -- the unreachable free inside the scalar
 * deleting destructor, where the object itself would be the game heap's --
 * and retires with the containers.  `nothrow` because the game's `operator new` returned
 * NULL on failure; the original then walked the NULL pointer in its copy
 * loop, and so does this, deliberately.
 *
 * ─── Controls and diags (CONTROLS.md) ────────────────────────────────────
 *
 * KAROO_WRAP_FX picks one DIRECTION change, never a value tweak:
 *   scrollback  negate the scroll delta -- scrolling textures run backwards
 *   sineflip    negate the sine term    -- the warp travels the other way
 *   envflip     negate the environment map's v -- reflections upside down
 * Every one of them moves texture coordinates only, so the blast radius is
 * strictly the render path: nothing here can reach the bridge/slide spawn
 * scans that crash levelreport.py.
 *
 * KAROO_WRAP_DIAG=1 logs a first-call line per entry point and a running
 * census, because that is the only thing either gate can see: the state
 * assertions are over game state and no recording asserts a pixel.
 */
#include <windows.h>
#include <d3d.h>
#include <math.h>
#include <new>

#include "wrapperobject.h"
#include "alloc.h"
#include "log.h"

/* ─── Layout ──────────────────────────────────────────────────────────── */

KAROO_LAYOUT_CHECKS(WrapperObject)
{
    KAROO_LAYOUT_AT(vtable_,  0x00);
    KAROO_LAYOUT_AT(pBaseUV_, 0x04);
    KAROO_LAYOUT_AT(pMesh_,   0x08);
    KAROO_LAYOUT_AT(dirty_,   0x0c);
    KAROO_LAYOUT_SIZE(0x0d);
}

/* ─── Constants, read out of .rdata rather than assumed ───────────────── */

#define WRAP_ONE   1.0f   /* 0x0045d298 = 0x3f800000 */
#define WRAP_ZERO  0.0f   /* 0x0045d2c8 = 0x00000000 */
#define WRAP_HALF  0.5f   /* 0x0045d318 = 0x3f000000 */

/* An FVF 0x212 vertex is 0x28 bytes: XYZ at +0, NORMAL at +0xc, texture
 * coordinate set 0 at +0x18, set 1 at +0x20.  Only set 0 is ever touched. */
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

/* The snapshot's length -- wFrameCount zero-extended, multiplied by the
 * per-frame vertex count, exactly as 0x0043f147 does it. */
static inline unsigned int snapshot_count(CFaktMesh *mesh)
{
    return (unsigned int)mesh->wFrameCount * mesh->dwVertexCount;
}

/* ─── KAROO_WRAP_FX -- read by value, never by presence ───────────────── */

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

/* ─── KAROO_WRAP_DIAG -- the census ───────────────────────────────────── */

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

/* ─── The class ───────────────────────────────────────────────────────── */

/* 0x0043f0c0.  The original zeroes pMesh_ before pBaseUV_; the order of two
 * independent stores is not observable, so they are written naturally. */
void WrapperObject::construct()
{
    vtable_  = Wrapper_Vtable();
    pBaseUV_ = NULL;
    pMesh_   = NULL;
    dirty_   = 0;
    wrap_census(WE_CTOR, 0);
}

/* 0x0043f100.  Re-install the table, then free the snapshot without NULLing
 * it (point 5). */
void WrapperObject::dtorBody()
{
    vtable_ = Wrapper_Vtable();
    delete[] pBaseUV_;
    wrap_census(WE_DTOR, 0);
}

/* 0x0043f120.  Free, attach, allocate, snapshot -- in the original's order,
 * which matters: pBaseUV_ is NULLed between the free and the allocation, so
 * a failed allocation leaves a NULL there rather than a dangling pointer,
 * and the copy loop below then walks it.  Point 6 above covers the NULL
 * mesh. */
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

/* 0x0043f1b0 */
void WrapperObject::releaseSnapshot()
{
    delete[] pBaseUV_;
    pBaseUV_ = NULL;
    wrap_census(WE_RELEASE, 0);
}

/* 0x0043f1d0 -- the whole snapshot, every frame (see the asymmetry note). */
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

/* 0x0043f230 -- frame 0 only.
 *
 *   u = ((2*su - 1) * skew + amplitude) * sin(ticks * rate) + su
 *   v = (cos(ticks * rate) * amplitude + sv)
 *       - (2*sv - 1) * skew * sin(ticks * rate)
 *
 * Note that `amplitude` enters u INSIDE the sine's factor and v OUTSIDE it,
 * multiplied by the cosine; the two axes are not symmetric.  The cosine term
 * is pre-multiplied by `amplitude` once before the loop (0x0043f269), which
 * is where this reading came from -- the decompile hides it in the loop. */
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

            /* The loop bound is re-read from the mesh every iteration by the
             * original (0x0043f2ce).  Nothing inside can change it. */
            count = pMesh_->dwVertexCount;
        }
        wrap_census(WE_SINE, count);
    }
    dirty_ = 1;
}

/* 0x0043f2f0 -- frame 0 only.  axisU selects which coordinate moves; the
 * delta is one float, computed once (point 2). */
void WrapperObject::scrollUVs(unsigned int ticks, int axisU, float speed)
{
    if (pMesh_ != NULL) {
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

/* 0x0043f390 -- spherical environment mapping.
 *
 * GetTransform(VIEW) and GetTransform(WORLD), multiply world * view in that
 * order (row-major, the D3D convention), then for each vertex of the given
 * frame transform the NORMAL by the upper 3x3 and map it into [0,1]:
 *
 *      u = (nx' + 1) * 0.5        v = (1 - ny') * 0.5
 *
 * written only for vertices whose transformed nz' is not greater than zero
 * (point 3) -- the ones facing the camera.  The rest keep whatever the
 * previous mode left there, which is why mode 5 needs a flush behind it as
 * much as the others do.
 *
 * The device pointer is the com_proxy device proxy the game passes; we call
 * through it on purpose, as faktmesh.cpp does. */
void WrapperObject::updateObjectTransform(IDirect3DDevice3 *dev,
                                          unsigned short frame)
{
    if (frame >= pMesh_->wFrameCount)
        return;                      /* dirty_ deliberately untouched */

    D3DMATRIX view, world;
    dev->GetTransform(D3DTRANSFORMSTATE_VIEW,  &view);
    dev->GetTransform(D3DTRANSFORMSTATE_WORLD, &world);

    const float *w = &world._11;
    const float *v = &view._11;
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

/* ─── Exports ─────────────────────────────────────────────────────────── */

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

/* 0x0043f0e0 -- vtable slot 0.
 *
 * The free is the GAME heap's and stays there: bit 0 is set only when the
 * object itself was allocated by the game's `operator new`, and no
 * WrapperObject is -- every one is embedded in a larger object.  The branch
 * is reproduced rather than asserted away, which is why this file's one game
 * heap reference exists at all.  It retires with the containers.
 *
 * Unverified by test, and said plainly: xref.py finds no reference of any
 * kind to 0x0043f0e0, our table's slot 0 is the only way in, and nothing
 * constructs a bare heap WrapperObject.  Same position as
 * LevelObjBase_ScalarDtor. */
__declspec(dllexport) void *__attribute__((thiscall))
Wrapper_ScalarDtor(WrapperObject *self, unsigned int flags)
{
    Wrapper_DtorBody(self);
    if (flags & 1)
        game_free2(self);
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
Wrapper_UpdateObjectTransform(WrapperObject *self, IDirect3DDevice3 *dev,
                              unsigned short frame)
{
    self->updateObjectTransform(dev, frame);
}

} // extern "C"
