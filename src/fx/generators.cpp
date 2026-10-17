/* Every Generator and Environment virtual: the per-tick bookkeeping each
 * hands the device (the particles themselves are simulated and drawn from
 * their distributions there; see particletypes.h), Load, CopyFrom and the
 * constructors. */

#include <string.h>
#include <stdint.h>
#include "generators.h"
#include "particletypes.h"
#include "fxrand.h"
#include "logger.h"
#include <new>
#include <math.h>
#include <stdlib.h>
#include "binio.h"
#include <vector>
#include <algorithm>
#include <iterator>

/* ─── Fading ─── */

/* Fade step for this tick: accumulate, truncate toward zero, suppress when it
 * does not clear the threshold, then subtract what was consumed. */
static uint32_t fade_step(float *accum, float rate, uint32_t threshold, float dt)
{
    *accum += dt * rate;
    uint32_t step = (uint32_t)(long long)*accum;
    if (step <= threshold)
        step = 0;
    *accum -= (float)step;
    return step;
}

/* The fade steps of every tick since the last take, which the device applies
 * as one (it stops at the target either way). */
static void take_fade(ParticleField &out, uint32_t rgb, float *pending)
{
    out.fadeRGB = rgb;
    out.fadeStep = std::min(*pending, 255.0f);
    *pending = 0.0f;
}

static Vec3 vec3_of(const float v[3])
{
    return { v[0], v[1], v[2] };
}

/* ─── The environments ─── */

/* The bases: a generator that emits nothing, a field that does nothing. */
void Generator::takeEmit(ParticleEmit &out)
{
    out.count = 0;
}

void Environment::takeField(ParticleField &out)
{
    out.kind = ParticleFieldKind::None;
}

void GravityEnvironment::tick(float dt)
{
    flFadePending_ += (float)fade_step(&flFadeAccum_, flFadeRate_, dwFadeThreshold_, dt);
}

void GravityEnvironment::takeField(ParticleField &out)
{
    out.kind = ParticleFieldKind::Gravity;
    out.accel = vec3_of(flGravity_);
    for (int a = 0; a < 3; a++)
        out.clip[a] = dwClipEnable_[a] != 0;
    out.clipMin = vec3_of(flClipMin_);
    out.clipMax = vec3_of(flClipMax_);
    take_fade(out, dwTargetRGB_, &flFadePending_);
}

void MagnetEnvironment::tick(float dt)
{
    flFadePending_ += (float)fade_step(&flFadeAccum_, flFadeRate_, dwFadeThreshold_, dt);
}

void MagnetEnvironment::takeField(ParticleField &out)
{
    out.kind = ParticleFieldKind::Magnet;
    out.centre = vec3_of(flCentre_);
    out.force = vec3_of(flForce_);
    out.halfExtent = vec3_of(flHalfExtent_);
    take_fade(out, dwTargetRGB_, &flFadePending_);
}

/* ─── Emission ─── */


EmitterGenerator::EmitterGenerator()
    : seed_((uint32_t)fx_index(0x7fffffff))
{
}

/* dt's share of the rate: whole particles, with the fraction carried into
 * the next tick. */
void EmitterGenerator::tick(float dt)
{
    if (dwEnabled_ == 0)
        return;
    float acc = dt * rate_ + accumulator_;
    accumulator_ = acc;
    if (!(acc >= 0.0f))
        return;
    int count = (int)acc;  // truncates toward zero
    accumulator_ = acc - (float)count;
    // More than the slots hold is lost anyway.
    if (count > 0)
        pendingCount_ = std::min<uint32_t>(pendingCount_ + (uint32_t)count, 1u << 20);
}

void EmitterGenerator::takeEmit(ParticleEmit &out)
{
    out.count = pendingCount_;
    pendingCount_ = 0;
    out.seed = seed_;
    seed_ = seed_ * 1664525u + 1013904223u;
    out.dirMean = vec3_of(dirMean_);
    out.dirVariance = vec3_of(dirVariance_);
    out.speedMean = speedMean_;
    out.speedVariance = speedVariance_;
    out.lifeMean = lifeMean_;
    out.lifeVariance = lifeVariance_;
    out.colourCount = std::min<uint32_t>((uint32_t)colours_.size() / 2, PARTICLE_COLOURS);
    for (uint32_t i = 0; i < out.colourCount; i++) {
        out.colours[i] = colours_[i * 2];
        out.weights[i] = (float)colours_[i * 2 + 1];
    }
    placement(out);
}

