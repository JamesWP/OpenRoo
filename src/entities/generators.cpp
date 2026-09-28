/* Every Generator and Environment virtual — Tick/emit, Save, Load, CopyFrom,
 * the dtors and constructors — plus the vtables the constructors install, the
 * ring helpers the two environment ticks share, and the exported thiscall
 * thunks the vtables point at.
 *
 * The ring is one NULL-terminated doubly-linked list, partitioned as
 * [pRingHead, pRingCurrent) live and [pRingCurrent, pRingTail] free.  An
 * environment's tick owns ageing and retirement; a generator's emit owns
 * adding new particles at pRingCurrent.  Ring allocation itself lives
 * elsewhere — this file only honours the contract. */

#include "generators.h"
#include "assetio.h"
#include "clock.h"
#include "crtrand.h"
#include <new>
#include "factory.h"
#include "log.h"
#include "gamestr.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define SIM_LOG_FIRST 8

/* Our vtables, defined at the foot of this file — one per class, in the game's
 * slot order.  Every constructor installs one of these; no object of ours ever
 * carries any other vtable address. */
extern void *const gen_vtbl_base[];
extern void *const gen_vtbl_point[];
extern void *const gen_vtbl_box[];
extern void *const gen_vtbl_std[];
extern void *const gen_vtbl_xstd[];
extern void *const gen_vtbl_cylinder[];
extern void *const env_vtbl_base[];
extern void *const env_vtbl_gravity[];
extern void *const env_vtbl_magnet[];

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
        if (GetEnvironmentVariableA("KAROO_PARTICLE_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "gravity") == 0)       { cached = FX_GRAVITY;  name = "gravity";  }
            else if (lstrcmpiA(buf, "nolife") == 0)   { cached = FX_NOLIFE;   name = "nolife";   }
            else if (lstrcmpiA(buf, "antigrav") == 0) { cached = FX_ANTIGRAV; name = "antigrav"; }
            else if (lstrcmpiA(buf, "burst") == 0)    { cached = FX_BURST;    name = "burst";    }
            else if (lstrcmpiA(buf, "loadflip") == 0) { cached = FX_LOADFLIP; name = "loadflip"; }
            else if (lstrcmpiA(buf, "fastemit") == 0) { cached = FX_FASTEMIT; name = "fastemit"; }
        }
        if (cached != FX_NONE)
            log_write("sim: FX mode = %s\n", name);
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

/* One colour channel moved by at most step toward target.  DWORD arithmetic
 * throughout, so a step larger than the true distance is clamped before the
 * subtraction, avoiding unsigned wraparound. */
static DWORD fade_channel(DWORD cur, DWORD target, DWORD step)
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
static DWORD fade_diffuse(DWORD diffuse, const DWORD target[3], DWORD step)
{
    DWORD blue  = diffuse & 0xff;
    DWORD green = (diffuse >> 8) & 0xff;
    DWORD red   = (diffuse >> 16) & 0xff;
    DWORD alpha = diffuse >> 24;

    DWORD out_red = red;
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
static DWORD fade_step(float *accum, float rate, DWORD threshold, float dt)
{
    *accum += dt * rate;
    DWORD step = (DWORD)(long long)*accum;
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

    DWORD step = fade_step(&flFadeAccum_, flFadeRate_,
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

    DWORD step = fade_step(&flFadeAccum_, flFadeRate_,
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
static DWORD wrap_index(DWORD old, DWORD step, DWORD limit)
{
    DWORD next = old + step;
    return (next > limit - 1) ? old - (limit - step) : next;
}

/* Life/prob indices use a different idiom: bump while below the last entry,
 * otherwise snap to 0. */
static DWORD bump_index(DWORD cur, DWORD count)
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
 * included.  Every caller reaches it the same way, through slot 3 of the
 * class's vtable — whether the game dispatches directly or through
 * sim_tick_slot3 at the foot of this file. */

#define SIM_LOG_ONCE(counter) \
    static LONG counter = 0; \
    if (InterlockedIncrement(&counter) <= SIM_LOG_FIRST)

static DWORD count_live(const RingBuffer *ring)
{
    DWORD live = 0;
    for (ParticleNode *n = ring->pRingHead; n && n != ring->pRingCurrent; n = n->pNext)
        live++;
    return live;
}

static DWORD count_free(const RingBuffer *ring)
{
    DWORD free_nodes = 0;
    for (ParticleNode *n = ring->pRingCurrent; n; n = n->pNext)
        free_nodes++;
    return free_nodes;
}

/* KAROO_SIM_STATS=N logs each environment's ring occupancy — live and free
 * counts, plus the life range and how many live nodes have already expired —
 * every N ticks, keyed by object address so several systems in one scene stay
 * distinguishable.  Off unless the variable is set. */
static DWORD stats_interval(void)
{
    static LONG cached = -1;
    if (cached < 0) {
        char buf[16];
        LONG v = 0;
        if (GetEnvironmentVariableA("KAROO_SIM_STATS", buf, sizeof(buf)))
            v = (LONG)strtol(buf, NULL, 10);
        if (v < 0)
            v = 0;
        InterlockedExchange(&cached, v);
        if (v > 0)
            log_write("sim: stats every %ld ticks\n", v);
    }
    return (DWORD)cached;
}

static void stats_tick(const char *what, void *self, const RingBuffer *ring, LONG *counter, float dt)
{
    DWORD every = stats_interval();
    if (every == 0)
        return;
    LONG n = InterlockedIncrement(counter);
    if ((DWORD)n % every)
        return;
    // Also report how many of the live nodes are already expired (flLife < 0).
    // A healthy ring retires those the same tick they expire, so this should
    // hover near zero; a live region full of expired nodes means retirement
    // has stopped and the ring can never recycle.
    DWORD expired = 0, oldest_seen = 0;
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
    log_write("stats: %s this=%p tick=%ld dt=%.9f live=%lu free=%lu ring=%lu expired=%lu "
              "life=[%f..%f] head=%p cur=%p tail=%p\n",
              what, self, n, dt, count_live(ring), count_free(ring), ring->dwRingCount,
              expired, minlife, maxlife,
              ring->pRingHead, ring->pRingCurrent, ring->pRingTail);
}

void GravityEnvironment::gravityEnvTick(float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: GravityTick this=%p dt=%f live=%lu ring=%lu\n",
                  this, dt, count_live(pRing_),
                  pRing_->dwRingCount);
    static LONG ticks = 0;
    stats_tick("gravity", this, pRing_, &ticks, dt);
    gravityTick(dt);
}

void MagnetEnvironment::magnetEnvTick(float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: MagnetTick this=%p dt=%f live=%lu ring=%lu centre=%f,%f,%f\n",
                  this, dt, count_live(pRing_),
                  pRing_->dwRingCount,
                  flCentre_[0], flCentre_[1], flCentre_[2]);
    static LONG ticks = 0;
    stats_tick("magnet", this, pRing_, &ticks, dt);
    magnetTick(dt);
}

void StdGenerator::stdGenTick(float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: StdEmit this=%p dt=%f enabled=%lu accum=%f free=%lu ring=%lu\n",
                  this, dt, dwEnabled_, flAccumulator_,
                  count_free(pRing_), pRing_->dwRingCount);
    stdEmit(dt, NULL, NULL);
}

void XStdGenerator::xstdGenTick(float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: XStdEmit this=%p dt=%f enabled=%lu posoff=%f,%f,%f "
                  "veloff=%f,%f,%f ring=%lu\n",
                  this, dt, dwEnabled_,
                  flPosOffset_[0], flPosOffset_[1], flPosOffset_[2],
                  flVelOffset_[0], flVelOffset_[1], flVelOffset_[2],
                  pRing_->dwRingCount);
    stdEmit(dt, flPosOffset_, flVelOffset_);
}

void CylinderGenerator::cylGenTick(float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: CylinderEmit this=%p dt=%f enabled=%lu accum=%f "
                  "origin=%f,%f,%f scale=%f ring=%lu\n",
                  this, dt, dwEnabled_, flAccumulator_,
                  flOrigin_[0], flOrigin_[1], flOrigin_[2],
                  flScale_, pRing_->dwRingCount);
    cylinderEmit(dt);
}

