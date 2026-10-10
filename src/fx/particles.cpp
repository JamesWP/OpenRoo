/* ParticleSystem: the fill/draw/render methods, the tick, the Face corner
 * geometry, the ring, and every class's lifecycle and serialisation
 * (particles.h).
 *
 * Fill writes only xyz + diffuse into the scratch vertex buffer; psize,
 * specular and u/v stay as the allocators left them.
 *
 * KAROO_PARTICLE_FX=tint forces every particle magenta; =spin exaggerates
 * XFace rotation velocities x10. */

#include <strings.h>
#include <atomic>
#include <stdint.h>
#include "particles.h"
#include "sysdev.h"
#include "generators.h"
#include "factory.h"
#include "logger.h"
#include <stdlib.h>
#include "gametime.h"
#include "crtrand.h"
#include "gamestr.h"
#include <math.h>
#include <string.h>
#include <new>
#include <fstream>
#include "binio.h"
#include "renderdevice.h"
#include <algorithm>
#include <memory>
#include <iterator>

#define PARTICLE_FVF       VertexFormat::Lit  // XYZ|PSIZE|DIFFUSE|SPECULAR|TEX1, 0x20 stride
#define PARTICLE_LOG_FIRST 8
#define FX_TINT_COLOUR     0xFFFF00FF

/* KAROO_PARTICLE_FX, read once per mode by each caller. */
static bool fx_is(const char *mode)
{
    char buf[16];
    return sysdev::getEnv("KAROO_PARTICLE_FX", buf, sizeof(buf))
        && strcasecmp(buf, mode) == 0;
}

static bool fx_tint(void)
{
    static const bool on = fx_is("tint");
    static std::atomic<long> logged = 0;
    if (logged.exchange(1) == 0)
        g_logger.write("particle: FX mode = %s\n", on ? "tint" : "off");
    return on;
}

uint32_t ParticleNode::colour() const
{
    return fx_tint() ? FX_TINT_COLOUR : dwDiffuse;
}

/* Per-class state so one busy class can't hide the others: the first
 * PARTICLE_LOG_FIRST draws are logged, and so is the first draw that actually
 * carries vertices (an empty ring draws with verts=0). */
struct DrawLogState { std::atomic<long> calls; std::atomic<long> nonempty; };

static void log_draw(DrawLogState *st, const char *name, const void *self,
                     RenderDevice *dev, uint32_t count, bool ok)
{
    bool report = ++st->calls <= PARTICLE_LOG_FIRST;
    if (count > 0 && st->nonempty.exchange(1) == 0)
        report = true;
    if (report)
        g_logger.write("particle: %s this=%p dev=%p verts=%lu -> ok=%d\n",
                  name, self, dev, count, ok);
}

/* ─── Fill ─────────────────────────────────────────────────────────────────
 *
 * All three fills walk the live region of the ring the generator emits into
 * and the environment retires from.  `emit` writes the vertices for one node
 * and returns how many it wrote. */
template <typename EmitFn> uint32_t ParticleSystem::fillRing(EmitFn emit)
{
    uint32_t n = 0;
    if (ring_.pRingHead != ring_.pRingCurrent) {
        ParticleNode *node = ring_.pRingHead;
        do {
            n += emit(node, n);
            node = node->pNext;
        } while (node != ring_.pRingCurrent);
    }
    return n;
}

