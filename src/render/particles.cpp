/* ParticleSystem: the fill/draw/render methods, the tick, the Face corner
 * geometry, the ring, and every class's lifecycle, serialisation and vtable
 * (particles.h).
 *
 * Fill writes only xyz + diffuse into the scratch vertex buffer; psize,
 * specular and u/v stay as the allocators left them.
 *
 * KAROO_PARTICLE_FX=tint forces every particle magenta; =spin exaggerates
 * XFace rotation velocities x10. */

#include "particles.h"
#include "generators.h"
#include "factory.h"
#include "com_proxy.h"
#include "log.h"
#include "determinism.h"
#include <stdlib.h>
#include "assetio.h"
#include "clock.h"
#include "crtrand.h"
#include "gamelog.h"
#include "gamestr.h"
#include <math.h>
#include <string.h>
#include <new>
#include <stdio.h>

#define PARTICLE_FVF       0x1e2  // XYZ|PSIZE|DIFFUSE|SPECULAR|TEX1, 0x20 stride
#define PARTICLE_LOG_FIRST 8
#define FX_TINT_COLOUR     0xFFFF00FF

#define THISCALL __attribute__((thiscall))
typedef void  (THISCALL *ps_fill_fn)(ParticleSystem *);
typedef DWORD (THISCALL *ps_draw_fn)(ParticleSystem *, IDirect3DDevice3 *);

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
 * PARTICLE_LOG_FIRST draws are logged, and so is the first draw that actually
 * carries vertices (an empty ring draws with verts=0). */
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

/* ─── Fill ─────────────────────────────────────────────────────────────────
 *
 * All three fills walk the live region of the ring the generator emits into
 * and the environment retires from.  `emit` writes the vertices for one node
 * and returns how many it wrote. */
template <typename EmitFn>
static DWORD fill_ring(ParticleSystem *ps, EmitFn emit)
{
    DWORD n = 0;
    if (ps->ring.pRingHead != ps->ring.pRingCurrent) {
        ParticleNode *node = ps->ring.pRingHead;
        do {
            n += emit(node, n);
            node = node->pNext;
        } while (node != ps->ring.pRingCurrent);
    }
    return n;
}

static void point_fill(PointParticleSystem *self)
{
    self->dwVertexCount = fill_ring(&self->base,
        [self](const ParticleNode *node, DWORD n) -> DWORD {
            ParticleVertex *v = &self->pVerts[n];
            v->flX = node->flX;
            v->flY = node->flY;
            v->flZ = node->flZ;
            v->dwDiffuse = node_colour(node);
            return 1;
        });
}

/* Face (baked corners) and XFace (per-particle corner-table entry): six
 * vertices = node position + the six xyz corner offsets. */
static void emit_face(ParticleVertex *v, const ParticleNode *node,
                      const float *corners )  // 18 floats
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
    self->nVertexCount = (WORD)fill_ring(&self->base,
        [self](const ParticleNode *node, DWORD n) -> DWORD {
            emit_face(&self->pVerts[n], node, &self->flCorner[0][0]);
            return 6;
        });
}

static void xface_fill(XFaceParticleSystem *self)
{
    self->nVertexCount = (WORD)fill_ring(&self->base,
        [self](const ParticleNode *node, DWORD n) -> DWORD {
            emit_face(&self->pVerts[n], node,
                      &self->pCornerTable[node->dwShapeIndex].flCorner[0][0]);
            return 6;
        });
}

/* ─── Draw ─────────────────────────────────────────────────────────────────
 */
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

/* Two passes, one per face winding: save CULLMODE, draw with D3DCULL_CCW, draw
 * again with D3DCULL_CW, restore.  That makes an XFace billboard two-sided: a
 * particle whose corner table has rotated past edge-on is still drawn. */
static DWORD xface_draw(XFaceParticleSystem *self, IDirect3DDevice3 *dev)
{
    DWORD saved = 0;
    dev->GetRenderState(D3DRENDERSTATE_CULLMODE, &saved);
    dev->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_CCW);
    dev->DrawPrimitive(D3DPT_TRIANGLELIST, PARTICLE_FVF,
                       self->pVerts, self->nVertexCount, 0);
    dev->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_CW);
    HRESULT hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, PARTICLE_FVF,
                                    self->pVerts, self->nVertexCount, 0);
    dev->SetRenderState(D3DRENDERSTATE_CULLMODE, saved);
    static DrawLogState st;
    log_draw(&st, "XFaceDraw", self, dev, self->nVertexCount, hr);
    return self->nVertexCount / 6;
}

/* ─── Render exports ───────────────────────────────────────────────────────
 */
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

/* Base Render (Face/XFace slot 8): fill then draw, through slots 9 and 12. */
__declspec(dllexport) DWORD THISCALL
Particle_BaseRender(ParticleSystem *self, IDirect3DDevice3 *dev)
{
    ps_fill(self);
    return ps_draw(self, dev);
}

/* Point Render (slot 8 override): fills through the vtable but draws the
 * POINTLIST itself, never calling slot 12. */
__declspec(dllexport) DWORD THISCALL
Particle_PointRender(PointParticleSystem *self, IDirect3DDevice3 *dev)
{
    ps_fill(&self->base);
    return point_draw(self, dev);
}

}  // extern "C"

/* ─── Dispatch ─────────────────────────────────────────────────────────────
 *
 * One virtual call each: every ParticleSystem carries one of this file's
 * vtables, so slots 9 and 12 land directly on the class's fill and draw. */
void ps_fill(ParticleSystem *self)
{
    ((ps_fill_fn)self->pVtable[PS_VT_FILL])(self);
}

DWORD ps_draw(ParticleSystem *self, IDirect3DDevice3 *dev)
{
    return ((ps_draw_fn)self->pVtable[PS_VT_DRAW])(self, dev);
}

/* ─── Tick and Face corner transform ───────────────────────────────────────
 *
 * Row-major 4x4, row-vector convention (D3D style): out[r][c] = sum_k a[r][k]
 * * b[k][c]. */
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

/* (v,1) x m as a row vector, w-divided when w != 0. */
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

/* Axis rotations, row-vector convention. */
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

/* A rotation angle is baked into the corners once it exceeds this. */
#define ROT_STEP_THRESHOLD 0.01

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

/* dt reaches slot 7 as untyped 4-byte bits; the emitters and integrators use
 * it as a float.  Reinterpret once here, at the vtable boundary. */
static float relay_dt(DWORD dt)
{
    float fdt;
    memcpy(&fdt, &dt, sizeof fdt);
    return fdt;
}