/* One fread of `size` bytes; on a short read this leaves whatever was already
 * read in place and returns false. */
static bool read1(void *dst, unsigned size, void *fp)
{
    return hooks_fread(dst, size, 1, fp) == 1;
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

void GravityEnvironment::gravitySetColour(DWORD argb, float fade)
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
BOOL GravityEnvironment::gravityEnvLoad(void *fp)
{
    float dir[3], mag, fade;
    DWORD argb;
    if (!read1(dir, 12, fp))                   return FALSE;
    if (!read1(&mag, 4, fp))                   return FALSE;
    if (!read1(&argb, 4, fp))                  return FALSE;
    if (!read1(&fade, 4, fp))                  return FALSE;
    if (!read1(&dwFadeThreshold_, 4, fp)) return FALSE;
    if (!read1(&dwClipEnable_[0], 4, fp)) return FALSE;
    if (!read1(&dwClipEnable_[1], 4, fp)) return FALSE;
    if (!read1(&dwClipEnable_[2], 4, fp)) return FALSE;
    if (!read1(flClipMax_, 12, fp))       return FALSE;
    if (!read1(flClipMin_, 12, fp))       return FALSE;
    if (sim_fx() == FX_LOADFLIP)
        mag = -mag;
    gravitySetVector(dir, mag);
    gravitySetColour(argb, fade);
    SIM_LOG_ONCE(calls)
        log_write("sim: GravityLoad this=%p gravity=%f,%f,%f argb=%08lX\n", this,
                  flGravity_[0], flGravity_[1], flGravity_[2], argb);
    return TRUE;
}

/* FORMAT: on disk, force precedes centre.  PRESERVED: dwTargetRGB is never
 * loaded — it keeps whatever the constructor or a CopyFrom set, not what a
 * saved file may have recorded. */
BOOL MagnetEnvironment::magnetEnvLoad(void *fp)
{
    // The base Environment::Load contributes nothing; its result is not
    // checked.
    if (!read1(flForce_, 12, fp))         return FALSE;
    if (!read1(flCentre_, 12, fp))        return FALSE;
    if (!read1(&flRange_, 4, fp))         return FALSE;
    if (!read1(&flFadeRate_, 4, fp))      return FALSE;
    if (!read1(&dwFadeThreshold_, 4, fp)) return FALSE;
    flFadeAccum_ = 0.0f;
    if (sim_fx() == FX_LOADFLIP)
        for (int i = 0; i < 3; i++)
            flForce_[i] = -flForce_[i];
    SIM_LOG_ONCE(calls)
        log_write("sim: MagnetLoad this=%p force=%f,%f,%f centre=%f,%f,%f\n", this,
                  flForce_[0], flForce_[1], flForce_[2],
                  flCentre_[0], flCentre_[1], flCentre_[2]);
    return TRUE;
}

/* The game's static-CRT fwrite; other files reach the same stream the same
 * way, so this is not a private handle. */

static bool write1(const void *src, unsigned size, void *fp)
{
    return fwrite(src, size, 1, (FILE *)fp) == 1;
}

/* Field-for-field mirror of Load, in the same order. */
BOOL GravityEnvironment::gravityEnvSave(void *fp)
{
    if (!write1(flDirection_, 12, fp))      return FALSE;
    if (!write1(&flMagnitude_, 4, fp))      return FALSE;
    if (!write1(&dwTargetARGB_, 4, fp))     return FALSE;
    if (!write1(&flFadeRate_, 4, fp))       return FALSE;
    if (!write1(&dwFadeThreshold_, 4, fp))  return FALSE;
    if (!write1(&dwClipEnable_[0], 4, fp))  return FALSE;
    if (!write1(&dwClipEnable_[1], 4, fp))  return FALSE;
    if (!write1(&dwClipEnable_[2], 4, fp))  return FALSE;
    if (!write1(flClipMax_, 12, fp))        return FALSE;
    return write1(flClipMin_, 12, fp);
}

/* Calls the base Environment::Save (a no-op) first, then mirrors Load.
 * PRESERVED: dwTargetRGB is not written either, matching Load. */
BOOL MagnetEnvironment::magnetEnvSave(void *fp)
{
    if (!write1(flForce_, 12, fp))          return FALSE;
    if (!write1(flCentre_, 12, fp))         return FALSE;
    if (!write1(&flRange_, 4, fp))          return FALSE;
    if (!write1(&flFadeRate_, 4, fp))       return FALSE;
    return write1(&dwFadeThreshold_, 4, fp);
}

/* The base Environment::CopyFrom only gates on type name; it copies nothing
 * itself. */
BOOL Environment::envSameName(const Environment *src) const
{
    return strcmp(src->pName_, pName_) == 0;
}

/* Copies every field past the base (vtable, pName and pRing keep the
 * destination's) in one memcpy; safe because src can never alias dst. */
BOOL GravityEnvironment::gravityEnvCopyFrom(const GravityEnvironment *src)
{
    if (!envSameName(src))
        return FALSE;
    memcpy((BYTE *)this + sizeof(Environment), (const BYTE *)src + sizeof(Environment),
           sizeof(GravityEnvironment) - sizeof(Environment));
    return TRUE;
}

BOOL MagnetEnvironment::magnetEnvCopyFrom(const MagnetEnvironment *src)
{
    if (!envSameName(src))
        return FALSE;
    memcpy((BYTE *)this + sizeof(Environment), (const BYTE *)src + sizeof(Environment),
           sizeof(MagnetEnvironment) - sizeof(Environment));
    return TRUE;
}

void Environment::baseEnvDestruct()
{
    pVtable_ = (void **)env_vtbl_base;
}

/* Restores the game-facing vtable, then runs the base body.  Nothing is freed
 * here: pName is not owned by either class. */
void GravityEnvironment::gravityEnvDestruct()
{
    pVtable_ = (void **)env_vtbl_gravity;
    pVtable_ = (void **)env_vtbl_base;
}

/* PRESERVED: runs the base body twice (once on each of the original's exit
 * paths) — harmless, kept rather than collapsed to one store. */
void MagnetEnvironment::magnetEnvDestruct()
{
    pVtable_ = (void **)env_vtbl_magnet;
    pVtable_ = (void **)env_vtbl_base;
    pVtable_ = (void **)env_vtbl_base;
}

/* The scalar deleting dtors' shared tail is factory.h's scalar_delete<T>. */

/* Refuses (and leaves pRing alone) when handed a NULL ring. */
BOOL Environment::envAttachRing(RingBuffer *ring)
{
    if (ring == NULL)
        return FALSE;
    pRing_ = ring;
    return TRUE;
}

/* Each constructor also built and destroyed a throwaway base-class instance on
 * its own stack; it has no effect outside that frame and isn't reproduced
 * here. */

/* Static type-name strings; pName points at them, never owned or freed here.
 */

void Environment::baseEnvConstruct()
{
    pVtable_ = (void **)env_vtbl_base;
    pName_   = GS_PSNAME_ENVIRONMENT;
    pRing_   = NULL;
}

/* Base ctor, class vtable, then every member zeroed except the fade threshold,
 * which starts at 10. */
void GravityEnvironment::gravityEnvConstruct()
{
    baseEnvConstruct();
    pVtable_ = (void **)env_vtbl_gravity;
    memset((BYTE *)this + sizeof(Environment), 0,
           sizeof(GravityEnvironment) - sizeof(Environment));
    pName_ = GS_PSNAME_GRAVITY_ENV;
    dwFadeThreshold_ = 10;
}

void MagnetEnvironment::magnetEnvConstruct()
{
    baseEnvConstruct();
    pVtable_ = (void **)env_vtbl_magnet;
    memset((BYTE *)this + sizeof(Environment), 0,
           sizeof(MagnetEnvironment) - sizeof(Environment));
    pName_ = GS_PSNAME_MAGNET_ENV;
    dwFadeThreshold_ = 10;
}

/* Matches names exactly, as the game's factory did: an unknown name, or a
 * failed allocation, both return NULL. */
Environment *Environment::create(const char *name)
{
    if (strcmp(name, "Environment") == 0) {
        Environment *e = (Environment *)::operator new(sizeof(Environment), std::nothrow);
        if (e) e->baseEnvConstruct();
        return e;
    }
    if (strcmp(name, "GravityEnvironment") == 0) {
        GravityEnvironment *g =
            (GravityEnvironment *)::operator new(sizeof(GravityEnvironment), std::nothrow);
        if (g) g->gravityEnvConstruct();
        return (Environment *)g;
    }
    if (strcmp(name, "MagnetEnvironment") == 0) {
        MagnetEnvironment *m =
            (MagnetEnvironment *)::operator new(sizeof(MagnetEnvironment), std::nothrow);
        if (m) m->magnetEnvConstruct();
        return (Environment *)m;
    }
    return NULL;
}

#define THISCALL_DECL __attribute__((thiscall))

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
static volatile BYTE g_uniformSeedPending = 1;

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

void Generator::baseGenDestruct()
{
    pVtable_ = (void **)gen_vtbl_base;
}

/* The scalar deleting dtors' shared tail is factory.h's scalar_delete<T>. */

/* Gates on matching type name, then copies only dwEnabled. */
BOOL Generator::genCopyBase(const Generator *src)
{
    if (strcmp(src->pName_, pName_) != 0)
        return FALSE;
    dwEnabled_ = src->dwEnabled_;
    return TRUE;
}

/* Slot 2 of all six generator vtables. */
BOOL Generator::genAttachRing(RingBuffer *ring)
{
    if (ring == NULL)
        return FALSE;
    pRing_ = ring;
    return TRUE;
}

/* DETERMINISM: reseeds from the clock, then draws (colour, weight) pairs by
 * rand() until all 200 slots are filled, skipping zero-weight pairs — the draw
 * order feeds the emitted particle colours.  An empty or missing source table
 * instead fills pEmitProb with all -1. */
static void type_table_clone(void **ptable, DWORD *pcount, DWORD *emit_prob,
                             const DWORD *src, DWORD count)
{
    if (*ptable)
        ::operator delete(*ptable);
    *pcount = count;
    DWORD *table = (DWORD *)::operator new(count * 8);
    *ptable = table;
    if (count)
        memcpy(table, src, count * 8);

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
        DWORD j = 0;
        do {
            emit_prob[out++] = src[idx * 2];
            if (out >= 200)
                return;
            j++;
        } while (j < src[idx * 2 + 1]);
    }
}

