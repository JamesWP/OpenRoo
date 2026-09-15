/* Particle simulation reimplementation — Stage C (PARTICLE_PLAN.md § 4).
 *
 * Stage A/B took the render path and the ParticleSystem-level tick; this file
 * takes the simulation underneath.  Because Stage B's Particle_BaseTick already
 * dispatches pGenerator->vtbl[3](dt) and pEnvironment->vtbl[3](dt) by pointer,
 * each replacement here is just a vtable slot swap plus a UD2 stub.
 *
 * Replaced so far:
 *   0x44c450 GravityEnvironment::TickUpdate  → Env_GravityTick   (vtbl 0x45f110 slot 3)
 *   0x44cca0 MagnetEnvironment::TickUpdate   → Env_MagnetTick    (vtbl 0x45f128 slot 3)
 *   0x449fe0 StdGenerator::EmitParticles     → Gen_StdEmit       (vtbl 0x45f094 slot 3)
 *   0x44ba70 CylinderGenerator::EmitParticles→ Gen_CylinderEmit  (vtbl 0x45f0e8 slot 3)
 *   0x44aac0 XStdGenerator::EmitParticles    → Gen_XStdEmit      (vtbl 0x45f0bc slot 3)
 *   0x448560 Environment::RetireParticleNode → inlined as retire_node() here;
 *            with both ticks replaced the original is unreachable (UD2)
 *
 * The ring is one NULL-terminated doubly-linked list, partitioned as
 * [pRingHead, pRingCurrent) live and [pRingCurrent, pRingTail] free.  The
 * environment owns ageing and retirement; the generator owns emission.  Ring
 * *allocation* stays game-owned — we only honour the contract.
 *
 * KAROO_PARTICLE_FX modes added here:
 *   gravity  — multiply flGravity x5, so particles visibly plummet
 *   antigrav — invert and amplify gravity (x-3): every falling effect rises
 *   nolife  — skip the flLife decrement, so nothing expires (also the
 *             ring-contract stress test: emission must stall, not corrupt)
 *   burst    — emit particles at x3 initial velocity, so every effect visibly
 *              throws further; proves the emission path, not the integration
 *   loadflip — negate the gravity magnitude and magnet force as Load reads
 *              them; only our Load can produce it (the tick is unchanged)
 *
 * Stage E4 (PARTICLE_PLAN.md § 6.10), installed by factory.cpp, not patch.py:
 *   0x44c7f0 GravityEnvironment::Load       → Env_GravityLoad  (slot 5)
 *   0x44cbf0 MagnetEnvironment::Load        → Env_MagnetLoad   (slot 5)
 *   0x44c320 / 0x44c410 Gravity's two setters, called only from its Load
 */
#include "generators.h"
#include "assetio.h"
#include "factory.h"
#include "log.h"
#include <math.h>
#include <stdlib.h>

#define SIM_LOG_FIRST 8

/* ─── FX ─── */

enum SimFx { FX_NONE = 0, FX_GRAVITY, FX_NOLIFE, FX_ANTIGRAV, FX_BURST, FX_LOADFLIP };

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
        }
        if (cached != FX_NONE)
            log_write("sim: FX mode = %s\n", name);
    }
    return (SimFx)cached;
}

/* Gravity multiplier for the current FX mode.  antigrav inverts and amplifies,
 * so every falling effect visibly rises instead — the clearest single proof
 * that the integration is running from this DLL and not from game code. */
static float fx_gravity_scale(SimFx fx)
{
    switch (fx) {
    case FX_GRAVITY:  return 5.0f;
    case FX_ANTIGRAV: return -3.0f;
    default:          return 1.0f;
    }
}

/* ─── Shared ring/colour helpers (C2 reuses all three) ─── */

/* Environment::RetireParticleNode @ 0x448560.  Unlinks an expired node from the
 * live region and appends it at the free end, re-seeding pRingCurrent if
 * emission had stalled.  Reproduces the original branch-for-branch. */
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

