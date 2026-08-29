/* ParticleSystem render-path reimplementation.
 *
 * Replaces the seven per-class fill/draw/render methods plus the shared base
 * Render dispatcher (all safety-stubbed by patch.py; reached only through the
 * patched vtable slots 8/9/12 — see PARTICLE_PLAN.md):
 *
 *   0x447d10 ParticleSystem::Render            → Particle_BaseRender  (Face+XFace slot 8)
 *   0x44d190 PointParticleSystem::Render       → Particle_PointRender (inlines the draw!)
 *   0x44d1c0 PointParticleSystem::FillParticleVerts → Particle_PointFill
 *   0x44d220 PointParticleSystem::DrawParticles     → Particle_PointDraw
 *   0x44d910 FaceParticleSystem::FillParticleVerts  → Particle_FaceFill
 *   0x44da80 FaceParticleSystem::DrawParticles      → Particle_FaceDraw
 *   0x44ea10 XFaceParticleSystem::FillParticleVerts → Particle_XFaceFill
 *   0x44eb90 XFaceParticleSystem::DrawParticles     → Particle_XFaceDraw
 *
 * Semantics are bit-exact to the originals: the game's scratch vertex buffer
 * is reused (sized by the game's ResizeRing), only xyz + diffuse are written
 * (psize/specular/u/v stay uninitialised, as the originals leave them), and
 * the device passed in is the com_proxy device proxy — draws stay visible to
 * the proxy layer.  Particle simulation (ring nodes, generators) stays
 * game-owned.
 *
 * KAROO_PARTICLE_FX=tint forces every particle magenta, as visual proof the
 * pixels come from this reimplementation.
 */
#include "particles.h"
#include "com_proxy.h"
#include "log.h"
#include <math.h>

#define PARTICLE_FVF       0x1e2  /* XYZ|PSIZE|DIFFUSE|SPECULAR|TEX1 — 0x20 stride */
#define PARTICLE_LOG_FIRST 8
#define FX_TINT_COLOUR     0xFFFF00FF

#define THISCALL __attribute__((thiscall))
typedef void  (THISCALL *ps_fill_fn)(ParticleSystem *);
typedef DWORD (THISCALL *ps_draw_fn)(ParticleSystem *, IDirect3DDevice3 *);
#define VT_FILL 9   /* vtable slot +0x24 */
#define VT_DRAW 12  /* vtable slot +0x30 */

static bool fx_tint(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_PARTICLE_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "tint") == 0);
        log_write("particle: FX mode = %s\n", cached ? "tint" : "off");
    }
    return cached != 0;
}

static DWORD node_colour(const ParticleNode *node)
{
    return fx_tint() ? FX_TINT_COLOUR : node->dwDiffuse;
}

/* Per-class state so one busy class can't hide the others: the first
 * PARTICLE_LOG_FIRST draws are logged, and so is the first draw that
 * actually carries vertices (an empty ring draws with verts=0). */
struct DrawLogState { LONG calls; LONG nonempty; };

static void log_draw(DrawLogState *st, const char *name, const void *self,
                     IDirect3DDevice3 *dev, DWORD count, HRESULT hr)
{
    bool report = InterlockedIncrement(&st->calls) <= PARTICLE_LOG_FIRST;
    if (count > 0 && InterlockedExchange(&st->nonempty, 1) == 0)
        report = true;
    if (report)
        log_write("particle: %s this=%p dev=%p verts=%lu -> hr=%08lX\n",
                  name, self, dev, count, hr);
}

/* ─── Fill: walk the ring from pRingHead until pRingCurrent ─── */

static void point_fill(PointParticleSystem *self)
{
    ParticleSystem *ps = &self->base;
    DWORD n = 0;
    if (ps->pRingHead != ps->pRingCurrent) {
        ParticleNode *node = ps->pRingHead;
        do {
            ParticleVertex *v = &self->pVerts[n++];
            v->flX = node->flX;
            v->flY = node->flY;
            v->flZ = node->flZ;
            v->dwDiffuse = node_colour(node);
            node = node->pNext;
        } while (node != ps->pRingCurrent);
    }
    self->dwVertexCount = n;
}

/* Shared by Face (baked corners) and XFace (per-particle corner-table entry):
 * emit 6 vertices = node position + the six xyz corner offsets. */
