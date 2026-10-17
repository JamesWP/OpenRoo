/* ParticleSystem: the slots, the tick and render that drive the device's
 * simulation (particletypes.h), the Face and XFace corner geometry, and every
 * class's lifecycle and serialisation (particles.h).
 *
 * DETERMINISM: loading and cloning draw from the game's rand(), which the
 * simulation shares (the slots' shapes, XFace's corner table, and the
 * generators' tables in generators.cpp), so those draws are kept exactly as
 * the original made them, redundant ones included. */

#include <stdint.h>
#include "particles.h"
#include "sysdev.h"
#include "generators.h"
#include "logger.h"
#include <stdlib.h>
#include "gametime.h"
#include "crtrand.h"
#include <math.h>
#include <string.h>
#include <new>
#include <fstream>
#include "binio.h"
#include "renderdevice.h"
#include <algorithm>
#include <memory>
#include <iterator>

/* ─── Tick and render ──────────────────────────────────────────────────────
 */
void ParticleSystem::tick(float dt)
{
    if (pGenerator_)
        pGenerator_->tick(dt);
    if (pEnvironment_)
        pEnvironment_->tick(dt);
    pendingDt_ += dt;
}

/* The buffer is made, and the generator's tables sent, at the first render:
 * the device is only to be had here.  The step is everything ticked since the
 * last render, and runs only once however many times the system is drawn. */