void PointParticleSystem::fill()
{
    dwVertexCount_ = fillRing([this](const ParticleNode *node, uint32_t n) -> uint32_t {
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
    uint32_t colour = node->colour();
    for (int c = 0; c < 6; c++, v++, corners += 3) {
        v->flX = node->flX + corners[0];
        v->flY = node->flY + corners[1];
        v->flZ = node->flZ + corners[2];
        v->dwDiffuse = colour;
    }
}

void FaceParticleSystem::fill()
{
    dwVertexCount_ = fillRing([this](const ParticleNode *node, uint32_t n) -> uint32_t {
            emit_face(&pVerts_[n], node, &flCorner_[0][0]);
            return 6;
        });
}

void XFaceParticleSystem::fill()
{
    dwVertexCount_ = fillRing([this](const ParticleNode *node, uint32_t n) -> uint32_t {
            emit_face(&pVerts_[n], node,
                      &pCornerTable_[node->dwShapeIndex].flCorner[0][0]);
            return 6;
        });
}

/* ─── Draw ─────────────────────────────────────────────────────────────────
 */
const VertexBuffer *ParticleSystem::uploadVerts(RenderDevice *dev)
{
    if (!vb_ && dwVertexCapacity_ > 0)
        vb_ = dev->CreateVertexBuffer(PARTICLE_FVF, dwVertexCapacity_, BufferUsage::Dynamic);
    if (vb_ && dwVertexCount_ > 0)
        dev->UpdateVertexBuffer(vb_, 0, pVerts_.get(), dwVertexCount_);
    return vb_;
}

uint32_t PointParticleSystem::draw(RenderDevice *dev)
{
    static DrawLogState st;
    bool ok = dev->DrawBuffer(Prim::PointList, uploadVerts(dev), 0, dwVertexCount_);
    log_draw(&st, "PointDraw", this, dev, dwVertexCount_, ok);
    return dwVertexCount_;
}

uint32_t FaceParticleSystem::draw(RenderDevice *dev)
{
    static DrawLogState st;
    bool ok = dev->DrawBuffer(Prim::TriangleList, uploadVerts(dev), 0, dwVertexCount_);
    log_draw(&st, "FaceDraw", this, dev, dwVertexCount_, ok);
    return dwVertexCount_ / 6;
}

/* Two passes, one per face winding: save the cull mode, draw with CullMode::CCW, draw
 * again with CullMode::CW, restore.  That makes an XFace billboard two-sided: a
 * particle whose corner table has rotated past edge-on is still drawn. */
uint32_t XFaceParticleSystem::draw(RenderDevice *dev)
{
    const RasterState saved = dev->raster();
    const VertexBuffer *vb = uploadVerts(dev);
    dev->SetRaster(RasterState{ CullMode::CCW });
    dev->DrawBuffer(Prim::TriangleList, vb, 0, dwVertexCount_);
    dev->SetRaster(RasterState{ CullMode::CW });
    bool ok = dev->DrawBuffer(Prim::TriangleList, vb, 0, dwVertexCount_);
    dev->SetRaster(saved);
    static DrawLogState st;
    log_draw(&st, "XFaceDraw", this, dev, dwVertexCount_, ok);
    return dwVertexCount_ / 6;
}

/* Fill the vertex buffer, then draw it. */
uint32_t ParticleSystem::render(RenderDevice *dev)
{
    fill();
    return draw(dev);
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
    static const bool on = fx_is("spin");
    static std::atomic<long> logged = 0;
    if (on && logged.exchange(1) == 0)
        g_logger.write("particle: FX mode = spin\n");
    return on;
}

void ParticleSystem::tick(float dt)
{
    static std::atomic<long> once = 0;
    if (once.exchange(1) == 0)
        g_logger.write("particle: BaseTick active (this=%p dt=%f gen=%p env=%p)\n",
                  this, dt, pGenerator_, pEnvironment_);
    if (pGenerator_)
        pGenerator_->tick(dt);
    if (pEnvironment_)
        pEnvironment_->tick(dt);
}

void XFaceParticleSystem::tick(float dt)
{
    static std::atomic<long> once = 0;
    if (once.exchange(1) == 0)
        g_logger.write("particle: XFaceTick active (this=%p entries=%lu)\n",
                  this, dwCornerTableCount_);
    if (pCornerTable_) {
        float spin = fx_spin() ? 10.0f : 1.0f;
        for (uint32_t i = 0; i < dwCornerTableCount_; i++) {
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
    ParticleSystem::tick(dt);
}

/* ─── Face corner geometry ───────────────────────────────────
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
void FaceParticleSystem::setVector(float x, float y, float z)
{
    static std::atomic<long> once = 0;
    if (once.exchange(1) == 0)
        g_logger.write("particle: FaceSetVector active (this=%p dir=%f,%f,%f)\n",
                  this, x, y, z);
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

/* Carry the six baked corners through a matrix. */
void FaceParticleSystem::transformCorners(float *matrix)
{
    static std::atomic<long> once = 0;
    if (once.exchange(1) == 0)
        g_logger.write("particle: FaceTransformCorners active (this=%p)\n", this);
    for (int c = 0; c < 6; c++)
        transform_point(flCorner_[c], matrix);
}

/* ─── The ring and the ParticleSystem lifecycle ────────────────────────────
 *
 * The virtuals that make, copy, resize, serialise and destroy a system, plus
 * the RingBuffer they all work on. */
static bool ps_write(const void *src, unsigned size, std::ostream &out)
{
    return writeBytes(out, src, size);
}

static bool ps_read(void *dst, unsigned size, std::istream &in)
{
    return readBytes(in, dst, size);
}

/* Sampling constant shared with the generators: 1/32767. */
static const float PS_RAND_SCALE = 1.0f / 32767.0f;

/* ─── RingBuffer ───────────────────────────────────────────────────────────
 */
/* Free the node block.  PRESERVED: pRingTail is not cleared; the other four
 * fields are. */
void RingBuffer::release()
{
    ringNodes.reset();
    dwRingCount  = 0;
    pRingHead    = NULL;
    pRingCurrent = NULL;
}

/* Give every node but the last a random shape index in [0, shapes-1].
 * DETERMINISM: rand() is reseeded from the game clock on every call, and the
 * last node is skipped (the loop bound is dwRingCount - 1). */
void RingBuffer::assignShapes(uint32_t shapes)
{
    CRT_RAND_SEED = (unsigned)hooks_GameTime(NULL);
    if (dwRingCount - 1 == 0)
        return;
    float span = (float)(int)(shapes - 1);
    for (uint32_t i = 0; i < dwRingCount - 1; i++) {
        int r = (int)crt_rand();
        ringNodes[i].dwShapeIndex =
            (uint32_t)(int)((double)r * span * PS_RAND_SCALE + 0.5);
    }
}

/* (Re)allocate `count` nodes and thread them into one doubly-linked list: head
 * and current at the front, tail at the last node, both ends NULL.  Fewer than
 * two nodes is refused. */
int RingBuffer::alloc(uint32_t count, uint32_t shapes)
{
    release();
    if (count < 2)
        return false;
    dwRingCount = count;
    ringNodes.reset(new ParticleNode[count]());
    ParticleNode *base = ringNodes.get();
    pRingTail    = base + (count - 1);
    pRingHead    = base;
    pRingCurrent = base;
    base[0].pPrev = NULL;
    base[0].pNext = &base[1];
    for (uint32_t i = 1; i + 1 < count; i++) {
        base[i].pPrev = &base[i - 1];
        base[i].pNext = &base[i + 1];
    }
    base[count - 1].pPrev = &base[count - 2];
    base[count - 1].pNext = NULL;

    assignShapes(shapes);
    return true;
}

/* ─── ParticleSystem, the base class ───────────────────────────────────────
 */
ParticleSystem::ParticleSystem()
    : pName_(GS_PSNAME_SYSTEM), pGenerator_(NULL), pEnvironment_(NULL),
      dwVertexCount_(0)
{
}

/* The release is the base's own, not a subclass's override. */
ParticleSystem::~ParticleSystem()
{
    release(1);
    ring_.release();
}

/* Release both sub-objects, the vertex buffer and the ring. */
void ParticleSystem::release(int flags)
{
    if (flags) {
        delete pGenerator_;
        delete pEnvironment_;
    }
    pGenerator_ = NULL;
    pEnvironment_ = NULL;
    pVerts_.reset();
    RenderDevice::DestroyVertexBuffer(vb_);
    vb_ = nullptr;
    dwVertexCapacity_ = 0;
    ring_.release();
}

int ParticleSystem::setCapacity(uint32_t count)
{
    return ring_.alloc(count, 0) != 0;
}

/* Free, then allocate again (RingBuffer::alloc frees as well). */
int ParticleSystem::resize(uint32_t count)
{
    ring_.release();
    return ring_.alloc(count, 0);
}

int ParticleSystem::setGenerator(Generator *gen)
{
    if (gen == NULL || !gen->attachRing(&ring_))
        return false;
    delete pGenerator_;
    pGenerator_ = gen;
    return true;
}

int ParticleSystem::setEnvironment(Environment *env)
{
    if (env == NULL || !env->attachRing(&ring_))
        return false;
    delete pEnvironment_;
    pEnvironment_ = env;
    return true;
}

/* Releases self (virtually, so a subclass frees its vertex buffer too),
 * refuses a source of a different class, re-makes the ring at the source's
 * size, then clones the generator and the environment and attaches each.
 * Every failure releases again. */
int ParticleSystem::copyFrom(const ParticleSystem *src)
{
    release(1);
    if (strcmp(src->pName_, pName_) != 0)
        return false;
    if (!ring_.alloc(src->ring_.dwRingCount, 0))
        return false;

    if (src->pGenerator_) {
        Generator *gen = src->pGenerator_->clone();
        if (gen == NULL || !setGenerator(gen)) {
            release(1);
            return false;
        }
    }
    if (src->pEnvironment_) {
        Environment *env = src->pEnvironment_->clone();
        if (env == NULL || !setEnvironment(env)) {
            release(1);
            return false;
        }
    }
    return true;
}

/* ─── Save / Load ──────────────────────────────────────────────────────────
 *
 * Save takes an ostream and load an istream; both return int.  The base
 * ignores the logger, the subclasses report through it.
 *
 * FORMAT: a sub-object's class name is its length including the terminator,
 * then that many bytes, so the NUL goes to the file too.  A missing sub-object
 * writes the literal "NULL". */
template <class T>
static int ps_write_sub_object(T *obj, std::ostream &out)
{
    const char *name = obj ? obj->name() : GS_PS_NAME_NULL;
    uint32_t len = (uint32_t)strlen(name) + 1;
    if (!ps_write(&len, 4, out))
        return false;
    if (!writeBytes(out, name, len))
        return false;
    return obj ? obj->save(out) : true;
}

/* Ring size, then the generator and the environment. */
int ParticleSystem::save(std::ostream &out)
{
    if (!ps_write(&ring_.dwRingCount, 4, out))
        return false;
    return ps_write_sub_object(pGenerator_, out)
        && ps_write_sub_object(pEnvironment_, out);
}

/* Read one length-prefixed class name into a fresh, terminated buffer.  Null
 * on failure. */
static std::unique_ptr<char[]> ps_read_name(std::istream &in, const char *msg_name)
{
    uint32_t len;
    if (!ps_read(&len, 4, in)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: error while loading the particle system, because the data could not be read");
        return nullptr;
    }
    std::unique_ptr<char[]> name(new char[(size_t)len + 1]());
    if (!readBytes(in, name.get(), len)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, msg_name);
        return nullptr;
    }
    return name;
}

/* One sub-object: its class name, the factory, then its load.  *out is NULL
 * for the literal "NULL" name.  Each failure logs its own message. */
template <class T>
static int ps_load_sub_object(std::istream &in, T **out,
                               const char *msg_noname, const char *msg_nocreate,
                               const char *msg_noload)
{
    *out = NULL;
    std::unique_ptr<char[]> name = ps_read_name(in, msg_noname);
    if (!name)
        return false;
    if (strcmp(name.get(), GS_PS_NAME_NULL) != 0) {
        T *obj = T::create(name.get());
        if (obj == NULL) {
            g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, msg_nocreate, name.get());
        } else if (!obj->load(in)) {
            g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, msg_noload, name.get());
            delete obj;
        } else {
            *out = obj;
        }
        if (*out == NULL)
            return false;
    }
    return true;
}

/* Release, re-make the ring at the stored size, then load and attach each
 * sub-object.  The ring is sized directly rather than through setCapacity, so
 * a subclass's override does not run here. */
int ParticleSystem::load(std::istream &in)
{
    release(1);

    uint32_t count;
    if (!ps_read(&count, 4, in)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: error while loading the particle system, because the particle count could not be read");
        return false;
    }
    if (!ring_.alloc(count, 0)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: error while loading the particle system, because the particle list could not be created");
        return false;
    }

    Generator *gen;
    if (!ps_load_sub_object(in, &gen, GS_PS_MSG_NONAME, GS_PS_MSG_NOGEN,
                            GS_PS_MSG_GENLOAD))
        return false;
    if (gen)
        setGenerator(gen);

    Environment *env;
    if (!ps_load_sub_object(in, &env, GS_PS_MSG_NOENV, GS_PS_MSG_ENVNAME,
                            GS_PS_MSG_ENVLOAD))
        return false;
    if (env)
        setEnvironment(env);
    return true;
}

/* ─── Point, Face and XFace lifecycles ─────────────────────────────────────
 *
 * Face and XFace use different texture corner orders:
 *   Face : (0,0) (1,0) (0,1) (0,1) (1,0) (1,1)
 *   XFace: (0,0) (1,1) (0,1) (1,0) (1,1) (0,0) */
const float FaceParticleSystem::FACE_UV[6][2]   = { {0,0}, {1,0}, {0,1}, {0,1}, {1,0}, {1,1} };
const float XFaceParticleSystem::XFACE_UV[6][2] = { {0,0}, {1,1}, {0,1}, {1,0}, {1,1}, {0,0} };

int ParticleSystem::allocVerts(unsigned perNode, const float (*uv)[2])
{
    RenderDevice::DestroyVertexBuffer(vb_);
    vb_ = nullptr;
    dwVertexCapacity_ = ring_.dwRingCount * perNode;
    pVerts_.reset(new ParticleVertex[dwVertexCapacity_]());
    if (uv)
        for (uint32_t i = 0; i < ring_.dwRingCount; i++)
            for (unsigned c = 0; c < perNode; c++) {
                pVerts_[i * perNode + c].flU = uv[c][0];
                pVerts_[i * perNode + c].flV = uv[c][1];
            }
    return true;
}

/* The corner-table entries and the vertex buffer go with the system; the base
 * release drops the rest. */
void XFaceParticleSystem::release(int flags)
{
    pCornerTable_.reset();
    ParticleSystem::release(flags);
}

XFaceParticleSystem::~XFaceParticleSystem()
{
}

/* Size the ring, then rebuild the vertex buffer.  Both return the second
 * step's result. */
int PointParticleSystem::setCapacity(uint32_t count)
{
    return ParticleSystem::setCapacity(count) && allocVerts(1);
}

int PointParticleSystem::resize(uint32_t count)
{
    return ParticleSystem::resize(count) && allocVerts(1);
}

/* Releases virtually first, and the base copyFrom releases again. */
int PointParticleSystem::copyFrom(const ParticleSystem *src)
{
    release(1);
    return ParticleSystem::copyFrom(src) && allocVerts(1);
}

/* The base write, whose result is discarded, then success. */
int PointParticleSystem::save(std::ostream &out)
{
    ParticleSystem::save(out);
    return true;
}

int PointParticleSystem::load(std::istream &in)
{
    if (!ParticleSystem::load(in))
        return false;
    if (!allocVerts(1)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: loading PointParticleSystem failed, because the vertex array could not be created");
        return false;
    }
    return true;
}

/* Release virtually, size the ring, reset the scale to 1 and rebuild the
 * corners. */
int FaceParticleSystem::setCapacity(uint32_t count)
{
    release(1);
    if (!ParticleSystem::setCapacity(count))
        return false;
    flScale_ = 1.0f;
    return allocVerts();
}

/* The scale comes from the source. */
int FaceParticleSystem::copyFrom(const ParticleSystem *src)
{
    release(1);
    if (!ParticleSystem::copyFrom(src))
        return false;
    flScale_ = static_cast<const FaceParticleSystem *>(src)->flScale_;
    return allocVerts();
}

int FaceParticleSystem::resize(uint32_t count)
{
    return ParticleSystem::resize(count) && allocVerts();
}

/* The base write (result discarded, as Point's), then the scale. */
int FaceParticleSystem::save(std::ostream &out)
{
    ParticleSystem::save(out);
    if (!ps_write(&flScale_, 4, out)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: saving FaceParticleSystem failed, because the face size could not be saved");
        return false;
    }
    return true;
}

int FaceParticleSystem::load(std::istream &in)
{
    if (!ParticleSystem::load(in))
        return false;
    if (!ps_read(&flScale_, 4, in)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: loading FaceParticleSystem failed, because the face size could not be read");
        return false;
    }
    if (!allocVerts()) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: loading ParticleSystem failed, because the vertex array could not be created");
        return false;
    }
    return true;
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
int XFaceParticleSystem::buildCorners()
{
    uint32_t count = dwCornerTableCount_;
    pCornerTable_.reset(new XFaceCornerEntry[count]());
    XFaceCornerEntry *table = pCornerTable_.get();

    const float *p = this->ranges;
    float size_min = p[0], size_max = p[1];
    float life_min = p[2], life_max = p[3];
    float speed_min = p[4], speed_max = p[5];
    float rot_min = p[6], rot_max = p[7];

    float step = (float)(((double)size_max - size_min) / (double)(int)count);
    static const float AXIS[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

    for (uint32_t i = 0; i < count; i++) {
        XFaceCornerEntry *e = &table[i];
        float s = (float)((double)(int)(i + 1) * step + size_min);
        float n = -s;
        const float quad[6][3] = { { n, 0, n }, { s, 0, s }, { n, 0, s },
                                   { s, 0, n }, { s, 0, s }, { n, 0, n } };
        std::copy_n(&quad[0][0], 18, &e->flCorner[0][0]);

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
    return true;
}

/* The base capacity result is discarded; the shape indices are re-drawn from
 * the corner count. */
int XFaceParticleSystem::setCapacity(uint32_t count)
{
    ParticleSystem::setCapacity(count);
    ring_.assignShapes(dwCornerTableCount_);
    if (!buildCorners() || !allocVerts()) {
        release(1);
        return false;
    }
    return true;
}

/* No virtual release first, unlike Point and Face. */
int XFaceParticleSystem::copyFrom(const ParticleSystem *src)
{
    if (!ParticleSystem::copyFrom(src))
        return false;
    std::copy(std::begin(static_cast<const XFaceParticleSystem *>(src)->ranges),
              std::end(static_cast<const XFaceParticleSystem *>(src)->ranges), ranges);
    ring_.assignShapes(dwCornerTableCount_);
    if (!buildCorners()) {
        ParticleSystem::release(1);
        return false;
    }
    if (!allocVerts()) {
        release(1);
        return false;
    }
    return true;
}

/* Vertices before corners here; the other two do corners first. */
int XFaceParticleSystem::resize(uint32_t count)
{
    if (!ParticleSystem::resize(count))
        return false;
    pCornerTable_.reset();
    ring_.assignShapes(dwCornerTableCount_);
    if (!allocVerts() || !buildCorners()) {
        release(1);
        return false;
    }
    return true;
}

/* The base stream, then the corner count and the eight ranges.  Load finishes
 * by re-running setCapacity with the ring size, which rebuilds the corners and
 * vertices. */
int XFaceParticleSystem::save(std::ostream &out)
{
    if (!ParticleSystem::save(out))
        return false;
    if (ps_write(&dwCornerTableCount_, 4, out)
        && ps_write(ranges, sizeof(ranges), out))
        return true;
    g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: saving XFaceParticleSystem failed, because the attributes could not be saved");
    return false;
}

int XFaceParticleSystem::load(std::istream &in)
{
    if (!ParticleSystem::load(in))
        return false;
    if (ps_read(&dwCornerTableCount_, 4, in) && ps_read(ranges, sizeof(ranges), in))
        return setCapacity(ring_.dwRingCount);
    release(1);
    g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: loading XFaceParticleSystem failed, because the attributes could not be read");
    return false;
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
void ParticleSystem::setRenderNode(uint32_t enabled)
{
    if (pGenerator_)
        pGenerator_->setEnabled(enabled);
}

void ParticleSystem::enableRenderNode()              { setRenderNode(1); }

void ParticleSystem::disableRenderNode()             { setRenderNode(0); }

/* ─── Construction and the factory ─────────────────────────────────────────
 *
 * The type-name strings; pName points at them as the constructors leave it. */
PointParticleSystem::PointParticleSystem()
{
    pName_ = GS_PSNAME_POINT_SYSTEM;
}

/* The six corners start zeroed. */
FaceParticleSystem::FaceParticleSystem() : flScale_(1.0f)
{
    pName_ = GS_PSNAME_FACE_SYSTEM;
    std::fill(&flCorner_[0][0], &flCorner_[0][0] + 18, 0.0f);
}

/* One corner entry by default, sizes 1.0, every other range 0. */
XFaceParticleSystem::XFaceParticleSystem()
    : dwCornerTableCount_(1),
      ranges{ 1.0f, 1.0f }  // size min/max; lifetime, speed and rotation are 0
{
    pName_ = GS_PSNAME_XFACE_SYSTEM;
}

/* The four names and sizes (0x28 / 0x30 / 0x7a / 0x96); NULL for an unknown
 * name. */
ParticleSystem *
ParticleSystem::create(const char *name)
{
    if (strcmp(name, "ParticleSystem") == 0)       return new (std::nothrow) ParticleSystem;
    if (strcmp(name, "PointParticleSystem") == 0)  return new (std::nothrow) PointParticleSystem;
    if (strcmp(name, "FaceParticleSystem") == 0)   return new (std::nothrow) FaceParticleSystem;
    if (strcmp(name, "XFaceParticleSystem") == 0)  return new (std::nothrow) XFaceParticleSystem;
    return NULL;
}

/* ─── The two ways the game builds a system ────────────────────────────────
 *
 * Clone and load from a .par file; both go through the factory.
 *
 * Length-prefixed class name, then the factory, then load. */
ParticleSystem *
ParticleSystem::loadStream(std::istream &in)
{
    uint32_t len;
    if (!readBytes(in, &len, 4)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: could not read the particle system, because the data could not be read");
        return NULL;
    }
    std::unique_ptr<char[]> name(new char[(size_t)len + 1]());
    if (!readBytes(in, name.get(), len)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: could not read the particle system, because the data could not be read");
        return NULL;
    }
    ParticleSystem *ps = ParticleSystem::create(name.get());
    if (ps == NULL) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: could not read the particle system, because the system '%s' could not be created", name.get());
        return NULL;
    }
    if (!ps->load(in)) {
        delete ps;
        return NULL;
    }
    return ps;
}

/* The .par entry point. */
ParticleSystem *
ParticleSystem::loadFile(const char *path)
{
    g_logger.logMessage(2, "PS: reading particle system from file %s ...", path);
    // Text mode, which is load-bearing: the files carry CRLF pairs that the
    // CRT folds, and the format counts on it.
    sysdev::TextFile in(path);
    if (!in) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: could not read the particle system, because file %s could not be opened", path);
        return NULL;
    }
    return ParticleSystem::loadStream(in);
}

/* Build one of the same class and copyFrom. */
ParticleSystem * ParticleSystem::clone() const
{
    ParticleSystem *made = ParticleSystem::create(pName_);
    if (made && !made->copyFrom(this)) {
        delete made;
        return NULL;
    }
    return made;
}