void StdGenerator::placement(ParticleEmit &out) const
{
    if (emitMode_ == 1) {
        out.spawn = ParticleSpawn::Box;
        out.posA = vec3_of(boxMin_);
        out.posB = vec3_of(boxMax_);
    } else {
        out.spawn = ParticleSpawn::Gauss;
        out.posA = vec3_of(posMean_);
        out.posB = vec3_of(posVariance_);
    }
}

void XStdGenerator::placement(ParticleEmit &out) const
{
    StdGenerator::placement(out);
    out.posOffset = vec3_of(flPosOffset_);
    out.velOffset = vec3_of(flVelOffset_);
}

void CylinderGenerator::placement(ParticleEmit &out) const
{
    out.spawn = ParticleSpawn::Circle;
    out.posScale = flScale_;
    std::copy(std::begin(flMatrix_), std::end(flMatrix_), out.posMatrix.m);
    out.posOffset = vec3_of(flOrigin_);
}

/* One read of `size` bytes; on a short read this leaves whatever was already
 * read in place and returns false. */
static bool read1(void *dst, unsigned size, std::istream &in)
{
    return readBytes(in, dst, size);
}

/* flGravity =
 * normalise(direction) * magnitude, or direction itself when it is exactly
 * zero. */
void GravityEnvironment::gravitySetVector(const float dir[3], float mag)
{
    if (dir[0] == 0.0f && dir[1] == 0.0f && dir[2] == 0.0f) {
        flGravity_[0] = dir[0];
        flGravity_[1] = dir[1];
        flGravity_[2] = dir[2];
        return;
    }
    double len = sqrt((double)dir[2] * dir[2] +
                            (double)dir[1] * dir[1] +
                            (double)dir[0] * dir[0]);
    float q[3] = { (float)(dir[0] / len), (float)(dir[1] / len), (float)(dir[2] / len) };
    flGravity_[0] = q[0] * mag;
    flGravity_[1] = q[1] * mag;
    flGravity_[2] = q[2] * mag;
}

/* Leaves flFadeAccum as constructed — unlike MagnetEnvironment::Load, which
 * resets it every time. */
int GravityEnvironment::load(std::istream &in)
{
    float dir[3], mag, fade;
    uint32_t argb;
    if (!read1(dir, 12, in))                   return false;
    if (!read1(&mag, 4, in))                   return false;
    if (!read1(&argb, 4, in))                  return false;
    if (!read1(&fade, 4, in))                  return false;
    if (!read1(&dwFadeThreshold_, 4, in)) return false;
    if (!read1(&dwClipEnable_[0], 4, in)) return false;
    if (!read1(&dwClipEnable_[1], 4, in)) return false;
    if (!read1(&dwClipEnable_[2], 4, in)) return false;
    if (!read1(flClipMax_, 12, in))       return false;
    if (!read1(flClipMin_, 12, in))       return false;
    gravitySetVector(dir, mag);
    dwTargetRGB_ = argb & 0xFFFFFF;
    flFadeRate_ = fade;
    return true;
}

/* FORMAT: on disk, force precedes centre.  PRESERVED: dwTargetRGB is never
 * loaded — it keeps whatever the constructor or a CopyFrom set, not what a
 * saved file may have recorded. */
int MagnetEnvironment::load(std::istream &in)
{
    // The base Environment::Load contributes nothing; its result is not
    // checked.
    if (!read1(flForce_, 12, in))         return false;
    if (!read1(flCentre_, 12, in))        return false;
    float range;  // serialised, unused
    if (!read1(&range, 4, in))            return false;
    if (!read1(&flFadeRate_, 4, in))      return false;
    if (!read1(&dwFadeThreshold_, 4, in)) return false;
    flFadeAccum_ = 0.0f;
    return true;
}

