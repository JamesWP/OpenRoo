/* ParticleSystem: the render that hands the device a draw's emission
 * (particletypes.h), the Face and XFace shapes, and every class's lifecycle
 * and serialisation (particles.h).
 *
 * The random choices loading makes -- XFace's shapes -- draw from the
 * effects' own random numbers (fxrand.h). */

#include <stdint.h>
#include "particles.h"
#include "sysdev.h"
#include "generators.h"
#include "logger.h"
#include <stdlib.h>
#include "fxrand.h"
#include <math.h>
#include <string.h>
#include <new>
#include <fstream>
#include "binio.h"
#include "renderdevice.h"
#include "particletypes.h"
#include <algorithm>
#include <memory>
#include <iterator>

/* ─── Render ───────────────────────────────────────────────────────────────
 */
void ParticleSystem::render(RenderDevice *dev, const ParticleClock &clock, uint32_t seed)
{
    ParticleInstance one = { dev->world(), {}, seed, {} };
    cornerTransform(one.corners.m);
    render(dev, clock, &one, 1);
}

/* Particle k (from 0) is emitted (k + 1) intervals after the start, as the
 * rate's whole particles come due. */
static void emission(ParticleEmit &out, float rate, const ParticleClock &clock)
{
    out.path = clock.path;
    const double upTo = std::min(clock.time, clock.end) - clock.start;
    if (!(rate > 0.0f) || !(upTo > 0.0))
        return;
    const double interval = 1.0 / rate;
    const double emitted = std::min(floor(upTo * rate), 2147483647.0);
    if (emitted < 1.0)
        return;
    out.newest = (int32_t)(emitted - 1.0);
    out.newestAge = (float)(clock.time - (clock.start + emitted * interval));
    out.interval = (float)interval;
}

/* The buffer is made at the first render: the device is only to be had
 * here. */
void ParticleSystem::render(RenderDevice *dev, const ParticleClock &clock,
                            const ParticleInstance *instances, uint32_t count)
{
    if (count_ == 0 || count == 0)
        return;
    if (!buffer_) {
        buffer_ = dev->CreateParticleBuffer(count_);
        if (!buffer_)
            return;
        uploadShapes(dev);
    }

    ParticleEmit emit;
    ParticleField field;
    if (pGenerator_) {
        pGenerator_->describe(emit);
        emission(emit, pGenerator_->rate(), clock);
    }
    if (pEnvironment_)
        pEnvironment_->describe(field);
    ParticleLook l;
    look(l);
    dev->DrawParticles(buffer_, emit, field, l, instances, count);
}

