/* Every Generator and Environment virtual — Tick/emit, Save, Load, CopyFrom,
 * the dtors and constructors, and the ring helpers the two environment ticks
 * share.
 *
 * The ring is one NULL-terminated doubly-linked list, partitioned as
 * [pRingHead, pRingCurrent) live and [pRingCurrent, pRingTail] free.  An
 * environment's tick owns ageing and retirement; a generator's emit owns
 * adding new particles at pRingCurrent.  Ring allocation itself lives
 * elsewhere — this file only honours the contract. */

#include <string.h>
#include "portable.h"
#include <stdint.h>
#include "generators.h"
#include "sysdev.h"
#include "clock.h"
#include "crtrand.h"
#include <new>
#include "factory.h"
#include "logger.h"
#include "gamestr.h"
#include <math.h>
#include <stdlib.h>
#include "binio.h"
#include <vector>
#include <algorithm>
#include <iterator>

/* Stores a float's bit pattern, as the emit tables keep their values. */
static inline void set_bits(float *dst, uint32_t bits)
{
    memcpy(dst, &bits, sizeof(bits));
}


#define SIM_LOG_FIRST 8

/* KAROO_PARTICLE_FX, read once and cached:
 *   gravity   multiply gravity (and magnet force) by 5;
 *   antigrav  invert and amplify by -3, so every falling effect rises;
 *   nolife    skip the life decrement, so nothing expires — also a
 *             ring-contract stress test: emission must stall, not corrupt;
 *   burst     scale initial velocity by 3 on emission;
 *   loadflip  negate gravity magnitude / magnet force as Load reads them;
 *   fastemit  scale every generator's emit rate by 5 as Load reads it. */

enum SimFx { FX_NONE = 0, FX_GRAVITY, FX_NOLIFE, FX_ANTIGRAV, FX_BURST, FX_LOADFLIP,
             FX_FASTEMIT };

static SimFx sim_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        const char *name = "off";
        cached = FX_NONE;
        if (sysdev::getEnv("KAROO_PARTICLE_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "gravity") == 0)       { cached = FX_GRAVITY;  name = "gravity";  }
            else if (lstrcmpiA(buf, "nolife") == 0)   { cached = FX_NOLIFE;   name = "nolife";   }
            else if (lstrcmpiA(buf, "antigrav") == 0) { cached = FX_ANTIGRAV; name = "antigrav"; }
            else if (lstrcmpiA(buf, "burst") == 0)    { cached = FX_BURST;    name = "burst";    }
            else if (lstrcmpiA(buf, "loadflip") == 0) { cached = FX_LOADFLIP; name = "loadflip"; }
            else if (lstrcmpiA(buf, "fastemit") == 0) { cached = FX_FASTEMIT; name = "fastemit"; }
        }
        if (cached != FX_NONE)
            g_logger.write("sim: FX mode = %s\n", name);
    }
    return (SimFx)cached;
}

static float fx_gravity_scale(SimFx fx)
{
    switch (fx) {
    case FX_GRAVITY:  return 5.0f;
    case FX_ANTIGRAV: return -3.0f;
    default:          return 1.0f;
    }
}

/* ─── Shared ring/colour helpers ─── */

/* Unlinks an expired node and appends it at the free end.  Re-seeds
 * pRingCurrent when emission had stalled with the ring empty, so it can resume
 * once free space returns. */
static void retire_node(RingBuffer *ring, ParticleNode *node)
{
    if (node->pNext == NULL) {
        ring->pRingCurrent = node;
        return;
    }
    node->pNext->pPrev = node->pPrev;
    if (node->pPrev == NULL)
        ring->pRingHead = node->pNext;
    else
        node->pPrev->pNext = node->pNext;

    ring->pRingTail->pNext = node;
    node->pNext = NULL;
    node->pPrev = ring->pRingTail;
    ring->pRingTail = node;

    if (ring->pRingCurrent == NULL)
        ring->pRingCurrent = node;
}

/* One colour channel moved by at most step toward target.  uint32_t arithmetic
 * throughout, so a step larger than the true distance is clamped before the
 * subtraction, avoiding unsigned wraparound. */
static uint32_t fade_channel(uint32_t cur, uint32_t target, uint32_t step)
{
    if (cur == target)
        return cur;
    if (target < cur)
        return (step < cur - target) ? cur - step : target;
    return (step < target - cur) ? cur + step : target;
}