/* One colour channel moved at most `step` toward `target`, snapping when
 * already within a step.  Unsigned throughout, as the original is. */
static DWORD fade_channel(DWORD cur, DWORD target, DWORD step)
{
    if (cur == target)
        return cur;
    if (target < cur)
        return (step < cur - target) ? cur - step : target;
    return (step < target - cur) ? cur + step : target;
}

/* The shared ARGB fade block of both environment ticks.
 *
 * BUG PRESERVED DELIBERATELY: in the red channel's "increase" branch the game
 * compares against and increments the ALPHA byte instead of red, leaving red
 * unchanged.  Identical in 0x44c450 and 0x44cca0 (copied code).  Reproducing it
 * is required for bit-exactness — see PARTICLE_PLAN.md § 4.5 C1. */
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
        } else if (step < target[0] - alpha) {  /* alpha, not red — see above */
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

/* ─── C1: GravityEnvironment::TickUpdate (0x44c450) ─── */

static void gravity_tick(GravityEnvironment *self, float dt)
{
    RingBuffer *ring = self->base.pRing;
    if (ring->pRingCurrent == ring->pRingHead)
        return;

    SimFx fx = sim_fx();
    float scale = fx_gravity_scale(fx);
    float gx = self->flGravity[0] * scale;
    float gy = self->flGravity[1] * scale;
    float gz = self->flGravity[2] * scale;

    DWORD step = fade_step(&self->flFadeAccum, self->flFadeRate,
                           self->dwFadeThreshold, dt);

    ring = self->base.pRing;
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
                node->dwDiffuse = fade_diffuse(node->dwDiffuse, self->dwTargetRGB, step);

            /* Kill planes: any enabled axis outside [min, max] retires. */
            const float pos[3] = { node->flX, node->flY, node->flZ };
            retire = false;
            for (int a = 0; a < 3; a++)
                if (self->dwClipEnable[a] != 0 &&
                    (pos[a] > self->flClipMax[a] || pos[a] < self->flClipMin[a]))
                    retire = true;
        } else {
            retire = true;
        }

        next = node->pNext;
        if (retire) {
            retire_node(self->base.pRing, node);
            if (next == NULL)
                return;
        }
        node = next;
    } while (node != self->base.pRing->pRingCurrent);
}

/* ─── C2: MagnetEnvironment::TickUpdate (0x44cca0) ─── */

/* Per-axis "has arrived" test, in the original's branch form rather than
 * fabsf(d) <= half — the two differ if a half-extent is ever negative. */
static bool within_extent(float d, float half)
{
    return (d <= 0.0f) ? (-half <= d) : (d <= half);
}

static void magnet_tick(MagnetEnvironment *self, float dt)
{
    RingBuffer *ring = self->base.pRing;
    if (ring->pRingCurrent == ring->pRingHead)
        return;

    SimFx fx = sim_fx();
    /* antigrav repels instead of attracting — the magnet equivalent of the
     * inverted gravity, and just as obvious on a shield effect. */
    float fscale = (fx == FX_ANTIGRAV) ? -3.0f : (fx == FX_GRAVITY ? 5.0f : 1.0f);

    DWORD step = fade_step(&self->flFadeAccum, self->flFadeRate,
                           self->dwFadeThreshold, dt);

    ring = self->base.pRing;
    ParticleNode *node = ring->pRingHead;
    if (node == ring->pRingCurrent)
        return;

    do {
        ParticleNode *next;
        bool retire = false;

        if (fx != FX_NOLIFE)
            node->flLife -= dt;

        if (node->flLife >= 0.0f) {
            float d[3] = { self->flCentre[0] - node->flX,
                           self->flCentre[1] - node->flY,
                           self->flCentre[2] - node->flZ };

            if (within_extent(d[0], self->flHalfExtent[0]) &&
                within_extent(d[1], self->flHalfExtent[1]) &&
                within_extent(d[2], self->flHalfExtent[2])) {
                retire = true;   /* arrived at the magnet */
            } else {
                /* Unguarded division, as the original is: a zero-length d is
                 * already inside the box above and has been retired. */
                float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                node->flVel[0] += (d[0] / len) * self->flForce[0] * fscale * dt;
                node->flVel[1] += (d[1] / len) * self->flForce[1] * fscale * dt;
                node->flVel[2] += (d[2] / len) * self->flForce[2] * fscale * dt;
                node->flX += node->flVel[0] * dt;
                node->flY += node->flVel[1] * dt;
                node->flZ += node->flVel[2] * dt;

                if (step != 0)
                    node->dwDiffuse = fade_diffuse(node->dwDiffuse,
                                                   self->dwTargetRGB, step);
            }
        } else {
            retire = true;
        }

        next = node->pNext;
        if (retire) {
            retire_node(self->base.pRing, node);
            if (next == NULL)
                return;
        }
        node = next;
    } while (node != self->base.pRing->pRingCurrent);
}

