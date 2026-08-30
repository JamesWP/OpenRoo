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
 */
#include "generators.h"
#include "log.h"
#include <math.h>

#define SIM_LOG_FIRST 8

/* ─── FX ─── */

enum SimFx { FX_NONE = 0, FX_GRAVITY, FX_NOLIFE, FX_ANTIGRAV, FX_BURST };

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

/* ─── Exports ─── */

#define THISCALL __attribute__((thiscall))

extern "C" {

__declspec(dllexport) void THISCALL
Env_GravityTick(GravityEnvironment *self, float dt)
{
    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SIM_LOG_FIRST) {
        RingBuffer *ring = self->base.pRing;
        DWORD live = 0;
        for (ParticleNode *n = ring->pRingHead;
             n && n != ring->pRingCurrent; n = n->pNext)
            live++;
        log_write("sim: GravityTick this=%p dt=%f live=%lu ring=%lu\n",
                  self, dt, live, ring->dwRingCount);
    }
    gravity_tick(self, dt);
}

__declspec(dllexport) void THISCALL
Env_MagnetTick(MagnetEnvironment *self, float dt)
{
    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SIM_LOG_FIRST) {
        RingBuffer *ring = self->base.pRing;
        DWORD live = 0;
        for (ParticleNode *n = ring->pRingHead;
             n && n != ring->pRingCurrent; n = n->pNext)
            live++;
        log_write("sim: MagnetTick this=%p dt=%f live=%lu ring=%lu centre=%f,%f,%f\n",
                  self, dt, live, ring->dwRingCount,
                  self->flCentre[0], self->flCentre[1], self->flCentre[2]);
    }
    magnet_tick(self, dt);
}

__declspec(dllexport) void THISCALL
Gen_StdEmit(StdGenerator *self, float dt)
{
    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SIM_LOG_FIRST) {
        RingBuffer *ring = self->base.pRing;
        DWORD free_nodes = 0;
        for (ParticleNode *n = ring->pRingCurrent; n; n = n->pNext)
            free_nodes++;
        log_write("sim: StdEmit this=%p dt=%f enabled=%lu accum=%f free=%lu ring=%lu\n",
                  self, dt, self->base.dwEnabled, self->flAccumulator,
                  free_nodes, ring->dwRingCount);
    }
    std_emit(self, dt, NULL, NULL);
}

__declspec(dllexport) void THISCALL
Gen_CylinderEmit(CylinderGenerator *self, float dt)
{
    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SIM_LOG_FIRST)
        log_write("sim: CylinderEmit this=%p dt=%f enabled=%lu accum=%f "
                  "origin=%f,%f,%f scale=%f ring=%lu\n",
                  self, dt, self->base.dwEnabled, self->flAccumulator,
                  self->flOrigin[0], self->flOrigin[1], self->flOrigin[2],
                  self->flScale, self->base.pRing->dwRingCount);
    cylinder_emit(self, dt);
}

__declspec(dllexport) void THISCALL
Gen_XStdEmit(XStdGenerator *self, float dt)
{
    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SIM_LOG_FIRST)
        log_write("sim: XStdEmit this=%p dt=%f enabled=%lu posoff=%f,%f,%f "
                  "veloff=%f,%f,%f ring=%lu\n",
                  self, dt, self->base.base.dwEnabled,
                  self->flPosOffset[0], self->flPosOffset[1], self->flPosOffset[2],
                  self->flVelOffset[0], self->flVelOffset[1], self->flVelOffset[2],
                  self->base.base.pRing->dwRingCount);
    std_emit(&self->base, dt, self->flPosOffset, self->flVelOffset);
}

} // extern "C"