static void emit_face(ParticleVertex *v, const ParticleNode *node,
                      const float *corners /* 18 floats */)
{
    DWORD colour = node_colour(node);
    for (int c = 0; c < 6; c++, v++, corners += 3) {
        v->flX = node->flX + corners[0];
        v->flY = node->flY + corners[1];
        v->flZ = node->flZ + corners[2];
        v->dwDiffuse = colour;
    }
}

static void face_fill(FaceParticleSystem *self)
{
    ParticleSystem *ps = &self->base;
    DWORD n = 0;
    if (ps->pRingHead != ps->pRingCurrent) {
        ParticleNode *node = ps->pRingHead;
        do {
            emit_face(&self->pVerts[n], node, &self->flCorner[0][0]);
            n += 6;
            node = node->pNext;
        } while (node != ps->pRingCurrent);
    }
    self->nVertexCount = (WORD)n;
}

static void xface_fill(XFaceParticleSystem *self)
{
    ParticleSystem *ps = &self->base;
    DWORD n = 0;
    if (ps->pRingHead != ps->pRingCurrent) {
        ParticleNode *node = ps->pRingHead;
        do {
            emit_face(&self->pVerts[n], node,
                      &self->pCornerTable[node->dwShapeIndex].flCorner[0][0]);
            n += 6;
            node = node->pNext;
        } while (node != ps->pRingCurrent);
    }
    self->nVertexCount = (WORD)n;
}

/* ─── Draw ─── */

static DWORD point_draw(PointParticleSystem *self, IDirect3DDevice3 *dev)
{
    static DrawLogState st;
    HRESULT hr = dev->DrawPrimitive(D3DPT_POINTLIST, PARTICLE_FVF,
                                    self->pVerts, self->dwVertexCount, 0);
    log_draw(&st, "PointDraw", self, dev, self->dwVertexCount, hr);
    return self->dwVertexCount;
}

static DWORD face_draw(FaceParticleSystem *self, IDirect3DDevice3 *dev)
{
    static DrawLogState st;
    HRESULT hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, PARTICLE_FVF,
                                    self->pVerts, self->nVertexCount, 0);
    log_draw(&st, "FaceDraw", self, dev, self->nVertexCount, hr);
    return self->nVertexCount / 6;
}

/* Two-pass draw: colour layer with SRCBLEND=SRCCOLOR, alpha layer with
 * SRCBLEND=SRCALPHA, original src blend restored afterwards. */
static DWORD xface_draw(XFaceParticleSystem *self, IDirect3DDevice3 *dev)
{
    DWORD saved = 0;
    dev->GetRenderState(D3DRENDERSTATE_SRCBLEND, &saved);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCCOLOR);
    dev->DrawPrimitive(D3DPT_TRIANGLELIST, PARTICLE_FVF,
                       self->pVerts, self->nVertexCount, 0);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    HRESULT hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, PARTICLE_FVF,
                                    self->pVerts, self->nVertexCount, 0);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, saved);
    static DrawLogState st;
    log_draw(&st, "XFaceDraw", self, dev, self->nVertexCount, hr);
    return self->nVertexCount / 6;
}

/* ─── Exports — thiscall wrappers, installed into vtable slots by patch.py ─── */
extern "C" {

__declspec(dllexport) void THISCALL
Particle_PointFill(PointParticleSystem *self)  { point_fill(self); }

__declspec(dllexport) void THISCALL
Particle_FaceFill(FaceParticleSystem *self)    { face_fill(self); }

__declspec(dllexport) void THISCALL
Particle_XFaceFill(XFaceParticleSystem *self)  { xface_fill(self); }

__declspec(dllexport) DWORD THISCALL
Particle_PointDraw(PointParticleSystem *self, IDirect3DDevice3 *dev)
{ return point_draw(self, dev); }

__declspec(dllexport) DWORD THISCALL
Particle_FaceDraw(FaceParticleSystem *self, IDirect3DDevice3 *dev)
{ return face_draw(self, dev); }

__declspec(dllexport) DWORD THISCALL
Particle_XFaceDraw(XFaceParticleSystem *self, IDirect3DDevice3 *dev)
{ return xface_draw(self, dev); }

/* Base Render (Face/XFace slot 8): virtual dispatch of fill then draw,
 * exactly like the original 0x447d10 — through the (patched) vtable, so any
 * future override still wins. */
__declspec(dllexport) DWORD THISCALL
Particle_BaseRender(ParticleSystem *self, IDirect3DDevice3 *dev)
{
    ((ps_fill_fn)self->pVtable[VT_FILL])(self);
    return ((ps_draw_fn)self->pVtable[VT_DRAW])(self, dev);
}

/* Point Render (slot 8 override): the original dispatches fill virtually but
 * inlines the POINTLIST draw and never calls slot 12. */
__declspec(dllexport) DWORD THISCALL
Particle_PointRender(PointParticleSystem *self, IDirect3DDevice3 *dev)
{
    ((ps_fill_fn)self->base.pVtable[VT_FILL])(&self->base);
    return point_draw(self, dev);
}

} // extern "C"