static void base_tick(ParticleSystem *self, float dt)
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("particle: BaseTick active (this=%p dt=%f gen=%p env=%p)\n",
                  self, dt, self->pGenerator, self->pEnvironment);
    if (self->pGenerator)
        sim_tick_slot3(self->pGenerator, dt);
    if (self->pEnvironment)
        sim_tick_slot3(self->pEnvironment, dt);
    dethash_particles(self);
}

static void xface_tick(XFaceParticleSystem *self, float dt)
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

/* ─── Face corner geometry (slots 10/11) ───────────────────────────────────
 */
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
 * basis vectors perpendicular to dir form a centred quad (two triangles 0-1-2
 * / 3-4-5 with 3=2, 4=1, 5=1+2, all shifted by -(1+2)/2). */
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
    vec_cross(e2, d, basis);  // corner 2 direction
    vec_cross(e1, d, e2);     // corner 1 direction (from unnormalised e2)

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

/* ─── The ring and the ParticleSystem lifecycle ────────────────────────────
 *
 * The slots that make, copy, resize, serialise and destroy a system, plus the
 * RingBuffer they all work on.  The vtables are defined at the foot of this
 * file, one per class, and the constructors install them. */
extern void *const ps_vtbl_base[];
extern void *const ps_vtbl_point[];
extern void *const ps_vtbl_face[];
extern void *const ps_vtbl_xface[];

static bool ps_write(const void *src, unsigned size, void *fp)
{
    return fwrite(src, size, 1, (FILE *)fp) == 1;
}

static bool ps_read(void *dst, unsigned size, void *fp)
{
    return hooks_fread(dst, size, 1, fp) == 1;
}

/* Sampling constant shared with the generators: 1/32767. */
static const float PS_RAND_SCALE = 1.0f / 32767.0f;

/* ─── RingBuffer ───────────────────────────────────────────────────────────
 *
 * The ctor: five zeroes, no allocation. */
static __attribute__((unused)) void ring_init(RingBuffer *ring)
{
    ring->dwRingCount  = 0;
    ring->pRingBase    = NULL;
    ring->pRingHead    = NULL;
    ring->pRingTail    = NULL;
    ring->pRingCurrent = NULL;
}

/* Free the node block.  PRESERVED: pRingTail is not cleared; the other four
 * fields are. */
static void ring_free(RingBuffer *ring)
{
    if (ring->pRingBase)
        ::operator delete(ring->pRingBase);
    ring->dwRingCount  = 0;
    ring->pRingBase    = NULL;
    ring->pRingHead    = NULL;
    ring->pRingCurrent = NULL;
}

/* Give every node but the last a random shape index in [0, shapes-1].
 * DETERMINISM: rand() is reseeded from the game clock on every call, and the
 * last node is skipped (the loop bound is dwRingCount - 1). */
static void ring_assign_shapes(RingBuffer *ring, DWORD shapes)
{
    CRT_RAND_SEED = (unsigned)hooks_GameTime(NULL);
    if (ring->dwRingCount - 1 == 0)
        return;
    float span = (float)(int)(shapes - 1);
    for (DWORD i = 0; i < ring->dwRingCount - 1; i++) {
        int r = (int)crt_rand();
        ring->pRingBase[i].dwShapeIndex =
            (DWORD)(int)((double)r * span * PS_RAND_SCALE + 0.5);
    }
}

/* (Re)allocate `count` nodes and thread them into one doubly-linked list: head
 * and current at the front, tail at the last node, both ends NULL.  Fewer than
 * two nodes is refused. */
static BOOL ring_alloc(RingBuffer *ring, DWORD count, DWORD shapes)
{
    ring_free(ring);
    if (count < 2)
        return FALSE;
    ring->dwRingCount = count;
    unsigned bytes = count * sizeof(ParticleNode);
    ParticleNode *base = (ParticleNode *)::operator new(bytes, std::nothrow);
    ring->pRingBase = base;
    if (base == NULL)
        return FALSE;
    ring->pRingTail    = base + (count - 1);
    ring->pRingHead    = base;
    ring->pRingCurrent = base;
    memset(base, 0, bytes);

    base[0].pPrev = NULL;
    base[0].pNext = &base[1];
    for (DWORD i = 1; i + 1 < count; i++) {
        base[i].pPrev = &base[i - 1];
        base[i].pNext = &base[i + 1];
    }
    base[count - 1].pPrev = &base[count - 2];
    base[count - 1].pNext = NULL;

    ring_assign_shapes(ring, shapes);
    return TRUE;
}

/* ─── ParticleSystem, the base class ───────────────────────────────────────
 */
typedef void  (THISCALL *ps_dtor_fn)(void *, unsigned);
typedef void  (THISCALL *ps_release_fn)(void *, int);
typedef BOOL  (THISCALL *ps_attach_fn)(void *, void *);
typedef BOOL  (THISCALL *ps_stream_fn)(void *, void *);
typedef BOOL  (THISCALL *ps_count_fn)(void *, DWORD);

/* Drop a Generator or an Environment through its own slot 0. */
static void sub_object_delete(void *obj)
{
    void **vtbl = *(void ***)obj;
    ((ps_dtor_fn)vtbl[0])(obj, 1);
}

/* Slot 1: release both sub-objects (each through its own scalar deleting dtor)
 * and free the ring.  The null tests are doubled. */
static void ps_release(ParticleSystem *self, int flags)
{
    if (self->pGenerator && flags && self->pGenerator)
        sub_object_delete(self->pGenerator);
    self->pGenerator = NULL;
    if (self->pEnvironment && flags && self->pEnvironment)
        sub_object_delete(self->pEnvironment);
    self->pEnvironment = NULL;
    ring_free(&self->ring);
}

/* The dtor body: restore the base vtable, release, free the ring.  The release
 * call is direct, not virtual, so a subclass's override does not run here. */
static void ps_base_destruct(ParticleSystem *self)
{
    self->pVtable = (void **)ps_vtbl_base;
    ps_release(self, 1);
    ring_free(&self->ring);
}

/* Slot 3, SetCapacity: size the ring, no shape indices. */
static BOOL ps_set_capacity(ParticleSystem *self, DWORD count)
{
    return ring_alloc(&self->ring, count, 0) != 0;
}

/* Slot 4, Resize: free, then allocate again (ring_alloc frees as well). */
static BOOL ps_resize(ParticleSystem *self, DWORD count)
{
    ring_free(&self->ring);
    return ring_alloc(&self->ring, count, 0);
}

/* Slots 5 and 6.  Hand the ring to the new sub-object first; only if it
 * accepts is the old one dropped and the pointer replaced.  A NULL argument is
 * refused. */