/* The base Environment::CopyFrom only gates on type name; it copies nothing
 * itself. */
int Environment::envSameName(const Environment *src) const
{
    return strcmp(src->pName_, pName_) == 0;
}

/* Copies every field. */
int GravityEnvironment::copyFrom(const Environment *src)
{
    if (!envSameName(src))
        return false;
    *this = *static_cast<const GravityEnvironment *>(src);
    return true;
}

int MagnetEnvironment::copyFrom(const Environment *src)
{
    if (!envSameName(src))
        return false;
    *this = *static_cast<const MagnetEnvironment *>(src);
    return true;
}

/* Static type-name strings; pName points at them, never owned or freed here.
 */

Environment::Environment() : pName_((char *)"Environment")
{
}

/* Base ctor, then every member zeroed except the fade threshold,
 * which starts at 10. */
GravityEnvironment::GravityEnvironment()
{
    pName_ = (char *)"GravityEnvironment";
    dwFadeThreshold_ = 10;
}

MagnetEnvironment::MagnetEnvironment()
{
    pName_ = (char *)"MagnetEnvironment";
    dwFadeThreshold_ = 10;
}

/* Matches names exactly, as the game's factory did: an unknown name, or a
 * failed allocation, both return NULL. */
Environment *Environment::create(const char *name)
{
    if (strcmp(name, "Environment") == 0)        return new (std::nothrow) Environment;
    if (strcmp(name, "GravityEnvironment") == 0) return new (std::nothrow) GravityEnvironment;
    if (strcmp(name, "MagnetEnvironment") == 0)  return new (std::nothrow) MagnetEnvironment;
    return NULL;
}

/* PRESERVED: the game's float compare treats an unordered operand (NaN) as
 * equal, so this treats NaN the same as exactly zero, unlike a plain `== 0`.
 */
static inline bool zero_or_nan(float v) { return !(v < 0.0f || v > 0.0f); }

static const float TINY_LENGTH = 1.1920928955078125e-07f;

/* Gates on matching type name, then copies only dwEnabled. */
int Generator::copyFrom(const Generator *src)
{
    if (strcmp(src->pName_, pName_) != 0)
        return false;
    dwEnabled_ = src->dwEnabled_;
    return true;
}

/* The direction, speed and life, as the files have them (see
 * EmitterGenerator). */
bool EmitterGenerator::loadMotion(std::istream &in)
{
    float v[4];
    if (!read1(dirMean_, 12, in) || !read1(dirVariance_, 12, in) || !read1(v, 16, in)
        || !read1(&rate_, 4, in))
        return false;
    speedMean_ = v[0];
    speedVariance_ = v[1];
    lifeMean_ = v[2];
    lifeVariance_ = v[3];
    return true;
}

/* A count, then that many (colour, weight) pairs.  Only the first
 * PARTICLE_COLOURS are used; no shipping effect has more than seven. */
bool EmitterGenerator::loadColours(std::istream &in)
{
    uint32_t count;
    if (!read1(&count, 4, in))
        return false;
    colours_.assign((size_t)count * 2, 0);
    if (count && !readBytes(in, colours_.data(), (size_t)count * 8))
        return false;
    if (count > PARTICLE_COLOURS)
        g_logger.write("particles: %s has %u colours; the first %u are used\n", pName_,
                       count, PARTICLE_COLOURS);
    return true;
}

int StdGenerator::copyFrom(const Generator *gsrc)
{
    if (!Generator::copyFrom(gsrc))
        return false;
    *this = *static_cast<const StdGenerator *>(gsrc);
    pendingCount_ = 0;
    return true;
}

int StdGenerator::load(std::istream &in)
{
    if (!read1(&emitMode_, 4, in) || !read1(boxMax_, 12, in) || !read1(boxMin_, 12, in)
        || !read1(posMean_, 12, in) || !read1(posVariance_, 12, in))
        return false;
    return loadMotion(in) && loadColours(in);
}