void ParticleSystem::render(RenderDevice *dev)
{
    if (slots_.count == 0)
        return;
    if (!buffer_) {
        buffer_ = dev->CreateParticleBuffer(slots_.count, slots_.shapes.data());
        if (!buffer_)
            return;
        ParticleTables t;
        if (pGenerator_ && pGenerator_->tables(t))
            dev->SetParticleTables(buffer_, t);
        nextSlot_ = 0;
    }

    ParticleStep step;
    step.dt = pendingDt_;
    if (pGenerator_)
        pGenerator_->takeEmit(step.emit);
    if (pEnvironment_)
        pEnvironment_->takeField(step.field);
    if (step.dt > 0.0f || step.emit.count > 0) {
        step.emit.count = std::min(step.emit.count, slots_.count);
        step.emit.firstSlot = nextSlot_;
        nextSlot_ = (nextSlot_ + step.emit.count) % slots_.count;
        dev->StepParticles(buffer_, step);
    }
    pendingDt_ = 0.0f;

    ParticleLook l;
    look(l);
    dev->DrawParticles(buffer_, l);
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

void XFaceParticleSystem::tick(float dt)
{
    if (pCornerTable_) {
        for (uint32_t i = 0; i < dwCornerTableCount_; i++) {
            XFaceCornerEntry *e = &pCornerTable_[i];
            PMat4 m;
            mat_identity(m);
            bool fired = false;
            for (int axis = 0; axis < 3; axis++)
                e->flRotAccum[axis] += e->flRotVel[axis];
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
    for (int c = 0; c < 6; c++)
        transform_point(flCorner_[c], matrix);
}

/* ─── The slots and the ParticleSystem lifecycle ───────────────────────────
 */
static bool ps_read(void *dst, unsigned size, std::istream &in)
{
    return readBytes(in, dst, size);
}

/* Sampling constant shared with the generators: 1/32767. */
static const float PS_RAND_SCALE = 1.0f / 32767.0f;

void ParticleSlots::release()
{
    shapes.clear();
    count = 0;
}

/* DETERMINISM: rand() is reseeded from the game clock on every call, and the
 * last slot is skipped (the loop bound is count - 1).  With shapes 0 the
 * indices come out meaningless, but are drawn all the same. */
void ParticleSlots::assignShapes(uint32_t nShapes)
{
    CRT_RAND_SEED = (unsigned)hooks_GameTime(NULL);
    if (count - 1 == 0)
        return;
    float span = (float)(int)(nShapes - 1);
    for (uint32_t i = 0; i < count - 1; i++) {
        int r = (int)crt_rand();
        shapes[i] = (uint32_t)(int)((double)r * span * PS_RAND_SCALE + 0.5);
    }
}

int ParticleSlots::alloc(uint32_t n, uint32_t nShapes)
{
    release();
    if (n < 2)
        return false;
    count = n;
    shapes.assign(n, 0);
    assignShapes(nShapes);
    return true;
}

/* ─── ParticleSystem, the base class ───────────────────────────────────────
 */
ParticleSystem::ParticleSystem()
    : pName_("ParticleSystem"), pGenerator_(NULL), pEnvironment_(NULL)
{
}

/* The release is the base's own, not a subclass's override. */
ParticleSystem::~ParticleSystem()
{
    ParticleSystem::release(1);
}

void ParticleSystem::release(int flags)
{
    if (flags) {
        delete pGenerator_;
        delete pEnvironment_;
    }
    pGenerator_ = NULL;
    pEnvironment_ = NULL;
    RenderDevice::DestroyParticleBuffer(buffer_);
    buffer_ = nullptr;
    pendingDt_ = 0.0f;
    slots_.release();
}

int ParticleSystem::setCapacity(uint32_t count)
{
    return slots_.alloc(count, 0) != 0;
}

int ParticleSystem::setGenerator(Generator *gen)
{
    if (gen == NULL)
        return false;
    delete pGenerator_;
    pGenerator_ = gen;
    return true;
}

int ParticleSystem::setEnvironment(Environment *env)
{
    if (env == NULL)
        return false;
    delete pEnvironment_;
    pEnvironment_ = env;
    return true;
}

/* Releases self (virtually, so a subclass frees its own too), refuses a
 * source of a different class, re-makes the slots at the source's count,
 * then clones the generator and the environment.  Every failure releases
 * again. */
int ParticleSystem::copyFrom(const ParticleSystem *src)
{
    release(1);
    if (strcmp(src->pName_, pName_) != 0)
        return false;
    if (!slots_.alloc(src->slots_.count, 0))
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
    if (strcmp(name.get(), "NULL") != 0) {
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

/* Release, re-make the slots at the stored count, then load and attach each
 * sub-object.  The slots are made directly rather than through setCapacity,
 * so a subclass's override does not run here. */
int ParticleSystem::load(std::istream &in)
{
    release(1);

    uint32_t count;
    if (!ps_read(&count, 4, in)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: error while loading the particle system, because the particle count could not be read");
        return false;
    }
    if (!slots_.alloc(count, 0)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: error while loading the particle system, because the particle list could not be created");
        return false;
    }

    Generator *gen;
    if (!ps_load_sub_object(in, &gen, "PS: Error while loading Particlesystem, because could not read GeneratorclassName", "PS: Error while loading Particlesystem, because Generator of Typ: '%s' coud not created",
                            "PS: Error while loading Particlesystem, because Generator coud not loaded"))
        return false;
    if (gen)
        setGenerator(gen);

    Environment *env;
    if (!ps_load_sub_object(in, &env, "PS: Error while loading Particlesystem, because could not read Environment-ClassName", "PS: Error while loading Particlesystem, because Environment of Typ: '%s' coud not created",
                            "PS: Error while loading Particlesystem, because Environment coud not loaded"))
        return false;
    if (env)
        setEnvironment(env);
    return true;
}

/* ─── Face and XFace lifecycles ────────────────────────────────────────────
 *
 * Face and XFace use different texture corner orders:
 *   Face : (0,0) (1,0) (0,1) (0,1) (1,0) (1,1)
 *   XFace: (0,0) (1,1) (0,1) (1,0) (1,1) (0,0) */
const float FaceParticleSystem::FACE_UV[6][2]   = { {0,0}, {1,0}, {0,1}, {0,1}, {1,0}, {1,1} };
const float XFaceParticleSystem::XFACE_UV[6][2] = { {0,0}, {1,1}, {0,1}, {1,0}, {1,1}, {0,0} };

void FaceParticleSystem::look(ParticleLook &out) const
{
    out.shapes = &flCorner_;
    out.shapeCount = 1;
    std::copy(&FACE_UV[0][0], &FACE_UV[0][0] + 12, &out.uv[0][0]);
}

/* Two-sided: the original drew every quad twice, culling each winding in
 * turn, so a corner entry rotated past edge-on still shows. */
void XFaceParticleSystem::look(ParticleLook &out) const
{
    static_assert(offsetof(XFaceCornerEntry, flCorner) == 0, "corners first");
    out.shapes = reinterpret_cast<const float (*)[6][3]>(pCornerTable_.get());
    out.shapeCount = pCornerTable_ ? dwCornerTableCount_ : 0;
    out.perParticleShape = true;
    std::copy(&XFACE_UV[0][0], &XFACE_UV[0][0] + 12, &out.uv[0][0]);
}

void XFaceParticleSystem::release(int flags)
{
    pCornerTable_.reset();
    ParticleSystem::release(flags);
}

/* Release virtually, size the slots and reset the scale to 1. */
int FaceParticleSystem::setCapacity(uint32_t count)
{
    release(1);
    if (!ParticleSystem::setCapacity(count))
        return false;
    flScale_ = 1.0f;
    return true;
}

/* The scale comes from the source. */
int FaceParticleSystem::copyFrom(const ParticleSystem *src)
{
    release(1);
    if (!ParticleSystem::copyFrom(src))
        return false;
    flScale_ = static_cast<const FaceParticleSystem *>(src)->flScale_;
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
    slots_.assignShapes(dwCornerTableCount_);
    if (!buildCorners()) {
        release(1);
        return false;
    }
    return true;
}

/* PRESERVED: dwCornerTableCount is not copied, so a clone has the one corner
 * entry the constructor leaves it -- and draws rand() for that many. */
int XFaceParticleSystem::copyFrom(const ParticleSystem *src)
{
    if (!ParticleSystem::copyFrom(src))
        return false;
    std::copy(std::begin(static_cast<const XFaceParticleSystem *>(src)->ranges),
              std::end(static_cast<const XFaceParticleSystem *>(src)->ranges), ranges);
    slots_.assignShapes(dwCornerTableCount_);
    if (!buildCorners()) {
        ParticleSystem::release(1);
        return false;
    }
    return true;
}

int XFaceParticleSystem::load(std::istream &in)
{
    if (!ParticleSystem::load(in))
        return false;
    if (ps_read(&dwCornerTableCount_, 4, in) && ps_read(ranges, sizeof(ranges), in))
        return setCapacity(slots_.count);
    release(1);
    g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: loading XFaceParticleSystem failed, because the attributes could not be read");
    return false;
}

/* ─── The non-virtual interface ────────────────────────────────────────────
 *
 * The raw Generator, optionally gated on its class name.  NULL both when there
 * is no generator and when the name differs. */
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
 * The six corners start zeroed. */
FaceParticleSystem::FaceParticleSystem() : flScale_(1.0f)
{
    pName_ = "FaceParticleSystem";
}

/* One corner entry by default, sizes 1.0, every other range 0. */
XFaceParticleSystem::XFaceParticleSystem()
    : dwCornerTableCount_(1),
      ranges{ 1.0f, 1.0f }  // size min/max; lifetime, speed and rotation are 0
{
    pName_ = "XFaceParticleSystem";
}

/* NULL for an unknown name.  The original's base ParticleSystem and its
 * PointParticleSystem are not among them: no shipping effect is either. */
ParticleSystem *
ParticleSystem::create(const char *name)
{
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