/* ─── C3: StdGenerator::EmitParticles (0x449fe0) ─── */

/* The originals wrap by SUBTRACTION off the pre-increment value, not by a
 * modulo, and the two differ if an index is ever driven out of range: e.g. the
 * step-3 index maps 497/498/499 -> 0/1/2 via (old - 497).  Mirror the
 * arithmetic rather than writing % 500. */
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

/* Shared by C3 and C5: XStdGenerator's emit is StdGenerator's with a constant
 * bias added to the sampled position and velocity, so both go through here.
 * pos_off / vel_off are NULL for a plain StdGenerator. */
static void std_emit(StdGenerator *self, float dt,
                     const float *pos_off, const float *vel_off)
{
    if (self->base.dwEnabled == 0)
        return;
    RingBuffer *ring = self->base.pRing;
    if (ring->pRingCurrent == NULL)
        return;

    float acc = dt * self->flDtScale + self->flAccumulator;
    self->flAccumulator = acc;
    if (!(acc >= 0.0f))
        return;

    int count = (int)acc;                       /* truncates toward zero */
    self->flAccumulator = acc - (float)count;
    if (count <= 0)
        return;

    SimFx fx = sim_fx();
    float vscale = (fx == FX_BURST) ? 3.0f : 1.0f;

    for (int emitted = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        const float *pos = &self->flPosTable[self->dwPosIdx * 3];
        const float *vel = &self->flVelTable[self->dwVelIdx * 3];

        node->flLife = self->pLifeTable[self->dwLifeIdx];
        node->flX = pos[0] + (pos_off ? pos_off[0] : 0.0f);
        node->flY = pos[1] + (pos_off ? pos_off[1] : 0.0f);
        node->flZ = pos[2] + (pos_off ? pos_off[2] : 0.0f);
        node->flVel[0] = (vel[0] + (vel_off ? vel_off[0] : 0.0f)) * vscale;
        node->flVel[1] = (vel[1] + (vel_off ? vel_off[1] : 0.0f)) * vscale;
        node->flVel[2] = (vel[2] + (vel_off ? vel_off[2] : 0.0f)) * vscale;
        node->dwDiffuse = self->pEmitProb[self->dwProbIdx];

        self->dwPosIdx  = wrap_index(self->dwPosIdx, 1, 500);
        self->dwVelIdx  = wrap_index(self->dwVelIdx, 3, 500);
        self->dwLifeIdx = bump_index(self->dwLifeIdx, 100);
        self->dwProbIdx = bump_index(self->dwProbIdx, 200);

        /* Claim the node: advance the free-list cursor past it. */
        self->base.pRing->pRingCurrent = node->pNext;
        ring = self->base.pRing;
        if (ring->pRingCurrent == NULL)
            return;                             /* ring full — stop early */
        if (count <= ++emitted)
            return;
    }
}

/* ─── C4: CylinderGenerator::EmitParticles (0x44ba70) ─── */

/* (v,1) x M as a row vector with w-divide, M row-major.  Same convention as
 * the Stage B corner transforms; the decompiled inner loop accumulates
 * out[c] = sum_r M[r][c] * v[r].  The w-divide is skipped when w is exactly
 * the value at 0x45d2e8 (0.0), as in the original. */