void StdGenerator::stdCloneTypeTable(const DWORD *src, DWORD count)
{
    type_table_clone(&pTypeTable_, &dwTypeTableCount_, pEmitProb_,
                     src, count);
}

/* FORMAT: count (DWORD), then that many (colour, weight) DWORD pairs. */
static BOOL type_table_save(void *table, const DWORD *pcount, void *fp)
{
    if (fp == NULL)
        return FALSE;
    if (!write1(pcount, 4, fp))
        return FALSE;
    return fwrite(table, 8, *pcount, (FILE *)fp) == *pcount;
}

/* PRESERVED: a short read returns FALSE without freeing the scratch buffer — a
 * genuine leak, reproduced. */
static BOOL type_table_load(void **ptable, DWORD *pcount, DWORD *emit_prob, void *fp)
{
    if (fp == NULL)
        return FALSE;
    DWORD count;
    if (hooks_fread(&count, 4, 1, fp) != 1)
        return FALSE;
    DWORD *pairs = (DWORD *)::operator new(count * 8);
    if (hooks_fread(pairs, 8, count, fp) != count)
        return FALSE;
    type_table_clone(ptable, pcount, emit_prob, pairs, count);
    ::operator delete(pairs);
    return TRUE;
}

BOOL StdGenerator::stdSaveTypeTable(void *fp)
{
    return type_table_save(pTypeTable_, &dwTypeTableCount_, fp);
}