void ParticleSystem::cornerTransform(float out[16]) const
{
    static const float IDENTITY[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    std::copy(IDENTITY, IDENTITY + 16, out);
}

/* ─── Matrices ─────────────────────────────────────────────────────────────
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

static void vec_cross(float o[3], const float a[3], const float b[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

/* ─── Face: billboards ─────────────────────────────────────────────────────
 *
 * The device builds each quad, square to the facing, and carries it through
 * the corner transform (particletypes.h). */
void FaceParticleSystem::setVector(float x, float y, float z)
{
    facing_[0] = x;
    facing_[1] = y;
    facing_[2] = z;
    mat_identity(corners_);
}

/* Composes: a second transform applies after the first. */
void FaceParticleSystem::transformCorners(float *matrix)
{
    mat_mul(corners_, corners_, matrix);
}

void FaceParticleSystem::cornerTransform(float out[16]) const
{
    std::copy(corners_, corners_ + 16, out);
}

/* ─── The ParticleSystem lifecycle ─────────────────────────────────────────
 */
static bool ps_read(void *dst, unsigned size, std::istream &in)
{
    return readBytes(in, dst, size);
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
    count_ = 0;
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
 * source of a different class, takes the source's count,
 * then clones the generator and the environment.  Every failure releases
 * again. */
int ParticleSystem::copyFrom(const ParticleSystem *src)
{
    release(1);
    if (strcmp(src->pName_, pName_) != 0)
        return false;
    count_ = src->count_;

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

/* Release, read the count (fewer than two is refused), then load and attach
 * each sub-object. */
int ParticleSystem::load(std::istream &in)
{
    release(1);

    uint32_t count;
    if (!ps_read(&count, 4, in)) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: error while loading the particle system, because the particle count could not be read");
        return false;
    }
    if (count < 2) {
        g_logger.logSourceLocation(4, "src/render/particles.cpp", __LINE__, "PS: error while loading the particle system, because the particle list could not be created");
        return false;
    }
    count_ = count;

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
    out.facing = { facing_[0], facing_[1], facing_[2] };
    out.size = flScale_;
    std::copy(&FACE_UV[0][0], &FACE_UV[0][0] + 12, &out.uv[0][0]);
}

/* Two-sided: the original drew every quad twice, culling each winding in
 * turn, so a shape turned past edge-on still shows. */
void XFaceParticleSystem::look(ParticleLook &out) const
{
    out.tumbling = true;
    std::copy(&XFACE_UV[0][0], &XFACE_UV[0][0] + 12, &out.uv[0][0]);
}

void XFaceParticleSystem::uploadShapes(RenderDevice *dev)
{
    dev->SetParticleShapes(buffer_, shapes_.get(), shapes_ ? dwCornerTableCount_ : 0);
}

void XFaceParticleSystem::release(int flags)
{
    shapes_.reset();
    ParticleSystem::release(flags);
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

/* The original turned each shape by these X, Y and Z angles a frame; it ran
 * at about this many frames a second. */
static const float SHAPE_TURNS_PER_SECOND = 60.0f;

/* Build dwCornerTableCount shapes.  Each is a quad whose half-size steps from
 * the first size range's low end to its high, turned to a random
 * orientation, which tumbles: a frame's turn is about X, Y and Z by angles
 * drawn from the other three ranges in turn.  That turn is a single rotation
 * about one axis, which is what the device is given. */
int XFaceParticleSystem::buildCorners()
{
    const uint32_t count = dwCornerTableCount_;
    shapes_.reset(new ParticleShape[count]);
    const float *p = this->ranges;
    const float size_min = p[0], size_max = p[1];
    const float step = (size_max - size_min) / (float)count;
    static const float AXIS_X[3] = { 1, 0, 0 }, AXIS_Y[3] = { 0, 1, 0 };

    for (uint32_t i = 0; i < count; i++) {
        ParticleShape &shape = shapes_[i];

        // A quad in the XZ plane, then the plane turned to a random normal.
        const float s = (float)(i + 1) * step + size_min, n = -s;
        const float quad[6][2] = { { n, n }, { s, s }, { n, s }, { s, n }, { s, s }, { n, n } };
        float normal[3] = { fx_uniform(-1.0f, 1.0f), fx_uniform(-1.0f, 1.0f),
                            fx_uniform(-1.0f, 1.0f) };
        float u[3], v[3];
        vec_cross(u, normal, normal[1] == 0.0f && normal[2] == 0.0f ? AXIS_Y : AXIS_X);
        vec_cross(v, normal, u);
        const float lu = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        const float lv = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int c = 0; c < 6; c++)
            for (int k = 0; k < 3; k++)
                shape.corners[c][k] = quad[c][0] * u[k] / lu + quad[c][1] * v[k] / lv;

        // A frame's turn, as an axis and an angle.
        PMat4 m, r;
        mat_rot_x(m, fx_uniform(p[2], p[3]));
        mat_rot_y(r, fx_uniform(p[4], p[5]));
        mat_mul(m, m, r);
        mat_rot_z(r, fx_uniform(p[6], p[7]));
        mat_mul(m, m, r);
        // As a column-vector rotation C = m^T: the axis is C - C^T's.
        const float cosine = std::min(1.0f, std::max(-1.0f, (m[0] + m[5] + m[10] - 1.0f) * 0.5f));
        const float angle = acosf(cosine);
        float axis[3] = { m[6] - m[9], m[8] - m[2], m[1] - m[4] };
        const float la = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        if (la > 1e-6f && angle > 0.0f) {
            shape.axis = { axis[0] / la, axis[1] / la, axis[2] / la };
            shape.rate = angle * SHAPE_TURNS_PER_SECOND;
        }
    }
    return true;
}

/* The ranges come from the source; the corner entries are drawn afresh. */
int XFaceParticleSystem::copyFrom(const ParticleSystem *src)
{
    if (!ParticleSystem::copyFrom(src))
        return false;
    const XFaceParticleSystem *x = static_cast<const XFaceParticleSystem *>(src);
    dwCornerTableCount_ = x->dwCornerTableCount_;
    std::copy(std::begin(x->ranges), std::end(x->ranges), ranges);
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
    if (ps_read(&dwCornerTableCount_, 4, in) && ps_read(ranges, sizeof(ranges), in)
        && buildCorners())
        return true;
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

/* ─── Construction and the factory ─────────────────────────────────────────
 *
 * The six corners start zeroed. */
FaceParticleSystem::FaceParticleSystem() : flScale_(1.0f)
{
    pName_ = "FaceParticleSystem";
}

/* One corner entry by default, sizes 1.0, every other range 0. */
XFaceParticleSystem::~XFaceParticleSystem() = default;

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