/* PRESERVED: the "increase" branch for the red channel gates and increments
 * the ALPHA byte instead of red.  While step is smaller than target[0]-alpha
 * it leaves red exactly where it was and only bumps alpha; once that threshold
 * is cleared, red jumps straight to the target instead of stepping gradually.
 * Both environment ticks share this exactly. */
static uint32_t fade_diffuse(uint32_t diffuse, const uint32_t target[3], uint32_t step)
{
    uint32_t blue  = diffuse & 0xff;
    uint32_t green = (diffuse >> 8) & 0xff;
    uint32_t red   = (diffuse >> 16) & 0xff;
    uint32_t alpha = diffuse >> 24;

    uint32_t out_red = red;
    if (red != target[0]) {
        out_red = target[0];
        if (target[0] < red) {
            if (step < red - target[0])
                out_red = red - step;
        } else if (step < target[0] - alpha) {
            alpha  += step;
            out_red = red;
        }
    }
    green = fade_channel(green, target[1], step);
    blue  = fade_channel(blue,  target[2], step);

    return ((alpha * 0x100 + out_red) * 0x100 + green) * 0x100 + blue;
}

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

/* ─── GravityEnvironment tick ─── */

void GravityEnvironment::gravityTick(float dt)
{
    RingBuffer *ring = pRing_;
    if (ring->pRingCurrent == ring->pRingHead)
        return;

    SimFx fx = sim_fx();
    float scale = fx_gravity_scale(fx);
    float gx = flGravity_[0] * scale;
    float gy = flGravity_[1] * scale;
    float gz = flGravity_[2] * scale;

    uint32_t step = fade_step(&flFadeAccum_, flFadeRate_,
                           dwFadeThreshold_, dt);

    ring = pRing_;
    ParticleNode *node = ring->pRingHead;
    if (node == ring->pRingCurrent)
        return;

    do {
        ParticleNode *next;
        bool retire;

        if (fx != FX_NOLIFE)
            node->flLife -= dt;

        if (node->flLife >= 0.0f) {
            node->flVel[0] += gx * dt;
            node->flVel[1] += gy * dt;
            node->flVel[2] += gz * dt;
            node->flX += node->flVel[0] * dt;
            node->flY += node->flVel[1] * dt;
            node->flZ += node->flVel[2] * dt;

            if (step != 0)
                node->dwDiffuse = fade_diffuse(node->dwDiffuse, dwTargetRGB_, step);

            const float pos[3] = { node->flX, node->flY, node->flZ };
            retire = false;
            for (int a = 0; a < 3; a++)
                if (dwClipEnable_[a] != 0 &&
                    (pos[a] > flClipMax_[a] || pos[a] < flClipMin_[a]))
                    retire = true;
        } else {
            retire = true;
        }

        next = node->pNext;
        if (retire) {
            retire_node(pRing_, node);
            if (next == NULL)
                return;
        }
        node = next;
    } while (node != pRing_->pRingCurrent);
}

/* ─── MagnetEnvironment tick ─── */

/* Per-axis "has arrived" test, in branch form rather than fabsf(d) <= half —
 * the two differ if a half-extent is ever negative. */
static bool within_extent(float d, float half)
{
    return (d <= 0.0f) ? (-half <= d) : (d <= half);
}

void MagnetEnvironment::magnetTick(float dt)
{
    RingBuffer *ring = pRing_;
    if (ring->pRingCurrent == ring->pRingHead)
        return;

    SimFx fx = sim_fx();
    // antigrav repels instead of attracting — the magnet equivalent of the
    // inverted gravity, and just as visible on a shield effect.
    float fscale = (fx == FX_ANTIGRAV) ? -3.0f : (fx == FX_GRAVITY ? 5.0f : 1.0f);

    uint32_t step = fade_step(&flFadeAccum_, flFadeRate_,
                           dwFadeThreshold_, dt);

    ring = pRing_;
    ParticleNode *node = ring->pRingHead;
    if (node == ring->pRingCurrent)
        return;

    do {
        ParticleNode *next;
        bool retire = false;

        if (fx != FX_NOLIFE)
            node->flLife -= dt;

        if (node->flLife >= 0.0f) {
            float d[3] = { flCentre_[0] - node->flX,
                           flCentre_[1] - node->flY,
                           flCentre_[2] - node->flZ };

            if (within_extent(d[0], flHalfExtent_[0]) &&
                within_extent(d[1], flHalfExtent_[1]) &&
                within_extent(d[2], flHalfExtent_[2])) {
                retire = true;  // arrived at the magnet
            } else {
                // Unguarded division: a zero-length d has already matched the
                // extent test above and been retired, so this can't divide by
                // zero.
                float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                node->flVel[0] += (d[0] / len) * flForce_[0] * fscale * dt;
                node->flVel[1] += (d[1] / len) * flForce_[1] * fscale * dt;
                node->flVel[2] += (d[2] / len) * flForce_[2] * fscale * dt;
                node->flX += node->flVel[0] * dt;
                node->flY += node->flVel[1] * dt;
                node->flZ += node->flVel[2] * dt;

                if (step != 0)
                    node->dwDiffuse = fade_diffuse(node->dwDiffuse,
                                                   dwTargetRGB_, step);
            }
        } else {
            retire = true;
        }

        next = node->pNext;
        if (retire) {
            retire_node(pRing_, node);
            if (next == NULL)
                return;
        }
        node = next;
    } while (node != pRing_->pRingCurrent);
}