/* ═══════════════ Stage B — tick + Face corner setup/transform ═══════════════
 *
 * Replaces (see PARTICLE_PLAN.md § 3, RE completed 2026-08-29):
 *   0x447ce0 ParticleSystem::TickSubObjects       → Particle_BaseTick
 *            (slot 7 in base/Point/Face vtables; its only direct E8 caller
 *             was the tail call inside the XFace tick override, also replaced)
 *   0x44ee90 XFaceParticleSystem tick override    → Particle_XFaceTick
 *   0x44d590 FaceParticleSystem::SetVector (slot 10)        → Particle_FaceSetVector
 *   0x44dac0 FaceParticleSystem::TransformCorners (slot 11) → Particle_FaceTransformCorners
 */

typedef void (THISCALL *gen_tick_fn)(void *, DWORD);
#define GEN_VT_TICK 3  /* Generator vtable slot +0x0c */

/* Row-major 4x4, row-vector convention (D3D style): out[r][c] = Σk a[r][k]·b[k][c]. */
typedef float Mat4[16];

static void mat_identity(Mat4 m)
{
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat_mul(Mat4 out, const Mat4 a, const Mat4 b)
{
    Mat4 t;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            t[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c]
                         + a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
    for (int i = 0; i < 16; i++) out[i] = t[i];
}

/* (v,1) × m as a row vector, with w-divide when w != 0 — the shared idiom of
 * FUN_00413350 and the inline loop in 0x44dac0. */
static void transform_point(float v[3], const Mat4 m)
{
    float o[4];
    for (int c = 0; c < 4; c++)
        o[c] = v[0] * m[0 * 4 + c] + v[1] * m[1 * 4 + c] + v[2] * m[2 * 4 + c] + m[3 * 4 + c];
    if (o[3] != 0.0f) {
        o[0] /= o[3]; o[1] /= o[3]; o[2] /= o[3];
    }
    v[0] = o[0]; v[1] = o[1]; v[2] = o[2];
}

/* Axis rotations exactly as 0x44ee90 lays them out (row-vector convention). */
static void mat_rot_x(Mat4 m, float a)
{
    mat_identity(m);
    float c = cosf(a), s = sinf(a);
    m[5] = c;  m[6] = -s;
    m[9] = s;  m[10] = c;
}
static void mat_rot_y(Mat4 m, float a)
{
    mat_identity(m);
    float c = cosf(a), s = sinf(a);
    m[0] = c;  m[2] = s;
    m[8] = -s; m[10] = c;
}
static void mat_rot_z(Mat4 m, float a)
{
    mat_identity(m);
    float c = cosf(a), s = sinf(a);
    m[0] = c;  m[1] = -s;
    m[4] = s;  m[5] = c;
}

/* Rotation-step threshold: fcompl against the double 0.01 @ 0x45f1f8. */
#define ROT_STEP_THRESHOLD 0.01

/* KAROO_PARTICLE_FX=spin exaggerates XFace rotation velocities ×10 as visual
 * proof the tick runs from this reimplementation. */
static bool fx_spin(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_PARTICLE_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "spin") == 0);
        if (cached)
            log_write("particle: FX mode = spin\n");
    }
    return cached != 0;
}

static void base_tick(ParticleSystem *self, DWORD dt)
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("particle: BaseTick active (this=%p dt=%lu)\n", self, dt);
    if (self->pGenerator)
        ((gen_tick_fn)((void ***)self->pGenerator)[0][GEN_VT_TICK])(self->pGenerator, dt);
    if (self->pEnvironment)
        ((gen_tick_fn)((void ***)self->pEnvironment)[0][GEN_VT_TICK])(self->pEnvironment, dt);
}