static void transform_point_row(float out[3], const float v[3], const float m[16])
{
    float o[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float in[4] = { v[0], v[1], v[2], 1.0f };
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            o[c] += m[r * 4 + c] * in[r];
    if (o[3] != 0.0f) {
        o[0] /= o[3]; o[1] /= o[3]; o[2] /= o[3];
    }
    out[0] = o[0]; out[1] = o[1]; out[2] = o[2];
}

static void cylinder_emit(CylinderGenerator *self, float dt)
{
    if (self->base.dwEnabled == 0)
        return;
    RingBuffer *ring = self->base.pRing;
    if (ring->pRingCurrent == NULL)
        return;

    float acc = dt * self->flDtScale + self->flAccumulator;
    self->flAccumulator = acc;
    if (!(acc >= 0.0f))
        return;

    int count = (int)acc;
    self->flAccumulator = acc - (float)count;
    if (count <= 0)
        return;

    float vscale = (sim_fx() == FX_BURST) ? 3.0f : 1.0f;

    for (int emitted = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        const float *pos = &self->flPosTable[self->dwPosIdx * 3];
        const float *vel = &self->flVelTable[self->dwVelIdx * 3];

        node->flLife = self->pLifeTable[self->dwLifeIdx];

        /* Sample -> scale -> transform -> offset. */
        float p[3] = { pos[0] * self->flScale,
                       pos[1] * self->flScale,
                       pos[2] * self->flScale };
        float t[3];
        transform_point_row(t, p, self->flMatrix);
        node->flX = t[0] + self->flOrigin[0];
        node->flY = t[1] + self->flOrigin[1];
        node->flZ = t[2] + self->flOrigin[2];

        /* Velocity is NOT run through the matrix. */
        node->flVel[0] = vel[0] * vscale;
        node->flVel[1] = vel[1] * vscale;
        node->flVel[2] = vel[2] * vscale;
        node->dwDiffuse = self->pEmitProb[self->dwProbIdx];

        self->dwPosIdx  = wrap_index(self->dwPosIdx, 1, 500);
        self->dwVelIdx  = wrap_index(self->dwVelIdx, 3, 500);
        self->dwLifeIdx = bump_index(self->dwLifeIdx, 100);
        self->dwProbIdx = bump_index(self->dwProbIdx, 200);

        self->base.pRing->pRingCurrent = node->pNext;
        ring = self->base.pRing;
        if (ring->pRingCurrent == NULL)
            return;
        if (count <= ++emitted)
            return;
    }
}

/* ─── One definition per class ─────────────────────────────────────────────
 *
 * Each of these is the single implementation of that class's tick, logging
 * included.  Both entry paths run it: the exported vtable thunk below (when
 * the game dispatches) and sim_tick_generator / sim_tick_environment (when we
 * dispatch from Particle_BaseTick).  Behaviour is therefore identical
 * whichever way the call arrives. */

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

/* --- Steady-state instrumentation (KAROO_SIM_STATS=N) ---------------------
 *
 * CLAUDE.md's rule for simulation code: log live/free and confirm they reach a
 * steady state rather than climbing to the buffer size or collapsing to zero.
 * Set KAROO_SIM_STATS to a tick interval (e.g. 60) and every environment logs
 * its ring occupancy every N ticks, keyed by object address so several systems
 * in one scene stay distinguishable.  Off (0) unless the variable is set. */
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
    /* Also report how many of the live nodes are already expired (flLife < 0).
     * A healthy ring retires those the same tick they expire, so this should
     * hover near zero; a live region full of expired nodes means retirement
     * has stopped and the ring can never recycle. */
    DWORD expired = 0, oldest_seen = 0;
    float minlife = 0.0f, maxlife = 0.0f;
    bool first = true;
    for (ParticleNode *nd = ring->pRingHead; nd && nd != ring->pRingCurrent; nd = nd->pNext) {
        if (nd->flLife < 0.0f)
            expired++;
        if (first || nd->flLife < minlife) minlife = nd->flLife;
        if (first || nd->flLife > maxlife) maxlife = nd->flLife;
        first = false;
        if (++oldest_seen > 4096) break;   /* cycle guard */
    }
    log_write("stats: %s this=%p tick=%ld dt=%.9f live=%lu free=%lu ring=%lu expired=%lu "
              "life=[%f..%f] head=%p cur=%p tail=%p\n",
              what, self, n, dt, count_live(ring), count_free(ring), ring->dwRingCount,
              expired, minlife, maxlife,
              ring->pRingHead, ring->pRingCurrent, ring->pRingTail);
}