/* ─── StdGenerator emit ─── */

/* PRESERVED: wraps by subtracting off the pre-increment value rather than by
 * modulo — the two only differ once an index is driven out of range (e.g. the
 * step-3 index maps 497/498/499 to 0/1/2 via old-(limit-step), not old%limit).
 */
static uint32_t wrap_index(uint32_t old, uint32_t step, uint32_t limit)
{
    uint32_t next = old + step;
    return (next > limit - 1) ? old - (limit - step) : next;
}

/* Life/prob indices use a different idiom: bump while below the last entry,
 * otherwise snap to 0. */
static uint32_t bump_index(uint32_t cur, uint32_t count)
{
    return (cur < count - 1) ? cur + 1 : 0;
}

/* Shared by StdGenerator and XStdGenerator: XStd's emit is Std's with a
 * constant bias added to the sampled position and velocity, so both go through
 * here.  pos_off / vel_off are NULL for a plain StdGenerator. */
void StdGenerator::stdEmit(float dt,
                     const float *pos_off, const float *vel_off)
{
    if (dwEnabled_ == 0)
        return;
    RingBuffer *ring = pRing_;
    if (ring->pRingCurrent == NULL)
        return;

    float acc = dt * flDtScale_ + flAccumulator_;
    flAccumulator_ = acc;
    if (!(acc >= 0.0f))
        return;

    int count = (int)acc;  // truncates toward zero
    flAccumulator_ = acc - (float)count;
    if (count <= 0)
        return;

    SimFx fx = sim_fx();
    float vscale = (fx == FX_BURST) ? 3.0f : 1.0f;

    for (int emitted = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        const float *pos = &flPosTable_[dwPosIdx_ * 3];
        const float *vel = &flVelTable_[dwVelIdx_ * 3];

        node->flLife = pLifeTable_[dwLifeIdx_];
        node->flX = pos[0] + (pos_off ? pos_off[0] : 0.0f);
        node->flY = pos[1] + (pos_off ? pos_off[1] : 0.0f);
        node->flZ = pos[2] + (pos_off ? pos_off[2] : 0.0f);
        node->flVel[0] = (vel[0] + (vel_off ? vel_off[0] : 0.0f)) * vscale;
        node->flVel[1] = (vel[1] + (vel_off ? vel_off[1] : 0.0f)) * vscale;
        node->flVel[2] = (vel[2] + (vel_off ? vel_off[2] : 0.0f)) * vscale;
        node->dwDiffuse = pEmitProb_[dwProbIdx_];

        dwPosIdx_  = wrap_index(dwPosIdx_, 1, 500);
        dwVelIdx_  = wrap_index(dwVelIdx_, 3, 500);
        dwLifeIdx_ = bump_index(dwLifeIdx_, 100);
        dwProbIdx_ = bump_index(dwProbIdx_, 200);

        // Claim the node: advance the free-list cursor past it.
        pRing_->pRingCurrent = node->pNext;
        ring = pRing_;
        if (ring->pRingCurrent == NULL)
            return;  // ring full — stop early
        if (count <= ++emitted)
            return;
    }
}

/* ─── CylinderGenerator emit ─── */

/* (v,1) x M as a row vector, with the w-divide skipped when w is exactly 0.0f.
 * M is row-major: out[c] = sum_r M[r][c] * v[r]. */
