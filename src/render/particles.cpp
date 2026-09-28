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
#include "renderdevice.h"

#define PARTICLE_FVF       VertexFormat::Lit  // XYZ|PSIZE|DIFFUSE|SPECULAR|TEX1, 0x20 stride
#define PARTICLE_LOG_FIRST 8
#define FX_TINT_COLOUR     0xFFFF00FF

#define THISCALL __attribute__((thiscall))
typedef void  (THISCALL *ps_fill_fn)(ParticleSystem *);
typedef DWORD (THISCALL *ps_draw_fn)(ParticleSystem *, RenderDevice *);

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

DWORD ParticleNode::colour() const
{
    return fx_tint() ? FX_TINT_COLOUR : dwDiffuse;
}

/* Per-class state so one busy class can't hide the others: the first
 * PARTICLE_LOG_FIRST draws are logged, and so is the first draw that actually
 * carries vertices (an empty ring draws with verts=0). */
struct DrawLogState { LONG calls; LONG nonempty; };

static void log_draw(DrawLogState *st, const char *name, const void *self,
                     RenderDevice *dev, DWORD count, bool ok)
{
    bool report = InterlockedIncrement(&st->calls) <= PARTICLE_LOG_FIRST;
    if (count > 0 && InterlockedExchange(&st->nonempty, 1) == 0)
        report = true;
    if (report)
        log_write("particle: %s this=%p dev=%p verts=%lu -> ok=%d\n",
                  name, self, dev, count, ok);
}

/* ─── Fill ─────────────────────────────────────────────────────────────────
 *
 * All three fills walk the live region of the ring the generator emits into
 * and the environment retires from.  `emit` writes the vertices for one node
 * and returns how many it wrote. */
template <typename EmitFn> DWORD ParticleSystem::fillRing(EmitFn emit)
{
    DWORD n = 0;
    if (ring_.pRingHead != ring_.pRingCurrent) {
        ParticleNode *node = ring_.pRingHead;
        do {
            n += emit(node, n);
            node = node->pNext;
        } while (node != ring_.pRingCurrent);
    }
    return n;
}

void PointParticleSystem::pointFill()
{
    dwVertexCount_ = fillRing([this](const ParticleNode *node, DWORD n) -> DWORD {
            ParticleVertex *v = &pVerts_[n];
            v->flX = node->flX;
            v->flY = node->flY;
            v->flZ = node->flZ;
            v->dwDiffuse = node->colour();
            return 1;
        });
}

/* Face (baked corners) and XFace (per-particle corner-table entry): six
 * vertices = node position + the six xyz corner offsets. */
static void emit_face(ParticleVertex *v, const ParticleNode *node,
                      const float *corners )  // 18 floats
{
    DWORD colour = node->colour();
    for (int c = 0; c < 6; c++, v++, corners += 3) {
        v->flX = node->flX + corners[0];
        v->flY = node->flY + corners[1];
        v->flZ = node->flZ + corners[2];
        v->dwDiffuse = colour;
    }
}

void FaceParticleSystem::faceFill()
{
    nVertexCount_ = (WORD)fillRing([this](const ParticleNode *node, DWORD n) -> DWORD {
            emit_face(&pVerts_[n], node, &flCorner_[0][0]);
            return 6;
        });
}

void XFaceParticleSystem::xfaceFill()
{
    nVertexCount_ = (WORD)fillRing([this](const ParticleNode *node, DWORD n) -> DWORD {
            emit_face(&pVerts_[n], node,
                      &pCornerTable_[node->dwShapeIndex].flCorner[0][0]);
            return 6;
        });
}

/* ─── Draw ─────────────────────────────────────────────────────────────────
 */
DWORD PointParticleSystem::pointDraw(RenderDevice *dev)
{
    static DrawLogState st;
    bool ok = dev->Draw(Prim::PointList, PARTICLE_FVF,
                                    pVerts_, dwVertexCount_, 0);
    log_draw(&st, "PointDraw", this, dev, dwVertexCount_, ok);
    return dwVertexCount_;
}

DWORD FaceParticleSystem::faceDraw(RenderDevice *dev)
{
    static DrawLogState st;
    bool ok = dev->Draw(Prim::TriangleList, PARTICLE_FVF,
                                    pVerts_, nVertexCount_, 0);
    log_draw(&st, "FaceDraw", this, dev, nVertexCount_, ok);
    return nVertexCount_ / 6;
}

/* Two passes, one per face winding: save CULLMODE, draw with Cull::CCW, draw
 * again with Cull::CW, restore.  That makes an XFace billboard two-sided: a
 * particle whose corner table has rotated past edge-on is still drawn. */
DWORD XFaceParticleSystem::xfaceDraw(RenderDevice *dev)
{
    DWORD saved = 0;
    saved = dev->GetRenderState(RS::CullMode);
    dev->SetRenderState(RS::CullMode, Cull::CCW);
    dev->Draw(Prim::TriangleList, PARTICLE_FVF,
                       pVerts_, nVertexCount_, 0);
    dev->SetRenderState(RS::CullMode, Cull::CW);
    bool ok = dev->Draw(Prim::TriangleList, PARTICLE_FVF,
                                    pVerts_, nVertexCount_, 0);
    dev->SetRenderState(RS::CullMode, saved);
    static DrawLogState st;
    log_draw(&st, "XFaceDraw", this, dev, nVertexCount_, ok);
    return nVertexCount_ / 6;
}

/* ─── Render slots ───────────────────────────────────────────────────────
 */

void THISCALL
PointParticleSystem::pointFillSlot(PointParticleSystem *self)  { self->pointFill(); }