static void gravity_env_tick(GravityEnvironment *self, float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: GravityTick this=%p dt=%f live=%lu ring=%lu\n",
                  self, dt, count_live(self->base.pRing),
                  self->base.pRing->dwRingCount);
    static LONG ticks = 0;
    stats_tick("gravity", self, self->base.pRing, &ticks, dt);
    gravity_tick(self, dt);
}

static void magnet_env_tick(MagnetEnvironment *self, float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: MagnetTick this=%p dt=%f live=%lu ring=%lu centre=%f,%f,%f\n",
                  self, dt, count_live(self->base.pRing),
                  self->base.pRing->dwRingCount,
                  self->flCentre[0], self->flCentre[1], self->flCentre[2]);
    static LONG ticks = 0;
    stats_tick("magnet", self, self->base.pRing, &ticks, dt);
    magnet_tick(self, dt);
}

static void std_gen_tick(StdGenerator *self, float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: StdEmit this=%p dt=%f enabled=%lu accum=%f free=%lu ring=%lu\n",
                  self, dt, self->base.dwEnabled, self->flAccumulator,
                  count_free(self->base.pRing), self->base.pRing->dwRingCount);
    std_emit(self, dt, NULL, NULL);
}

static void xstd_gen_tick(XStdGenerator *self, float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: XStdEmit this=%p dt=%f enabled=%lu posoff=%f,%f,%f "
                  "veloff=%f,%f,%f ring=%lu\n",
                  self, dt, self->base.base.dwEnabled,
                  self->flPosOffset[0], self->flPosOffset[1], self->flPosOffset[2],
                  self->flVelOffset[0], self->flVelOffset[1], self->flVelOffset[2],
                  self->base.base.pRing->dwRingCount);
    std_emit(&self->base, dt, self->flPosOffset, self->flVelOffset);
}

static void cyl_gen_tick(CylinderGenerator *self, float dt)
{
    SIM_LOG_ONCE(calls)
        log_write("sim: CylinderEmit this=%p dt=%f enabled=%lu accum=%f "
                  "origin=%f,%f,%f scale=%f ring=%lu\n",
                  self, dt, self->base.dwEnabled, self->flAccumulator,
                  self->flOrigin[0], self->flOrigin[1], self->flOrigin[2],
                  self->flScale, self->base.pRing->dwRingCount);
    cylinder_emit(self, dt);
}

/* ─── Load (slot 5) ─── */

/* One fread of `size` bytes; the originals test `!= 1` after every call and
 * bail out with 0, leaving whatever was already read in place. */
static bool read1(void *dst, unsigned size, void *fp)
{
    return hooks_fread(dst, size, 1, fp) == 1;
}

/* 0x44c320.  Stores the direction and magnitude as read, then flGravity =
 * normalise(dir) * magnitude — or dir itself when it is exactly zero.  x87
 * order kept: z*z + y*y + x*x, sqrt unrounded, each quotient rounded to float
 * before the multiply. */