static void transform_point_row(float out[3], const float v[3], const float m[16])
{
    float o[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float in[4] = { v[0], v[1], v[2], 1.0f };
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            o[c] += m[r * 4 + c] * in[r];
    // PRESERVED: the w-divide is skipped only when w is exactly 0.0f, not
    // merely small — an epsilon guard here would change results whenever w
    // rounds close to zero without being it.
    if (o[3] != 0.0f) {
        o[0] /= o[3]; o[1] /= o[3]; o[2] /= o[3];
    }
    out[0] = o[0]; out[1] = o[1]; out[2] = o[2];
}

void CylinderGenerator::cylinderEmit(float dt)
{
    if (dwEnabled_ == 0)
        return;
    RingBuffer *ring = pRing_;
    if (ring->pRingCurrent == NULL)
        return;

    float acc = dt * flDtScale_ + flAccumulator_;
    flAccumulator_ = acc;
    if (!(acc >= 0.0f))
        return;

    int count = (int)acc;
    flAccumulator_ = acc - (float)count;
    if (count <= 0)
        return;

    float vscale = (sim_fx() == FX_BURST) ? 3.0f : 1.0f;

    for (int emitted = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        const float *pos = &flPosTable_[dwPosIdx_ * 3];
        const float *vel = &flVelTable_[dwVelIdx_ * 3];

        node->flLife = pLifeTable_[dwLifeIdx_];

        // Sample -> scale -> transform -> offset.
        float p[3] = { pos[0] * flScale_,
                       pos[1] * flScale_,
                       pos[2] * flScale_ };
        float t[3];
        transform_point_row(t, p, flMatrix_);
        node->flX = t[0] + flOrigin_[0];
        node->flY = t[1] + flOrigin_[1];
        node->flZ = t[2] + flOrigin_[2];

        // Velocity is NOT run through the matrix.
        node->flVel[0] = vel[0] * vscale;
        node->flVel[1] = vel[1] * vscale;
        node->flVel[2] = vel[2] * vscale;
        node->dwDiffuse = pEmitProb_[dwProbIdx_];

        dwPosIdx_  = wrap_index(dwPosIdx_, 1, 500);
        dwVelIdx_  = wrap_index(dwVelIdx_, 3, 500);
        dwLifeIdx_ = bump_index(dwLifeIdx_, 100);
        dwProbIdx_ = bump_index(dwProbIdx_, 200);

        pRing_->pRingCurrent = node->pNext;
        ring = pRing_;
        if (ring->pRingCurrent == NULL)
            return;
        if (count <= ++emitted)
            return;
    }
}

/* ─── One definition per class ─────────────────────────────────────────────
 *
 * Each of these is the single implementation of that class's tick, logging
 * included; every caller reaches it through the virtual tick(). */

#define SIM_LOG_ONCE(counter) \
    static AtomicInt counter = 0; \
    if (atomicIncrement(&counter) <= SIM_LOG_FIRST)

static uint32_t count_live(const RingBuffer *ring)
{
    uint32_t live = 0;
    for (ParticleNode *n = ring->pRingHead; n && n != ring->pRingCurrent; n = n->pNext)
        live++;
    return live;
}

static uint32_t count_free(const RingBuffer *ring)
{
    uint32_t free_nodes = 0;
    for (ParticleNode *n = ring->pRingCurrent; n; n = n->pNext)
        free_nodes++;
    return free_nodes;
}

/* KAROO_SIM_STATS=N logs each environment's ring occupancy — live and free
 * counts, plus the life range and how many live nodes have already expired —
 * every N ticks, keyed by object address so several systems in one scene stay
 * distinguishable.  Off unless the variable is set. */
static uint32_t stats_interval(void)
{
    static AtomicInt cached = -1;
    if (cached < 0) {
        char buf[16];
        long v = 0;
        if (sysdev::getEnv("KAROO_SIM_STATS", buf, sizeof(buf)))
            v = (long)strtol(buf, NULL, 10);
        if (v < 0)
            v = 0;
        atomicExchange(&cached, v);
        if (v > 0)
            g_logger.write("sim: stats every %ld ticks\n", v);
    }
    return (uint32_t)cached;
}

static void stats_tick(const char *what, void *self, const RingBuffer *ring, AtomicInt *counter, float dt)
{
    uint32_t every = stats_interval();
    if (every == 0)
        return;
    long n = atomicIncrement(counter);
    if ((uint32_t)n % every)
        return;
    // Also report how many of the live nodes are already expired (flLife < 0).
    // A healthy ring retires those the same tick they expire, so this should
    // hover near zero; a live region full of expired nodes means retirement
    // has stopped and the ring can never recycle.
    uint32_t expired = 0, oldest_seen = 0;
    float minlife = 0.0f, maxlife = 0.0f;
    bool first = true;
    for (ParticleNode *nd = ring->pRingHead; nd && nd != ring->pRingCurrent; nd = nd->pNext) {
        if (nd->flLife < 0.0f)
            expired++;
        if (first || nd->flLife < minlife) minlife = nd->flLife;
        if (first || nd->flLife > maxlife) maxlife = nd->flLife;
        first = false;
        if (++oldest_seen > 4096) break;  // cycle guard
    }
    g_logger.write("stats: %s this=%p tick=%ld dt=%.9f live=%lu free=%lu ring=%lu expired=%lu "
              "life=[%f..%f] head=%p cur=%p tail=%p\n",
              what, self, n, dt, count_live(ring), count_free(ring), ring->dwRingCount,
              expired, minlife, maxlife,
              ring->pRingHead, ring->pRingCurrent, ring->pRingTail);
}

void GravityEnvironment::tick(float dt)
{
    SIM_LOG_ONCE(calls)
        g_logger.write("sim: GravityTick this=%p dt=%f live=%lu ring=%lu\n",
                  this, dt, count_live(pRing_),
                  pRing_->dwRingCount);
    static AtomicInt ticks = 0;
    stats_tick("gravity", this, pRing_, &ticks, dt);
    gravityTick(dt);
}

void MagnetEnvironment::tick(float dt)
{
    SIM_LOG_ONCE(calls)
        g_logger.write("sim: MagnetTick this=%p dt=%f live=%lu ring=%lu centre=%f,%f,%f\n",
                  this, dt, count_live(pRing_),
                  pRing_->dwRingCount,
                  flCentre_[0], flCentre_[1], flCentre_[2]);
    static AtomicInt ticks = 0;
    stats_tick("magnet", this, pRing_, &ticks, dt);
    magnetTick(dt);
}

void StdGenerator::tick(float dt)
{
    SIM_LOG_ONCE(calls)
        g_logger.write("sim: StdEmit this=%p dt=%f enabled=%lu accum=%f free=%lu ring=%lu\n",
                  this, dt, dwEnabled_, flAccumulator_,
                  count_free(pRing_), pRing_->dwRingCount);
    stdEmit(dt, NULL, NULL);
}

void XStdGenerator::tick(float dt)
{
    SIM_LOG_ONCE(calls)
        g_logger.write("sim: XStdEmit this=%p dt=%f enabled=%lu posoff=%f,%f,%f "
                  "veloff=%f,%f,%f ring=%lu\n",
                  this, dt, dwEnabled_,
                  flPosOffset_[0], flPosOffset_[1], flPosOffset_[2],
                  flVelOffset_[0], flVelOffset_[1], flVelOffset_[2],
                  pRing_->dwRingCount);
    stdEmit(dt, flPosOffset_, flVelOffset_);
}

void CylinderGenerator::tick(float dt)
{
    SIM_LOG_ONCE(calls)
        g_logger.write("sim: CylinderEmit this=%p dt=%f enabled=%lu accum=%f "
                  "origin=%f,%f,%f scale=%f ring=%lu\n",
                  this, dt, dwEnabled_, flAccumulator_,
                  flOrigin_[0], flOrigin_[1], flOrigin_[2],
                  flScale_, pRing_->dwRingCount);
    cylinderEmit(dt);
}

/* One read of `size` bytes; on a short read this leaves whatever was already
 * read in place and returns false. */
static bool read1(void *dst, unsigned size, std::istream &in)
{
    return readBytes(in, dst, size);
}

/* Stores flDirection/flMagnitude as given, and flGravity =
 * normalise(direction) * magnitude, or direction itself when it is exactly
 * zero. */
void GravityEnvironment::gravitySetVector(const float dir[3], float mag)
{
    flDirection_[0] = dir[0];
    flDirection_[1] = dir[1];
    flDirection_[2] = dir[2];
    flMagnitude_ = mag;
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

void GravityEnvironment::gravitySetColour(uint32_t argb, float fade)
{
    dwTargetARGB_   = argb;
    dwTargetA_      = argb >> 24;
    dwTargetRGB_[0] = (argb >> 16) & 0xff;
    dwTargetRGB_[2] = argb & 0xff;
    dwTargetRGB_[1] = (argb >> 8) & 0xff;
    flFadeRate_     = fade;
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
    if (sim_fx() == FX_LOADFLIP)
        mag = -mag;
    gravitySetVector(dir, mag);
    gravitySetColour(argb, fade);
    SIM_LOG_ONCE(calls)
        g_logger.write("sim: GravityLoad this=%p gravity=%f,%f,%f argb=%08lX\n", this,
                  flGravity_[0], flGravity_[1], flGravity_[2], argb);
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
    if (!read1(&flRange_, 4, in))         return false;
    if (!read1(&flFadeRate_, 4, in))      return false;
    if (!read1(&dwFadeThreshold_, 4, in)) return false;
    flFadeAccum_ = 0.0f;
    if (sim_fx() == FX_LOADFLIP)
        for (int i = 0; i < 3; i++)
            flForce_[i] = -flForce_[i];
    SIM_LOG_ONCE(calls)
        g_logger.write("sim: MagnetLoad this=%p force=%f,%f,%f centre=%f,%f,%f\n", this,
                  flForce_[0], flForce_[1], flForce_[2],
                  flCentre_[0], flCentre_[1], flCentre_[2]);
    return true;
}

/* One write of `size` bytes. */
static bool write1(const void *src, unsigned size, std::ostream &out)
{
    return writeBytes(out, src, size);
}

/* Field-for-field mirror of Load, in the same order. */
int GravityEnvironment::save(std::ostream &out)
{
    if (!write1(flDirection_, 12, out))      return false;
    if (!write1(&flMagnitude_, 4, out))      return false;
    if (!write1(&dwTargetARGB_, 4, out))     return false;
    if (!write1(&flFadeRate_, 4, out))       return false;
    if (!write1(&dwFadeThreshold_, 4, out))  return false;
    if (!write1(&dwClipEnable_[0], 4, out))  return false;
    if (!write1(&dwClipEnable_[1], 4, out))  return false;
    if (!write1(&dwClipEnable_[2], 4, out))  return false;
    if (!write1(flClipMax_, 12, out))        return false;
    return write1(flClipMin_, 12, out);
}

/* Calls the base Environment::Save (a no-op) first, then mirrors Load.
 * PRESERVED: dwTargetRGB is not written either, matching Load. */
int MagnetEnvironment::save(std::ostream &out)
{
    if (!write1(flForce_, 12, out))          return false;
    if (!write1(flCentre_, 12, out))         return false;
    if (!write1(&flRange_, 4, out))          return false;
    if (!write1(&flFadeRate_, 4, out))       return false;
    return write1(&dwFadeThreshold_, 4, out);
}

/* The base Environment::CopyFrom only gates on type name; it copies nothing
 * itself. */
int Environment::envSameName(const Environment *src) const
{
    return strcmp(src->pName_, pName_) == 0;
}

/* Copies every field past the base (pName and pRing keep the
 * destination's) in one memcpy; safe because src can never alias dst. */
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

/* Refuses (and leaves pRing alone) when handed a NULL ring. */
int Environment::attachRing(RingBuffer *ring)
{
    if (ring == NULL)
        return false;
    pRing_ = ring;
    return true;
}

/* Static type-name strings; pName points at them, never owned or freed here.
 */

Environment::Environment() : pName_(GS_PSNAME_ENVIRONMENT), pRing_(NULL)
{
}

/* Base ctor, then every member zeroed except the fade threshold,
 * which starts at 10. */
GravityEnvironment::GravityEnvironment()
{
    pName_ = GS_PSNAME_GRAVITY_ENV;
    dwFadeThreshold_ = 10;
}

MagnetEnvironment::MagnetEnvironment()
{
    pName_ = GS_PSNAME_MAGNET_ENV;
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

int Generator::attachRing(RingBuffer *ring)
{
    if (ring == NULL)
        return false;
    pRing_ = ring;
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

void StdGenerator::stdCloneTypeTable(const uint32_t *src, uint32_t count)
{
    type_table_clone(&typeTable_, &dwTypeTableCount_, pEmitProb_,
                     src, count);
}

/* FORMAT: count (uint32_t), then that many (colour, weight) uint32_t pairs. */
static int type_table_save(const std::vector<uint32_t> &table, const uint32_t *pcount, std::ostream &out)
{
    if (!write1(pcount, 4, out))
        return false;
    return writeBytes(out, table.data(), (size_t)*pcount * 8);
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

int StdGenerator::stdSaveTypeTable(std::ostream &out)
{
    return type_table_save(typeTable_, &dwTypeTableCount_, out);
}

int StdGenerator::stdLoadTypeTable(std::istream &in)
{
    return type_table_load(&typeTable_, &dwTypeTableCount_,
                           pEmitProb_, in);
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
    this->stdCloneTypeTable(src->typeTable_.data(), src->dwTypeTableCount_);
    return true;
}

int StdGenerator::save(std::ostream &out)
{
    if (!write1(&dwEmitMode_, 4, out))     return false;
    if (!write1(flBoxMax_, 12, out))       return false;
    if (!write1(flBoxMin_, 12, out))       return false;
    if (!write1(flSphMin_, 12, out))       return false;
    if (!write1(flSphMax_, 12, out))       return false;
    if (!write1(flVelMin_, 12, out))       return false;
    if (!write1(flVelMax_, 12, out))       return false;
    if (!write1(&flLifeMin_, 4, out))      return false;
    if (!write1(&flLifeMax_, 4, out))      return false;
    if (!write1(&flEmitRateMin_, 4, out))  return false;
    if (!write1(&flEmitRateMax_, 4, out))  return false;
    if (!write1(&flDtScale_, 4, out))      return false;
    return stdSaveTypeTable(out);
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
    if (sim_fx() == FX_FASTEMIT)
        flDtScale_ = (float)((double)flDtScale_ * 5.0);
    if (dwEmitMode_ == 0)
        stdBuildSphere(flSphMin_, flSphMax_);
    if (dwEmitMode_ == 1)
        stdBuildBox(flBoxMin_, flBoxMax_);
    stdBuildVelocity(flVelMin_, flVelMax_,
                       flLifeMin_, flLifeMax_);
    stdBuildRate(flEmitRateMin_, flEmitRateMax_);
    return stdLoadTypeTable(in);
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

int XStdGenerator::save(std::ostream &out)
{
    if (!StdGenerator::save(out))        return false;
    if (!write1(flPosOffset_, 12, out))    return false;
    return write1(flVelOffset_, 12, out);
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

/* ─── PointGenerator / BoxGenerator emit ─── */

/* DETERMINISM: dt*rate accumulated in double, stored back as a float, then
 * truncated toward zero for the emit count; only the truncated fraction
 * carries forward. */
static int dead_gen_claim(float *acc, float rate, float dt)
{
    double total = (double)dt * rate + *acc;
    *acc = (float)total;
    if (!(total >= 0.0))
        return 0;
    int n = (int)total;
    *acc = (float)(total - (double)n);
    return n;
}

/* No dwEnabled check, unlike StdGenerator. */
void PointGenerator::tick(float dt)
{
    RingBuffer *ring = pRing_;
    if (ring->pRingCurrent == NULL)
        return;
    int n = dead_gen_claim(&flAccumulator_, flEmitRate_, dt);
    if (n <= 0)
        return;
    const uint32_t *life = this->dwLifeTable_;
    for (int i = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        set_bits(&node->flLife, life[dwLifeIdx_]);
        std::copy_n(flEmitPos_, 3, &node->flX);
        node->dwDiffuse = dwDiffuse_;
        for (int a = 0; a < 3; a++)
            node->flVel[a] = flVelTable_[dwVelIdx_[a]] + flVelBias_[a];
        uint32_t i0 = dwVelIdx_[0] + 1, i1 = dwVelIdx_[1] + 2,
              i2 = dwVelIdx_[2] + 3;
        dwVelIdx_[0] = (i0 >= 1000) ? 1000 - i0 : i0;  // wraps at 1000, not 999 or 998, like the next two indices
        dwVelIdx_[1] = (i1 >= 999)  ? 1000 - i1 : i1;
        dwVelIdx_[2] = (i2 >= 998)  ? 1000 - i2 : i2;
        dwLifeIdx_ = (dwLifeIdx_ > 100) ? 0 : dwLifeIdx_ + 1;  // runs 0..101 — two entries past the 100-entry table
        ring->pRingCurrent = node->pNext;
        if (ring->pRingCurrent == NULL)
            return;
        if (++i >= n)
            return;
    }
}

/* Position and colour are raw table copies; velocity is table plus bias.  Both
 * index triples wrap by 500 - i past 499. */
void BoxGenerator::tick(float dt)
{
    RingBuffer *ring = pRing_;
    if (ring->pRingCurrent == NULL)
        return;
    int n = dead_gen_claim(&flAccumulator_, flEmitRate_, dt);
    if (n <= 0)
        return;
    for (int i = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        set_bits(&node->flLife, dwLifeTable_[dwLifeIdx_]);
        set_bits(&node->flX, dwPosX_[dwPosIdx_[0]]);
        set_bits(&node->flY, dwPosY_[dwPosIdx_[1]]);
        set_bits(&node->flZ, dwPosZ_[dwPosIdx_[2]]);
        node->dwDiffuse = dwDiffuse_[dwDiffuseIdx_];
        for (int a = 0; a < 3; a++)
            node->flVel[a] = flVelTable_[dwVelIdx_[a]] + flVelBias_[a];
        for (int a = 0; a < 3; a++) {
            uint32_t p = dwPosIdx_[a] + (uint32_t)(a + 1);
            dwPosIdx_[a] = (p > 499) ? 500 - p : p;
        }
        for (int a = 0; a < 3; a++) {
            uint32_t v = dwVelIdx_[a] + (uint32_t)(a + 1);
            dwVelIdx_[a] = (v > 499) ? 500 - v : v;
        }
        dwLifeIdx_ = (dwLifeIdx_ < 99) ? dwLifeIdx_ + 1 : 0;
        dwDiffuseIdx_ = (dwDiffuseIdx_ < 199) ? dwDiffuseIdx_ + 1 : 0;
        ring->pRingCurrent = node->pNext;
        if (ring->pRingCurrent == NULL)
            return;
        if (++i >= n)
            return;
    }
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
    type_table_clone(&this->typeTable_, &this->dwTypeTableCount_, this->pEmitProb_,
                     src->typeTable_.data(), src->dwTypeTableCount_);
    return true;
}

int CylinderGenerator::save(std::ostream &out)
{
    if (!write1(flOrigin_, 12, out))       return false;
    if (!write1(&flScale_, 4, out))        return false;
    if (!write1(flDirection_, 12, out))    return false;
    if (!write1(flVelMin_, 12, out))       return false;
    if (!write1(flVelMax_, 12, out))       return false;
    if (!write1(&flLifeMin_, 4, out))      return false;
    if (!write1(&flLifeMax_, 4, out))      return false;
    if (!write1(&flEmitRateMin_, 4, out))  return false;
    if (!write1(&flEmitRateMax_, 4, out))  return false;
    if (!write1(&flDtScale_, 4, out))      return false;
    return type_table_save(typeTable_, &dwTypeTableCount_, out);
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
    if (sim_fx() == FX_FASTEMIT)
        flDtScale_ = (float)((double)flDtScale_ * 5.0);
    setDirection(flDirection_[0], flDirection_[1],
                      flDirection_[2]);
    cylBuildVelocity(flVelMin_, flVelMax_,
                       flLifeMin_, flLifeMax_);
    cylBuildRate(flEmitRateMin_, flEmitRateMax_);
    return type_table_load(&typeTable_, &dwTypeTableCount_,
                           pEmitProb_, in);
}

Generator::Generator()
    : pName_(GS_PSNAME_GENERATOR), dwEnabled_(1), pRing_(NULL)
{
}

/* PRESERVED: only the accumulator and diffuse colour are initialised — the
 * position, velocity and life tables are left uninitialised, and nothing in
 * this file ever fills them. */
PointGenerator::PointGenerator()
{
    pName_ = GS_PSNAME_POINT_GEN;
    flAccumulator_ = 0.0f;
    dwDiffuse_ = 0xFFFFFFFF;
}

/* PRESERVED: nothing past the base class is initialised. */
BoxGenerator::BoxGenerator()
{
    pName_ = GS_PSNAME_BOX_GEN;
}

StdGenerator::StdGenerator()
{
    pName_ = GS_PSNAME_STD_GEN;
    for (int i = 0; i < 200; i++)
        pEmitProb_[i] = 0xFFFFFFFF;
}

StdGenerator::~StdGenerator()
{
}

CylinderGenerator::~CylinderGenerator()
{
}

XStdGenerator::XStdGenerator()
{
    pName_ = GS_PSNAME_XSTD_GEN;
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
    pName_ = GS_PSNAME_CYL_GEN;
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
    if (strcmp(name, "PointGenerator") == 0)    return gen_new<PointGenerator>();
    if (strcmp(name, "BoxGenerator") == 0)      return gen_new<BoxGenerator>();
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