int XStdGenerator::copyFrom(const Generator *gsrc)
{
    if (!StdGenerator::copyFrom(gsrc))
        return false;
    const XStdGenerator *src = static_cast<const XStdGenerator *>(gsrc);
    std::copy(std::begin(src->flPosOffset_), std::end(src->flPosOffset_), flPosOffset_);
    std::copy(std::begin(src->flVelOffset_), std::end(src->flVelOffset_), flVelOffset_);
    return true;
}

int XStdGenerator::load(std::istream &in)
{
    if (!StdGenerator::load(in))        return false;
    if (!read1(flPosOffset_, 12, in))     return false;
    return read1(flVelOffset_, 12, in);
}

void XStdGenerator::setPosition(float x, float y, float z)
{
    flPosOffset_[0] = x;
    flPosOffset_[1] = y;
    flPosOffset_[2] = z;
}

void XStdGenerator::xstdStoreScaled(double x, double y,
                             double z, double len, double mag)
{
    float q0 = (float)(x / len);
    float q1 = (float)(y / len);
    double q2 = z / len;
    flVelOffset_[0] = (float)(q0 * mag);
    flVelOffset_[1] = (float)(q1 * mag);
    flVelOffset_[2] = (float)(q2 * mag);
}

/* Keeps the current speed, takes the new direction.  Zero speed becomes
 * FLT_EPSILON; a zero direction zeroes the velocity. */
void XStdGenerator::setDirection(float x, float y, float z)
{
    double vx = flVelOffset_[0], vy = flVelOffset_[1],
                vz = flVelOffset_[2];
    double mag = sqrt(vx * vx + vy * vy + vz * vz);
    if (!(mag < 0.0 || mag > 0.0))
        mag = TINY_LENGTH;
    if (zero_or_nan(x) && zero_or_nan(y) && zero_or_nan(z)) {
        flVelOffset_[0] = flVelOffset_[1] = flVelOffset_[2] = 0.0f;
        return;
    }
    double lx = x, ly = y, lz = z;
    double len = sqrt(lz * lz + ly * ly + lx * lx);
    xstdStoreScaled(lx, ly, lz, len, mag);
}

/* PRESERVED: if the current velocity offset is already zero, this leaves it
 * zero and ignores every argument — direction, speed, all of it. */
void XStdGenerator::setVelocity(float x, float y, float z, float mag)
{
    if (zero_or_nan(flVelOffset_[0]) && zero_or_nan(flVelOffset_[1]) &&
        zero_or_nan(flVelOffset_[2])) {
        flVelOffset_[0] = flVelOffset_[1] = flVelOffset_[2] = 0.0f;
        return;
    }
    double lx = x, ly = y, lz = z;
    double len = sqrt(lz * lz + ly * ly + lx * lx);
    xstdStoreScaled(lx, ly, lz, len, mag);
}

/* Zero speed becomes FLT_EPSILON.  If the current velocity is already zero
 * there is no direction to preserve, so it is left as-is. */
void XStdGenerator::setSpeed(float mag)
{
    if (zero_or_nan(mag))
        mag = TINY_LENGTH;
    if (zero_or_nan(flVelOffset_[0]) && zero_or_nan(flVelOffset_[1]) &&
        zero_or_nan(flVelOffset_[2]))
        return;
    double x = flVelOffset_[0], y = flVelOffset_[1],
                z = flVelOffset_[2];
    double len = sqrt(x * x + y * y + z * z);
    xstdStoreScaled(x, y, z, len, mag);
}

static double vec3_sqlen(const float *v)
{
    return ((double)v[0] * v[0] + (double)v[1] * v[1]) + (double)v[2] * v[2];
}

static double vec3_dot(const float *a, const float *b)
{
    return ((double)a[2] * b[2] + (double)a[1] * b[1]) + (double)a[0] * b[0];
}

/* The cosine of the angle between v and axis e, computed the long way (acos
 * then cos).  A zero v yields NaN. */
static float direction_cosine(const float *v, const float *e)
{
    float lv = (float)sqrt(vec3_sqlen(v));
    float le = (float)sqrt(vec3_sqlen(e));
    float angle = (float)acos(vec3_dot(v, e) / ((double)le * lv));
    return (float)cos((double)angle);
}