void THISCALL
FaceParticleSystem::faceFillSlot(FaceParticleSystem *self)    { self->faceFill(); }

void THISCALL
XFaceParticleSystem::xfaceFillSlot(XFaceParticleSystem *self)  { self->xfaceFill(); }

DWORD THISCALL
PointParticleSystem::pointDrawSlot(PointParticleSystem *self, RenderDevice *dev)
{ return self->pointDraw(dev); }

DWORD THISCALL
FaceParticleSystem::faceDrawSlot(FaceParticleSystem *self, RenderDevice *dev)
{ return self->faceDraw(dev); }

DWORD THISCALL
XFaceParticleSystem::xfaceDrawSlot(XFaceParticleSystem *self, RenderDevice *dev)
{ return self->xfaceDraw(dev); }

/* Base Render (Face/XFace slot 8): fill then draw, through slots 9 and 12. */
DWORD THISCALL
ParticleSystem::baseRender(ParticleSystem *self, RenderDevice *dev)
{
    self->fill();
    return self->draw(dev);
}

/* Point Render (slot 8 override): fills through the vtable but draws the
 * POINTLIST itself, never calling slot 12. */
DWORD THISCALL
PointParticleSystem::pointRender(PointParticleSystem *self, RenderDevice *dev)
{
    self->fill();
    return self->pointDraw(dev);
}


/* ─── Dispatch ─────────────────────────────────────────────────────────────
 *
 * One virtual call each: every ParticleSystem carries one of this file's
 * vtables, so slots 9 and 12 land directly on the class's fill and draw. */
void ParticleSystem::fill()
{
    ((ps_fill_fn)pVtable_[PS_VT_FILL])(this);
}

DWORD ParticleSystem::draw(RenderDevice *dev)
{
    return ((ps_draw_fn)pVtable_[PS_VT_DRAW])(this, dev);
}

/* ─── Tick and Face corner transform ───────────────────────────────────────
 *
 * Row-major 4x4, row-vector convention (D3D style): out[r][c] = sum_k a[r][k]
 * * b[k][c]. */
typedef float PMat4[16];

static void mat_identity(PMat4 m)
{
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat_mul(PMat4 out, const PMat4 a, const PMat4 b)
{
    PMat4 t;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            t[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c]
                         + a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
    for (int i = 0; i < 16; i++) out[i] = t[i];
}

/* (v,1) x m as a row vector, w-divided when w != 0. */
static void transform_point(float v[3], const PMat4 m)
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
static void mat_rot_x(PMat4 m, float a)
{
    mat_identity(m);
    float c = cosf(a), s = sinf(a);
    m[5] = c;  m[6] = -s;
    m[9] = s;  m[10] = c;
}
static void mat_rot_y(PMat4 m, float a)
{
    mat_identity(m);
    float c = cosf(a), s = sinf(a);
    m[0] = c;  m[2] = s;
    m[8] = -s; m[10] = c;
}
static void mat_rot_z(PMat4 m, float a)
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

void ParticleSystem::tick(float dt)
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("particle: BaseTick active (this=%p dt=%f gen=%p env=%p)\n",
                  this, dt, pGenerator_, pEnvironment_);
    if (pGenerator_)
        sim_tick_slot3(pGenerator_, dt);
    if (pEnvironment_)
        sim_tick_slot3(pEnvironment_, dt);
    dethash_particles(this);
}

