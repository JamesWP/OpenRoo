/* Every Generator and Environment virtual: the per-tick bookkeeping each
 * hands the device (the particles themselves are simulated there; see
 * particletypes.h), Load, CopyFrom, the constructors, and the table builders
 * Load and CopyFrom run.
 *
 * DETERMINISM: the table builders draw from the game's rand(), which the
 * simulation shares, so what they draw and in what order is kept exactly as
 * the original's, quirks included. */

#include <string.h>
#include <stdint.h>
#include "generators.h"
#include "gametime.h"
#include "crtrand.h"
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

/* ─── TableGenerator emission ─── */

/* dt's share of the emission rate: whole particles, with the fraction carried
 * into the next tick.  Their table cursors step on as they would have one
 * particle at a time. */
void TableGenerator::tick(float dt)
{
    if (dwEnabled_ == 0)
        return;
    float acc = dt * flDtScale_ + flAccumulator_;
    flAccumulator_ = acc;
    if (!(acc >= 0.0f))
        return;
    int count = (int)acc;  // truncates toward zero
    flAccumulator_ = acc - (float)count;
    if (count <= 0)
        return;

    if (pendingCount_ == 0) {
        pendingCursor_[0] = dwPosIdx_;
        pendingCursor_[1] = dwVelIdx_;
        pendingCursor_[2] = dwLifeIdx_;
        pendingCursor_[3] = dwProbIdx_;
    }
    // More than the slots hold is lost anyway.
    pendingCount_ = std::min<uint32_t>(pendingCount_ + (uint32_t)count, 1u << 20);
    dwPosIdx_  = (dwPosIdx_ + (uint32_t)count) % POS_COUNT;
    dwVelIdx_  = (uint32_t)((dwVelIdx_ + 3ull * (uint32_t)count) % VEL_COUNT);
    dwLifeIdx_ = (dwLifeIdx_ + (uint32_t)count) % LIFE_COUNT;
    dwProbIdx_ = (dwProbIdx_ + (uint32_t)count) % COLOUR_COUNT;
}

void TableGenerator::takeEmit(ParticleEmit &out)
{
    out.count = pendingCount_;
    out.pos = pendingCursor_[0];
    out.vel = pendingCursor_[1];
    out.life = pendingCursor_[2];
    out.colour = pendingCursor_[3];
    out.velStride = 3;
    placement(out);
    pendingCount_ = 0;
}

bool TableGenerator::tables(ParticleTables &out) const
{
    out.pos = flPosTable_;      out.posCount = POS_COUNT;
    out.vel = flVelTable_;      out.velCount = VEL_COUNT;
    out.life = pLifeTable_;     out.lifeCount = LIFE_COUNT;
    out.colour = pEmitProb_;    out.colourCount = COLOUR_COUNT;
    return true;
}

void StdGenerator::placement(ParticleEmit &out) const
{
    (void)out;  // as sampled
}

void XStdGenerator::placement(ParticleEmit &out) const
{
    out.posOffset = vec3_of(flPosOffset_);
    out.velOffset = vec3_of(flVelOffset_);
}

/* Sample -> scale -> transform -> offset; velocity is NOT run through the
 * matrix. */