/* Rebuilds flMatrix's upper 3x3 from the new direction: a = d x r, then d
 * itself, then b = d x a, where r is the Y axis when d is exactly (1,0,0) (an
 * unordered-safe compare, so NaN counts as equal) and the X axis otherwise.
 *
 * PRESERVED: d, a and b are not normalised — only their pairwise angles are,
 * via direction_cosine — and a, b are rounded to float before that.  The
 * fourth row and column are left as identity. */
void CylinderGenerator::setDirection(float x, float y, float z)
{
    static const float AXIS[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    flDirection_[0] = x;
    flDirection_[1] = y;
    flDirection_[2] = z;
    bool along_x = !(x < 1.0f || x > 1.0f) && zero_or_nan(y) && zero_or_nan(z);
    const float *r = along_x ? AXIS[1] : AXIS[0];
    float d[3] = { x, y, z };
    float a[3] = { (float)((double)r[2] * y - (double)r[1] * z),
                   (float)((double)r[0] * z - (double)r[2] * x),
                   (float)((double)r[1] * x - (double)r[0] * y) };
    float b[3] = { (float)((double)a[2] * y - (double)a[1] * z),
                   (float)((double)a[0] * z - (double)a[2] * x),
                   (float)((double)a[1] * x - (double)a[0] * y) };
    const float *rows[3] = { a, d, b };
    float m[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[i * 4 + j] = direction_cosine(rows[i], AXIS[j]);
    std::copy(std::begin(m), std::end(m), flMatrix_);
}

void CylinderGenerator::setPosition(float x, float y, float z)
{
    flOrigin_[0] = x;
    flOrigin_[1] = y;
    flOrigin_[2] = z;
}

int CylinderGenerator::copyFrom(const Generator *gsrc)
{
    if (!Generator::copyFrom(gsrc))
        return false;
    *this = *static_cast<const CylinderGenerator *>(gsrc);
    pendingCount_ = 0;
    return true;
}

int CylinderGenerator::load(std::istream &in)
{
    if (!read1(flOrigin_, 12, in))        return false;
    if (!read1(&flScale_, 4, in))         return false;
    if (!read1(flDirection_, 12, in))     return false;
    setDirection(flDirection_[0], flDirection_[1], flDirection_[2]);
    return loadMotion(in) && loadColours(in);
}

Generator::Generator()
    : pName_((char *)"Generator"), dwEnabled_(1)
{
}

StdGenerator::StdGenerator()
{
    pName_ = (char *)"StdGenerator";
}

XStdGenerator::XStdGenerator()
{
    pName_ = (char *)"XStdGenerator";
}

/* PRESERVED: calls SetDirection(0,1,0) to set flDirection, then immediately
 * overwrites the matrix it built with plain identity — so flDirection reads
 * (0,1,0) but flMatrix does not reflect it until SetDirection is called again.
 */
CylinderGenerator::CylinderGenerator()
{
    pName_ = (char *)"CylinderGenerator";
    setDirection(0.0f, 1.0f, 0.0f);
    static const float IDENTITY[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    std::copy(std::begin(IDENTITY), std::end(IDENTITY), flMatrix_);
}

template <typename T>
static Generator *gen_new()
{
    return new (std::nothrow) T;
}

Generator *Generator::create(const char *name)
{
    if (strcmp(name, "Generator") == 0)         return gen_new<Generator>();
    if (strcmp(name, "StdGenerator") == 0)      return gen_new<StdGenerator>();
    if (strcmp(name, "XStdGenerator") == 0)     return gen_new<XStdGenerator>();
    if (strcmp(name, "CylinderGenerator") == 0) return gen_new<CylinderGenerator>();
    return NULL;
}

/* ─── Cloning ─── */

/* The factory by class name, then CopyFrom; on refusal the new object is
 * deleted and NULL comes back.  A NULL from the factory is returned as-is. */
template <class T>
static T *clone_by_name(T *made, const T *src)
{
    if (made && !made->copyFrom(src)) {
        delete made;
        return NULL;
    }
    return made;
}

Generator * Generator::clone() const
{
    return clone_by_name(Generator::create(pName_), this);
}

Environment * Environment::clone() const
{
    return clone_by_name(Environment::create(pName_), this);
}