static void gravity_set_vector(GravityEnvironment *self, const float dir[3], float mag)
{
    self->flDirection[0] = dir[0];
    self->flDirection[1] = dir[1];
    self->flDirection[2] = dir[2];
    self->flMagnitude = mag;
    if (dir[0] == 0.0f && dir[1] == 0.0f && dir[2] == 0.0f) {
        self->flGravity[0] = dir[0];
        self->flGravity[1] = dir[1];
        self->flGravity[2] = dir[2];
        return;
    }
    long double len = sqrtl((long double)dir[2] * dir[2] +
                            (long double)dir[1] * dir[1] +
                            (long double)dir[0] * dir[0]);
    float q[3] = { (float)(dir[0] / len), (float)(dir[1] / len), (float)(dir[2] / len) };
    self->flGravity[0] = q[0] * mag;
    self->flGravity[1] = q[1] * mag;
    self->flGravity[2] = q[2] * mag;
}

/* 0x44c410. */
static void gravity_set_colour(GravityEnvironment *self, DWORD argb, float fade)
{
    self->dwTargetARGB   = argb;
    self->dwTargetA      = argb >> 24;
    self->dwTargetRGB[0] = (argb >> 16) & 0xff;
    self->dwTargetRGB[2] = argb & 0xff;
    self->dwTargetRGB[1] = (argb >> 8) & 0xff;
    self->flFadeRate     = fade;
}

/* 0x44c7f0.  Does not call the base Environment::Load (0x4485c0, a bare
 * `return 1`), and leaves flFadeAccum untouched — unlike Magnet. */
static BOOL gravity_env_load(GravityEnvironment *self, void *fp)
{
    float dir[3], mag, fade;
    DWORD argb;
    if (!read1(dir, 12, fp))                   return FALSE;
    if (!read1(&mag, 4, fp))                   return FALSE;
    if (!read1(&argb, 4, fp))                  return FALSE;
    if (!read1(&fade, 4, fp))                  return FALSE;
    if (!read1(&self->dwFadeThreshold, 4, fp)) return FALSE;
    if (!read1(&self->dwClipEnable[0], 4, fp)) return FALSE;
    if (!read1(&self->dwClipEnable[1], 4, fp)) return FALSE;
    if (!read1(&self->dwClipEnable[2], 4, fp)) return FALSE;
    if (!read1(self->flClipMax, 12, fp))       return FALSE;
    if (!read1(self->flClipMin, 12, fp))       return FALSE;
    if (sim_fx() == FX_LOADFLIP)
        mag = -mag;
    gravity_set_vector(self, dir, mag);
    gravity_set_colour(self, argb, fade);
    SIM_LOG_ONCE(calls)
        log_write("sim: GravityLoad this=%p gravity=%f,%f,%f argb=%08lX\n", self,
                  self->flGravity[0], self->flGravity[1], self->flGravity[2], argb);
    return TRUE;
}

/* 0x44cbf0.  Force before centre in the file.  dwTargetRGB (+0x38..+0x40) is
 * never loaded — it keeps the constructor's value; preserved as found. */
static BOOL magnet_env_load(MagnetEnvironment *self, void *fp)
{
    /* base Environment::Load (0x4485c0) is `return 1`, result ignored. */
    if (!read1(self->flForce, 12, fp))         return FALSE;
    if (!read1(self->flCentre, 12, fp))        return FALSE;
    if (!read1(&self->flRange, 4, fp))         return FALSE;
    if (!read1(&self->flFadeRate, 4, fp))      return FALSE;
    if (!read1(&self->dwFadeThreshold, 4, fp)) return FALSE;
    self->flFadeAccum = 0.0f;
    if (sim_fx() == FX_LOADFLIP)
        for (int i = 0; i < 3; i++)
            self->flForce[i] = -self->flForce[i];
    SIM_LOG_ONCE(calls)
        log_write("sim: MagnetLoad this=%p force=%f,%f,%f centre=%f,%f,%f\n", self,
                  self->flForce[0], self->flForce[1], self->flForce[2],
                  self->flCentre[0], self->flCentre[1], self->flCentre[2]);
    return TRUE;
}

/* ─── Exports — vtable thunks, installed by factory.cpp's clone table ─── */

#define THISCALL __attribute__((thiscall))