void CylinderGenerator::placement(ParticleEmit &out) const
{
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

/* Float constants: each is the float nearest its written value. */
static const float RAND_SCALE  = 1.0f / 32767.0f;
static const float GAUSS_STEP  = 0.001f;
static const float GAUSS_PI    = 3.14159265358979f;
static const float TINY_LENGTH = 1.1920928955078125e-07f;

/* PRESERVED: divides by 2*sigma, not 2*sigma^2 as a normalised Gaussian would
 * — every consumer only uses this as relative histogram weight, so the missing
 * square is absorbed by gauss_fill's later scaling.
 *
 * Every intermediate here is double, with float rounding only at the points
 * noted below (gauss_bucket, and the drawn samples), not extended x87
 * precision. */
static double gauss_pdf(float x, float mu, float sigma)
{
    if (zero_or_nan(sigma))
        return (!(x < mu || x > mu)) ? 1.0 : 0.0;
    double d = (double)x - mu;
    double q = (d * d) / ((double)sigma + sigma);
    double root = sqrt((double)GAUSS_PI + GAUSS_PI);
    return exp(-q) * (1.0 / (root * sigma));
}

/* pdf * scale, rounded to nearest via floor(x + 0.5); the outer cast is then a
 * no-op. */
static int gauss_bucket(float x, float mu, float sigma, float scale)
{
    double v = (double)(gauss_pdf(x, mu, sigma) * scale + 0.5);
    return (int)floor(v);
}

/* DETERMINISM: reseeds via srand(rand()) before drawing, then draws n samples
 * by rand(), in order — replays depend on this exact call sequence.
 *
 * PRESERVED: assumes the bucket at the centre (mu) is exactly 10; the first
 * loop hard-codes that count rather than computing it, and the second loop
 * mirrors the first's shape to fill exactly `total` entries only if that
 * assumption holds. */
static void gauss_fill(float *out, int n, float mu, float sigma, float step)
{
    if (!(step > 0.0f))
        step = 0.1f;
    float scale = (float)(10.0 / gauss_pdf(mu, mu, sigma));

    float x = mu;
    int total = 0, k = 10;
    do {
        x = (float)((double)x + step);
        total += k;
        k = gauss_bucket(x, mu, sigma, scale);
    } while (k > 0);
    total = total * 2 - 10;

    float *table = new float[total];
    int filled = 0;
    int k0 = gauss_bucket(mu, mu, sigma, scale);
    if (k0 > 0) {
        for (int i = 0; i < k0; i++)
            table[i] = mu;
        filled = k0;
    }
    x = mu;
    while (filled < total) {
        x = (float)((double)x + step);
        int kk = gauss_bucket(x, mu, sigma, scale);
        if (kk > 0) {
            float mirror = (float)(((double)mu + mu) - x);
            for (; kk; kk--) {
                table[filled++] = x;
                table[filled++] = mirror;
            }
        }
    }

    CRT_RAND_SEED = crt_rand();
    if (n > 0) {
        float span = (float)((double)total - 1.0f);
        for (; n; n--) {
            int r = (int)crt_rand();
            int idx = (int)((double)r * span * RAND_SCALE + 0.5f);
            *out++ = table[idx];
        }
    }
    delete[] table;
}

/* One-shot: only the very first call this process makes reseeds from the
 * clock; every call after that (and after, every call at all) reseeds via
 * srand(rand()). */
static volatile uint8_t g_uniformSeedPending = 1;

/* DETERMINISM: n samples uniform on [a, b], centred on the midpoint.  See
 * g_uniformSeedPending above for the seeding order this depends on. */
static void uniform_fill(float *out, int n, float a, float b)
{
    float mid = (float)(((double)a + b) * 0.5f);
    float span = (float)((double)b - a);
    if (!(span >= 0.0f))
        span = (float)((double)span * -1.0f);
    if (g_uniformSeedPending) {
        CRT_RAND_SEED = (unsigned)hooks_GameTime(NULL);
        g_uniformSeedPending = 0;
    }
    CRT_RAND_SEED = crt_rand();
    for (; n > 0; n--) {
        int r = (int)crt_rand();
        *out++ = (float)((((double)r - 16383.5) * span * RAND_SCALE) + mid);
    }
}

/* Gates on matching type name, then copies only dwEnabled. */
int Generator::copyFrom(const Generator *src)
{
    if (strcmp(src->pName_, pName_) != 0)
        return false;
    dwEnabled_ = src->dwEnabled_;
    return true;
}

/* DETERMINISM: reseeds from the clock, then draws (colour, weight) pairs by
 * rand() until all 200 slots are filled, skipping zero-weight pairs — the draw
 * order feeds the emitted particle colours.  An empty or missing source table
 * instead fills pEmitProb with all -1. */
static void type_table_clone(std::vector<uint32_t> *table, uint32_t *pcount, uint32_t *emit_prob,
                             const uint32_t *src, uint32_t count)
{
    *pcount = count;
    if (src != NULL && count)
        table->assign(src, src + (size_t)count * 2);
    else
        table->assign((size_t)count * 2, 0);

    if (src == NULL || count == 0) {
        for (int i = 0; i < 200; i++)
            emit_prob[i] = 0xFFFFFFFF;
        return;
    }

    CRT_RAND_SEED = (unsigned)hooks_GameTime(NULL);
    float span = (float)((double)(unsigned long long)count - 1.0f);
    int out = 0;
    for (;;) {
        int idx;
        do {
            int r = (int)crt_rand();
            idx = (int)((double)r * span * RAND_SCALE + 0.5f);
        } while (src[idx * 2 + 1] == 0);
        uint32_t j = 0;
        do {
            emit_prob[out++] = src[idx * 2];
            if (out >= 200)
                return;
            j++;
        } while (j < src[idx * 2 + 1]);
    }
}

static int type_table_load(std::vector<uint32_t> *ptable, uint32_t *pcount, uint32_t *emit_prob, std::istream &in)
{
    uint32_t count;
    if (!readBytes(in, &count, 4))
        return false;
    std::vector<uint32_t> pairs((size_t)count * 2);
    if (!readBytes(in, pairs.data(), (size_t)count * 8))
        return false;
    type_table_clone(ptable, pcount, emit_prob, pairs.data(), count);
    return true;
}

/* Interleave three 500-sample columns into flPosTable (x, y, z per entry). */
void StdGenerator::stdInterleavePos(const float *x, const float *y,
                               const float *z)
{
    for (int k = 0; k < 500; k++) {
        flPosTable_[k * 3 + 0] = x[k];
        flPosTable_[k * 3 + 1] = y[k];
        flPosTable_[k * 3 + 2] = z[k];
    }
}

void StdGenerator::stdBuildSphere(const float *mn, const float *mx)
{
    float a[3] = { mn[0], mn[1], mn[2] }, b[3] = { mx[0], mx[1], mx[2] };
    std::copy(std::begin(a), std::end(a), flSphMin_);
    std::copy(std::begin(b), std::end(b), flSphMax_);
    dwEmitMode_ = 0;
    float *col[3] = { new float[500], new float[500], new float[500] };
    for (int i = 0; i < 3; i++)
        gauss_fill(col[i], 500, a[i], b[i], (float)((double)b[i] * GAUSS_STEP));
    stdInterleavePos(col[0], col[1], col[2]);
    for (int i = 0; i < 3; i++)
        delete[] col[i];
    dwPosIdx_ = 0;
}

void StdGenerator::stdBuildBox(const float *mn, const float *mx)
{
    float a[3] = { mn[0], mn[1], mn[2] }, b[3] = { mx[0], mx[1], mx[2] };
    std::copy(std::begin(b), std::end(b), flBoxMax_);
    std::copy(std::begin(a), std::end(a), flBoxMin_);
    dwEmitMode_ = 1;
    float *col[3] = { new float[500], new float[500], new float[500] };
    for (int i = 0; i < 3; i++)
        uniform_fill(col[i], 500, a[i], b[i]);
    stdInterleavePos(col[0], col[1], col[2]);
    for (int i = 0; i < 3; i++)
        delete[] col[i];
    dwPosIdx_ = 0;
}

static void build_velocity_table(float *table, const float *a, const float *b,
                                 float lmin, float lmax)
{
    float *col[4] = { new float[500], new float[500], new float[500], new float[500] };
    for (int i = 0; i < 3; i++)
        gauss_fill(col[i], 500, a[i], b[i], (float)((double)b[i] * GAUSS_STEP));
    gauss_fill(col[3], 500, lmin, lmax, (float)((double)lmax * GAUSS_STEP));

    for (int k = 0; k < 500; k++) {
        float *v = &table[k * 3];
        v[0] = col[0][k];
        v[1] = col[1][k];
        v[2] = col[2][k];
        double x = v[0], y = v[1], z = v[2];
        double len = sqrt(z * z + x * x + y * y);
        if (len > 0.0) {
            v[0] = (float)(x / len);
            v[1] = (float)(y / len);
            v[2] = (float)(z / len);
        }
        double s = col[3][k];
        v[0] = (float)(s * v[0]);
        v[1] = (float)(s * v[1]);
        v[2] = (float)(s * v[2]);
    }
    for (int i = 0; i < 4; i++)
        delete[] col[i];
}

void StdGenerator::stdBuildVelocity(const float *vmin, const float *vmax,
                               float lmin, float lmax)
{
    float a[3] = { vmin[0], vmin[1], vmin[2] }, b[3] = { vmax[0], vmax[1], vmax[2] };
    build_velocity_table(flVelTable_, a, b, lmin, lmax);
    dwVelIdx_ = 0;
    std::copy(std::begin(a), std::end(a), flVelMin_);
    std::copy(std::begin(b), std::end(b), flVelMax_);
    flLifeMin_ = lmin;
    flLifeMax_ = lmax;
}

void StdGenerator::stdBuildRate(float lo, float hi)
{
    float step = (float)((double)hi * GAUSS_STEP);
    flEmitRateMin_ = lo;
    flEmitRateMax_ = hi;
    gauss_fill(pLifeTable_, 100, lo, hi, step);
}

/* Copies every field but the type table, which is cloned instead — cloning
 * redraws pEmitProb from a fresh seed, so a copy's colours differ from its
 * source's. */
int StdGenerator::copyFrom(const Generator *gsrc)
{
    if (!Generator::copyFrom(gsrc))
        return false;
    const StdGenerator *src = static_cast<const StdGenerator *>(gsrc);
    *this = *src;
    pendingCount_ = 0;
    type_table_clone(&typeTable_, &dwTypeTableCount_, pEmitProb_,
                     src->typeTable_.data(), src->dwTypeTableCount_);
    return true;
}

/* Builds the sphere or box position table (whichever dwEmitMode selects), then
 * always rebuilds the velocity and rate tables. */
int StdGenerator::load(std::istream &in)
{
    if (!read1(&dwEmitMode_, 4, in))      return false;
    if (!read1(flBoxMax_, 12, in))        return false;
    if (!read1(flBoxMin_, 12, in))        return false;
    if (!read1(flSphMin_, 12, in))        return false;
    if (!read1(flSphMax_, 12, in))        return false;
    if (!read1(flVelMin_, 12, in))        return false;
    if (!read1(flVelMax_, 12, in))        return false;
    if (!read1(&flLifeMin_, 4, in))       return false;
    if (!read1(&flLifeMax_, 4, in))       return false;
    if (!read1(&flEmitRateMin_, 4, in))   return false;
    if (!read1(&flEmitRateMax_, 4, in))   return false;
    if (!read1(&flDtScale_, 4, in))       return false;
    if (dwEmitMode_ == 0)
        stdBuildSphere(flSphMin_, flSphMax_);
    if (dwEmitMode_ == 1)
        stdBuildBox(flBoxMin_, flBoxMax_);
    stdBuildVelocity(flVelMin_, flVelMax_,
                       flLifeMin_, flLifeMax_);
    stdBuildRate(flEmitRateMin_, flEmitRateMax_);
    return type_table_load(&typeTable_, &dwTypeTableCount_, pEmitProb_, in);
}

/* PRESERVED: copies flPosOffset but not flVelOffset — a copied generator keeps
 * its own velocity offset regardless of the source's. */
int XStdGenerator::copyFrom(const Generator *gsrc)
{
    if (!StdGenerator::copyFrom(gsrc))
        return false;
    const XStdGenerator *src = static_cast<const XStdGenerator *>(gsrc);
    std::copy(std::begin(src->flPosOffset_), std::end(src->flPosOffset_), flPosOffset_);
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

/* PRESERVED: drops the type table by zeroing the pointer and count without
 * freeing it first — a leak on every reload, papered over because Load's own
 * type-table read immediately replaces both. */
void CylinderGenerator::cylBuildVelocity(const float *vmin, const float *vmax,
                               float lmin, float lmax)
{
    float a[3] = { vmin[0], vmin[1], vmin[2] }, b[3] = { vmax[0], vmax[1], vmax[2] };
    build_velocity_table(flVelTable_, a, b, lmin, lmax);
    dwVelIdx_ = 0;
    std::copy(std::begin(a), std::end(a), flVelMin_);
    std::copy(std::begin(b), std::end(b), flVelMax_);
    typeTable_.clear();
    dwTypeTableCount_ = 0;
    flLifeMin_ = lmin;
    flLifeMax_ = lmax;
}

/* Std's rate-table builder (StdGenerator::stdBuildRate), run on Cylinder's
 * own fields. */
void CylinderGenerator::cylBuildRate(float lo, float hi)
{
    float step = (float)((double)hi * GAUSS_STEP);
    flEmitRateMin_ = lo;
    flEmitRateMax_ = hi;
    gauss_fill(pLifeTable_, 100, lo, hi, step);
}

int CylinderGenerator::copyFrom(const Generator *gsrc)
{
    if (!Generator::copyFrom(gsrc))
        return false;
    const CylinderGenerator *src = static_cast<const CylinderGenerator *>(gsrc);
    *this = *src;
    pendingCount_ = 0;
    type_table_clone(&this->typeTable_, &this->dwTypeTableCount_, this->pEmitProb_,
                     src->typeTable_.data(), src->dwTypeTableCount_);
    return true;
}

/* Reads its own direction back out to rebuild the matrix via
 * cylSetDirection.  The position table (the unit circle) is the constructor's
 * and is never rebuilt here. */
int CylinderGenerator::load(std::istream &in)
{
    if (!read1(flOrigin_, 12, in))        return false;
    if (!read1(&flScale_, 4, in))         return false;
    if (!read1(flDirection_, 12, in))     return false;
    if (!read1(flVelMin_, 12, in))        return false;
    if (!read1(flVelMax_, 12, in))        return false;
    if (!read1(&flLifeMin_, 4, in))       return false;
    if (!read1(&flLifeMax_, 4, in))       return false;
    if (!read1(&flEmitRateMin_, 4, in))   return false;
    if (!read1(&flEmitRateMax_, 4, in))   return false;
    if (!read1(&flDtScale_, 4, in))       return false;
    setDirection(flDirection_[0], flDirection_[1],
                      flDirection_[2]);
    cylBuildVelocity(flVelMin_, flVelMax_,
                       flLifeMin_, flLifeMax_);
    cylBuildRate(flEmitRateMin_, flEmitRateMax_);
    return type_table_load(&typeTable_, &dwTypeTableCount_,
                           pEmitProb_, in);
}

Generator::Generator()
    : pName_((char *)"Generator"), dwEnabled_(1)
{
}

StdGenerator::StdGenerator()
{
    pName_ = (char *)"StdGenerator";
    for (int i = 0; i < 200; i++)
        pEmitProb_[i] = 0xFFFFFFFF;
}

XStdGenerator::XStdGenerator()
{
    pName_ = (char *)"XStdGenerator";
}

/* DETERMINISM: builds the position table from 500 angles drawn uniformly by
 * uniform_fill, so constructing a CylinderGenerator consumes rand() calls; its
 * order relative to other construction matters for replays.
 *
 * PRESERVED: calls SetDirection(0,1,0) to set flDirection, then immediately
 * overwrites the matrix it built with plain identity — so flDirection reads
 * (0,1,0) but flMatrix does not reflect it until SetDirection is called again.
 */
CylinderGenerator::CylinderGenerator()
{
    pName_ = (char *)"CylinderGenerator";
    setDirection(0.0f, 1.0f, 0.0f);
    static const float IDENTITY[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    std::copy(std::begin(IDENTITY), std::end(IDENTITY), flMatrix_);
    for (int i = 0; i < 200; i++)
        pEmitProb_[i] = 0xFFFFFFFF;
    float *angle = new float[500];
    uniform_fill(angle, 500, 0.0f, 6.2831855f);
    for (int k = 0; k < 500; k++) {
        flPosTable_[k * 3 + 0] = (float)sin((double)angle[k]);
        flPosTable_[k * 3 + 2] = (float)cos((double)angle[k]);
    }
    delete[] angle;
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

/* ─── The Gaussian sampler's one caller outside this file's own generators ───
 *
 * ExplodeDebris fills a 30-entry table from it at construction (mu=2.0,
 * sigma=1.0); theme.cpp fills one the same way from parsed config.  Only that
 * table and its read cursor are touched here. */
static void fill_gaussian_field(ExplodeDebris *self, float mu, float sigma)
{
    gauss_fill(self->samples(), 30, mu, sigma, 0.01f);
    self->setCursor(0);
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

/* Called directly by explodedebris.cpp and theme.cpp — see
 * fill_gaussian_field. */
  void 
Gen_FillGaussianField(ExplodeDebris *self, float mu, float sigma)
{
    fill_gaussian_field(self, mu, sigma);
}