BOOL StdGenerator::stdLoadTypeTable(void *fp)
{
    return type_table_load(&pTypeTable_, &dwTypeTableCount_,
                           pEmitProb_, fp);
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
    memcpy(flSphMin_, a, sizeof a);
    memcpy(flSphMax_, b, sizeof b);
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
    memcpy(flBoxMax_, b, sizeof b);
    memcpy(flBoxMin_, a, sizeof a);
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
    memcpy(flVelMin_, a, sizeof a);
    memcpy(flVelMax_, b, sizeof b);
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

void StdGenerator::stdGenDestruct()
{
    pVtable_ = (void **)gen_vtbl_std;
    if (pTypeTable_)
        ::operator delete(pTypeTable_);
    baseGenDestruct();
}

/* Copies every field but the type table, which is cloned instead — cloning
 * redraws pEmitProb from a fresh seed, so a copy's colours differ from its
 * source's. */
BOOL StdGenerator::stdGenCopyFrom(const StdGenerator *src)
{
    if (!genCopyBase(src))
        return FALSE;
    memcpy((BYTE *)this + 0x10, (const BYTE *)src + 0x10, 0x70 - 0x10);
    memcpy((BYTE *)this + 0x78, (const BYTE *)src + 0x78, 0x3420 - 0x78);
    stdCloneTypeTable((const DWORD *)src->pTypeTable_, src->dwTypeTableCount_);
    return TRUE;
}

BOOL StdGenerator::stdGenSave(void *fp)
{
    if (!write1(&dwEmitMode_, 4, fp))     return FALSE;
    if (!write1(flBoxMax_, 12, fp))       return FALSE;
    if (!write1(flBoxMin_, 12, fp))       return FALSE;
    if (!write1(flSphMin_, 12, fp))       return FALSE;
    if (!write1(flSphMax_, 12, fp))       return FALSE;
    if (!write1(flVelMin_, 12, fp))       return FALSE;
    if (!write1(flVelMax_, 12, fp))       return FALSE;
    if (!write1(&flLifeMin_, 4, fp))      return FALSE;
    if (!write1(&flLifeMax_, 4, fp))      return FALSE;
    if (!write1(&flEmitRateMin_, 4, fp))  return FALSE;
    if (!write1(&flEmitRateMax_, 4, fp))  return FALSE;
    if (!write1(&flDtScale_, 4, fp))      return FALSE;
    return stdSaveTypeTable(fp);
}

/* Builds the sphere or box position table (whichever dwEmitMode selects), then
 * always rebuilds the velocity and rate tables. */
BOOL StdGenerator::stdGenLoad(void *fp)
{
    if (!read1(&dwEmitMode_, 4, fp))      return FALSE;
    if (!read1(flBoxMax_, 12, fp))        return FALSE;
    if (!read1(flBoxMin_, 12, fp))        return FALSE;
    if (!read1(flSphMin_, 12, fp))        return FALSE;
    if (!read1(flSphMax_, 12, fp))        return FALSE;
    if (!read1(flVelMin_, 12, fp))        return FALSE;
    if (!read1(flVelMax_, 12, fp))        return FALSE;
    if (!read1(&flLifeMin_, 4, fp))       return FALSE;
    if (!read1(&flLifeMax_, 4, fp))       return FALSE;
    if (!read1(&flEmitRateMin_, 4, fp))   return FALSE;
    if (!read1(&flEmitRateMax_, 4, fp))   return FALSE;
    if (!read1(&flDtScale_, 4, fp))       return FALSE;
    if (sim_fx() == FX_FASTEMIT)
        flDtScale_ = (float)((double)flDtScale_ * 5.0);
    if (dwEmitMode_ == 0)
        stdBuildSphere(flSphMin_, flSphMax_);
    if (dwEmitMode_ == 1)
        stdBuildBox(flBoxMin_, flBoxMax_);
    stdBuildVelocity(flVelMin_, flVelMax_,
                       flLifeMin_, flLifeMax_);
    stdBuildRate(flEmitRateMin_, flEmitRateMax_);
    return stdLoadTypeTable(fp);
}

/* Own vtable first, then Std's dtor body. */
void XStdGenerator::xstdGenDestruct()
{
    pVtable_ = (void **)gen_vtbl_xstd;
    stdGenDestruct();
}

/* PRESERVED: copies flPosOffset but not flVelOffset — a copied generator keeps
 * its own velocity offset regardless of the source's. */
BOOL XStdGenerator::xstdGenCopyFrom(const XStdGenerator *src)
{
    if (!stdGenCopyFrom(src))
        return FALSE;
    memcpy(flPosOffset_, src->flPosOffset_, sizeof flPosOffset_);
    return TRUE;
}

BOOL XStdGenerator::xstdGenSave(void *fp)
{
    if (!stdGenSave(fp))        return FALSE;
    if (!write1(flPosOffset_, 12, fp))    return FALSE;
    return write1(flVelOffset_, 12, fp);
}

BOOL XStdGenerator::xstdGenLoad(void *fp)
{
    if (!stdGenLoad(fp))        return FALSE;
    if (!read1(flPosOffset_, 12, fp))     return FALSE;
    return read1(flVelOffset_, 12, fp);
}

void XStdGenerator::xstdSetPosition(float x, float y, float z)
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
void XStdGenerator::xstdSetDirection(float x, float y, float z)
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
void XStdGenerator::xstdSetVelocity(float x, float y, float z, float mag)
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
void XStdGenerator::xstdSetSpeed(float mag)
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
void PointGenerator::pointGenEmit(float dt)
{
    RingBuffer *ring = pRing_;
    if (ring->pRingCurrent == NULL)
        return;
    int n = dead_gen_claim(&flAccumulator_, flEmitRate_, dt);
    if (n <= 0)
        return;
    const DWORD *life = (const DWORD *)((const BYTE *)this + 0x0fe4);
    for (int i = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        *(DWORD *)&node->flLife = life[dwLifeIdx_];
        memcpy(&node->flX, flEmitPos_, 12);
        node->dwDiffuse = dwDiffuse_;
        for (int a = 0; a < 3; a++)
            node->flVel[a] = flVelTable_[dwVelIdx_[a]] + flVelBias_[a];
        DWORD i0 = dwVelIdx_[0] + 1, i1 = dwVelIdx_[1] + 2,
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
void BoxGenerator::boxGenEmit(float dt)
{
    RingBuffer *ring = pRing_;
    if (ring->pRingCurrent == NULL)
        return;
    int n = dead_gen_claim(&flAccumulator_, flEmitRate_, dt);
    if (n <= 0)
        return;
    for (int i = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        *(DWORD *)&node->flLife = dwLifeTable_[dwLifeIdx_];
        *(DWORD *)&node->flX = dwPosX_[dwPosIdx_[0]];
        *(DWORD *)&node->flY = dwPosY_[dwPosIdx_[1]];
        *(DWORD *)&node->flZ = dwPosZ_[dwPosIdx_[2]];
        node->dwDiffuse = dwDiffuse_[dwDiffuseIdx_];
        for (int a = 0; a < 3; a++)
            node->flVel[a] = flVelTable_[dwVelIdx_[a]] + flVelBias_[a];
        for (int a = 0; a < 3; a++) {
            DWORD p = dwPosIdx_[a] + (DWORD)(a + 1);
            dwPosIdx_[a] = (p > 499) ? 500 - p : p;
        }
        for (int a = 0; a < 3; a++) {
            DWORD v = dwVelIdx_[a] + (DWORD)(a + 1);
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

void CylinderGenerator::cylGenDestruct()
{
    pVtable_ = (void **)gen_vtbl_cylinder;
    if (pTypeTable_)
        ::operator delete(pTypeTable_);
    baseGenDestruct();
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
void CylinderGenerator::cylSetDirection(float x, float y, float z)
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
    memcpy(flMatrix_, m, sizeof m);
}

void CylinderGenerator::cylSetPosition(float x, float y, float z)
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
    memcpy(flVelMin_, a, sizeof a);
    memcpy(flVelMax_, b, sizeof b);
    pTypeTable_ = NULL;
    dwTypeTableCount_ = 0;
    flLifeMin_ = lmin;
    flLifeMax_ = lmax;
}

/* Std's rate-table builder (std_build_rate), run on Cylinder's own fields. */
void CylinderGenerator::cylBuildRate(float lo, float hi)
{
    float step = (float)((double)hi * GAUSS_STEP);
    flEmitRateMin_ = lo;
    flEmitRateMax_ = hi;
    gauss_fill(pLifeTable_, 100, lo, hi, step);
}

BOOL CylinderGenerator::cylGenCopyFrom(const CylinderGenerator *src)
{
    if (!genCopyBase(src))
        return FALSE;
    memcpy((BYTE *)this + 0x10, (const BYTE *)src + 0x10, 0x98 - 0x10);
    memcpy((BYTE *)this + 0xa0, (const BYTE *)src + 0xa0, 0x3444 - 0xa0);
    type_table_clone(&pTypeTable_, &dwTypeTableCount_, pEmitProb_,
                     (const DWORD *)src->pTypeTable_, src->dwTypeTableCount_);
    return TRUE;
}

BOOL CylinderGenerator::cylGenSave(void *fp)
{
    if (!write1(flOrigin_, 12, fp))       return FALSE;
    if (!write1(&flScale_, 4, fp))        return FALSE;
    if (!write1(flDirection_, 12, fp))    return FALSE;
    if (!write1(flVelMin_, 12, fp))       return FALSE;
    if (!write1(flVelMax_, 12, fp))       return FALSE;
    if (!write1(&flLifeMin_, 4, fp))      return FALSE;
    if (!write1(&flLifeMax_, 4, fp))      return FALSE;
    if (!write1(&flEmitRateMin_, 4, fp))  return FALSE;
    if (!write1(&flEmitRateMax_, 4, fp))  return FALSE;
    if (!write1(&flDtScale_, 4, fp))      return FALSE;
    return type_table_save(pTypeTable_, &dwTypeTableCount_, fp);
}

/* Reads its own direction back out to rebuild the matrix via
 * cyl_set_direction.  The position table (the unit circle) is the
 * constructor's and is never rebuilt here. */
BOOL CylinderGenerator::cylGenLoad(void *fp)
{
    if (!read1(flOrigin_, 12, fp))        return FALSE;
    if (!read1(&flScale_, 4, fp))         return FALSE;
    if (!read1(flDirection_, 12, fp))     return FALSE;
    if (!read1(flVelMin_, 12, fp))        return FALSE;
    if (!read1(flVelMax_, 12, fp))        return FALSE;
    if (!read1(&flLifeMin_, 4, fp))       return FALSE;
    if (!read1(&flLifeMax_, 4, fp))       return FALSE;
    if (!read1(&flEmitRateMin_, 4, fp))   return FALSE;
    if (!read1(&flEmitRateMax_, 4, fp))   return FALSE;
    if (!read1(&flDtScale_, 4, fp))       return FALSE;
    if (sim_fx() == FX_FASTEMIT)
        flDtScale_ = (float)((double)flDtScale_ * 5.0);
    cylSetDirection(flDirection_[0], flDirection_[1],
                      flDirection_[2]);
    cylBuildVelocity(flVelMin_, flVelMax_,
                       flLifeMin_, flLifeMax_);
    cylBuildRate(flEmitRateMin_, flEmitRateMax_);
    return type_table_load(&pTypeTable_, &dwTypeTableCount_,
                           pEmitProb_, fp);
}

void Generator::baseGenConstruct()
{
    pVtable_ = (void **)gen_vtbl_base;
    pName_ = GS_PSNAME_GENERATOR;
    pRing_ = NULL;
    dwEnabled_ = 1;
}

/* PRESERVED: only the accumulator and diffuse colour are initialised — the
 * position, velocity and life tables are left uninitialised, and nothing in
 * this file ever fills them. */
void PointGenerator::pointGenConstruct()
{
    baseGenConstruct();
    pVtable_ = (void **)gen_vtbl_point;
    pName_ = GS_PSNAME_POINT_GEN;
    flAccumulator_ = 0.0f;
    dwDiffuse_ = 0xFFFFFFFF;
}

/* PRESERVED: nothing past the base class is initialised. */
void BoxGenerator::boxGenConstruct()
{
    baseGenConstruct();
    pVtable_ = (void **)gen_vtbl_box;
    pName_ = GS_PSNAME_BOX_GEN;
}

void StdGenerator::stdGenConstruct()
{
    baseGenConstruct();
    pVtable_ = (void **)gen_vtbl_std;
    memset((BYTE *)this + sizeof(Generator), 0, sizeof(StdGenerator) - sizeof(Generator));
    pName_ = GS_PSNAME_STD_GEN;
    for (int i = 0; i < 200; i++)
        pEmitProb_[i] = 0xFFFFFFFF;
}

void XStdGenerator::xstdGenConstruct()
{
    stdGenConstruct();
    pVtable_ = (void **)gen_vtbl_xstd;
    memset(flPosOffset_, 0, sizeof flPosOffset_);
    memset(flVelOffset_, 0, sizeof flVelOffset_);
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
void CylinderGenerator::cylGenConstruct()
{
    baseGenConstruct();
    pVtable_ = (void **)gen_vtbl_cylinder;
    memset((BYTE *)this + sizeof(Generator), 0,
           sizeof(CylinderGenerator) - sizeof(Generator));
    pName_ = GS_PSNAME_CYL_GEN;
    cylSetDirection(0.0f, 1.0f, 0.0f);
    static const float IDENTITY[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    memcpy(flMatrix_, IDENTITY, sizeof IDENTITY);
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
static Generator *gen_new(void (T::*construct)())
{
    T *obj = (T *)::operator new(sizeof(T), std::nothrow);
    if (obj)
        (obj->*construct)();
    return (Generator *)obj;
}

Generator *Generator::create(const char *name)
{
    if (strcmp(name, "Generator") == 0)         return gen_new(&Generator::baseGenConstruct);
    if (strcmp(name, "PointGenerator") == 0)    return gen_new(&PointGenerator::pointGenConstruct);
    if (strcmp(name, "BoxGenerator") == 0)      return gen_new(&BoxGenerator::boxGenConstruct);
    if (strcmp(name, "StdGenerator") == 0)      return gen_new(&StdGenerator::stdGenConstruct);
    if (strcmp(name, "XStdGenerator") == 0)     return gen_new(&XStdGenerator::xstdGenConstruct);
    if (strcmp(name, "CylinderGenerator") == 0) return gen_new(&CylinderGenerator::cylGenConstruct);
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

typedef BOOL  (THISCALL_DECL *clone_copy_fn)(void *, const void *);
typedef void *(THISCALL_DECL *clone_dtor_fn)(void *, unsigned);

/* Shared body of both: the factory by class name, then slot 1 (CopyFrom); on
 * refusal the new object is destroyed through its own slot 0 and NULL comes
 * back.  A NULL from the factory is returned as-is. */
static void *clone_by_name(void *made, const void *src)
{
    if (made == NULL)
        return NULL;
    void **vtbl = *(void ***)made;
    if (!((clone_copy_fn)vtbl[GEN_VT_COPY_SLOT])(made, src)) {
        ((clone_dtor_fn)vtbl[GEN_VT_DTOR_SLOT])(made, 1);
        return NULL;
    }
    return made;
}

Generator * Generator::clone() const
{
    return (Generator *)clone_by_name(Generator::create(pName_), this);
}

Environment * Environment::clone() const
{
    return (Environment *)clone_by_name(Environment::create(pName_), this);
}

/* ─── Exports — vtable thunks ─── */

#define THISCALL __attribute__((thiscall))

/* All six slots, so a plain Environment never falls through to unimplemented
 * behaviour either. */
void *THISCALL
Environment::baseDtorSlot(Environment *self, unsigned flags)
{
    self->baseEnvDestruct();
    return scalar_delete(self, flags);
}

BOOL THISCALL
Environment::baseCopyFromSlot(Environment *self, const Environment *src) { return self->envSameName(src); }

BOOL THISCALL
Environment::attachRingSlot(Environment *self, RingBuffer *ring)         { return self->envAttachRing(ring); }

void THISCALL
Environment::baseTickSlot(Environment *, float)                          { }

BOOL THISCALL
Environment::baseSaveSlot(Environment *, void *)                         { return TRUE; }

BOOL THISCALL
Environment::baseLoadSlot(Environment *, void *)                         { return TRUE; }

void *THISCALL
GravityEnvironment::gravityDtorSlot(GravityEnvironment *self, unsigned flags)
{
    self->gravityEnvDestruct();
    return scalar_delete(self, flags);
}

void *THISCALL
MagnetEnvironment::magnetDtorSlot(MagnetEnvironment *self, unsigned flags)
{
    self->magnetEnvDestruct();
    return scalar_delete(self, flags);
}

BOOL THISCALL
GravityEnvironment::gravityCopyFromSlot(GravityEnvironment *self, const GravityEnvironment *src)
{ return self->gravityEnvCopyFrom(src); }

BOOL THISCALL
MagnetEnvironment::magnetCopyFromSlot(MagnetEnvironment *self, const MagnetEnvironment *src)
{ return self->magnetEnvCopyFrom(src); }

BOOL THISCALL
GravityEnvironment::gravitySaveSlot(GravityEnvironment *self, void *fp)  { return self->gravityEnvSave(fp); }

BOOL THISCALL
MagnetEnvironment::magnetSaveSlot(MagnetEnvironment *self, void *fp)    { return self->magnetEnvSave(fp); }

/* Shared no-op bodies for the base classes' do-nothing slots; the argument
 * counts match so callee cleanup stays correct under thiscall. */
void THISCALL Generator::nop1(void *, float)                      { }
void THISCALL Generator::nop3(void *, float, float, float)        { }
void THISCALL Generator::nop4(void *, float, float, float, float) { }
BOOL THISCALL Generator::returnTrue(void *, void *)               { return TRUE; }

BOOL THISCALL
Generator::attachRingSlot(Generator *self, RingBuffer *ring)        { return self->genAttachRing(ring); }

/* Called directly by explodedebris.cpp and theme.cpp — see
 * fill_gaussian_field. */
__declspec(dllexport) void THISCALL
Gen_FillGaussianField(ExplodeDebris *self, float mu, float sigma)
{
    fill_gaussian_field(self, mu, sigma);
}

BOOL THISCALL
Generator::baseCopyFromSlot(Generator *self, const Generator *src)  { return self->genCopyBase(src); }

void *THISCALL
Generator::baseDtorSlot(Generator *self, unsigned flags)
{
    self->baseGenDestruct();
    return scalar_delete(self, flags);
}

void *THISCALL
PointGenerator::pointDtorSlot(PointGenerator *self, unsigned flags)
{
    self->pVtable_ = (void **)gen_vtbl_point;
    self->baseGenDestruct();
    return scalar_delete(self, flags);
}

void *THISCALL
BoxGenerator::boxDtorSlot(BoxGenerator *self, unsigned flags)
{
    self->pVtable_ = (void **)gen_vtbl_box;
    self->baseGenDestruct();
    return scalar_delete(self, flags);
}

void *THISCALL
StdGenerator::stdDtorSlot(StdGenerator *self, unsigned flags)
{
    self->stdGenDestruct();
    return scalar_delete(self, flags);
}

void *THISCALL
XStdGenerator::xStdDtorSlot(XStdGenerator *self, unsigned flags)
{
    self->xstdGenDestruct();
    return scalar_delete(self, flags);
}

void THISCALL
PointGenerator::pointEmitSlot(PointGenerator *self, float dt)             { self->pointGenEmit(dt); }

void THISCALL
BoxGenerator::boxEmitSlot(BoxGenerator *self, float dt)                 { self->boxGenEmit(dt); }

BOOL THISCALL
StdGenerator::stdCopyFromSlot(StdGenerator *self, const StdGenerator *src)   { return self->stdGenCopyFrom(src); }

BOOL THISCALL
StdGenerator::stdSaveSlot(StdGenerator *self, void *fp)                 { return self->stdGenSave(fp); }

BOOL THISCALL
StdGenerator::stdLoadSlot(StdGenerator *self, void *fp)                 { return self->stdGenLoad(fp); }

BOOL THISCALL
XStdGenerator::xStdCopyFromSlot(XStdGenerator *self, const XStdGenerator *src) { return self->xstdGenCopyFrom(src); }

BOOL THISCALL
XStdGenerator::xStdSaveSlot(XStdGenerator *self, void *fp)               { return self->xstdGenSave(fp); }

BOOL THISCALL
XStdGenerator::xStdLoadSlot(XStdGenerator *self, void *fp)               { return self->xstdGenLoad(fp); }

void THISCALL
XStdGenerator::xStdSetPositionSlot(XStdGenerator *self, float x, float y, float z) { self->xstdSetPosition(x, y, z); }

void THISCALL
XStdGenerator::xStdSetVelocitySlot(XStdGenerator *self, float x, float y, float z, float m) { self->xstdSetVelocity(x, y, z, m); }

void THISCALL
XStdGenerator::xStdSetDirectionSlot(XStdGenerator *self, float x, float y, float z) { self->xstdSetDirection(x, y, z); }

void THISCALL
XStdGenerator::xStdSetSpeedSlot(XStdGenerator *self, float m)            { self->xstdSetSpeed(m); }

void *THISCALL
CylinderGenerator::cylDtorSlot(CylinderGenerator *self, unsigned flags)
{
    self->cylGenDestruct();
    return scalar_delete(self, flags);
}

BOOL THISCALL
CylinderGenerator::cylCopyFromSlot(CylinderGenerator *self, const CylinderGenerator *src) { return self->cylGenCopyFrom(src); }

BOOL THISCALL
CylinderGenerator::cylSaveSlot(CylinderGenerator *self, void *fp)            { return self->cylGenSave(fp); }

BOOL THISCALL
CylinderGenerator::cylLoadSlot(CylinderGenerator *self, void *fp)            { return self->cylGenLoad(fp); }

void THISCALL
CylinderGenerator::cylSetPositionSlot(CylinderGenerator *self, float x, float y, float z) { self->cylSetPosition(x, y, z); }

void THISCALL
CylinderGenerator::cylSetDirectionSlot(CylinderGenerator *self, float x, float y, float z) { self->cylSetDirection(x, y, z); }

BOOL THISCALL
GravityEnvironment::gravityLoadSlot(GravityEnvironment *self, void *fp)  { return self->gravityEnvLoad(fp); }

BOOL THISCALL
MagnetEnvironment::magnetLoadSlot(MagnetEnvironment *self, void *fp)    { return self->magnetEnvLoad(fp); }

void THISCALL
GravityEnvironment::gravityTickSlot(GravityEnvironment *self, float dt)  { self->gravityEnvTick(dt); }

void THISCALL
MagnetEnvironment::magnetTickSlot(MagnetEnvironment *self, float dt)    { self->magnetEnvTick(dt); }

void THISCALL
StdGenerator::stdEmitSlot(StdGenerator *self, float dt)            { self->stdGenTick(dt); }

void THISCALL
CylinderGenerator::cylinderEmitSlot(CylinderGenerator *self, float dt)  { self->cylGenTick(dt); }

void THISCALL
XStdGenerator::xStdEmitSlot(XStdGenerator *self, float dt)          { self->xstdGenTick(dt); }

/* ─── Our vtables ──────────────────────────────────────────────────────────
 *
 * One table per class, in the game's slot order, installed by each constructor
 * above: every object of ours carries a DLL address at +0x00, so every virtual
 * call the game makes on it lands directly in this code.
 *
 * Built by hand rather than through C++ virtuals: the game calls `(*(code
 * **)(*obj + 0x0c))(...)` under MSVC __thiscall, and neither mingw's slot
 * layout nor its calling convention is guaranteed to match, so each entry
 * below is one of the explicit thiscall exports above.
 *
 * The static_asserts guard against an initialiser one entry short, which would
 * otherwise leave a slot silently NULL.  Slot meanings are in generators.h. */

extern void *const gen_vtbl_base[] = {
    (void *)&Generator::baseDtorSlot,  (void *)&Generator::baseCopyFromSlot, (void *)&Generator::attachRingSlot,
    (void *)&Generator::nop1,      (void *)&Generator::returnTrue,   (void *)&Generator::returnTrue,
    (void *)&Generator::nop3,      (void *)&Generator::nop4,         (void *)&Generator::nop3,
    (void *)&Generator::nop1,
};

extern void *const gen_vtbl_point[] = {
    (void *)&PointGenerator::pointDtorSlot, (void *)&Generator::baseCopyFromSlot, (void *)&Generator::attachRingSlot,
    (void *)&PointGenerator::pointEmitSlot, (void *)&Generator::returnTrue,   (void *)&Generator::returnTrue,
    (void *)&Generator::nop3,      (void *)&Generator::nop4,         (void *)&Generator::nop3,
    (void *)&Generator::nop1,
};

extern void *const gen_vtbl_box[] = {
    (void *)&BoxGenerator::boxDtorSlot,   (void *)&Generator::baseCopyFromSlot, (void *)&Generator::attachRingSlot,
    (void *)&BoxGenerator::boxEmitSlot,   (void *)&Generator::returnTrue,   (void *)&Generator::returnTrue,
    (void *)&Generator::nop3,      (void *)&Generator::nop4,         (void *)&Generator::nop3,
    (void *)&Generator::nop1,
};

extern void *const gen_vtbl_std[] = {
    (void *)&StdGenerator::stdDtorSlot,   (void *)&StdGenerator::stdCopyFromSlot,  (void *)&Generator::attachRingSlot,
    (void *)&StdGenerator::stdEmitSlot,   (void *)&StdGenerator::stdSaveSlot,      (void *)&StdGenerator::stdLoadSlot,
    (void *)&Generator::nop3,      (void *)&Generator::nop4,         (void *)&Generator::nop3,
    (void *)&Generator::nop1,
};

extern void *const gen_vtbl_xstd[] = {
    (void *)&XStdGenerator::xStdDtorSlot,  (void *)&XStdGenerator::xStdCopyFromSlot, (void *)&Generator::attachRingSlot,
    (void *)&XStdGenerator::xStdEmitSlot,  (void *)&XStdGenerator::xStdSaveSlot,     (void *)&XStdGenerator::xStdLoadSlot,
    (void *)&XStdGenerator::xStdSetPositionSlot, (void *)&XStdGenerator::xStdSetVelocitySlot,
    (void *)&XStdGenerator::xStdSetDirectionSlot, (void *)&XStdGenerator::xStdSetSpeedSlot,
};

extern void *const gen_vtbl_cylinder[] = {
    (void *)&CylinderGenerator::cylDtorSlot,   (void *)&CylinderGenerator::cylCopyFromSlot,  (void *)&Generator::attachRingSlot,
    (void *)&CylinderGenerator::cylinderEmitSlot, (void *)&CylinderGenerator::cylSaveSlot,   (void *)&CylinderGenerator::cylLoadSlot,
    (void *)&CylinderGenerator::cylSetPositionSlot, (void *)&Generator::nop4,
    (void *)&CylinderGenerator::cylSetDirectionSlot, (void *)&Generator::nop1,
};

extern void *const env_vtbl_base[] = {
    (void *)&Environment::baseDtorSlot,    (void *)&Environment::baseCopyFromSlot,    (void *)&Environment::attachRingSlot,
    (void *)&Environment::baseTickSlot,    (void *)&Environment::baseSaveSlot,        (void *)&Environment::baseLoadSlot,
};

extern void *const env_vtbl_gravity[] = {
    (void *)&GravityEnvironment::gravityDtorSlot, (void *)&GravityEnvironment::gravityCopyFromSlot, (void *)&Environment::attachRingSlot,
    (void *)&GravityEnvironment::gravityTickSlot, (void *)&GravityEnvironment::gravitySaveSlot,     (void *)&GravityEnvironment::gravityLoadSlot,
};

extern void *const env_vtbl_magnet[] = {
    (void *)&MagnetEnvironment::magnetDtorSlot,  (void *)&MagnetEnvironment::magnetCopyFromSlot,  (void *)&Environment::attachRingSlot,
    (void *)&MagnetEnvironment::magnetTickSlot,  (void *)&MagnetEnvironment::magnetSaveSlot,      (void *)&MagnetEnvironment::magnetLoadSlot,
};

#define SLOT_COUNT(t) (sizeof (t) / sizeof *(t))
static_assert(SLOT_COUNT(gen_vtbl_base)     == GEN_VTBL_SLOTS, "Generator vtable");
static_assert(SLOT_COUNT(gen_vtbl_point)    == GEN_VTBL_SLOTS, "Point vtable");
static_assert(SLOT_COUNT(gen_vtbl_box)      == GEN_VTBL_SLOTS, "Box vtable");
static_assert(SLOT_COUNT(gen_vtbl_std)      == GEN_VTBL_SLOTS, "Std vtable");
static_assert(SLOT_COUNT(gen_vtbl_xstd)     == GEN_VTBL_SLOTS, "XStd vtable");
static_assert(SLOT_COUNT(gen_vtbl_cylinder) == GEN_VTBL_SLOTS, "Cylinder vtable");
static_assert(SLOT_COUNT(env_vtbl_base)     == ENV_VTBL_SLOTS, "Environment vtable");
static_assert(SLOT_COUNT(env_vtbl_gravity)  == ENV_VTBL_SLOTS, "Gravity vtable");
static_assert(SLOT_COUNT(env_vtbl_magnet)   == ENV_VTBL_SLOTS, "Magnet vtable");
#undef SLOT_COUNT

/* Generators and Environments share slot 3 (Tick / emit) and both carry one of
 * the tables above, so this is a direct call into this DLL — no class switch
 * or identity lookup needed. */

typedef void (THISCALL *sim_tick_fn)(void *, float);

void sim_tick_slot3(void *obj, float dt)
{
    void **vtbl = *(void ***)obj;
    ((sim_tick_fn)vtbl[GEN_VT_TICK_SLOT])(obj, dt);
}
