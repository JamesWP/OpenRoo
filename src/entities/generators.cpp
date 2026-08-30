/* Particle simulation reimplementation — Stage C (PARTICLE_PLAN.md § 4).
 *
 * Stage A/B took the render path and the ParticleSystem-level tick; this file
 * takes the simulation underneath.  Because Stage B's Particle_BaseTick already
 * dispatches pGenerator->vtbl[3](dt) and pEnvironment->vtbl[3](dt) by pointer,
 * each replacement here is just a vtable slot swap plus a UD2 stub.
 *
 * C1 (this file, so far):
 *   0x44c450 GravityEnvironment::TickUpdate  → Env_GravityTick   (vtbl 0x45f110 slot 3)
 *
 * The ring is one NULL-terminated doubly-linked list, partitioned as
 * [pRingHead, pRingCurrent) live and [pRingCurrent, pRingTail] free.  The
 * environment owns ageing and retirement; the generator owns emission.  Ring
 * *allocation* stays game-owned — we only honour the contract.
 *
 * KAROO_PARTICLE_FX modes added here:
 *   gravity — multiply flGravity x5, so particles visibly plummet
 *   nolife  — skip the flLife decrement, so nothing expires (also the
 *             ring-contract stress test: emission must stall, not corrupt)
 */
#include "generators.h"
#include "log.h"
#include <math.h>

#define SIM_LOG_FIRST 8

/* ─── FX ─── */

enum SimFx { FX_NONE = 0, FX_GRAVITY, FX_NOLIFE };

static SimFx sim_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = FX_NONE;
        if (GetEnvironmentVariableA("KAROO_PARTICLE_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "gravity") == 0)     cached = FX_GRAVITY;
            else if (lstrcmpiA(buf, "nolife") == 0) cached = FX_NOLIFE;
        }
        if (cached != FX_NONE)
            log_write("sim: FX mode = %s\n", cached == FX_GRAVITY ? "gravity" : "nolife");
    }
    return (SimFx)cached;
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
    float scale = (fx == FX_GRAVITY) ? 5.0f : 1.0f;
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

} // extern "C"
