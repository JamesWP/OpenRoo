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

static void log_draw(const char *name, const void *self, IDirect3DDevice3 *dev,
                     DWORD count, HRESULT hr)
{
    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= PARTICLE_LOG_FIRST)
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
            const float *corners = (const float *)
                ((const char *)self->pCornerTable + node->dwShapeIndex * 100);
            emit_face(&self->pVerts[n], node, corners);
            n += 6;
            node = node->pNext;
        } while (node != ps->pRingCurrent);
    }
    self->nVertexCount = (WORD)n;
}

/* ─── Draw ─── */

static DWORD point_draw(PointParticleSystem *self, IDirect3DDevice3 *dev)
{
    HRESULT hr = dev->DrawPrimitive(D3DPT_POINTLIST, PARTICLE_FVF,
                                    self->pVerts, self->dwVertexCount, 0);
    log_draw("PointDraw", self, dev, self->dwVertexCount, hr);
    return self->dwVertexCount;
}

static DWORD face_draw(FaceParticleSystem *self, IDirect3DDevice3 *dev)
{
    HRESULT hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, PARTICLE_FVF,
                                    self->pVerts, self->nVertexCount, 0);
    log_draw("FaceDraw", self, dev, self->nVertexCount, hr);
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
    log_draw("XFaceDraw", self, dev, self->nVertexCount, hr);
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