static void xface_tick(XFaceParticleSystem *self, DWORD dt)
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("particle: XFaceTick active (this=%p entries=%lu)\n",
                  self, self->dwCornerTableCount);
    if (self->pCornerTable) {
        float spin = fx_spin() ? 10.0f : 1.0f;
        for (DWORD i = 0; i < self->dwCornerTableCount; i++) {
            XFaceCornerEntry *e = &self->pCornerTable[i];
            Mat4 m;
            mat_identity(m);
            bool fired = false;
            for (int axis = 0; axis < 3; axis++)
                e->flRotAccum[axis] += e->flRotVel[axis] * spin;
            for (int axis = 0; axis < 3; axis++) {
                if ((double)e->flRotAccum[axis] > ROT_STEP_THRESHOLD) {
                    Mat4 rot;
                    switch (axis) {
                    case 0: mat_rot_x(rot, e->flRotAccum[axis]); break;
                    case 1: mat_rot_y(rot, e->flRotAccum[axis]); break;
                    default: mat_rot_z(rot, e->flRotAccum[axis]); break;
                    }
                    mat_mul(m, m, rot);
                    e->flRotAccum[axis] = 0.0f;
                    fired = true;
                }
            }
            if (fired)
                for (int c = 0; c < 6; c++)
                    transform_point(e->flCorner[c], m);
        }
    }
    base_tick(&self->base, dt);
}

/* ─── Face corner geometry (slots 10/11) ─── */

static void vec_cross(float o[3], const float a[3], const float b[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static float vec_len(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

/* Rebuild the six baked corner offsets from a direction vector: two scaled
 * basis vectors perpendicular to dir form a centred quad (two triangles
 * 0-1-2 / 3-4-5 with 3=2, 4=1, 5=1+2, all shifted by -(1+2)/2). */
static void face_set_vector(FaceParticleSystem *self, float x, float y, float z)
{
    float basis[3] = { 1.0f, 0.0f, 0.0f };
    if (x == 1.0f && y == 0.0f && z == 0.0f) {
        basis[0] = 0.0f; basis[1] = 1.0f;
    }
    float dir[3] = { x, y, z };
    float len = vec_len(dir);
    float d[3] = { x / len, y / len, z / len };

    float e2[3], e1[3];
    vec_cross(e2, d, basis);   /* corner 2 direction */
    vec_cross(e1, d, e2);      /* corner 1 direction (from unnormalised e2) */

    float s2 = self->flScale / vec_len(e2);
    float s1 = self->flScale / vec_len(e1);
    for (int i = 0; i < 3; i++) { e2[i] *= s2; e1[i] *= s1; }

    float (*c)[3] = self->flCorner;
    for (int i = 0; i < 3; i++) {
        c[0][i] = 0.0f;
        c[1][i] = e1[i];
        c[2][i] = e2[i];
        c[3][i] = e2[i];
        c[4][i] = e1[i];
        c[5][i] = e1[i] + e2[i];
    }
    float centre[3] = { c[5][0] * 0.5f, c[5][1] * 0.5f, c[5][2] * 0.5f };
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < 3; k++)
            c[i][k] -= centre[k];
}

/* ─── Stage B exports ─── */
extern "C" {

__declspec(dllexport) void THISCALL
Particle_BaseTick(ParticleSystem *self, DWORD dt)  { base_tick(self, dt); }

__declspec(dllexport) void THISCALL
Particle_XFaceTick(XFaceParticleSystem *self, DWORD dt)  { xface_tick(self, dt); }

__declspec(dllexport) void THISCALL
Particle_FaceSetVector(FaceParticleSystem *self, float x, float y, float z)
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("particle: FaceSetVector active (this=%p dir=%f,%f,%f)\n",
                  self, x, y, z);
    face_set_vector(self, x, y, z);
}

__declspec(dllexport) void THISCALL
Particle_FaceTransformCorners(FaceParticleSystem *self, float *matrix)
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("particle: FaceTransformCorners active (this=%p)\n", self);
    for (int c = 0; c < 6; c++)
        transform_point(self->flCorner[c], matrix);
}

} // extern "C"