void XFaceParticleSystem::xfaceTick(float dt)
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("particle: XFaceTick active (this=%p entries=%lu)\n",
                  this, dwCornerTableCount_);
    if (pCornerTable_) {
        float spin = fx_spin() ? 10.0f : 1.0f;
        for (DWORD i = 0; i < dwCornerTableCount_; i++) {
            XFaceCornerEntry *e = &pCornerTable_[i];
            PMat4 m;
            mat_identity(m);
            bool fired = false;
            for (int axis = 0; axis < 3; axis++)
                e->flRotAccum[axis] += e->flRotVel[axis] * spin;
            for (int axis = 0; axis < 3; axis++) {
                if ((double)e->flRotAccum[axis] > ROT_STEP_THRESHOLD) {
                    PMat4 rot;
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
    tick(dt);
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
void FaceParticleSystem::faceSetVector(float x, float y, float z)
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

    float s2 = flScale_ / vec_len(e2);
    float s1 = flScale_ / vec_len(e1);
    for (int i = 0; i < 3; i++) { e2[i] *= s2; e1[i] *= s1; }

    float (*c)[3] = flCorner_;
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
__attribute__((unused)) void RingBuffer::init()
{
    dwRingCount  = 0;
    pRingBase    = NULL;
    pRingHead    = NULL;
    pRingTail    = NULL;
    pRingCurrent = NULL;
}

/* Free the node block.  PRESERVED: pRingTail is not cleared; the other four
 * fields are. */
void RingBuffer::release()
{
    if (pRingBase)
        ::operator delete(pRingBase);
    dwRingCount  = 0;
    pRingBase    = NULL;
    pRingHead    = NULL;
    pRingCurrent = NULL;
}

/* Give every node but the last a random shape index in [0, shapes-1].
 * DETERMINISM: rand() is reseeded from the game clock on every call, and the
 * last node is skipped (the loop bound is dwRingCount - 1). */
void RingBuffer::assignShapes(DWORD shapes)
{
    CRT_RAND_SEED = (unsigned)hooks_GameTime(NULL);
    if (dwRingCount - 1 == 0)
        return;
    float span = (float)(int)(shapes - 1);
    for (DWORD i = 0; i < dwRingCount - 1; i++) {
        int r = (int)crt_rand();
        pRingBase[i].dwShapeIndex =
            (DWORD)(int)((double)r * span * PS_RAND_SCALE + 0.5);
    }
}

/* (Re)allocate `count` nodes and thread them into one doubly-linked list: head
 * and current at the front, tail at the last node, both ends NULL.  Fewer than
 * two nodes is refused. */
BOOL RingBuffer::alloc(DWORD count, DWORD shapes)
{
    release();
    if (count < 2)
        return FALSE;
    dwRingCount = count;
    unsigned bytes = count * sizeof(ParticleNode);
    ParticleNode *base = (ParticleNode *)::operator new(bytes, std::nothrow);
    pRingBase = base;
    if (base == NULL)
        return FALSE;
    pRingTail    = base + (count - 1);
    pRingHead    = base;
    pRingCurrent = base;
    memset(base, 0, bytes);

    base[0].pPrev = NULL;
    base[0].pNext = &base[1];
    for (DWORD i = 1; i + 1 < count; i++) {
        base[i].pPrev = &base[i - 1];
        base[i].pNext = &base[i + 1];
    }
    base[count - 1].pPrev = &base[count - 2];
    base[count - 1].pNext = NULL;

    assignShapes(shapes);
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
void ParticleSystem::release(int flags)
{
    if (pGenerator_ && flags && pGenerator_)
        sub_object_delete(pGenerator_);
    pGenerator_ = NULL;
    if (pEnvironment_ && flags && pEnvironment_)
        sub_object_delete(pEnvironment_);
    pEnvironment_ = NULL;
    ring_.release();
}

/* The dtor body: restore the base vtable, release, free the ring.  The release
 * call is direct, not virtual, so a subclass's override does not run here. */
void ParticleSystem::destructBase()
{
    pVtable_ = (void **)ps_vtbl_base;
    release(1);
    ring_.release();
}

/* Slot 3, SetCapacity: size the ring, no shape indices. */
BOOL ParticleSystem::setCapacity(DWORD count)
{
    return ring_.alloc(count, 0) != 0;
}

/* Slot 4, Resize: free, then allocate again (RingBuffer::alloc frees as well). */
BOOL ParticleSystem::resize(DWORD count)
{
    ring_.release();
    return ring_.alloc(count, 0);
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
BOOL ParticleSystem::copyFrom(const ParticleSystem *src)
{
    ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
    if (strcmp(src->pName_, pName_) != 0)
        return FALSE;
    if (!ring_.alloc(src->ring_.dwRingCount, 0))
        return FALSE;

    if (src->pGenerator_) {
        Generator *gen = src->pGenerator_->clone();
        if (gen == NULL) {
            ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
            return FALSE;
        }
        if (!((ps_attach_fn)pVtable_[PS_VT_SETGEN])(this, gen)) {
            ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
            return FALSE;
        }
    }
    if (src->pEnvironment_) {
        Environment *env = src->pEnvironment_->clone();
        if (env == NULL) {
            ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
            return FALSE;
        }
        if (!((ps_attach_fn)pVtable_[PS_VT_SETENV])(this, env)) {
            ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
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
BOOL ParticleSystem::serialize(void *fp, GameLogger *)
{
    if (fp == NULL)
        return FALSE;
    if (!ps_write(&ring_.dwRingCount, 4, fp))
        return FALSE;
    if (!ps_write_sub_object(pGenerator_, fp))
        return FALSE;
    return ps_write_sub_object(pEnvironment_, fp);
}

/* Read one length-prefixed class name into a fresh buffer.  NULL on failure;
 * the caller frees. */
static char *ps_read_name(void *fp, GameLogger *log, int line_len,
                          int line_name, const char *msg_name)
{
    DWORD len;
    if (!ps_read(&len, 4, fp)) {
        log->logSourceLocation(4, GS_PS_SRC_FILE, line_len, GS_PS_MSG_NOLEN);
        return NULL;
    }
    char *name = (char *)::operator new(len, std::nothrow);
    if (hooks_fread(name, 1, len, fp) != len) {
        log->logSourceLocation(4, GS_PS_SRC_FILE, line_name, msg_name);
        ::operator delete(name);
        return NULL;
    }
    return name;
}

/* Slot 14: release, re-make the ring at the stored size, then read each
 * sub-object's class name, build it through the factory, Load it and attach it
 * through slots 5 and 6.  The ring is sized directly rather than through slot
 * 3, so a subclass's capacity override does not run here. */
BOOL ParticleSystem::deserialize(void *fp, GameLogger *log)
{
    ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);

    DWORD count;
    if (!ps_read(&count, 4, fp)) {
        log->logSourceLocation(4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_NOCOUNT);
        return FALSE;
    }
    if (!ring_.alloc(count, 0)) {
        log->logSourceLocation(4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_NORING);
        return FALSE;
    }

    char *name = ps_read_name(fp, log, __LINE__, __LINE__, GS_PS_MSG_NONAME);
    if (name == NULL)
        return FALSE;
    if (strcmp(name, GS_PS_NAME_NULL) != 0) {
        Generator *gen = Generator::create(name);
        if (gen == NULL) {
            log->logSourceLocation(4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_NOGEN, name);
            ::operator delete(name);
            return FALSE;
        }
        ::operator delete(name);
        void **gvt = *(void ***)gen;
        if (!((ps_stream_fn)gvt[GEN_VT_LOAD_SLOT])(gen, fp)) {
/* PRESERVED: `name` was freed above and is still handed to the logger. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
            log->logSourceLocation(4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_GENLOAD, name);
#pragma GCC diagnostic pop
            sub_object_delete(gen);
            return FALSE;
        }
        ((ps_attach_fn)pVtable_[PS_VT_SETGEN])(this, gen);
    }

    // PRESERVED: a sub-object named "NULL" leaks `name`.
    name = ps_read_name(fp, log, __LINE__, __LINE__, GS_PS_MSG_NOENV);
    if (name == NULL)
        return FALSE;
    if (strcmp(name, GS_PS_NAME_NULL) != 0) {
        Environment *env = Environment::create(name);
        if (env == NULL) {
            log->logSourceLocation(4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_ENVNAME, name);
            ::operator delete(name);
            return FALSE;
        }
        ::operator delete(name);
        void **evt = *(void ***)env;
        if (!((ps_stream_fn)evt[GEN_VT_LOAD_SLOT])(env, fp)) {
/* PRESERVED: the same use-after-free as the generator branch. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
            log->logSourceLocation(4, GS_PS_SRC_FILE, __LINE__, GS_PS_MSG_ENVLOAD, name);
#pragma GCC diagnostic pop
            sub_object_delete(env);
            return FALSE;
        }
        ((ps_attach_fn)pVtable_[PS_VT_SETENV])(this, env);
    }
    return TRUE;
}

/* ─── PointParticleSystem and FaceParticleSystem lifecycles ────────────────
 *
 * Point's vertex buffer: one 0x20-byte vertex per ring node.  The count field
 * is not touched here; Fill maintains it. */
BOOL PointParticleSystem::pointAllocVerts()
{
    if (pVerts_)
        ::operator delete(pVerts_);
    void *p = ::operator new(ring_.dwRingCount * sizeof(ParticleVertex),
                             std::nothrow);
    if (p) {
        pVerts_ = (ParticleVertex *)p;
        return TRUE;
    }
    pVerts_ = NULL;
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

BOOL FaceParticleSystem::faceAllocVerts()
{
    return quad_alloc_verts(&pVerts_, ring_.dwRingCount, false);
}

/* Slot 1 for both Point and Face: drop the vertex buffer, then the base
 * release. */
void ParticleSystem::quadRelease(int flags)
{
    ParticleVertex **verts = (ParticleVertex **)((BYTE *)this + 0x28);
    if (*verts)
        ::operator delete(*verts);
    *verts = NULL;
    release(flags);
}

/* The two dtor bodies: own vtable, the shared release (called directly, not
 * virtually), then the base body. */
void ParticleSystem::quadDestruct(void *const *vtbl)
{
    pVtable_ = (void **)vtbl;
    quadRelease(1);
    destructBase();
}

/* Point slots 3 and 4: size the ring, then rebuild the vertex buffer.  Both
 * return the second step's result. */
BOOL PointParticleSystem::pointSetCapacity(DWORD count)
{
    if (!setCapacity(count))
        return FALSE;
    return pointAllocVerts();
}

BOOL PointParticleSystem::pointResize(DWORD count)
{
    if (!resize(count))
        return FALSE;
    return pointAllocVerts();
}

/* Point slot 2.  Releases virtually first, and the base CopyFrom releases
 * again. */
BOOL PointParticleSystem::pointCopyFrom(const ParticleSystem *src)
{
    ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
    if (!copyFrom(src))
        return FALSE;
    return pointAllocVerts();
}

/* Point slot 13: the base write, whose result is discarded, then success. */
BOOL PointParticleSystem::pointSerialize(void *fp, GameLogger *log)
{
    serialize(fp, log);
    return TRUE;
}

/* Point slot 14. */
BOOL PointParticleSystem::pointDeserialize(void *fp, GameLogger *log)
{
    if (!deserialize(fp, log))
        return FALSE;
    if (!pointAllocVerts()) {
        log->logSourceLocation(4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_PTVERTS);
        return FALSE;
    }
    return TRUE;
}

/* Face slot 3: release virtually, size the ring, reset the scale to 1 and
 * rebuild the corners. */
BOOL FaceParticleSystem::faceSetCapacity(DWORD count)
{
    ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
    if (!setCapacity(count))
        return FALSE;
    flScale_ = 1.0f;
    return faceAllocVerts();
}

/* Face slot 2: the scale comes from the source. */
BOOL FaceParticleSystem::faceCopyFrom(const FaceParticleSystem *src)
{
    ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
    if (!copyFrom(src))
        return FALSE;
    flScale_ = src->flScale_;
    return faceAllocVerts();
}

/* Face slot 4. */
BOOL FaceParticleSystem::faceResize(DWORD count)
{
    if (!resize(count))
        return FALSE;
    if (pVerts_)
        ::operator delete(pVerts_);
    pVerts_ = NULL;
    return faceAllocVerts();
}

/* Face slot 13: the base write (result discarded, as Point's), then the scale.
 */
BOOL FaceParticleSystem::faceSerialize(void *fp, GameLogger *log)
{
    serialize(fp, log);
    if (!ps_write(&flScale_, 4, fp)) {
        log->logSourceLocation(4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_SAVESIZE);
        return FALSE;
    }
    return TRUE;
}

/* Face slot 14. */
BOOL FaceParticleSystem::faceDeserialize(void *fp, GameLogger *log)
{
    if (!deserialize(fp, log))
        return FALSE;
    if (!ps_read(&flScale_, 4, fp)) {
        log->logSourceLocation(4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_FACESIZE);
        return FALSE;
    }
    if (!faceAllocVerts()) {
        log->logSourceLocation(4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_VERTARR);
        return FALSE;
    }
    return TRUE;
}

/* ─── XFaceParticleSystem lifecycle ────────────────────────────────────────
 */
BOOL XFaceParticleSystem::xfaceAllocVerts()
{
    return quad_alloc_verts(&pVerts_, ring_.dwRingCount, true);
}

/* Slot 1: drop the corner table and the vertex buffer, then the base release.
 */
void XFaceParticleSystem::xfaceRelease(int flags)
{
    if (pCornerTable_)
        ::operator delete(pCornerTable_);
    pCornerTable_ = NULL;
    if (pVerts_)
        ::operator delete(pVerts_);
    pVerts_ = NULL;
    release(flags);
}

/* The dtor body runs the base destructor twice and never calls its own slot 1.
 * PRESERVED: destroying an XFace leaks its corner table and vertex buffer. */
void XFaceParticleSystem::xfaceDestruct()
{
    pVtable_ = (void **)ps_vtbl_xface;
    destructBase();
    destructBase();
}

/* The corner transform divides by w when w != 1.0 -- not the `w != 0` test
 * transform_point uses for the Face corners. */
static void xface_transform_corner(float v[3], const PMat4 m)
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
BOOL XFaceParticleSystem::xfaceBuildCorners()
{
    if (pCornerTable_)
        ::operator delete(pCornerTable_);
    DWORD count = dwCornerTableCount_;
    XFaceCornerEntry *table =
        (XFaceCornerEntry *)::operator new(count * sizeof(XFaceCornerEntry), std::nothrow);
    pCornerTable_ = table;
    if (table == NULL)
        return FALSE;

    const float *p = (const float *)((const BYTE *)this + 0x76);  // the 8 ranges
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
        PMat4 m;
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
BOOL XFaceParticleSystem::xfaceSetCapacity(DWORD count)
{
    setCapacity(count);
    ring_.assignShapes(dwCornerTableCount_);
    if (!xfaceBuildCorners()) {
        ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
        return FALSE;
    }
    if (!xfaceAllocVerts()) {
        ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
        return FALSE;
    }
    return TRUE;
}

/* Slot 2.  No virtual release first, unlike Point and Face. */
BOOL XFaceParticleSystem::xfaceCopyFrom(const XFaceParticleSystem *src)
{
    if (!copyFrom(src))
        return FALSE;
    dwCornerTableCount_ = src->dwCornerTableCount_;
    memcpy((BYTE *)this + 0x76, (const BYTE *)src + 0x76, 0x20);  // the 8 ranges
    ring_.assignShapes(dwCornerTableCount_);
    if (!xfaceBuildCorners()) {
        ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
        return FALSE;
    }
    if (!xfaceAllocVerts()) {
        ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
        return FALSE;
    }
    return TRUE;
}

/* Slot 4.  Vertices before corners here; the other two do corners first. */
BOOL XFaceParticleSystem::xfaceResize(DWORD count)
{
    if (!resize(count))
        return FALSE;
    if (pVerts_)
        ::operator delete(pVerts_);
    pVerts_ = NULL;
    if (pCornerTable_)
        ::operator delete(pCornerTable_);
    pCornerTable_ = NULL;
    ring_.assignShapes(dwCornerTableCount_);
    if (!xfaceAllocVerts()) {
        ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
        return FALSE;
    }
    if (!xfaceBuildCorners()) {
        ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
        return FALSE;
    }
    return TRUE;
}

/* Slots 13 and 14: the base stream, then the corner count and the eight
 * ranges.  Load finishes by re-running its own slot 3 with the ring size,
 * which rebuilds the corners and vertices. */
BOOL XFaceParticleSystem::xfaceSerialize(void *fp, GameLogger *log)
{
    if (!serialize(fp, log))
        return FALSE;
    if (!ps_write(&dwCornerTableCount_, 4, fp))
        goto failed;
    for (int i = 0; i < 8; i++)
        if (!ps_write((const BYTE *)this + 0x76 + i * 4, 4, fp))
            goto failed;
    return TRUE;
failed:
    log->logSourceLocation(4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_XSAVE);
    return FALSE;
}

BOOL XFaceParticleSystem::xfaceDeserialize(void *fp, GameLogger *log)
{
    if (!deserialize(fp, log))
        return FALSE;
    if (!ps_read(&dwCornerTableCount_, 4, fp))
        goto failed;
    for (int i = 0; i < 8; i++)
        if (!ps_read((BYTE *)this + 0x76 + i * 4, 4, fp))
            goto failed;
    return ((ps_count_fn)pVtable_[PS_VT_SETCAP])
               (this, ring_.dwRingCount);
failed:
    ((ps_release_fn)pVtable_[PS_VT_RELEASE])(this, 1);
    log->logSourceLocation(4, GS_PS_SUB_FILE, __LINE__, GS_PS_MSG_XLOAD);
    return FALSE;
}

/* ─── The non-virtual interface ────────────────────────────────────────────
 *
 * The raw Generator, optionally gated on its class name.  NULL both when there
 * is no generator and when the name differs.  PRESERVED: no caller checks
 * before dereferencing. */
Generator * ParticleSystem::getGenerator(const char *name)
{
    Generator *gen = pGenerator_;
    if (gen == NULL)
        return NULL;
    if (name != NULL && strcmp(gen->name(), name) != 0)
        return NULL;
    return gen;
}

/* The generator's enable flag. */
void ParticleSystem::setRenderNode(DWORD enabled)
{
    if (pGenerator_)
        pGenerator_->setEnabled(enabled);
}

/* ─── Construction (the four ctors and the factory) ────────────────────────
 *
 * The type-name strings; pName points at them as the ctors leave it. */
void ParticleSystem::constructBase()
{
    ring_.init();
    pVtable_      = (void **)ps_vtbl_base;
    pGenerator_   = NULL;
    pEnvironment_ = NULL;
    pField24_     = NULL;
    pName_        = GS_PSNAME_SYSTEM;
}

void PointParticleSystem::pointConstruct()
{
    constructBase();
    pVtable_  = (void **)ps_vtbl_point;
    pField24_ = NULL;
    dwVertexCount_ = 0;
    pVerts_        = NULL;
    pName_    = GS_PSNAME_POINT_SYSTEM;
}

/* pField24 is 1 here, and the six corners start zeroed. */
void FaceParticleSystem::faceConstruct()
{
    constructBase();
    pVtable_  = (void **)ps_vtbl_face;
    pVerts_        = NULL;
    nVertexCount_  = 0;
    pName_    = GS_PSNAME_FACE_SYSTEM;
    pField24_ = (void *)1;
    flScale_       = 1.0f;
    memset(flCorner_, 0, sizeof flCorner_);
}

/* One corner entry by default, sizes 1.0, every other range 0. */
void XFaceParticleSystem::xfaceConstruct()
{
    constructBase();
    pVtable_        = (void **)ps_vtbl_xface;
    pField24_       = (void *)1;
    dwCornerTableCount_  = 1;
    pCornerTable_        = NULL;
    nVertexCount_        = 0;
    pVerts_              = NULL;
    float *ranges = (float *)((BYTE *)this + 0x76);
    ranges[0] = ranges[1] = 1.0f;  // size min/max
    ranges[2] = ranges[3] = 0.0f;  // lifetime
    ranges[4] = ranges[5] = 0.0f;  // speed
    ranges[6] = ranges[7] = 0.0f;  // rotation
    pName_ = GS_PSNAME_XFACE_SYSTEM;
}

template <typename T>
static ParticleSystem *ps_new(void (T::*construct)())
{
    T *obj = (T *)::operator new(sizeof(T), std::nothrow);
    if (obj)
        (obj->*construct)();
    return (ParticleSystem *)obj;
}

/* The four names and sizes (0x28 / 0x30 / 0x7a / 0x96); NULL for an unknown
 * name. */
ParticleSystem *
ParticleSystem::create(const char *name)
{
    if (strcmp(name, "ParticleSystem") == 0)       return ps_new(&ParticleSystem::constructBase);
    if (strcmp(name, "PointParticleSystem") == 0)  return ps_new(&PointParticleSystem::pointConstruct);
    if (strcmp(name, "FaceParticleSystem") == 0)   return ps_new(&FaceParticleSystem::faceConstruct);
    if (strcmp(name, "XFaceParticleSystem") == 0)  return ps_new(&XFaceParticleSystem::xfaceConstruct);
    return NULL;
}

/* ─── The two ways the game builds a system ────────────────────────────────
 *
 * Clone and load from a .par file; both go through the factory. */
typedef BOOL (THISCALL *ps_load_fn)(void *, void *, GameLogger *);

/* Length-prefixed class name, then the factory, then slot 14. */
ParticleSystem *
ParticleSystem::loadStream(void *fp, GameLogger *log)
{
    DWORD len;
    if (hooks_fread(&len, 4, 1, fp) != 1) {
        log->logSourceLocation(4, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NODATA);
        return NULL;
    }
    char *name = (char *)::operator new(len, std::nothrow);
    if (hooks_fread(name, 1, len, fp) != len) {
        ::operator delete(name);
        log->logSourceLocation(4, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NAMEREAD);
        return NULL;
    }
    ParticleSystem *ps = ParticleSystem::create(name);
    if (ps == NULL) {
        // Logged before the free here, unlike Deserialize's two branches.
        log->logSourceLocation(4, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NOSYSTEM, name);
        ::operator delete(name);
        return NULL;
    }
    ::operator delete(name);
    if (!((ps_load_fn)ps->pVtable_[PS_VT_LOAD])(ps, fp, log)) {
        sub_object_delete(ps);
        return NULL;
    }
    return ps;
}

/* The .par entry point.  A failed fclose discards the system that was read. */
ParticleSystem *
ParticleSystem::loadFile(const char *path, GameLogger *log)
{
    log->logMessage(2, GS_PS_MSG_STARTREAD, path);
    void *fp = hooks_fopen(path, "r");  // text mode
    if (fp == NULL) {
        log->logSourceLocation(4, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NOOPEN, path);
        return NULL;
    }
    ParticleSystem *ps = ParticleSystem::loadStream(fp, log);
    if (hooks_fclose(fp) != 0) {
        log->logSourceLocation(3, GS_PS_OPENSAVE_FILE, __LINE__, GS_PS_MSG_NOCLOSE, path);
        if (ps)
            sub_object_delete(ps);
        return NULL;
    }
    return ps;
}

/* Build one of the same class and CopyFrom (slot 2). */
ParticleSystem * ParticleSystem::clone() const
{
    ParticleSystem *made = ParticleSystem::create(pName_);
    if (made == NULL)
        return NULL;
    if (!((ps_attach_fn)made->pVtable_[PS_VT_COPY])(made, (void *)this)) {
        sub_object_delete(made);
        return NULL;
    }
    return made;
}

/* ─── Lifecycle slots ────────────────────────────────────────────────────
 */

/* Base ParticleSystem */
void *THISCALL
ParticleSystem::baseDtor(ParticleSystem *self, unsigned flags)
{
    self->destructBase();
    return scalar_delete(self, flags);
}

void THISCALL
ParticleSystem::baseRelease(ParticleSystem *self, int flags)        { self->release(flags); }

BOOL THISCALL
ParticleSystem::baseCopyFrom(ParticleSystem *self, const ParticleSystem *src)
{ return self->copyFrom(src); }

BOOL THISCALL
ParticleSystem::baseSetCapacity(ParticleSystem *self, DWORD n)      { return self->setCapacity(n); }

BOOL THISCALL
ParticleSystem::baseResize(ParticleSystem *self, DWORD n)           { return self->resize(n); }

/* Slots 5 and 6: one function each across all four classes. */
BOOL THISCALL
ParticleSystem::setGeneratorSlot(ParticleSystem *self, void *gen)
{ return ps_set_sub_object((void **)&self->pGenerator_, &self->ring_, gen); }

BOOL THISCALL
ParticleSystem::setEnvironmentSlot(ParticleSystem *self, void *env)
{ return ps_set_sub_object((void **)&self->pEnvironment_, &self->ring_, env); }

BOOL THISCALL
ParticleSystem::baseSave(ParticleSystem *self, void *fp, GameLogger *log)
{ return self->serialize(fp, log); }

BOOL THISCALL
ParticleSystem::baseLoad(ParticleSystem *self, void *fp, GameLogger *log)
{ return self->deserialize(fp, log); }

/* The base class's own fill and draw: nothing, and 0. */
void THISCALL
ParticleSystem::baseFill(ParticleSystem *)                          { }

DWORD THISCALL
ParticleSystem::baseDrawNull(ParticleSystem *, RenderDevice *)  { return 0; }

/* Slots 10 and 11 where the class does not override them (base, Point, XFace):
 * shared no-ops with the same argument counts, so the same callee cleanup. */
void THISCALL
ParticleSystem::nopVec3(ParticleSystem *, float, float, float)      { }

void THISCALL
ParticleSystem::nopPtr(ParticleSystem *, void *)                    { }

/* Two of the three non-virtual entry points (getGenerator is above). */
void ParticleSystem::enableRenderNode()              { setRenderNode(1); }

void ParticleSystem::disableRenderNode()             { setRenderNode(0); }

/* Slot 1 for Point and Face. */
void THISCALL
ParticleSystem::quadReleaseSlot(ParticleSystem *self, int flags)        { self->quadRelease(flags); }

/* PointParticleSystem */
void *THISCALL
PointParticleSystem::pointDtor(PointParticleSystem *self, unsigned flags)
{
    self->quadDestruct(ps_vtbl_point);
    return scalar_delete(self, flags);
}

BOOL THISCALL
PointParticleSystem::pointCopyFromSlot(PointParticleSystem *self, const ParticleSystem *src)
{ return self->pointCopyFrom(src); }

BOOL THISCALL
PointParticleSystem::pointSetCapacitySlot(PointParticleSystem *self, DWORD n) { return self->pointSetCapacity(n); }

BOOL THISCALL
PointParticleSystem::pointResizeSlot(PointParticleSystem *self, DWORD n)      { return self->pointResize(n); }

BOOL THISCALL
PointParticleSystem::pointSave(PointParticleSystem *self, void *fp, GameLogger *log)
{ return self->pointSerialize(fp, log); }

BOOL THISCALL
PointParticleSystem::pointLoad(PointParticleSystem *self, void *fp, GameLogger *log)
{ return self->pointDeserialize(fp, log); }

/* FaceParticleSystem */
void *THISCALL
FaceParticleSystem::faceDtor(FaceParticleSystem *self, unsigned flags)
{
    self->quadDestruct(ps_vtbl_face);
    return scalar_delete(self, flags);
}

BOOL THISCALL
FaceParticleSystem::faceCopyFromSlot(FaceParticleSystem *self, const FaceParticleSystem *src)
{ return self->faceCopyFrom(src); }

BOOL THISCALL
FaceParticleSystem::faceSetCapacitySlot(FaceParticleSystem *self, DWORD n)  { return self->faceSetCapacity(n); }

BOOL THISCALL
FaceParticleSystem::faceResizeSlot(FaceParticleSystem *self, DWORD n)       { return self->faceResize(n); }

BOOL THISCALL
FaceParticleSystem::faceSave(FaceParticleSystem *self, void *fp, GameLogger *log)
{ return self->faceSerialize(fp, log); }

BOOL THISCALL
FaceParticleSystem::faceLoad(FaceParticleSystem *self, void *fp, GameLogger *log)
{ return self->faceDeserialize(fp, log); }

/* XFaceParticleSystem */
void *THISCALL
XFaceParticleSystem::xfaceDtor(XFaceParticleSystem *self, unsigned flags)
{
    self->xfaceDestruct();
    return scalar_delete(self, flags);
}

void THISCALL
XFaceParticleSystem::xfaceReleaseSlot(XFaceParticleSystem *self, int flags)  { self->xfaceRelease(flags); }

BOOL THISCALL
XFaceParticleSystem::xfaceCopyFromSlot(XFaceParticleSystem *self, const XFaceParticleSystem *src)
{ return self->xfaceCopyFrom(src); }

BOOL THISCALL
XFaceParticleSystem::xfaceSetCapacitySlot(XFaceParticleSystem *self, DWORD n) { return self->xfaceSetCapacity(n); }

BOOL THISCALL
XFaceParticleSystem::xfaceResizeSlot(XFaceParticleSystem *self, DWORD n)      { return self->xfaceResize(n); }

BOOL THISCALL
XFaceParticleSystem::xfaceSave(XFaceParticleSystem *self, void *fp, GameLogger *log)
{ return self->xfaceSerialize(fp, log); }

BOOL THISCALL
XFaceParticleSystem::xfaceLoad(XFaceParticleSystem *self, void *fp, GameLogger *log)
{ return self->xfaceDeserialize(fp, log); }


/* ─── Tick and corner slots ──────────────────────────────────────────────
 */

void THISCALL
ParticleSystem::baseTick(ParticleSystem *self, DWORD dt)  { self->tick(relay_dt(dt)); }

void THISCALL
XFaceParticleSystem::xfaceTickSlot(XFaceParticleSystem *self, DWORD dt)  { self->xfaceTick(relay_dt(dt)); }

void THISCALL
FaceParticleSystem::faceSetVectorSlot(FaceParticleSystem *self, float x, float y, float z)
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("particle: FaceSetVector active (this=%p dir=%f,%f,%f)\n",
                  self, x, y, z);
    self->faceSetVector(x, y, z);
}

void THISCALL
FaceParticleSystem::faceTransformCorners(FaceParticleSystem *self, float *matrix)
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("particle: FaceTransformCorners active (this=%p)\n", self);
    for (int c = 0; c < 6; c++)
        transform_point(self->flCorner_[c], matrix);
}


/* ─── The vtables ─────────────────────────────────────────────────────────
 *
 * One table per class, in the slot order particles.h names, installed by the
 * constructors above.  Hand-built rather than C++ virtuals: the callers use
 * __thiscall and this slot order.  The static_asserts below catch an
 * initialiser that is one entry short, which would otherwise leave a silent
 * NULL slot. */
extern void *const ps_vtbl_base[] = {
    (void *)&ParticleSystem::baseDtor,        (void *)&ParticleSystem::baseRelease,
    (void *)&ParticleSystem::baseCopyFrom,    (void *)&ParticleSystem::baseSetCapacity,
    (void *)&ParticleSystem::baseResize,      (void *)&ParticleSystem::setGeneratorSlot,
    (void *)&ParticleSystem::setEnvironmentSlot,  (void *)&ParticleSystem::baseTick,
    (void *)&ParticleSystem::baseRender,      (void *)&ParticleSystem::baseFill,
    (void *)&ParticleSystem::nopVec3,         (void *)&ParticleSystem::nopPtr,
    (void *)&ParticleSystem::baseDrawNull,    (void *)&ParticleSystem::baseSave,
    (void *)&ParticleSystem::baseLoad,
};

extern void *const ps_vtbl_point[] = {
    (void *)&PointParticleSystem::pointDtor,       (void *)&ParticleSystem::quadReleaseSlot,
    (void *)&PointParticleSystem::pointCopyFromSlot,   (void *)&PointParticleSystem::pointSetCapacitySlot,
    (void *)&PointParticleSystem::pointResizeSlot,     (void *)&ParticleSystem::setGeneratorSlot,
    (void *)&ParticleSystem::setEnvironmentSlot,  (void *)&ParticleSystem::baseTick,
    (void *)&PointParticleSystem::pointRender,     (void *)&PointParticleSystem::pointFillSlot,
    (void *)&ParticleSystem::nopVec3,         (void *)&ParticleSystem::nopPtr,
    (void *)&PointParticleSystem::pointDrawSlot,       (void *)&PointParticleSystem::pointSave,
    (void *)&PointParticleSystem::pointLoad,
};

extern void *const ps_vtbl_face[] = {
    (void *)&FaceParticleSystem::faceDtor,        (void *)&ParticleSystem::quadReleaseSlot,
    (void *)&FaceParticleSystem::faceCopyFromSlot,    (void *)&FaceParticleSystem::faceSetCapacitySlot,
    (void *)&FaceParticleSystem::faceResizeSlot,      (void *)&ParticleSystem::setGeneratorSlot,
    (void *)&ParticleSystem::setEnvironmentSlot,  (void *)&ParticleSystem::baseTick,
    (void *)&ParticleSystem::baseRender,      (void *)&FaceParticleSystem::faceFillSlot,
    (void *)&FaceParticleSystem::faceSetVectorSlot,   (void *)&FaceParticleSystem::faceTransformCorners,
    (void *)&FaceParticleSystem::faceDrawSlot,        (void *)&FaceParticleSystem::faceSave,
    (void *)&FaceParticleSystem::faceLoad,
};

extern void *const ps_vtbl_xface[] = {
    (void *)&XFaceParticleSystem::xfaceDtor,       (void *)&XFaceParticleSystem::xfaceReleaseSlot,
    (void *)&XFaceParticleSystem::xfaceCopyFromSlot,   (void *)&XFaceParticleSystem::xfaceSetCapacitySlot,
    (void *)&XFaceParticleSystem::xfaceResizeSlot,     (void *)&ParticleSystem::setGeneratorSlot,
    (void *)&ParticleSystem::setEnvironmentSlot,  (void *)&XFaceParticleSystem::xfaceTickSlot,
    (void *)&ParticleSystem::baseRender,      (void *)&XFaceParticleSystem::xfaceFillSlot,
    (void *)&ParticleSystem::nopVec3,         (void *)&ParticleSystem::nopPtr,
    (void *)&XFaceParticleSystem::xfaceDrawSlot,       (void *)&XFaceParticleSystem::xfaceSave,
    (void *)&XFaceParticleSystem::xfaceLoad,
};

#define PS_SLOTS(t) (sizeof (t) / sizeof *(t))
static_assert(PS_SLOTS(ps_vtbl_base)  == PS_VTBL_SLOTS, "ParticleSystem vtable");
static_assert(PS_SLOTS(ps_vtbl_point) == PS_VTBL_SLOTS, "Point vtable");
static_assert(PS_SLOTS(ps_vtbl_face)  == PS_VTBL_SLOTS, "Face vtable");
static_assert(PS_SLOTS(ps_vtbl_xface) == PS_VTBL_SLOTS, "XFace vtable");
#undef PS_SLOTS