extern "C" {

__declspec(dllexport) BOOL THISCALL
Env_GravityLoad(GravityEnvironment *self, void *fp)  { return gravity_env_load(self, fp); }

__declspec(dllexport) BOOL THISCALL
Env_MagnetLoad(MagnetEnvironment *self, void *fp)    { return magnet_env_load(self, fp); }

__declspec(dllexport) void THISCALL
Env_GravityTick(GravityEnvironment *self, float dt)  { gravity_env_tick(self, dt); }

__declspec(dllexport) void THISCALL
Env_MagnetTick(MagnetEnvironment *self, float dt)    { magnet_env_tick(self, dt); }

__declspec(dllexport) void THISCALL
Gen_StdEmit(StdGenerator *self, float dt)            { std_gen_tick(self, dt); }

__declspec(dllexport) void THISCALL
Gen_CylinderEmit(CylinderGenerator *self, float dt)  { cyl_gen_tick(self, dt); }

__declspec(dllexport) void THISCALL
Gen_XStdEmit(XStdGenerator *self, float dt)          { xstd_gen_tick(self, dt); }

} // extern "C"

/* ─── Direct dispatch ──────────────────────────────────────────────────────
 *
 * Every live Generator and Environment class is ours, so dispatching slot 3
 * through the game's vtable only leaves this DLL and comes straight back —
 * via a .khook trampoline, an IAT entry and an indirect call.  Recognise the
 * class by its vtable address and call the implementation directly.
 *
 * NOTE it must be the *vtable* address, not the slot contents: patch.py does
 * not write DLL addresses into the slots, it writes trampolines inside the
 * game image, so comparing slots never matches.
 *
 * Since Stage E1 the object's +0x00 may be one of our cloned tables rather than
 * the game VA, so the address goes through vtbl_identity() first, which maps a
 * clone back to the game vtable it was cloned from.
 *
 * A vtable we do not know still gets a genuine virtual call, which is what
 * keeps the dead PointGenerator / BoxGenerator classes (and anything replaced
 * later) working unchanged. */

#define GEN_VT_TICK 3  /* Generator/Environment vtable slot +0x0c */

typedef void (THISCALL *sim_tick_fn)(void *, float);

/* One line per kind, the first time through, recording which path was taken.
 * "direct" is the point of this layer; a "virtual" line means a class we do
 * not own is in play and the fallback did its job. */
static void log_path_once(LONG *once, const char *what, bool direct, void *slot)
{
    if (InterlockedExchange(once, 1) == 0)
        log_write("sim: %s dispatch = %s (vtbl=%p)\n",
                  what, direct ? "direct" : "virtual", slot);
}

void sim_tick_generator(Generator *gen, float dt)
{
    DWORD vtbl = vtbl_identity(gen->pVtable);
    static LONG once = 0;
    log_path_once(&once, "generator",
                  vtbl == VTBL_GEN_STD || vtbl == VTBL_GEN_XSTD ||
                  vtbl == VTBL_GEN_CYLINDER, gen->pVtable);
    switch (vtbl) {
    case VTBL_GEN_STD:      std_gen_tick((StdGenerator *)gen, dt);      return;
    case VTBL_GEN_XSTD:     xstd_gen_tick((XStdGenerator *)gen, dt);    return;
    case VTBL_GEN_CYLINDER: cyl_gen_tick((CylinderGenerator *)gen, dt); return;
    }
    ((sim_tick_fn)gen->pVtable[GEN_VT_TICK])(gen, dt);  /* not ours — virtual */
}

void sim_tick_environment(Environment *env, float dt)
{
    DWORD vtbl = vtbl_identity(env->pVtable);
    static LONG once = 0;
    log_path_once(&once, "environment",
                  vtbl == VTBL_ENV_GRAVITY || vtbl == VTBL_ENV_MAGNET, env->pVtable);
    switch (vtbl) {
    case VTBL_ENV_GRAVITY: gravity_env_tick((GravityEnvironment *)env, dt); return;
    case VTBL_ENV_MAGNET:  magnet_env_tick((MagnetEnvironment *)env, dt);   return;
    }
    ((sim_tick_fn)env->pVtable[GEN_VT_TICK])(env, dt);
}
