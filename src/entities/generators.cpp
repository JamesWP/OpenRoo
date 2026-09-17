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
 *   fastemit — x5 the emit rate every generator Load reads, so every effect
 *              emits five times as fast; the generator counterpart of
 *              loadflip, and it moves only a counter, never a coordinate
 *
 * Stage E4 (PARTICLE_PLAN.md § 6.10), installed by factory.cpp, not patch.py:
 *   0x44c7f0 GravityEnvironment::Load       → Env_GravityLoad  (slot 5)
 *   0x44cbf0 MagnetEnvironment::Load        → Env_MagnetLoad   (slot 5)
 *   0x44c320 / 0x44c410 Gravity's two setters, called only from its Load
 *   0x44c220 / 0x44ca40 scalar deleting dtors → Env_GravityDtor / Env_MagnetDtor (slot 0)
 *            (with their bodies 0x44c240 / 0x44ca60)
 *   0x44c250 / 0x44cab0 CopyFrom → Env_GravityCopyFrom / Env_MagnetCopyFrom (slot 1)
 *            (with the shared name check 0x448500, called only from these two)
 *   0x44c6c0 / 0x44cb50 Save     → Env_GravitySave / Env_MagnetSave (slot 4)
 *
 * Stage E4 construction — the environment family is now wholly ours:
 *   0x4488f0 EnvironmentFactoryCreate → env_create (via factory.cpp)
 *   0x448490 / 0x44c130 / 0x44c970 the three constructors
 *   0x4484b0 / 0x4484d0 base Environment scalar dtor + body
 *   0x4484e0 AttachEnvironmentRing (slot 2 of all three) → Env_AttachRing
 * Allocation is our own operator new/delete: every environment is released
 * through vtable slot 0 (ParticleSystem release 0x447b50, SetEnvironment
 * 0x447d80, Deserialize's failure path, the clone path 0x448a70), and slot 0
 * is ours for all three classes, so no game code ever frees one.
 *
 * Stage E4, generators (all but Cylinder, and all but construction):
 *   base / Point / Box / Std / XStd — every vtable slot, via factory.cpp
 *   0x449200 PointGenerator emit, 0x449420 BoxGenerator emit
 *   0x449860 / 0x44a160 / 0x44a2e0 Std CopyFrom / Save / Load, and Load's
 *     builders 0x449af0 (sphere) 0x4499d0 (box) 0x449c50 (velocity)
 *     0x449fa0 (rate) 0x44a530 / 0x44a4d0 (type table) 0x449ea0 (clone table)
 *   0x44a6f0 / 0x44aa00 / 0x44aa60 XStd CopyFrom / Save / Load
 *   0x44a6a0 / 0x44a850 / 0x44a730 / 0x44a930 XStd slots 6-9
 *   0x4483c0 AttachGeneratorRing (slot 2 of all six generator vtables)
 *   Cylinder: every slot, its builders, and SetDirection's frame (0x44b0c0)
 *   0x4485d0 GeneratorFactoryCreate → gen_create, and all six constructors
 * The Gaussian sampler 0x448fb0 is reimplemented here but stays live in the
 * game (0x438188 calls it).  Every generator is released only through vtable
 * slot 0 (the same four paths as environments, plus CloneGeneratorFromSource
 * 0x4488b0), and every slot 0 is ours, so objects, type tables and scratch
 * buffers all use our own new / delete.
 */
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

#define SIM_LOG_FIRST 8

/* Our vtables, defined at the foot of this file — one per class, in the game's
 * slot order.  Every constructor installs one of these; no object of ours ever
 * carries a game vtable address. */
extern void *const gen_vtbl_base[];
extern void *const gen_vtbl_point[];
extern void *const gen_vtbl_box[];
extern void *const gen_vtbl_std[];
extern void *const gen_vtbl_xstd[];
extern void *const gen_vtbl_cylinder[];
extern void *const env_vtbl_base[];
extern void *const env_vtbl_gravity[];
extern void *const env_vtbl_magnet[];

/* ─── FX ─── */

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
 * included.  Every caller reaches it the same way — through slot 3 of the
 * class's vtable, which is one of ours — whether the game dispatches or
 * Particle_BaseTick does via sim_tick_slot3. */

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
 * order kept (z*z + y*y + x*x); intermediates in double, each quotient
 * rounded to float before the multiply, as the original does. */
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
    double len = sqrt((double)dir[2] * dir[2] +
                            (double)dir[1] * dir[1] +
                            (double)dir[0] * dir[0]);
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

/* ─── Save (slot 4) ─── */

/* The game's CRT fwrite.  A callback, and a deliberate one: the FILE * is the
 * game's static-CRT stream, which no other fwrite can write to (texturetga.cpp
 * and friends reach the same function the same way). */
typedef unsigned (__cdecl *fwrite_fn)(const void *, unsigned, unsigned, void *);
#define ORIG_FWRITE ((fwrite_fn)0x004513c7)

static bool write1(const void *src, unsigned size, void *fp)
{
    return ORIG_FWRITE(src, size, 1, fp) == 1;
}

/* 0x44c6c0.  The exact mirror of Load: raw direction and magnitude, the packed
 * colour, then the fields Load reads straight into place. */
static BOOL gravity_env_save(GravityEnvironment *self, void *fp)
{
    if (!write1(self->flDirection, 12, fp))      return FALSE;
    if (!write1(&self->flMagnitude, 4, fp))      return FALSE;
    if (!write1(&self->dwTargetARGB, 4, fp))     return FALSE;
    if (!write1(&self->flFadeRate, 4, fp))       return FALSE;
    if (!write1(&self->dwFadeThreshold, 4, fp))  return FALSE;
    if (!write1(&self->dwClipEnable[0], 4, fp))  return FALSE;
    if (!write1(&self->dwClipEnable[1], 4, fp))  return FALSE;
    if (!write1(&self->dwClipEnable[2], 4, fp))  return FALSE;
    if (!write1(self->flClipMax, 12, fp))        return FALSE;
    return write1(self->flClipMin, 12, fp);
}

/* 0x44cb50.  Calls the base Environment::Save first — `return 1` (0x4485c0),
 * result ignored — then the mirror of Load; dwTargetRGB is not written. */
static BOOL magnet_env_save(MagnetEnvironment *self, void *fp)
{
    if (!write1(self->flForce, 12, fp))          return FALSE;
    if (!write1(self->flCentre, 12, fp))         return FALSE;
    if (!write1(&self->flRange, 4, fp))          return FALSE;
    if (!write1(&self->flFadeRate, 4, fp))       return FALSE;
    return write1(&self->dwFadeThreshold, 4, fp);
}

/* ─── CopyFrom (slot 1) ─── */

/* 0x448500, Environment's base CopyFrom: no copy at all, only the gate — the
 * source must carry the same type name.  Inline strcmp, == 0 → TRUE. */
static BOOL env_same_name(const Environment *self, const Environment *src)
{
    return strcmp(src->pName, self->pName) == 0;
}

/* 0x44c250 / 0x44cab0.  After the name gate, each copies every field past the
 * base (+0x0c to the end) one DWORD at a time — the vtable, pName and pRing
 * stay the destination's.  The originals' copy order differs from memory
 * order, which only matters if src aliases dst; it never does. */
static BOOL gravity_env_copy_from(GravityEnvironment *self, const GravityEnvironment *src)
{
    if (!env_same_name(&self->base, &src->base))
        return FALSE;
    memcpy((BYTE *)self + sizeof(Environment), (const BYTE *)src + sizeof(Environment),
           sizeof(GravityEnvironment) - sizeof(Environment));
    return TRUE;
}

static BOOL magnet_env_copy_from(MagnetEnvironment *self, const MagnetEnvironment *src)
{
    if (!env_same_name(&self->base, &src->base))
        return FALSE;
    memcpy((BYTE *)self + sizeof(Environment), (const BYTE *)src + sizeof(Environment),
           sizeof(MagnetEnvironment) - sizeof(Environment));
    return TRUE;
}

/* ─── Destructor (slot 0) ─── */

/* 0x4484d0, the base Environment dtor body: a single store of the base vtable. */
static void base_env_destruct(Environment *self)
{
    self->pVtable = (void **)env_vtbl_base;
}

/* 0x44c240 / 0x44ca60.  Each restores its own game vtable, then runs the base
 * body.  Nothing is freed: pName is not owned here (no Free in either).
 * Magnet's calls the base body TWICE (0x44ca8b and 0x44ca9a, the second the
 * EH-state -1 exit) — harmless, kept. */
static void gravity_env_destruct(GravityEnvironment *self)
{
    self->base.pVtable = (void **)env_vtbl_gravity;
    self->base.pVtable = (void **)env_vtbl_base;
}

static void magnet_env_destruct(MagnetEnvironment *self)
{
    self->base.pVtable = (void **)env_vtbl_magnet;
    self->base.pVtable = (void **)env_vtbl_base;
    self->base.pVtable = (void **)env_vtbl_base;
}

/* 0x4484b0 / 0x44c220 / 0x44ca40 are the Environment family's MSVC scalar
 * deleting dtors; their shared tail is factory.h's scalar_delete<T>. */

/* ─── Base Environment slots 1-5 ─── */

/* 0x4484e0, AttachEnvironmentRing — slot 2 of all three classes.  A NULL ring
 * is refused (returns the NULL itself, i.e. 0) and leaves pRing alone. */
static BOOL env_attach_ring(Environment *self, RingBuffer *ring)
{
    if (ring == NULL)
        return FALSE;
    self->pRing = ring;
    return TRUE;
}

/* ─── Construction ─── */

/* The game's own type-name strings; pName keeps pointing at them, exactly as
 * the constructors leave it (read-only data, never freed). */

/* 0x448490. */
static void base_env_construct(Environment *self)
{
    self->pVtable = (void **)env_vtbl_base;
    self->pName   = GS_PSNAME_ENVIRONMENT;
    self->pRing   = NULL;
}

/* 0x44c130 / 0x44c970.  Base ctor, class vtable, then every member zeroed
 * except the fade threshold, which starts at 10.  (Each original also builds
 * and destroys a throwaway Environment temporary on the stack — no effect
 * outside its own frame, omitted.) */
static void gravity_env_construct(GravityEnvironment *self)
{
    base_env_construct(&self->base);
    self->base.pVtable = (void **)env_vtbl_gravity;
    memset((BYTE *)self + sizeof(Environment), 0,
           sizeof(GravityEnvironment) - sizeof(Environment));
    self->base.pName = GS_PSNAME_GRAVITY_ENV;
    self->dwFadeThreshold = 10;
}

static void magnet_env_construct(MagnetEnvironment *self)
{
    base_env_construct(&self->base);
    self->base.pVtable = (void **)env_vtbl_magnet;
    memset((BYTE *)self + sizeof(Environment), 0,
           sizeof(MagnetEnvironment) - sizeof(Environment));
    self->base.pName = GS_PSNAME_MAGNET_ENV;
    self->dwFadeThreshold = 10;
}

/* 0x4488f0.  Same strcmp chain, same sizes (0x0c / 0x6c / 0x50), same NULL for
 * an unknown name or a failed allocation. */
Environment *env_create(const char *name)
{
    if (strcmp(name, "Environment") == 0) {
        Environment *e = (Environment *)::operator new(sizeof(Environment), std::nothrow);
        if (e) base_env_construct(e);
        return e;
    }
    if (strcmp(name, "GravityEnvironment") == 0) {
        GravityEnvironment *g =
            (GravityEnvironment *)::operator new(sizeof(GravityEnvironment), std::nothrow);
        if (g) gravity_env_construct(g);
        return (Environment *)g;
    }
    if (strcmp(name, "MagnetEnvironment") == 0) {
        MagnetEnvironment *m =
            (MagnetEnvironment *)::operator new(sizeof(MagnetEnvironment), std::nothrow);
        if (m) magnet_env_construct(m);
        return (Environment *)m;
    }
    return NULL;
}

/* ═══ Generators ═══════════════════════════════════════════════════════════ */

#define THISCALL_DECL __attribute__((thiscall))

/* ─── x87 helpers ─── */

/* The x87 compare the originals use (FCOMP + TEST AH,0x40) treats an unordered
 * operand as equal, so "== 0" in the game is "zero or NaN" here. */
static inline bool zero_or_nan(float v) { return !(v < 0.0f || v > 0.0f); }

/* 1/32767 (0x38000100), 0.001 (0x3a83126f), pi (0x40490fdb), FLT_EPSILON
 * (0x34000000): each the float nearest its expression, as in .rdata. */
static const float RAND_SCALE  = 1.0f / 32767.0f;
static const float GAUSS_STEP  = 0.001f;
static const float GAUSS_PI    = 3.14159265358979f;
static const float TINY_LENGTH = 1.1920928955078125e-07f;

/* 0x448f30 — a Gaussian density, with the variance slot holding sigma itself:
 * exp(-(x-mu)^2 / (2 sigma)) / (sqrt(2 pi) sigma).  The 2-sigma denominator is
 * the game's (not 2 sigma^2); kept.  sigma zero (or NaN, per the x87 compare)
 * degenerates to an indicator on x == mu.
 *
 * Precision: the original runs in x87 extended precision (its own F2XM1/FSCALE
 * exp, CRT pow(d, 2.0)); every intermediate here is double, by decision
 * (James, 2026-09-15) — the float roundings the original performs are kept,
 * the extended-precision intermediates are not. */
static double gauss_pdf(float x, float mu, float sigma)
{
    if (zero_or_nan(sigma))
        return (!(x < mu || x > mu)) ? 1.0 : 0.0;
    double d = (double)x - mu;
    double q = (d * d) / ((double)sigma + sigma);
    double root = sqrt((double)GAUSS_PI + GAUSS_PI);
    return exp(-q) * (1.0 / (root * sigma));
}

/* One histogram bucket of 0x448fb0: floor(pdf * scale + 0.5) through a double,
 * then __ftol (truncation). */
static int gauss_bucket(float x, float mu, float sigma, float scale)
{
    double v = (double)(gauss_pdf(x, mu, sigma) * scale + 0.5);
    return (int)floor(v);
}

/* 0x448fb0 — fill out[0..n) with samples of the density above.  Builds a
 * histogram table walking outward from mu in `step`s (10 copies of mu, then a
 * mirrored pair per unit of each bucket), reseeds with srand(rand()), and draws
 * n table entries by rand().  Loop 2 replays loop 1's buckets, so the table is
 * filled exactly to `total` provided the centre bucket is 10 — as the original
 * assumes; neither checks. */
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

    CRT_RAND_SEED = crt_rand();                          /* srand(rand()) */
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

/* The one-shot "seed from the clock" flag 0x448e80 consumes (game .data,
 * initially 1).  Shared with nothing else we replace. */
#define UNIFORM_SEED_PENDING (*(volatile BYTE *)0x00469020)

/* 0x448e80 — n samples uniform on [a, b] (either order), centred on the
 * midpoint.  The first call ever reseeds from the game clock; every call then
 * does srand(rand()). */
static void uniform_fill(float *out, int n, float a, float b)
{
    float mid = (float)(((double)a + b) * 0.5f);
    float span = (float)((double)b - a);
    if (!(span >= 0.0f))
        span = (float)((double)span * -1.0f);
    if (UNIFORM_SEED_PENDING) {
        CRT_RAND_SEED = (unsigned)hooks_GameTime(NULL);
        UNIFORM_SEED_PENDING = 0;
    }
    CRT_RAND_SEED = crt_rand();
    for (; n > 0; n--) {
        int r = (int)crt_rand();
        *out++ = (float)((((double)r - 16383.5) * span * RAND_SCALE) + mid);
    }
}

/* ─── Shared generator slots ─── */

/* 0x4483b0, the base Generator dtor body. */
static void base_gen_destruct(Generator *self)
{
    self->pVtable = (void **)gen_vtbl_base;
}

/* The scalar deleting dtors' tail is factory.h's scalar_delete<T>. */

/* 0x4483e0, Generator's base CopyFrom: the type-name gate, then dwEnabled. */
static BOOL gen_copy_base(Generator *self, const Generator *src)
{
    if (strcmp(src->pName, self->pName) != 0)
        return FALSE;
    self->dwEnabled = src->dwEnabled;
    return TRUE;
}

/* 0x4483c0, AttachGeneratorRing — slot 2 of all six generator vtables. */
static BOOL gen_attach_ring(Generator *self, RingBuffer *ring)
{
    if (ring == NULL)
        return FALSE;
    self->pRing = ring;
    return TRUE;
}

/* ─── StdGenerator tables ─── */

/* 0x449ea0 — replace pTypeTable with a copy of (colour, weight) pairs, then
 * refill pEmitProb: reseed from the clock and draw pairs by rand(), writing
 * each drawn colour `weight` times, until all 200 slots are full.  Zero-weight
 * pairs are redrawn.  An empty or missing table fills pEmitProb with -1. */
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

/* Std's copy (0x449ea0); Cylinder's (0x44b920) is the same code on its own
 * fields. */
static void std_clone_type_table(StdGenerator *self, const DWORD *src, DWORD count)
{
    type_table_clone(&self->pTypeTable, &self->dwTypeTableCount, self->pEmitProb,
                     src, count);
}

/* 0x44a4d0 (Std) / 0x44c040 (Cylinder) — count, then the raw pairs. */
static BOOL type_table_save(void *table, const DWORD *pcount, void *fp)
{
    if (fp == NULL)
        return FALSE;
    if (!write1(pcount, 4, fp))
        return FALSE;
    return ORIG_FWRITE(table, 8, *pcount, fp) == *pcount;
}

/* 0x44a530 (Std) / 0x44c0a0 (Cylinder) — read count and pairs, then clone them
 * in.  A short read returns FALSE and leaks the scratch buffer, exactly as the
 * originals do. */
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

static BOOL std_save_type_table(StdGenerator *self, void *fp)
{
    return type_table_save(self->pTypeTable, &self->dwTypeTableCount, fp);
}

static BOOL std_load_type_table(StdGenerator *self, void *fp)
{
    return type_table_load(&self->pTypeTable, &self->dwTypeTableCount,
                           self->pEmitProb, fp);
}

/* Interleave three 500-sample columns into flPosTable (x, y, z per entry). */
static void std_interleave_pos(StdGenerator *self, const float *x, const float *y,
                               const float *z)
{
    for (int k = 0; k < 500; k++) {
        self->flPosTable[k * 3 + 0] = x[k];
        self->flPosTable[k * 3 + 1] = y[k];
        self->flPosTable[k * 3 + 2] = z[k];
    }
}

/* 0x449af0 — sphere mode: a Gaussian per axis, centre mn[i], spread mx[i]. */
static void std_build_sphere(StdGenerator *self, const float *mn, const float *mx)
{
    float a[3] = { mn[0], mn[1], mn[2] }, b[3] = { mx[0], mx[1], mx[2] };
    memcpy(self->flSphMin, a, sizeof a);
    memcpy(self->flSphMax, b, sizeof b);
    self->dwEmitMode = 0;
    float *col[3] = { new float[500], new float[500], new float[500] };
    for (int i = 0; i < 3; i++)
        gauss_fill(col[i], 500, a[i], b[i], (float)((double)b[i] * GAUSS_STEP));
    std_interleave_pos(self, col[0], col[1], col[2]);
    for (int i = 0; i < 3; i++)
        delete[] col[i];
    self->dwPosIdx = 0;
}

/* 0x4499d0 — box mode: uniform per axis on [mn[i], mx[i]]. */
static void std_build_box(StdGenerator *self, const float *mn, const float *mx)
{
    float a[3] = { mn[0], mn[1], mn[2] }, b[3] = { mx[0], mx[1], mx[2] };
    memcpy(self->flBoxMax, b, sizeof b);
    memcpy(self->flBoxMin, a, sizeof a);
    self->dwEmitMode = 1;
    float *col[3] = { new float[500], new float[500], new float[500] };
    for (int i = 0; i < 3; i++)
        uniform_fill(col[i], 500, a[i], b[i]);
    std_interleave_pos(self, col[0], col[1], col[2]);
    for (int i = 0; i < 3; i++)
        delete[] col[i];
    self->dwPosIdx = 0;
}

/* 0x449c50 — velocity table: a Gaussian direction per axis, normalised (when
 * its length is positive), then scaled by a Gaussian magnitude drawn from
 * (lmin, lmax).  Length summed z^2 + x^2 + y^2, x87 order; each normalised
 * component rounded to float before the scale. */
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

/* Std's (0x449c50): the table, then the parameters it was built from. */
static void std_build_velocity(StdGenerator *self, const float *vmin, const float *vmax,
                               float lmin, float lmax)
{
    float a[3] = { vmin[0], vmin[1], vmin[2] }, b[3] = { vmax[0], vmax[1], vmax[2] };
    build_velocity_table(self->flVelTable, a, b, lmin, lmax);
    self->dwVelIdx = 0;
    memcpy(self->flVelMin, a, sizeof a);
    memcpy(self->flVelMax, b, sizeof b);
    self->flLifeMin = lmin;
    self->flLifeMax = lmax;
}

/* 0x449fa0 — the 100-entry table emit copies into flLife, from the emit-rate
 * parameters. */
static void std_build_rate(StdGenerator *self, float lo, float hi)
{
    float step = (float)((double)hi * GAUSS_STEP);
    self->flEmitRateMin = lo;
    self->flEmitRateMax = hi;
    gauss_fill(self->pLifeTable, 100, lo, hi, step);
}

/* ─── StdGenerator slots 0/1/4/5 ─── */

/* 0x449800 — dtor body. */
static void std_gen_destruct(StdGenerator *self)
{
    self->base.pVtable = (void **)gen_vtbl_std;
    if (self->pTypeTable)
        ::operator delete(self->pTypeTable);
    base_gen_destruct(&self->base);
}

/* 0x449860 — base gate, then every field except the type table, which is
 * cloned — and cloning re-draws pEmitProb from a fresh clock seed, so a copy's
 * colours are not the source's.  Kept. */
static BOOL std_gen_copy_from(StdGenerator *self, const StdGenerator *src)
{
    if (!gen_copy_base(&self->base, &src->base))
        return FALSE;
    memcpy((BYTE *)self + 0x10, (const BYTE *)src + 0x10, 0x70 - 0x10);
    memcpy((BYTE *)self + 0x78, (const BYTE *)src + 0x78, 0x3420 - 0x78);
    std_clone_type_table(self, (const DWORD *)src->pTypeTable, src->dwTypeTableCount);
    return TRUE;
}

/* 0x44a160 — the mirror of Load. */
static BOOL std_gen_save(StdGenerator *self, void *fp)
{
    if (!write1(&self->dwEmitMode, 4, fp))     return FALSE;
    if (!write1(self->flBoxMax, 12, fp))       return FALSE;
    if (!write1(self->flBoxMin, 12, fp))       return FALSE;
    if (!write1(self->flSphMin, 12, fp))       return FALSE;
    if (!write1(self->flSphMax, 12, fp))       return FALSE;
    if (!write1(self->flVelMin, 12, fp))       return FALSE;
    if (!write1(self->flVelMax, 12, fp))       return FALSE;
    if (!write1(&self->flLifeMin, 4, fp))      return FALSE;
    if (!write1(&self->flLifeMax, 4, fp))      return FALSE;
    if (!write1(&self->flEmitRateMin, 4, fp))  return FALSE;
    if (!write1(&self->flEmitRateMax, 4, fp))  return FALSE;
    if (!write1(&self->flDtScale, 4, fp))      return FALSE;
    return std_save_type_table(self, fp);
}

/* 0x44a2e0 — parameters, then the builders.  dwEmitMode is re-read between the
 * two shape tests, as the original does. */
static BOOL std_gen_load(StdGenerator *self, void *fp)
{
    if (!read1(&self->dwEmitMode, 4, fp))      return FALSE;
    if (!read1(self->flBoxMax, 12, fp))        return FALSE;
    if (!read1(self->flBoxMin, 12, fp))        return FALSE;
    if (!read1(self->flSphMin, 12, fp))        return FALSE;
    if (!read1(self->flSphMax, 12, fp))        return FALSE;
    if (!read1(self->flVelMin, 12, fp))        return FALSE;
    if (!read1(self->flVelMax, 12, fp))        return FALSE;
    if (!read1(&self->flLifeMin, 4, fp))       return FALSE;
    if (!read1(&self->flLifeMax, 4, fp))       return FALSE;
    if (!read1(&self->flEmitRateMin, 4, fp))   return FALSE;
    if (!read1(&self->flEmitRateMax, 4, fp))   return FALSE;
    if (!read1(&self->flDtScale, 4, fp))       return FALSE;
    if (sim_fx() == FX_FASTEMIT)
        self->flDtScale = (float)((double)self->flDtScale * 5.0);
    if (self->dwEmitMode == 0)
        std_build_sphere(self, self->flSphMin, self->flSphMax);
    if (self->dwEmitMode == 1)
        std_build_box(self, self->flBoxMin, self->flBoxMax);
    std_build_velocity(self, self->flVelMin, self->flVelMax,
                       self->flLifeMin, self->flLifeMax);
    std_build_rate(self, self->flEmitRateMin, self->flEmitRateMax);
    return std_load_type_table(self, fp);
}

/* ─── XStdGenerator ─── */

/* 0x44a6e0 — dtor body: own vtable, then Std's body. */
static void xstd_gen_destruct(XStdGenerator *self)
{
    self->base.base.pVtable = (void **)gen_vtbl_xstd;
    std_gen_destruct(&self->base);
}

/* 0x44a6f0 — Std's copy, then flPosOffset ONLY: flVelOffset is not copied. */
static BOOL xstd_gen_copy_from(XStdGenerator *self, const XStdGenerator *src)
{
    if (!std_gen_copy_from(&self->base, &src->base))
        return FALSE;
    memcpy(self->flPosOffset, src->flPosOffset, sizeof self->flPosOffset);
    return TRUE;
}

/* 0x44aa00 / 0x44aa60. */
static BOOL xstd_gen_save(XStdGenerator *self, void *fp)
{
    if (!std_gen_save(&self->base, fp))        return FALSE;
    if (!write1(self->flPosOffset, 12, fp))    return FALSE;
    return write1(self->flVelOffset, 12, fp);
}

static BOOL xstd_gen_load(XStdGenerator *self, void *fp)
{
    if (!std_gen_load(&self->base, fp))        return FALSE;
    if (!read1(self->flPosOffset, 12, fp))     return FALSE;
    return read1(self->flVelOffset, 12, fp);
}

/* 0x44a6a0 — slot 6, SetPosition. */
static void xstd_set_position(XStdGenerator *self, float x, float y, float z)
{
    self->flPosOffset[0] = x;
    self->flPosOffset[1] = y;
    self->flPosOffset[2] = z;
}

/* Shared tail of slots 7/8/9: v = normalise(dir) * mag, with the first two
 * quotients rounded to float and the third kept in extended precision, as all
 * three originals do. */
static void xstd_store_scaled(XStdGenerator *self, double x, double y,
                             double z, double len, double mag)
{
    float q0 = (float)(x / len);
    float q1 = (float)(y / len);
    double q2 = z / len;
    self->flVelOffset[0] = (float)(q0 * mag);
    self->flVelOffset[1] = (float)(q1 * mag);
    self->flVelOffset[2] = (float)(q2 * mag);
}

/* 0x44a730 — slot 8, SetDirection: keep the current speed, take the new
 * direction.  A zero speed becomes FLT_EPSILON; a zero direction zeroes the
 * velocity.  Lengths: speed (x^2+y^2)+z^2, direction (z^2+y^2)+x^2. */
static void xstd_set_direction(XStdGenerator *self, float x, float y, float z)
{
    double vx = self->flVelOffset[0], vy = self->flVelOffset[1],
                vz = self->flVelOffset[2];
    double mag = sqrt(vx * vx + vy * vy + vz * vz);
    if (!(mag < 0.0 || mag > 0.0))
        mag = TINY_LENGTH;
    if (zero_or_nan(x) && zero_or_nan(y) && zero_or_nan(z)) {
        self->flVelOffset[0] = self->flVelOffset[1] = self->flVelOffset[2] = 0.0f;
        return;
    }
    double lx = x, ly = y, lz = z;
    double len = sqrt(lz * lz + ly * ly + lx * lx);
    xstd_store_scaled(self, lx, ly, lz, len, mag);
}

/* 0x44a850 — slot 7 (4 args): direction and speed together.  Refused (and the
 * velocity re-zeroed) while the current velocity is zero. */
static void xstd_set_velocity(XStdGenerator *self, float x, float y, float z, float mag)
{
    if (zero_or_nan(self->flVelOffset[0]) && zero_or_nan(self->flVelOffset[1]) &&
        zero_or_nan(self->flVelOffset[2])) {
        self->flVelOffset[0] = self->flVelOffset[1] = self->flVelOffset[2] = 0.0f;
        return;
    }
    double lx = x, ly = y, lz = z;
    double len = sqrt(lz * lz + ly * ly + lx * lx);
    xstd_store_scaled(self, lx, ly, lz, len, mag);
}

/* 0x44a930 — slot 9 (1 arg): new speed, same direction.  Zero speed becomes
 * FLT_EPSILON; a zero velocity is left alone.  Length (x^2+y^2)+z^2. */
static void xstd_set_speed(XStdGenerator *self, float mag)
{
    if (zero_or_nan(mag))
        mag = TINY_LENGTH;
    if (zero_or_nan(self->flVelOffset[0]) && zero_or_nan(self->flVelOffset[1]) &&
        zero_or_nan(self->flVelOffset[2]))
        return;
    double x = self->flVelOffset[0], y = self->flVelOffset[1],
                z = self->flVelOffset[2];
    double len = sqrt(x * x + y * y + z * z);
    xstd_store_scaled(self, x, y, z, len, mag);
}

/* ─── PointGenerator / BoxGenerator emit (dead content, kept faithful) ─── */

/* The claim-count step both share: accumulate in extended precision, store the
 * float, bail on a negative (or NaN) total, then carry the fraction of the
 * EXTENDED total.  Returns the number of particles to emit. */
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

/* 0x449200.  No dwEnabled check (unlike Std).  The three velocity indices wrap
 * by 1000 - i at 1000 / 999 / 998, and the life index runs to 101, two past
 * its table — both as found. */
static void point_gen_emit(PointGenerator *self, float dt)
{
    RingBuffer *ring = self->base.pRing;
    if (ring->pRingCurrent == NULL)
        return;
    int n = dead_gen_claim(&self->flAccumulator, self->flEmitRate, dt);
    if (n <= 0)
        return;
    const DWORD *life = (const DWORD *)((const BYTE *)self + 0x0fe4);
    for (int i = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        *(DWORD *)&node->flLife = life[self->dwLifeIdx];
        memcpy(&node->flX, self->flEmitPos, 12);
        node->dwDiffuse = self->dwDiffuse;
        for (int a = 0; a < 3; a++)
            node->flVel[a] = self->flVelTable[self->dwVelIdx[a]] + self->flVelBias[a];
        DWORD i0 = self->dwVelIdx[0] + 1, i1 = self->dwVelIdx[1] + 2,
              i2 = self->dwVelIdx[2] + 3;
        self->dwVelIdx[0] = (i0 >= 1000) ? 1000 - i0 : i0;
        self->dwVelIdx[1] = (i1 >= 999)  ? 1000 - i1 : i1;
        self->dwVelIdx[2] = (i2 >= 998)  ? 1000 - i2 : i2;
        self->dwLifeIdx = (self->dwLifeIdx > 100) ? 0 : self->dwLifeIdx + 1;
        ring->pRingCurrent = node->pNext;
        if (ring->pRingCurrent == NULL)
            return;
        if (++i >= n)
            return;
    }
}

/* 0x449420.  Position and colour are raw table copies; velocity is table +
 * bias.  Both index triples wrap by 500 - i past 499. */
static void box_gen_emit(BoxGenerator *self, float dt)
{
    RingBuffer *ring = self->base.pRing;
    if (ring->pRingCurrent == NULL)
        return;
    int n = dead_gen_claim(&self->flAccumulator, self->flEmitRate, dt);
    if (n <= 0)
        return;
    for (int i = 0; ; ) {
        ParticleNode *node = ring->pRingCurrent;
        *(DWORD *)&node->flLife = self->dwLifeTable[self->dwLifeIdx];
        *(DWORD *)&node->flX = self->dwPosX[self->dwPosIdx[0]];
        *(DWORD *)&node->flY = self->dwPosY[self->dwPosIdx[1]];
        *(DWORD *)&node->flZ = self->dwPosZ[self->dwPosIdx[2]];
        node->dwDiffuse = self->dwDiffuse[self->dwDiffuseIdx];
        for (int a = 0; a < 3; a++)
            node->flVel[a] = self->flVelTable[self->dwVelIdx[a]] + self->flVelBias[a];
        for (int a = 0; a < 3; a++) {
            DWORD p = self->dwPosIdx[a] + (DWORD)(a + 1);
            self->dwPosIdx[a] = (p > 499) ? 500 - p : p;
        }
        for (int a = 0; a < 3; a++) {
            DWORD v = self->dwVelIdx[a] + (DWORD)(a + 1);
            self->dwVelIdx[a] = (v > 499) ? 500 - v : v;
        }
        self->dwLifeIdx = (self->dwLifeIdx < 99) ? self->dwLifeIdx + 1 : 0;
        self->dwDiffuseIdx = (self->dwDiffuseIdx < 199) ? self->dwDiffuseIdx + 1 : 0;
        ring->pRingCurrent = node->pNext;
        if (ring->pRingCurrent == NULL)
            return;
        if (++i >= n)
            return;
    }
}

/* ─── CylinderGenerator ─── */

/* 0x44aef0 — dtor body. */
static void cyl_gen_destruct(CylinderGenerator *self)
{
    self->base.pVtable = (void **)gen_vtbl_cylinder;
    if (self->pTypeTable)
        ::operator delete(self->pTypeTable);
    base_gen_destruct(&self->base);
}

/* 0x4037e0 / 0x403810, the game's vec3 helpers: |v|^2 summed (x^2+y^2)+z^2,
 * and a.b summed (az bz + ay by) + ax bx. */
static double vec3_sqlen(const float *v)
{
    return ((double)v[0] * v[0] + (double)v[1] * v[1]) + (double)v[2] * v[2];
}

static double vec3_dot(const float *a, const float *b)
{
    return ((double)a[2] * b[2] + (double)a[1] * b[1]) + (double)a[0] * b[0];
}

/* The cosine of the angle between v and axis e, the long way the original
 * takes: both lengths stored as floats, acos (0x450eb0) of the normalised dot
 * stored as a float, then FCOS of that.  A zero v gives NaN, as it does there. */
static float direction_cosine(const float *v, const float *e)
{
    float lv = (float)sqrt(vec3_sqlen(v));
    float le = (float)sqrt(vec3_sqlen(e));
    float angle = (float)acos(vec3_dot(v, e) / ((double)le * lv));
    return (float)cos((double)angle);
}

/* 0x44b0c0 — slot 8, SetDirection.  Stores the direction as given, then
 * rebuilds flMatrix's upper 3x3 as three rows of direction cosines against the
 * world axes: a = d x r, then d itself, then b = d x a, where r is the Y axis
 * when d is exactly (1,0,0) (x87 compare: NaN counts as equal) and the X axis
 * otherwise.  d is not normalised and a, b are rounded to float, as found; the
 * fourth row and column are identity. */
static void cyl_set_direction(CylinderGenerator *self, float x, float y, float z)
{
    static const float AXIS[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    self->flDirection[0] = x;
    self->flDirection[1] = y;
    self->flDirection[2] = z;
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
    memcpy(self->flMatrix, m, sizeof m);
}

/* 0x44aeb0 — slot 6, SetPosition. */
static void cyl_set_position(CylinderGenerator *self, float x, float y, float z)
{
    self->flOrigin[0] = x;
    self->flOrigin[1] = y;
    self->flOrigin[2] = z;
}

/* 0x44b6c0 — Std's velocity table on Cylinder's fields.  It also drops the
 * type table — pointer and count zeroed WITHOUT a free, a leak — which Load's
 * type-table read then replaces.  Kept. */
static void cyl_build_velocity(CylinderGenerator *self, const float *vmin, const float *vmax,
                               float lmin, float lmax)
{
    float a[3] = { vmin[0], vmin[1], vmin[2] }, b[3] = { vmax[0], vmax[1], vmax[2] };
    build_velocity_table(self->flVelTable, a, b, lmin, lmax);
    self->dwVelIdx = 0;
    memcpy(self->flVelMin, a, sizeof a);
    memcpy(self->flVelMax, b, sizeof b);
    self->pTypeTable = NULL;
    self->dwTypeTableCount = 0;
    self->flLifeMin = lmin;
    self->flLifeMax = lmax;
}

/* 0x44ba30 — Std's rate table (0x449fa0) on Cylinder's fields. */
static void cyl_build_rate(CylinderGenerator *self, float lo, float hi)
{
    float step = (float)((double)hi * GAUSS_STEP);
    self->flEmitRateMin = lo;
    self->flEmitRateMax = hi;
    gauss_fill(self->pLifeTable, 100, lo, hi, step);
}

/* 0x44af50 — base gate, every field but the type table, then clone it (which
 * re-draws pEmitProb from the clock, as Std's copy does). */
static BOOL cyl_gen_copy_from(CylinderGenerator *self, const CylinderGenerator *src)
{
    if (!gen_copy_base(&self->base, &src->base))
        return FALSE;
    memcpy((BYTE *)self + 0x10, (const BYTE *)src + 0x10, 0x98 - 0x10);
    memcpy((BYTE *)self + 0xa0, (const BYTE *)src + 0xa0, 0x3444 - 0xa0);
    type_table_clone(&self->pTypeTable, &self->dwTypeTableCount, self->pEmitProb,
                     (const DWORD *)src->pTypeTable, src->dwTypeTableCount);
    return TRUE;
}

/* 0x44bd40 — the mirror of Load. */
static BOOL cyl_gen_save(CylinderGenerator *self, void *fp)
{
    if (!write1(self->flOrigin, 12, fp))       return FALSE;
    if (!write1(&self->flScale, 4, fp))        return FALSE;
    if (!write1(self->flDirection, 12, fp))    return FALSE;
    if (!write1(self->flVelMin, 12, fp))       return FALSE;
    if (!write1(self->flVelMax, 12, fp))       return FALSE;
    if (!write1(&self->flLifeMin, 4, fp))      return FALSE;
    if (!write1(&self->flLifeMax, 4, fp))      return FALSE;
    if (!write1(&self->flEmitRateMin, 4, fp))  return FALSE;
    if (!write1(&self->flEmitRateMax, 4, fp))  return FALSE;
    if (!write1(&self->flDtScale, 4, fp))      return FALSE;
    return type_table_save(self->pTypeTable, &self->dwTypeTableCount, fp);
}

/* 0x44be90 — parameters, then SetDirection (through the object's own vtable
 * slot 8 in the original — ours, so called directly), the velocity and rate
 * tables, and the type table.  The position table is the constructor's and is
 * never rebuilt. */
static BOOL cyl_gen_load(CylinderGenerator *self, void *fp)
{
    if (!read1(self->flOrigin, 12, fp))        return FALSE;
    if (!read1(&self->flScale, 4, fp))         return FALSE;
    if (!read1(self->flDirection, 12, fp))     return FALSE;
    if (!read1(self->flVelMin, 12, fp))        return FALSE;
    if (!read1(self->flVelMax, 12, fp))        return FALSE;
    if (!read1(&self->flLifeMin, 4, fp))       return FALSE;
    if (!read1(&self->flLifeMax, 4, fp))       return FALSE;
    if (!read1(&self->flEmitRateMin, 4, fp))   return FALSE;
    if (!read1(&self->flEmitRateMax, 4, fp))   return FALSE;
    if (!read1(&self->flDtScale, 4, fp))       return FALSE;
    if (sim_fx() == FX_FASTEMIT)
        self->flDtScale = (float)((double)self->flDtScale * 5.0);
    cyl_set_direction(self, self->flDirection[0], self->flDirection[1],
                      self->flDirection[2]);
    cyl_build_velocity(self, self->flVelMin, self->flVelMax,
                       self->flLifeMin, self->flLifeMax);
    cyl_build_rate(self, self->flEmitRateMin, self->flEmitRateMax);
    return type_table_load(&self->pTypeTable, &self->dwTypeTableCount,
                           self->pEmitProb, fp);
}

/* ─── Generator construction ─── */

/* The game's type-name strings; pName points at them as the ctors leave it. */

/* Each original ctor also builds and destroys a throwaway base-class temporary
 * on its own stack — no effect outside the frame, omitted throughout. */

/* 0x448370. */
static void base_gen_construct(Generator *self)
{
    self->pVtable = (void **)gen_vtbl_base;
    self->pName = GS_PSNAME_GENERATOR;
    self->pRing = NULL;
    self->dwEnabled = 1;
}

/* 0x449160 — only the accumulator and colour are set; the tables stay
 * uninitialised (and nothing ever fills them). */
static void point_gen_construct(PointGenerator *self)
{
    base_gen_construct(&self->base);
    self->base.pVtable = (void **)gen_vtbl_point;
    self->base.pName = GS_PSNAME_POINT_GEN;
    self->flAccumulator = 0.0f;
    self->dwDiffuse = 0xFFFFFFFF;
}

/* 0x449390 — nothing past the base is initialised. */
static void box_gen_construct(BoxGenerator *self)
{
    base_gen_construct(&self->base);
    self->base.pVtable = (void **)gen_vtbl_box;
    self->base.pName = GS_PSNAME_BOX_GEN;
}

/* 0x449670 — every member zero, except pEmitProb, which is all -1. */
static void std_gen_construct(StdGenerator *self)
{
    base_gen_construct(&self->base);
    self->base.pVtable = (void **)gen_vtbl_std;
    memset((BYTE *)self + sizeof(Generator), 0, sizeof(StdGenerator) - sizeof(Generator));
    self->base.pName = GS_PSNAME_STD_GEN;
    for (int i = 0; i < 200; i++)
        self->pEmitProb[i] = 0xFFFFFFFF;
}

/* 0x44a5c0 — Std's ctor, then both offsets zeroed. */
static void xstd_gen_construct(XStdGenerator *self)
{
    std_gen_construct(&self->base);
    self->base.base.pVtable = (void **)gen_vtbl_xstd;
    memset(self->flPosOffset, 0, sizeof self->flPosOffset);
    memset(self->flVelOffset, 0, sizeof self->flVelOffset);
    self->base.base.pName = GS_PSNAME_XSTD_GEN;
}

/* 0x44aca0 — everything zero, pEmitProb -1; SetDirection(0, 1, 0), whose
 * matrix is then overwritten with identity (the direction stays (0,1,0)); and
 * the position table is a unit circle in XZ from 500 uniform angles on
 * [0, 2 pi) — this consumes rand() at construction, and is the only place the
 * position table is ever written. */
static void cyl_gen_construct(CylinderGenerator *self)
{
    base_gen_construct(&self->base);
    self->base.pVtable = (void **)gen_vtbl_cylinder;
    memset((BYTE *)self + sizeof(Generator), 0,
           sizeof(CylinderGenerator) - sizeof(Generator));
    self->base.pName = GS_PSNAME_CYL_GEN;
    cyl_set_direction(self, 0.0f, 1.0f, 0.0f);
    static const float IDENTITY[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    memcpy(self->flMatrix, IDENTITY, sizeof IDENTITY);
    for (int i = 0; i < 200; i++)
        self->pEmitProb[i] = 0xFFFFFFFF;
    float *angle = new float[500];
    uniform_fill(angle, 500, 0.0f, 6.2831855f);
    for (int k = 0; k < 500; k++) {
        self->flPosTable[k * 3 + 0] = (float)sin((double)angle[k]);
        self->flPosTable[k * 3 + 2] = (float)cos((double)angle[k]);
    }
    delete[] angle;
}

/* Allocate `size` bytes with our own new (NULL on failure, as the game's
 * operator new returns) and run `construct` on them. */
template <typename T>
static Generator *gen_new(void (*construct)(T *))
{
    T *obj = (T *)::operator new(sizeof(T), std::nothrow);
    if (obj)
        construct(obj);
    return (Generator *)obj;
}

/* 0x4485d0 — same strcmp chain, same order, same sizes; NULL for an unknown
 * name. */
Generator *gen_create(const char *name)
{
    if (strcmp(name, "Generator") == 0)         return gen_new(base_gen_construct);
    if (strcmp(name, "PointGenerator") == 0)    return gen_new(point_gen_construct);
    if (strcmp(name, "BoxGenerator") == 0)      return gen_new(box_gen_construct);
    if (strcmp(name, "StdGenerator") == 0)      return gen_new(std_gen_construct);
    if (strcmp(name, "XStdGenerator") == 0)     return gen_new(xstd_gen_construct);
    if (strcmp(name, "CylinderGenerator") == 0) return gen_new(cyl_gen_construct);
    return NULL;
}

/* ─── The Gaussian sampler's one caller outside the particle code ───
 *
 * 0x438170 is a 15-instruction method of a class in the CvtSyms TU that is
 * otherwise untouched; it is here because it exists only to fill a 30-entry
 * table with gauss_fill, and it is the last thing keeping the sampler
 * (0x448fb0) and its density (0x448f30) alive in the game.  Only two facts
 * about its owner are needed, both visible in those instructions: the table is
 * at +0x14 and a DWORD index at +0x8c is reset. */
static void fill_gaussian_field(GaussianFieldHost *self, float mu, float sigma)
{
    gauss_fill(self->samples, 30, mu, sigma, 0.01f);
    self->cursor = 0;
}

/* ─── Cloning (0x4488b0 / 0x448a70) ─── */

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

Generator *gen_clone(const Generator *src)
{
    return (Generator *)clone_by_name(gen_create(src->pName), src);
}

Environment *env_clone(const Environment *src)
{
    return (Environment *)clone_by_name(env_create(src->pName), src);
}

/* ─── Exports — vtable thunks, installed by factory.cpp's clone table ─── */

#define THISCALL __attribute__((thiscall))

extern "C" {

/* Base Environment — all six slots, so a plain Environment never touches game
 * code either (no .par file names one, but the factory accepts the name). */
__declspec(dllexport) void *THISCALL
Env_BaseDtor(Environment *self, unsigned flags)
{
    base_env_destruct(self);
    return scalar_delete(self, flags);
}

__declspec(dllexport) BOOL THISCALL
Env_BaseCopyFrom(Environment *self, const Environment *src) { return env_same_name(self, src); }

__declspec(dllexport) BOOL THISCALL
Env_AttachRing(Environment *self, RingBuffer *ring)         { return env_attach_ring(self, ring); }

/* 0x448440 (`RET 4`) and 0x4485c0 (`return 1`) are shared with the generator
 * vtables and stay live in the game; these are our own copies. */
__declspec(dllexport) void THISCALL
Env_BaseTick(Environment *, float)                          { }

__declspec(dllexport) BOOL THISCALL
Env_BaseSave(Environment *, void *)                         { return TRUE; }

__declspec(dllexport) BOOL THISCALL
Env_BaseLoad(Environment *, void *)                         { return TRUE; }

__declspec(dllexport) void *THISCALL
Env_GravityDtor(GravityEnvironment *self, unsigned flags)
{
    gravity_env_destruct(self);
    return scalar_delete(self, flags);
}

__declspec(dllexport) void *THISCALL
Env_MagnetDtor(MagnetEnvironment *self, unsigned flags)
{
    magnet_env_destruct(self);
    return scalar_delete(self, flags);
}

__declspec(dllexport) BOOL THISCALL
Env_GravityCopyFrom(GravityEnvironment *self, const GravityEnvironment *src)
{ return gravity_env_copy_from(self, src); }

__declspec(dllexport) BOOL THISCALL
Env_MagnetCopyFrom(MagnetEnvironment *self, const MagnetEnvironment *src)
{ return magnet_env_copy_from(self, src); }

__declspec(dllexport) BOOL THISCALL
Env_GravitySave(GravityEnvironment *self, void *fp)  { return gravity_env_save(self, fp); }

__declspec(dllexport) BOOL THISCALL
Env_MagnetSave(MagnetEnvironment *self, void *fp)    { return magnet_env_save(self, fp); }

/* Generators.  The shared no-ops stand in for 0x448440 (RET 4), 0x448470
 * (RET 0xc), 0x448480 (RET 0x10) and 0x4485c0 (return 1), which stay live in
 * the game for the ParticleSystem vtables; the argument counts give the same
 * callee cleanup. */
__declspec(dllexport) void THISCALL Gen_Nop1(void *, float)                      { }
__declspec(dllexport) void THISCALL Gen_Nop3(void *, float, float, float)        { }
__declspec(dllexport) void THISCALL Gen_Nop4(void *, float, float, float, float) { }
__declspec(dllexport) BOOL THISCALL Gen_ReturnTrue(void *, void *)               { return TRUE; }

__declspec(dllexport) BOOL THISCALL
Gen_AttachRing(Generator *self, RingBuffer *ring)        { return gen_attach_ring(self, ring); }

/* 0x438170, reached by CALL_PATCHES (2 sites) — see fill_gaussian_field. */
__declspec(dllexport) void THISCALL
Gen_FillGaussianField(GaussianFieldHost *self, float mu, float sigma)
{
    fill_gaussian_field(self, mu, sigma);
}

__declspec(dllexport) BOOL THISCALL
Gen_BaseCopyFrom(Generator *self, const Generator *src)  { return gen_copy_base(self, src); }

__declspec(dllexport) void *THISCALL
Gen_BaseDtor(Generator *self, unsigned flags)
{
    base_gen_destruct(self);
    return scalar_delete(self, flags);
}

__declspec(dllexport) void *THISCALL
Gen_PointDtor(PointGenerator *self, unsigned flags)
{
    self->base.pVtable = (void **)gen_vtbl_point;
    base_gen_destruct(&self->base);
    return scalar_delete(self, flags);
}

__declspec(dllexport) void *THISCALL
Gen_BoxDtor(BoxGenerator *self, unsigned flags)
{
    self->base.pVtable = (void **)gen_vtbl_box;
    base_gen_destruct(&self->base);
    return scalar_delete(self, flags);
}

__declspec(dllexport) void *THISCALL
Gen_StdDtor(StdGenerator *self, unsigned flags)
{
    std_gen_destruct(self);
    return scalar_delete(self, flags);
}

__declspec(dllexport) void *THISCALL
Gen_XStdDtor(XStdGenerator *self, unsigned flags)
{
    xstd_gen_destruct(self);
    return scalar_delete(self, flags);
}

__declspec(dllexport) void THISCALL
Gen_PointEmit(PointGenerator *self, float dt)             { point_gen_emit(self, dt); }

__declspec(dllexport) void THISCALL
Gen_BoxEmit(BoxGenerator *self, float dt)                 { box_gen_emit(self, dt); }

__declspec(dllexport) BOOL THISCALL
Gen_StdCopyFrom(StdGenerator *self, const StdGenerator *src)   { return std_gen_copy_from(self, src); }

__declspec(dllexport) BOOL THISCALL
Gen_StdSave(StdGenerator *self, void *fp)                 { return std_gen_save(self, fp); }

__declspec(dllexport) BOOL THISCALL
Gen_StdLoad(StdGenerator *self, void *fp)                 { return std_gen_load(self, fp); }

__declspec(dllexport) BOOL THISCALL
Gen_XStdCopyFrom(XStdGenerator *self, const XStdGenerator *src) { return xstd_gen_copy_from(self, src); }

__declspec(dllexport) BOOL THISCALL
Gen_XStdSave(XStdGenerator *self, void *fp)               { return xstd_gen_save(self, fp); }

__declspec(dllexport) BOOL THISCALL
Gen_XStdLoad(XStdGenerator *self, void *fp)               { return xstd_gen_load(self, fp); }

__declspec(dllexport) void THISCALL
Gen_XStdSetPosition(XStdGenerator *self, float x, float y, float z) { xstd_set_position(self, x, y, z); }

__declspec(dllexport) void THISCALL
Gen_XStdSetVelocity(XStdGenerator *self, float x, float y, float z, float m) { xstd_set_velocity(self, x, y, z, m); }

__declspec(dllexport) void THISCALL
Gen_XStdSetDirection(XStdGenerator *self, float x, float y, float z) { xstd_set_direction(self, x, y, z); }

__declspec(dllexport) void THISCALL
Gen_XStdSetSpeed(XStdGenerator *self, float m)            { xstd_set_speed(self, m); }

__declspec(dllexport) void *THISCALL
Gen_CylDtor(CylinderGenerator *self, unsigned flags)
{
    cyl_gen_destruct(self);
    return scalar_delete(self, flags);
}

__declspec(dllexport) BOOL THISCALL
Gen_CylCopyFrom(CylinderGenerator *self, const CylinderGenerator *src) { return cyl_gen_copy_from(self, src); }

__declspec(dllexport) BOOL THISCALL
Gen_CylSave(CylinderGenerator *self, void *fp)            { return cyl_gen_save(self, fp); }

__declspec(dllexport) BOOL THISCALL
Gen_CylLoad(CylinderGenerator *self, void *fp)            { return cyl_gen_load(self, fp); }

__declspec(dllexport) void THISCALL
Gen_CylSetPosition(CylinderGenerator *self, float x, float y, float z) { cyl_set_position(self, x, y, z); }

__declspec(dllexport) void THISCALL
Gen_CylSetDirection(CylinderGenerator *self, float x, float y, float z) { cyl_set_direction(self, x, y, z); }

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

/* ─── Our vtables ──────────────────────────────────────────────────────────
 *
 * One table per class, in the game's slot order, installed by the constructors
 * above.  These replace both the vtable patches (gone since E2) and the clone
 * machinery in factory.cpp (gone for these classes since E5): an object of ours
 * carries a DLL address at +0x00 and every virtual call the game makes on it
 * lands directly on our code.
 *
 * Built by hand rather than with C++ virtuals on purpose (§ 6.6): the game
 * calls `(*(code **)(*obj + 0x0c))(...)` with MSVC __thiscall, and neither
 * mingw's slot order nor its calling convention is guaranteed to match.  Each
 * entry is one of the __attribute__((thiscall)) exports defined above.
 *
 * The originals stay UD2-stubbed, so a slot we get wrong still dies loudly
 * rather than silently doing the wrong thing — but only if it reaches a
 * *stubbed* function.  The static_asserts below are the guard against the
 * other mistake, an initialiser that is one entry short: a missing slot would
 * otherwise be a silent NULL.  Slot meanings are in generators.h. */

extern void *const gen_vtbl_base[] = {
    (void *)Gen_BaseDtor,  (void *)Gen_BaseCopyFrom, (void *)Gen_AttachRing,
    (void *)Gen_Nop1,      (void *)Gen_ReturnTrue,   (void *)Gen_ReturnTrue,
    (void *)Gen_Nop3,      (void *)Gen_Nop4,         (void *)Gen_Nop3,
    (void *)Gen_Nop1,
};

extern void *const gen_vtbl_point[] = {
    (void *)Gen_PointDtor, (void *)Gen_BaseCopyFrom, (void *)Gen_AttachRing,
    (void *)Gen_PointEmit, (void *)Gen_ReturnTrue,   (void *)Gen_ReturnTrue,
    (void *)Gen_Nop3,      (void *)Gen_Nop4,         (void *)Gen_Nop3,
    (void *)Gen_Nop1,
};

extern void *const gen_vtbl_box[] = {
    (void *)Gen_BoxDtor,   (void *)Gen_BaseCopyFrom, (void *)Gen_AttachRing,
    (void *)Gen_BoxEmit,   (void *)Gen_ReturnTrue,   (void *)Gen_ReturnTrue,
    (void *)Gen_Nop3,      (void *)Gen_Nop4,         (void *)Gen_Nop3,
    (void *)Gen_Nop1,
};

extern void *const gen_vtbl_std[] = {
    (void *)Gen_StdDtor,   (void *)Gen_StdCopyFrom,  (void *)Gen_AttachRing,
    (void *)Gen_StdEmit,   (void *)Gen_StdSave,      (void *)Gen_StdLoad,
    (void *)Gen_Nop3,      (void *)Gen_Nop4,         (void *)Gen_Nop3,
    (void *)Gen_Nop1,
};

extern void *const gen_vtbl_xstd[] = {
    (void *)Gen_XStdDtor,  (void *)Gen_XStdCopyFrom, (void *)Gen_AttachRing,
    (void *)Gen_XStdEmit,  (void *)Gen_XStdSave,     (void *)Gen_XStdLoad,
    (void *)Gen_XStdSetPosition, (void *)Gen_XStdSetVelocity,
    (void *)Gen_XStdSetDirection, (void *)Gen_XStdSetSpeed,
};

extern void *const gen_vtbl_cylinder[] = {
    (void *)Gen_CylDtor,   (void *)Gen_CylCopyFrom,  (void *)Gen_AttachRing,
    (void *)Gen_CylinderEmit, (void *)Gen_CylSave,   (void *)Gen_CylLoad,
    (void *)Gen_CylSetPosition, (void *)Gen_Nop4,
    (void *)Gen_CylSetDirection, (void *)Gen_Nop1,
};

extern void *const env_vtbl_base[] = {
    (void *)Env_BaseDtor,    (void *)Env_BaseCopyFrom,    (void *)Env_AttachRing,
    (void *)Env_BaseTick,    (void *)Env_BaseSave,        (void *)Env_BaseLoad,
};

extern void *const env_vtbl_gravity[] = {
    (void *)Env_GravityDtor, (void *)Env_GravityCopyFrom, (void *)Env_AttachRing,
    (void *)Env_GravityTick, (void *)Env_GravitySave,     (void *)Env_GravityLoad,
};

extern void *const env_vtbl_magnet[] = {
    (void *)Env_MagnetDtor,  (void *)Env_MagnetCopyFrom,  (void *)Env_AttachRing,
    (void *)Env_MagnetTick,  (void *)Env_MagnetSave,      (void *)Env_MagnetLoad,
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

/* ─── Dispatch ─────────────────────────────────────────────────────────────
 *
 * One virtual call.  Generators and Environments share slot 3 (Tick / emit)
 * and both carry one of the tables above, so this is a direct call into this
 * DLL — no class switch, no identity lookup, no trampoline.  It replaces the
 * sim_tick_generator / sim_tick_environment pair and the Stage D "is this
 * vtable ours?" test they needed. */

typedef void (THISCALL *sim_tick_fn)(void *, float);

void sim_tick_slot3(void *obj, float dt)
{
    void **vtbl = *(void ***)obj;
    ((sim_tick_fn)vtbl[GEN_VT_TICK_SLOT])(obj, dt);
}