static BOOL ps_set_sub_object(void **slot, RingBuffer *ring, void *obj)
{
    if (obj == NULL)
        return FALSE;
    void **vtbl = *(void ***)obj;
    if (!((ps_attach_fn)vtbl[2])(obj, ring))  // AttachRing, slot 2
        return FALSE;
    if (*slot)
        sub_object_delete(*slot);
    *slot = obj;
    return TRUE;
}

/* Slot 2, CopyFrom.  Releases self (virtually, so a subclass frees its vertex
 * buffer too), refuses a source of a different class, re-makes the ring at the
 * source's size, then clones the generator and the environment and attaches
 * each through slots 5 and 6.  Every failure releases again. */
static BOOL ps_copy_from(ParticleSystem *self, const ParticleSystem *src)
{
    ((ps_release_fn)self->pVtable[PS_VT_RELEASE])(self, 1);
    if (strcmp(src->pName, self->pName) != 0)
        return FALSE;
    if (!ring_alloc(&self->ring, src->ring.dwRingCount, 0))
        return FALSE;

    if (src->pGenerator) {
        Generator *gen = gen_clone(src->pGenerator);
        if (gen == NULL) {
            ((ps_release_fn)self->pVtable[PS_VT_RELEASE])(self, 1);
            return FALSE;
        }
        if (!((ps_attach_fn)self->pVtable[PS_VT_SETGEN])(self, gen)) {
            ((ps_release_fn)self->pVtable[PS_VT_RELEASE])(self, 1);
            return FALSE;
        }
    }
    if (src->pEnvironment) {
        Environment *env = env_clone(src->pEnvironment);
        if (env == NULL) {
            ((ps_release_fn)self->pVtable[PS_VT_RELEASE])(self, 1);
            return FALSE;
        }
        if (!((ps_attach_fn)self->pVtable[PS_VT_SETENV])(self, env)) {
            ((ps_release_fn)self->pVtable[PS_VT_RELEASE])(self, 1);
            return FALSE;
        }
    }
    return TRUE;
}

/* ─── Serialize / Deserialize (slots 13 and 14) ────────────────────────────
 *
 * Both take (FILE *, GameLogger *) and return BOOL; the base ignores the
 * logger, the subclasses report through it.
 *
 * FORMAT: a sub-object's class name is its length including the terminator,
 * then that many bytes, so the NUL goes to the file too.  A missing sub-object
 * writes the literal "NULL". */
static BOOL ps_write_sub_object(const void *obj, void *fp)
{
    const char *name = obj ? *(const char *const *)((const BYTE *)obj + 4)
                           : GS_PS_NAME_NULL;
    DWORD len = (DWORD)strlen(name) + 1;
    if (!ps_write(&len, 4, fp))
        return FALSE;
    if (fwrite(name, 1, len, (FILE *)fp) != len)
        return FALSE;
    if (obj) {
        void **vtbl = *(void ***)obj;
        if (!((ps_stream_fn)vtbl[GEN_VT_SAVE_SLOT])((void *)obj, fp))
            return FALSE;
    }
    return TRUE;
}

/* Slot 13: ring size, then the generator and the environment. */
static BOOL ps_serialize(ParticleSystem *self, void *fp, GameLogger *)
{
    if (fp == NULL)
        return FALSE;
    if (!ps_write(&self->ring.dwRingCount, 4, fp))
        return FALSE;
    if (!ps_write_sub_object(self->pGenerator, fp))
        return FALSE;
    return ps_write_sub_object(self->pEnvironment, fp);
}

/* Read one length-prefixed class name into a fresh buffer.  NULL on failure;
 * the caller frees. */
static char *ps_read_name(void *fp, GameLogger *log, int line_len,
                          int line_name, const char *msg_name)
{
    DWORD len;
    if (!ps_read(&len, 4, fp)) {
        GameLog_LogSourceLocation(log, 4, GS_PS_SRC_FILE, line_len, GS_PS_MSG_NOLEN);
        return NULL;
    }
    char *name = (char *)::operator new(len, std::nothrow);
    if (hooks_fread(name, 1, len, fp) != len) {
        GameLog_LogSourceLocation(log, 4, GS_PS_SRC_FILE, line_name, msg_name);
        ::operator delete(name);
        return NULL;
    }
    return name;
}

/* Slot 14: release, re-make the ring at the stored size, then read each
 * sub-object's class name, build it through the factory, Load it and attach it
 * through slots 5 and 6.  The ring is sized directly rather than through slot
 * 3, so a subclass's capacity override does not run here. */
static BOOL ps_deserialize(ParticleSystem *self, void *fp, GameLogger *log)
{
    ((ps_release_fn)self->pVtable[PS_VT_RELEASE])(self, 1);

    DWORD count;
    if (!ps_read(&count, 4, fp)) {
        GameLog_LogSourceLocation(log, 4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_NOCOUNT);
        return FALSE;
    }
    if (!ring_alloc(&self->ring, count, 0)) {
        GameLog_LogSourceLocation(log, 4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_NORING);
        return FALSE;
    }

    char *name = ps_read_name(fp, log, __LINE__, __LINE__, GS_PS_MSG_NONAME);
    if (name == NULL)
        return FALSE;
    if (strcmp(name, GS_PS_NAME_NULL) != 0) {
        Generator *gen = gen_create(name);
        if (gen == NULL) {
            GameLog_LogSourceLocation(log, 4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_NOGEN, name);
            ::operator delete(name);
            return FALSE;
        }
        ::operator delete(name);
        void **gvt = *(void ***)gen;
        if (!((ps_stream_fn)gvt[GEN_VT_LOAD_SLOT])(gen, fp)) {
/* PRESERVED: `name` was freed above and is still handed to the logger. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
            GameLog_LogSourceLocation(log, 4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_GENLOAD, name);
#pragma GCC diagnostic pop
            sub_object_delete(gen);
            return FALSE;
        }
        ((ps_attach_fn)self->pVtable[PS_VT_SETGEN])(self, gen);
    }

    // PRESERVED: a sub-object named "NULL" leaks `name`.
    name = ps_read_name(fp, log, __LINE__, __LINE__, GS_PS_MSG_NOENV);
    if (name == NULL)
        return FALSE;
    if (strcmp(name, GS_PS_NAME_NULL) != 0) {
        Environment *env = env_create(name);
        if (env == NULL) {
            GameLog_LogSourceLocation(log, 4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_ENVNAME, name);
            ::operator delete(name);
            return FALSE;
        }
        ::operator delete(name);
        void **evt = *(void ***)env;
        if (!((ps_stream_fn)evt[GEN_VT_LOAD_SLOT])(env, fp)) {
/* PRESERVED: the same use-after-free as the generator branch. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
            GameLog_LogSourceLocation(log, 4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_ENVLOAD, name);
#pragma GCC diagnostic pop
            sub_object_delete(env);
            return FALSE;
        }
        ((ps_attach_fn)self->pVtable[PS_VT_SETENV])(self, env);
    }
    return TRUE;
}

/* ─── PointParticleSystem and FaceParticleSystem lifecycles ────────────────
 *
 * Point's vertex buffer: one 0x20-byte vertex per ring node.  The count field
 * is not touched here; Fill maintains it. */
static BOOL point_alloc_verts(PointParticleSystem *self)
{
    if (self->pVerts)
        ::operator delete(self->pVerts);
    void *p = ::operator new(self->base.ring.dwRingCount * sizeof(ParticleVertex),
                             std::nothrow);
    if (p) {
        self->pVerts = (ParticleVertex *)p;
        return TRUE;
    }
    self->pVerts = NULL;
    return FALSE;
}

/* Six vertices per ring node, zeroed, then the texture corners baked in.  Face
 * and XFace use different corner orders:
 *   Face : (0,0) (1,0) (0,1) (0,1) (1,0) (1,1)
 *   XFace: (0,0) (1,1) (0,1) (1,0) (1,1) (0,0) */
static BOOL quad_alloc_verts(ParticleVertex **slot, DWORD nodes, bool xface)
{
    if (xface && *slot)
        ::operator delete(*slot);  // XFace frees first; Face does not
    unsigned bytes = nodes * 6 * sizeof(ParticleVertex);
    ParticleVertex *v = (ParticleVertex *)::operator new(bytes, std::nothrow);
    *slot = v;
    if (v == NULL)
        return FALSE;
    memset(v, 0, bytes);

    static const float FACE_UV[6][2]  = { {0,0}, {1,0}, {0,1}, {0,1}, {1,0}, {1,1} };
    static const float XFACE_UV[6][2] = { {0,0}, {1,1}, {0,1}, {1,0}, {1,1}, {0,0} };
    const float (*uv)[2] = xface ? XFACE_UV : FACE_UV;
    for (DWORD i = 0; i < nodes; i++)
        for (int c = 0; c < 6; c++) {
            v[i * 6 + c].flU = uv[c][0];
            v[i * 6 + c].flV = uv[c][1];
        }
    return TRUE;
}

static BOOL face_alloc_verts(FaceParticleSystem *self)
{
    return quad_alloc_verts(&self->pVerts, self->base.ring.dwRingCount, false);
}

/* Slot 1 for both Point and Face: drop the vertex buffer, then the base
 * release. */
static void quad_release(ParticleSystem *self, int flags)
{
    ParticleVertex **verts = (ParticleVertex **)((BYTE *)self + 0x28);
    if (*verts)
        ::operator delete(*verts);
    *verts = NULL;
    ps_release(self, flags);
}

/* The two dtor bodies: own vtable, the shared release (called directly, not
 * virtually), then the base body. */
static void quad_destruct(ParticleSystem *self, void *const *vtbl)
{
    self->pVtable = (void **)vtbl;
    quad_release(self, 1);
    ps_base_destruct(self);
}

/* Point slots 3 and 4: size the ring, then rebuild the vertex buffer.  Both
 * return the second step's result. */
static BOOL point_set_capacity(PointParticleSystem *self, DWORD count)
{
    if (!ps_set_capacity(&self->base, count))
        return FALSE;
    return point_alloc_verts(self);
}

static BOOL point_resize(PointParticleSystem *self, DWORD count)
{
    if (!ps_resize(&self->base, count))
        return FALSE;
    return point_alloc_verts(self);
}

/* Point slot 2.  Releases virtually first, and the base CopyFrom releases
 * again. */
static BOOL point_copy_from(PointParticleSystem *self, const ParticleSystem *src)
{
    ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
    if (!ps_copy_from(&self->base, src))
        return FALSE;
    return point_alloc_verts(self);
}

/* Point slot 13: the base write, whose result is discarded, then success. */
static BOOL point_serialize(PointParticleSystem *self, void *fp, GameLogger *log)
{
    ps_serialize(&self->base, fp, log);
    return TRUE;
}

/* Point slot 14. */
static BOOL point_deserialize(PointParticleSystem *self, void *fp, GameLogger *log)
{
    if (!ps_deserialize(&self->base, fp, log))
        return FALSE;
    if (!point_alloc_verts(self)) {
        GameLog_LogSourceLocation(log, 4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_PTVERTS);
        return FALSE;
    }
    return TRUE;
}

/* Face slot 3: release virtually, size the ring, reset the scale to 1 and
 * rebuild the corners. */
static BOOL face_set_capacity(FaceParticleSystem *self, DWORD count)
{
    ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
    if (!ps_set_capacity(&self->base, count))
        return FALSE;
    self->flScale = 1.0f;
    return face_alloc_verts(self);
}

/* Face slot 2: the scale comes from the source. */
static BOOL face_copy_from(FaceParticleSystem *self, const FaceParticleSystem *src)
{
    ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
    if (!ps_copy_from(&self->base, &src->base))
        return FALSE;
    self->flScale = src->flScale;
    return face_alloc_verts(self);
}

/* Face slot 4. */
static BOOL face_resize(FaceParticleSystem *self, DWORD count)
{
    if (!ps_resize(&self->base, count))
        return FALSE;
    if (self->pVerts)
        ::operator delete(self->pVerts);
    self->pVerts = NULL;
    return face_alloc_verts(self);
}

/* Face slot 13: the base write (result discarded, as Point's), then the scale.
 */
static BOOL face_serialize(FaceParticleSystem *self, void *fp, GameLogger *log)
{
    ps_serialize(&self->base, fp, log);
    if (!ps_write(&self->flScale, 4, fp)) {
        GameLog_LogSourceLocation(log, 4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_SAVESIZE);
        return FALSE;
    }
    return TRUE;
}

/* Face slot 14. */
static BOOL face_deserialize(FaceParticleSystem *self, void *fp, GameLogger *log)
{
    if (!ps_deserialize(&self->base, fp, log))
        return FALSE;
    if (!ps_read(&self->flScale, 4, fp)) {
        GameLog_LogSourceLocation(log, 4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_FACESIZE);
        return FALSE;
    }
    if (!face_alloc_verts(self)) {
        GameLog_LogSourceLocation(log, 4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_VERTARR);
        return FALSE;
    }
    return TRUE;
}

/* ─── XFaceParticleSystem lifecycle ────────────────────────────────────────
 */
static BOOL xface_alloc_verts(XFaceParticleSystem *self)
{
    return quad_alloc_verts(&self->pVerts, self->base.ring.dwRingCount, true);
}

/* Slot 1: drop the corner table and the vertex buffer, then the base release.
 */
static void xface_release(XFaceParticleSystem *self, int flags)
{
    if (self->pCornerTable)
        ::operator delete(self->pCornerTable);
    self->pCornerTable = NULL;
    if (self->pVerts)
        ::operator delete(self->pVerts);
    self->pVerts = NULL;
    ps_release(&self->base, flags);
}

/* The dtor body runs the base destructor twice and never calls its own slot 1.
 * PRESERVED: destroying an XFace leaks its corner table and vertex buffer. */
static void xface_destruct(XFaceParticleSystem *self)
{
    self->base.pVtable = (void **)ps_vtbl_xface;
    ps_base_destruct(&self->base);
    ps_base_destruct(&self->base);
}

/* The corner transform divides by w when w != 1.0 -- not the `w != 0` test
 * transform_point uses for the Face corners. */
static void xface_transform_corner(float v[3], const Mat4 m)
{
    float o[4];
    for (int c = 0; c < 4; c++)
        o[c] = v[0] * m[0 * 4 + c] + v[1] * m[1 * 4 + c] + v[2] * m[2 * 4 + c] + m[3 * 4 + c];
    if ((double)o[3] != 1.0) {
        o[0] /= o[3]; o[1] /= o[3]; o[2] /= o[3];
    }
    v[0] = o[0]; v[1] = o[1]; v[2] = o[2];
}

/* cos(acos(a.b / (|a||b|))): the cosine of the angle between a and b, the long
 * way round. */
static float corner_direction_cosine(const float a[3], const float b[3])
{
    double dot = ((double)a[2] * b[2] + (double)a[1] * b[1]) + (double)a[0] * b[0];
    double la = sqrt(((double)a[0] * a[0] + (double)a[1] * a[1]) + (double)a[2] * a[2]);
    double lb = sqrt(((double)b[0] * b[0] + (double)b[1] * b[1]) + (double)b[2] * b[2]);
    return (float)cos(acos(dot / (la * lb)));
}

/* DETERMINISM: one rand() draw, mapped to [-1, 1]. */
static float rand_signed_unit(void)
{
    return (float)((double)(int)crt_rand() * (2.0f / 32767.0f) - 1.0f);
}

/* Build dwCornerTableCount corner entries.  Each is a quad in the XZ plane
 * whose half-size steps from flSizeMin to flSizeMax, rotated into a random
 * orientation, plus three per-entry values drawn from the lifetime, speed and
 * rotation ranges.
 *
 * PRESERVED: those three land in flRotVel[0..2], which the tick treats as
 * X/Y/Z rotation velocities. */
static BOOL xface_build_corners(XFaceParticleSystem *self)
{
    if (self->pCornerTable)
        ::operator delete(self->pCornerTable);
    DWORD count = self->dwCornerTableCount;
    XFaceCornerEntry *table =
        (XFaceCornerEntry *)::operator new(count * sizeof(XFaceCornerEntry), std::nothrow);
    self->pCornerTable = table;
    if (table == NULL)
        return FALSE;

    const float *p = (const float *)((const BYTE *)self + 0x76);  // the 8 ranges
    float size_min = p[0], size_max = p[1];
    float life_min = p[2], life_max = p[3];
    float speed_min = p[4], speed_max = p[5];
    float rot_min = p[6], rot_max = p[7];

    float step = (float)(((double)size_max - size_min) / (double)(int)count);
    static const float AXIS[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

    for (DWORD i = 0; i < count; i++) {
        XFaceCornerEntry *e = &table[i];
        float s = (float)((double)(int)(i + 1) * step + size_min);
        float n = -s;
        const float quad[6][3] = { { n, 0, n }, { s, 0, s }, { n, 0, s },
                                   { s, 0, n }, { s, 0, s }, { n, 0, n } };
        memcpy(e->flCorner, quad, sizeof quad);

        float axis[3] = { rand_signed_unit(), rand_signed_unit(), rand_signed_unit() };
        bool along_x = !(axis[0] < 1.0f || axis[0] > 1.0f)
                    && !(axis[1] < 0.0f || axis[1] > 0.0f)
                    && !(axis[2] < 0.0f || axis[2] > 0.0f);
        const float *ref = along_x ? AXIS[1] : AXIS[0];

        float u[3], v[3];
        vec_cross(u, axis, ref);
        vec_cross(v, axis, u);

        const float *rows[3] = { u, axis, v };
        Mat4 m;
        mat_identity(m);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                m[r * 4 + c] = corner_direction_cosine(rows[r], AXIS[c]);
        for (int c = 0; c < 6; c++)
            xface_transform_corner(e->flCorner[c], m);

        e->flRotVel[0] = (float)(((double)life_max - life_min)
                                 * (int)crt_rand() * PS_RAND_SCALE + life_min);
        e->flRotVel[1] = (float)(((double)speed_max - speed_min)
                                 * (int)crt_rand() * PS_RAND_SCALE + speed_min);
        e->flRotVel[2] = (float)(((double)rot_max - rot_min)
                                 * (int)crt_rand() * PS_RAND_SCALE + rot_min);
        e->flRotAccum[0] = e->flRotAccum[1] = e->flRotAccum[2] = 0.0f;
    }
    return TRUE;
}

/* Slot 3.  The base capacity result is discarded; the shape indices are
 * re-drawn from the corner count. */
static BOOL xface_set_capacity(XFaceParticleSystem *self, DWORD count)
{
    ps_set_capacity(&self->base, count);
    ring_assign_shapes(&self->base.ring, self->dwCornerTableCount);
    if (!xface_build_corners(self)) {
        ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
        return FALSE;
    }
    if (!xface_alloc_verts(self)) {
        ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
        return FALSE;
    }
    return TRUE;
}

/* Slot 2.  No virtual release first, unlike Point and Face. */
static BOOL xface_copy_from(XFaceParticleSystem *self, const XFaceParticleSystem *src)
{
    if (!ps_copy_from(&self->base, &src->base))
        return FALSE;
    self->dwCornerTableCount = src->dwCornerTableCount;
    memcpy((BYTE *)self + 0x76, (const BYTE *)src + 0x76, 0x20);  // the 8 ranges
    ring_assign_shapes(&self->base.ring, self->dwCornerTableCount);
    if (!xface_build_corners(self)) {
        ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
        return FALSE;
    }
    if (!xface_alloc_verts(self)) {
        ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
        return FALSE;
    }
    return TRUE;
}

/* Slot 4.  Vertices before corners here; the other two do corners first. */
static BOOL xface_resize(XFaceParticleSystem *self, DWORD count)
{
    if (!ps_resize(&self->base, count))
        return FALSE;
    if (self->pVerts)
        ::operator delete(self->pVerts);
    self->pVerts = NULL;
    if (self->pCornerTable)
        ::operator delete(self->pCornerTable);
    self->pCornerTable = NULL;
    ring_assign_shapes(&self->base.ring, self->dwCornerTableCount);
    if (!xface_alloc_verts(self)) {
        ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
        return FALSE;
    }
    if (!xface_build_corners(self)) {
        ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
        return FALSE;
    }
    return TRUE;
}

/* Slots 13 and 14: the base stream, then the corner count and the eight
 * ranges.  Load finishes by re-running its own slot 3 with the ring size,
 * which rebuilds the corners and vertices. */
static BOOL xface_serialize(XFaceParticleSystem *self, void *fp, GameLogger *log)
{
    if (!ps_serialize(&self->base, fp, log))
        return FALSE;
    if (!ps_write(&self->dwCornerTableCount, 4, fp))
        goto failed;
    for (int i = 0; i < 8; i++)
        if (!ps_write((const BYTE *)self + 0x76 + i * 4, 4, fp))
            goto failed;
    return TRUE;
failed:
    GameLog_LogSourceLocation(log, 4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_XSAVE);
    return FALSE;
}

static BOOL xface_deserialize(XFaceParticleSystem *self, void *fp, GameLogger *log)
{
    if (!ps_deserialize(&self->base, fp, log))
        return FALSE;
    if (!ps_read(&self->dwCornerTableCount, 4, fp))
        goto failed;
    for (int i = 0; i < 8; i++)
        if (!ps_read((BYTE *)self + 0x76 + i * 4, 4, fp))
            goto failed;
    return ((ps_count_fn)self->base.pVtable[PS_VT_SETCAP])
               (self, self->base.ring.dwRingCount);
failed:
    ((ps_release_fn)self->base.pVtable[PS_VT_RELEASE])(self, 1);
    GameLog_LogSourceLocation(log, 4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_XLOAD);
    return FALSE;
}

/* ─── The non-virtual interface ────────────────────────────────────────────
 *
 * The raw Generator, optionally gated on its class name.  NULL both when there
 * is no generator and when the name differs.  PRESERVED: no caller checks
 * before dereferencing. */
static Generator *ps_get_generator(ParticleSystem *self, const char *name)
{
    Generator *gen = self->pGenerator;
    if (gen == NULL)
        return NULL;
    if (name != NULL && strcmp(gen->pName, name) != 0)
        return NULL;
    return gen;
}

/* The generator's enable flag. */
static void ps_set_render_node(ParticleSystem *self, DWORD enabled)
{
    if (self->pGenerator)
        self->pGenerator->dwEnabled = enabled;
}

/* ─── Construction (the four ctors and the factory) ────────────────────────
 *
 * The type-name strings; pName points at them as the ctors leave it. */
static void ps_base_construct(ParticleSystem *self)
{
    ring_init(&self->ring);
    self->pVtable      = (void **)ps_vtbl_base;
    self->pGenerator   = NULL;
    self->pEnvironment = NULL;
    self->pField24     = NULL;
    self->pName        = GS_PSNAME_SYSTEM;
}

static void point_construct(PointParticleSystem *self)
{
    ps_base_construct(&self->base);
    self->base.pVtable  = (void **)ps_vtbl_point;
    self->base.pField24 = NULL;
    self->dwVertexCount = 0;
    self->pVerts        = NULL;
    self->base.pName    = GS_PSNAME_POINT_SYSTEM;
}

/* pField24 is 1 here, and the six corners start zeroed. */
static void face_construct(FaceParticleSystem *self)
{
    ps_base_construct(&self->base);
    self->base.pVtable  = (void **)ps_vtbl_face;
    self->pVerts        = NULL;
    self->nVertexCount  = 0;
    self->base.pName    = GS_PSNAME_FACE_SYSTEM;
    self->base.pField24 = (void *)1;
    self->flScale       = 1.0f;
    memset(self->flCorner, 0, sizeof self->flCorner);
}

/* One corner entry by default, sizes 1.0, every other range 0. */
static void xface_construct(XFaceParticleSystem *self)
{
    ps_base_construct(&self->base);
    self->base.pVtable        = (void **)ps_vtbl_xface;
    self->base.pField24       = (void *)1;
    self->dwCornerTableCount  = 1;
    self->pCornerTable        = NULL;
    self->nVertexCount        = 0;
    self->pVerts              = NULL;
    float *ranges = (float *)((BYTE *)self + 0x76);
    ranges[0] = ranges[1] = 1.0f;  // size min/max
    ranges[2] = ranges[3] = 0.0f;  // lifetime
    ranges[4] = ranges[5] = 0.0f;  // speed
    ranges[6] = ranges[7] = 0.0f;  // rotation
    self->base.pName = GS_PSNAME_XFACE_SYSTEM;
}

template <typename T>
static ParticleSystem *ps_new(void (*construct)(T *))
{
    T *obj = (T *)::operator new(sizeof(T), std::nothrow);
    if (obj)
        construct(obj);
    return (ParticleSystem *)obj;
}

/* The four names and sizes (0x28 / 0x30 / 0x7a / 0x96); NULL for an unknown
 * name. */
ParticleSystem *ps_create(const char *name)
{
    if (strcmp(name, "ParticleSystem") == 0)       return ps_new(ps_base_construct);
    if (strcmp(name, "PointParticleSystem") == 0)  return ps_new(point_construct);
    if (strcmp(name, "FaceParticleSystem") == 0)   return ps_new(face_construct);
    if (strcmp(name, "XFaceParticleSystem") == 0)  return ps_new(xface_construct);
    return NULL;
}

/* ─── The two ways the game builds a system ────────────────────────────────
 *
 * Clone and load from a .par file; both go through the factory. */
typedef BOOL (THISCALL *ps_load_fn)(void *, void *, GameLogger *);

/* Length-prefixed class name, then the factory, then slot 14. */
static ParticleSystem *ps_load_stream(void *fp, GameLogger *log)
{
    DWORD len;
    if (hooks_fread(&len, 4, 1, fp) != 1) {
        GameLog_LogSourceLocation(log, 4, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NODATA);
        return NULL;
    }
    char *name = (char *)::operator new(len, std::nothrow);
    if (hooks_fread(name, 1, len, fp) != len) {
        ::operator delete(name);
        GameLog_LogSourceLocation(log, 4, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NAMEREAD);
        return NULL;
    }
    ParticleSystem *ps = ps_create(name);
    if (ps == NULL) {
        // Logged before the free here, unlike Deserialize's two branches.
        GameLog_LogSourceLocation(log, 4, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NOSYSTEM, name);
        ::operator delete(name);
        return NULL;
    }
    ::operator delete(name);
    if (!((ps_load_fn)ps->pVtable[PS_VT_LOAD])(ps, fp, log)) {
        sub_object_delete(ps);
        return NULL;
    }
    return ps;
}

/* The .par entry point.  A failed fclose discards the system that was read. */
ParticleSystem *ps_load_file(const char *path, GameLogger *log)
{
    GameLog_LogMessage(log, 2, GS_PS_MSG_STARTREAD, path);
    void *fp = hooks_fopen(path, "r");  // text mode
    if (fp == NULL) {
        GameLog_LogSourceLocation(log, 4, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NOOPEN, path);
        return NULL;
    }
    ParticleSystem *ps = ps_load_stream(fp, log);
    if (hooks_fclose(fp) != 0) {
        GameLog_LogSourceLocation(log, 3, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NOCLOSE, path);
        if (ps)
            sub_object_delete(ps);
        return NULL;
    }
    return ps;
}

/* Build one of the same class and CopyFrom (slot 2). */
ParticleSystem *ps_clone(const ParticleSystem *src)
{
    ParticleSystem *made = ps_create(src->pName);
    if (made == NULL)
        return NULL;
    if (!((ps_attach_fn)made->pVtable[PS_VT_COPY])(made, (void *)src)) {
        sub_object_delete(made);
        return NULL;
    }
    return made;
}

/* ─── Lifecycle exports ────────────────────────────────────────────────────
 */
extern "C" {

/* The two __cdecl entry points. */
__declspec(dllexport) ParticleSystem *__cdecl
Particle_CloneSystem(const ParticleSystem *src)            { return ps_clone(src); }

__declspec(dllexport) ParticleSystem *__cdecl
Particle_LoadFromFile(const char *path, GameLogger *log)   { return ps_load_file(path, log); }

/* Base ParticleSystem */
__declspec(dllexport) void *THISCALL
Particle_BaseDtor(ParticleSystem *self, unsigned flags)
{
    ps_base_destruct(self);
    return scalar_delete(self, flags);
}

__declspec(dllexport) void THISCALL
Particle_BaseRelease(ParticleSystem *self, int flags)        { ps_release(self, flags); }

__declspec(dllexport) BOOL THISCALL
Particle_BaseCopyFrom(ParticleSystem *self, const ParticleSystem *src)
{ return ps_copy_from(self, src); }

__declspec(dllexport) BOOL THISCALL
Particle_BaseSetCapacity(ParticleSystem *self, DWORD n)      { return ps_set_capacity(self, n); }

__declspec(dllexport) BOOL THISCALL
Particle_BaseResize(ParticleSystem *self, DWORD n)           { return ps_resize(self, n); }

/* Slots 5 and 6: one function each across all four classes. */
__declspec(dllexport) BOOL THISCALL
Particle_SetGenerator(ParticleSystem *self, void *gen)
{ return ps_set_sub_object((void **)&self->pGenerator, &self->ring, gen); }

__declspec(dllexport) BOOL THISCALL
Particle_SetEnvironment(ParticleSystem *self, void *env)
{ return ps_set_sub_object((void **)&self->pEnvironment, &self->ring, env); }

__declspec(dllexport) BOOL THISCALL
Particle_BaseSave(ParticleSystem *self, void *fp, GameLogger *log)
{ return ps_serialize(self, fp, log); }

__declspec(dllexport) BOOL THISCALL
Particle_BaseLoad(ParticleSystem *self, void *fp, GameLogger *log)
{ return ps_deserialize(self, fp, log); }

/* The base class's own fill and draw: nothing, and 0. */
__declspec(dllexport) void THISCALL
Particle_BaseFill(ParticleSystem *)                          { }

__declspec(dllexport) DWORD THISCALL
Particle_BaseDrawNull(ParticleSystem *, IDirect3DDevice3 *)  { return 0; }

/* Slots 10 and 11 where the class does not override them (base, Point, XFace):
 * shared no-ops with the same argument counts, so the same callee cleanup. */
__declspec(dllexport) void THISCALL
Particle_NopVec3(ParticleSystem *, float, float, float)      { }

__declspec(dllexport) void THISCALL
Particle_NopPtr(ParticleSystem *, void *)                    { }

/* The three non-virtual entry points. */
__declspec(dllexport) Generator *THISCALL
Particle_GetGenerator(ParticleSystem *self, const char *name)
{ return ps_get_generator(self, name); }

__declspec(dllexport) void THISCALL
Particle_EnableRenderNode(ParticleSystem *self)              { ps_set_render_node(self, 1); }

__declspec(dllexport) void THISCALL
Particle_DisableRenderNode(ParticleSystem *self)             { ps_set_render_node(self, 0); }

/* Slot 1 for Point and Face. */
__declspec(dllexport) void THISCALL
Particle_QuadRelease(ParticleSystem *self, int flags)        { quad_release(self, flags); }

/* PointParticleSystem */
__declspec(dllexport) void *THISCALL
Particle_PointDtor(PointParticleSystem *self, unsigned flags)
{
    quad_destruct(&self->base, ps_vtbl_point);
    return scalar_delete(self, flags);
}

__declspec(dllexport) BOOL THISCALL
Particle_PointCopyFrom(PointParticleSystem *self, const ParticleSystem *src)
{ return point_copy_from(self, src); }

__declspec(dllexport) BOOL THISCALL
Particle_PointSetCapacity(PointParticleSystem *self, DWORD n) { return point_set_capacity(self, n); }

__declspec(dllexport) BOOL THISCALL
Particle_PointResize(PointParticleSystem *self, DWORD n)      { return point_resize(self, n); }

__declspec(dllexport) BOOL THISCALL
Particle_PointSave(PointParticleSystem *self, void *fp, GameLogger *log)
{ return point_serialize(self, fp, log); }

__declspec(dllexport) BOOL THISCALL
Particle_PointLoad(PointParticleSystem *self, void *fp, GameLogger *log)
{ return point_deserialize(self, fp, log); }

/* FaceParticleSystem */
__declspec(dllexport) void *THISCALL
Particle_FaceDtor(FaceParticleSystem *self, unsigned flags)
{
    quad_destruct(&self->base, ps_vtbl_face);
    return scalar_delete(self, flags);
}

__declspec(dllexport) BOOL THISCALL
Particle_FaceCopyFrom(FaceParticleSystem *self, const FaceParticleSystem *src)
{ return face_copy_from(self, src); }

__declspec(dllexport) BOOL THISCALL
Particle_FaceSetCapacity(FaceParticleSystem *self, DWORD n)  { return face_set_capacity(self, n); }

__declspec(dllexport) BOOL THISCALL
Particle_FaceResize(FaceParticleSystem *self, DWORD n)       { return face_resize(self, n); }

__declspec(dllexport) BOOL THISCALL
Particle_FaceSave(FaceParticleSystem *self, void *fp, GameLogger *log)
{ return face_serialize(self, fp, log); }

__declspec(dllexport) BOOL THISCALL
Particle_FaceLoad(FaceParticleSystem *self, void *fp, GameLogger *log)
{ return face_deserialize(self, fp, log); }

/* XFaceParticleSystem */
__declspec(dllexport) void *THISCALL
Particle_XFaceDtor(XFaceParticleSystem *self, unsigned flags)
{
    xface_destruct(self);
    return scalar_delete(self, flags);
}

__declspec(dllexport) void THISCALL
Particle_XFaceRelease(XFaceParticleSystem *self, int flags)  { xface_release(self, flags); }

__declspec(dllexport) BOOL THISCALL
Particle_XFaceCopyFrom(XFaceParticleSystem *self, const XFaceParticleSystem *src)
{ return xface_copy_from(self, src); }

__declspec(dllexport) BOOL THISCALL
Particle_XFaceSetCapacity(XFaceParticleSystem *self, DWORD n) { return xface_set_capacity(self, n); }

__declspec(dllexport) BOOL THISCALL
Particle_XFaceResize(XFaceParticleSystem *self, DWORD n)      { return xface_resize(self, n); }

__declspec(dllexport) BOOL THISCALL
Particle_XFaceSave(XFaceParticleSystem *self, void *fp, GameLogger *log)
{ return xface_serialize(self, fp, log); }

__declspec(dllexport) BOOL THISCALL
Particle_XFaceLoad(XFaceParticleSystem *self, void *fp, GameLogger *log)
{ return xface_deserialize(self, fp, log); }

}  // extern "C"

/* ─── Tick and corner exports ──────────────────────────────────────────────
 */
extern "C" {

__declspec(dllexport) void THISCALL
Particle_BaseTick(ParticleSystem *self, DWORD dt)  { base_tick(self, relay_dt(dt)); }

__declspec(dllexport) void THISCALL
Particle_XFaceTick(XFaceParticleSystem *self, DWORD dt)  { xface_tick(self, relay_dt(dt)); }

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

}  // extern "C"

/* ─── The vtables ─────────────────────────────────────────────────────────
 *
 * One table per class, in the slot order particles.h names, installed by the
 * constructors above.  Hand-built rather than C++ virtuals: the callers use
 * __thiscall and this slot order.  The static_asserts below catch an
 * initialiser that is one entry short, which would otherwise leave a silent
 * NULL slot. */
extern void *const ps_vtbl_base[] = {
    (void *)Particle_BaseDtor,        (void *)Particle_BaseRelease,
    (void *)Particle_BaseCopyFrom,    (void *)Particle_BaseSetCapacity,
    (void *)Particle_BaseResize,      (void *)Particle_SetGenerator,
    (void *)Particle_SetEnvironment,  (void *)Particle_BaseTick,
    (void *)Particle_BaseRender,      (void *)Particle_BaseFill,
    (void *)Particle_NopVec3,         (void *)Particle_NopPtr,
    (void *)Particle_BaseDrawNull,    (void *)Particle_BaseSave,
    (void *)Particle_BaseLoad,
};

extern void *const ps_vtbl_point[] = {
    (void *)Particle_PointDtor,       (void *)Particle_QuadRelease,
    (void *)Particle_PointCopyFrom,   (void *)Particle_PointSetCapacity,
    (void *)Particle_PointResize,     (void *)Particle_SetGenerator,
    (void *)Particle_SetEnvironment,  (void *)Particle_BaseTick,
    (void *)Particle_PointRender,     (void *)Particle_PointFill,
    (void *)Particle_NopVec3,         (void *)Particle_NopPtr,
    (void *)Particle_PointDraw,       (void *)Particle_PointSave,
    (void *)Particle_PointLoad,
};

extern void *const ps_vtbl_face[] = {
    (void *)Particle_FaceDtor,        (void *)Particle_QuadRelease,
    (void *)Particle_FaceCopyFrom,    (void *)Particle_FaceSetCapacity,
    (void *)Particle_FaceResize,      (void *)Particle_SetGenerator,
    (void *)Particle_SetEnvironment,  (void *)Particle_BaseTick,
    (void *)Particle_BaseRender,      (void *)Particle_FaceFill,
    (void *)Particle_FaceSetVector,   (void *)Particle_FaceTransformCorners,
    (void *)Particle_FaceDraw,        (void *)Particle_FaceSave,
    (void *)Particle_FaceLoad,
};

extern void *const ps_vtbl_xface[] = {
    (void *)Particle_XFaceDtor,       (void *)Particle_XFaceRelease,
    (void *)Particle_XFaceCopyFrom,   (void *)Particle_XFaceSetCapacity,
    (void *)Particle_XFaceResize,     (void *)Particle_SetGenerator,
    (void *)Particle_SetEnvironment,  (void *)Particle_XFaceTick,
    (void *)Particle_BaseRender,      (void *)Particle_XFaceFill,
    (void *)Particle_NopVec3,         (void *)Particle_NopPtr,
    (void *)Particle_XFaceDraw,       (void *)Particle_XFaceSave,
    (void *)Particle_XFaceLoad,
};

#define PS_SLOTS(t) (sizeof (t) / sizeof *(t))
static_assert(PS_SLOTS(ps_vtbl_base)  == PS_VTBL_SLOTS, "ParticleSystem vtable");
static_assert(PS_SLOTS(ps_vtbl_point) == PS_VTBL_SLOTS, "Point vtable");
static_assert(PS_SLOTS(ps_vtbl_face)  == PS_VTBL_SLOTS, "Face vtable");
static_assert(PS_SLOTS(ps_vtbl_xface) == PS_VTBL_SLOTS, "XFace vtable");
#undef PS_SLOTS
