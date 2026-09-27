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

/* ─── MagnetEnvironment tick ─── */

/* Per-axis "has arrived" test, in branch form rather than fabsf(d) <= half —
 * the two differ if a half-extent is ever negative. */
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
    // antigrav repels instead of attracting — the magnet equivalent of the
    // inverted gravity, and just as visible on a shield effect.
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
                retire = true;  // arrived at the magnet
            } else {
                // Unguarded division: a zero-length d has already matched the
                // extent test above and been retired, so this can't divide by
                // zero.
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

    int count = (int)acc;  // truncates toward zero
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

        // Claim the node: advance the free-list cursor past it.
        self->base.pRing->pRingCurrent = node->pNext;
        ring = self->base.pRing;
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

        // Sample -> scale -> transform -> offset.
        float p[3] = { pos[0] * self->flScale,
                       pos[1] * self->flScale,
                       pos[2] * self->flScale };
        float t[3];
        transform_point_row(t, p, self->flMatrix);
        node->flX = t[0] + self->flOrigin[0];
        node->flY = t[1] + self->flOrigin[1];
        node->flZ = t[2] + self->flOrigin[2];

        // Velocity is NOT run through the matrix.
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

/* One fread of `size` bytes; on a short read this leaves whatever was already
 * read in place and returns false. */
static bool read1(void *dst, unsigned size, void *fp)
{
    return hooks_fread(dst, size, 1, fp) == 1;
}

/* Stores flDirection/flMagnitude as given, and flGravity =
 * normalise(direction) * magnitude, or direction itself when it is exactly
 * zero. */
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

static void gravity_set_colour(GravityEnvironment *self, DWORD argb, float fade)
{
    self->dwTargetARGB   = argb;
    self->dwTargetA      = argb >> 24;
    self->dwTargetRGB[0] = (argb >> 16) & 0xff;
    self->dwTargetRGB[2] = argb & 0xff;
    self->dwTargetRGB[1] = (argb >> 8) & 0xff;
    self->flFadeRate     = fade;
}

/* Leaves flFadeAccum as constructed — unlike MagnetEnvironment::Load, which
 * resets it every time. */
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

/* FORMAT: on disk, force precedes centre.  PRESERVED: dwTargetRGB is never
 * loaded — it keeps whatever the constructor or a CopyFrom set, not what a
 * saved file may have recorded. */
static BOOL magnet_env_load(MagnetEnvironment *self, void *fp)
{
    // The base Environment::Load contributes nothing; its result is not
    // checked.
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

/* The game's static-CRT fwrite; other files reach the same stream the same
 * way, so this is not a private handle. */

static bool write1(const void *src, unsigned size, void *fp)
{
    return fwrite(src, size, 1, (FILE *)fp) == 1;
}

/* Field-for-field mirror of Load, in the same order. */
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

/* Calls the base Environment::Save (a no-op) first, then mirrors Load.
 * PRESERVED: dwTargetRGB is not written either, matching Load. */
static BOOL magnet_env_save(MagnetEnvironment *self, void *fp)
{
    if (!write1(self->flForce, 12, fp))          return FALSE;
    if (!write1(self->flCentre, 12, fp))         return FALSE;
    if (!write1(&self->flRange, 4, fp))          return FALSE;
    if (!write1(&self->flFadeRate, 4, fp))       return FALSE;
    return write1(&self->dwFadeThreshold, 4, fp);
}

/* The base Environment::CopyFrom only gates on type name; it copies nothing
 * itself. */
static BOOL env_same_name(const Environment *self, const Environment *src)
{
    return strcmp(src->pName, self->pName) == 0;
}

/* Copies every field past the base (vtable, pName and pRing keep the
 * destination's) in one memcpy; safe because src can never alias dst. */
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

static void base_env_destruct(Environment *self)
{
    self->pVtable = (void **)env_vtbl_base;
}

/* Restores the game-facing vtable, then runs the base body.  Nothing is freed
 * here: pName is not owned by either class. */
static void gravity_env_destruct(GravityEnvironment *self)
{
    self->base.pVtable = (void **)env_vtbl_gravity;
    self->base.pVtable = (void **)env_vtbl_base;
}

/* PRESERVED: runs the base body twice (once on each of the original's exit
 * paths) — harmless, kept rather than collapsed to one store. */
static void magnet_env_destruct(MagnetEnvironment *self)
{
    self->base.pVtable = (void **)env_vtbl_magnet;
    self->base.pVtable = (void **)env_vtbl_base;
    self->base.pVtable = (void **)env_vtbl_base;
}

/* The scalar deleting dtors' shared tail is factory.h's scalar_delete<T>. */

/* Refuses (and leaves pRing alone) when handed a NULL ring. */
static BOOL env_attach_ring(Environment *self, RingBuffer *ring)
{
    if (ring == NULL)
        return FALSE;
    self->pRing = ring;
    return TRUE;
}

/* Each constructor also built and destroyed a throwaway base-class instance on
 * its own stack; it has no effect outside that frame and isn't reproduced
 * here. */

/* Static type-name strings; pName points at them, never owned or freed here.
 */

static void base_env_construct(Environment *self)
{
    self->pVtable = (void **)env_vtbl_base;
    self->pName   = GS_PSNAME_ENVIRONMENT;
    self->pRing   = NULL;
}

/* Base ctor, class vtable, then every member zeroed except the fade threshold,
 * which starts at 10. */
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

/* Matches names exactly, as the game's factory did: an unknown name, or a
 * failed allocation, both return NULL. */
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

static void base_gen_destruct(Generator *self)
{
    self->pVtable = (void **)gen_vtbl_base;
}

/* The scalar deleting dtors' shared tail is factory.h's scalar_delete<T>. */

/* Gates on matching type name, then copies only dwEnabled. */
static BOOL gen_copy_base(Generator *self, const Generator *src)
{
    if (strcmp(src->pName, self->pName) != 0)
        return FALSE;
    self->dwEnabled = src->dwEnabled;
    return TRUE;
}

/* Slot 2 of all six generator vtables. */
static BOOL gen_attach_ring(Generator *self, RingBuffer *ring)
{
    if (ring == NULL)
        return FALSE;
    self->pRing = ring;
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

static void std_clone_type_table(StdGenerator *self, const DWORD *src, DWORD count)
{
    type_table_clone(&self->pTypeTable, &self->dwTypeTableCount, self->pEmitProb,
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

static void std_build_rate(StdGenerator *self, float lo, float hi)
{
    float step = (float)((double)hi * GAUSS_STEP);
    self->flEmitRateMin = lo;
    self->flEmitRateMax = hi;
    gauss_fill(self->pLifeTable, 100, lo, hi, step);
}

static void std_gen_destruct(StdGenerator *self)
{
    self->base.pVtable = (void **)gen_vtbl_std;
    if (self->pTypeTable)
        ::operator delete(self->pTypeTable);
    base_gen_destruct(&self->base);
}

/* Copies every field but the type table, which is cloned instead — cloning
 * redraws pEmitProb from a fresh seed, so a copy's colours differ from its
 * source's. */
static BOOL std_gen_copy_from(StdGenerator *self, const StdGenerator *src)
{
    if (!gen_copy_base(&self->base, &src->base))
        return FALSE;
    memcpy((BYTE *)self + 0x10, (const BYTE *)src + 0x10, 0x70 - 0x10);
    memcpy((BYTE *)self + 0x78, (const BYTE *)src + 0x78, 0x3420 - 0x78);
    std_clone_type_table(self, (const DWORD *)src->pTypeTable, src->dwTypeTableCount);
    return TRUE;
}

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

/* Builds the sphere or box position table (whichever dwEmitMode selects), then
 * always rebuilds the velocity and rate tables. */
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

/* Own vtable first, then Std's dtor body. */
static void xstd_gen_destruct(XStdGenerator *self)
{
    self->base.base.pVtable = (void **)gen_vtbl_xstd;
    std_gen_destruct(&self->base);
}

/* PRESERVED: copies flPosOffset but not flVelOffset — a copied generator keeps
 * its own velocity offset regardless of the source's. */
static BOOL xstd_gen_copy_from(XStdGenerator *self, const XStdGenerator *src)
{
    if (!std_gen_copy_from(&self->base, &src->base))
        return FALSE;
    memcpy(self->flPosOffset, src->flPosOffset, sizeof self->flPosOffset);
    return TRUE;
}

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

static void xstd_set_position(XStdGenerator *self, float x, float y, float z)
{
    self->flPosOffset[0] = x;
    self->flPosOffset[1] = y;
    self->flPosOffset[2] = z;
}

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

/* Keeps the current speed, takes the new direction.  Zero speed becomes
 * FLT_EPSILON; a zero direction zeroes the velocity. */
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

/* PRESERVED: if the current velocity offset is already zero, this leaves it
 * zero and ignores every argument — direction, speed, all of it. */
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

/* Zero speed becomes FLT_EPSILON.  If the current velocity is already zero
 * there is no direction to preserve, so it is left as-is. */
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
        self->dwVelIdx[0] = (i0 >= 1000) ? 1000 - i0 : i0;  // wraps at 1000, not 999 or 998, like the next two indices
        self->dwVelIdx[1] = (i1 >= 999)  ? 1000 - i1 : i1;
        self->dwVelIdx[2] = (i2 >= 998)  ? 1000 - i2 : i2;
        self->dwLifeIdx = (self->dwLifeIdx > 100) ? 0 : self->dwLifeIdx + 1;  // runs 0..101 — two entries past the 100-entry table
        ring->pRingCurrent = node->pNext;
        if (ring->pRingCurrent == NULL)
            return;
        if (++i >= n)
            return;
    }
}

/* Position and colour are raw table copies; velocity is table plus bias.  Both
 * index triples wrap by 500 - i past 499. */
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

static void cyl_gen_destruct(CylinderGenerator *self)
{
    self->base.pVtable = (void **)gen_vtbl_cylinder;
    if (self->pTypeTable)
        ::operator delete(self->pTypeTable);
    base_gen_destruct(&self->base);
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

static void cyl_set_position(CylinderGenerator *self, float x, float y, float z)
{
    self->flOrigin[0] = x;
    self->flOrigin[1] = y;
    self->flOrigin[2] = z;
}

/* PRESERVED: drops the type table by zeroing the pointer and count without
 * freeing it first — a leak on every reload, papered over because Load's own
 * type-table read immediately replaces both. */
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

/* Std's rate-table builder (std_build_rate), run on Cylinder's own fields. */
static void cyl_build_rate(CylinderGenerator *self, float lo, float hi)
{
    float step = (float)((double)hi * GAUSS_STEP);
    self->flEmitRateMin = lo;
    self->flEmitRateMax = hi;
    gauss_fill(self->pLifeTable, 100, lo, hi, step);
}

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

/* Reads its own direction back out to rebuild the matrix via
 * cyl_set_direction.  The position table (the unit circle) is the
 * constructor's and is never rebuilt here. */
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

static void base_gen_construct(Generator *self)
{
    self->pVtable = (void **)gen_vtbl_base;
    self->pName = GS_PSNAME_GENERATOR;
    self->pRing = NULL;
    self->dwEnabled = 1;
}

/* PRESERVED: only the accumulator and diffuse colour are initialised — the
 * position, velocity and life tables are left uninitialised, and nothing in
 * this file ever fills them. */
static void point_gen_construct(PointGenerator *self)
{
    base_gen_construct(&self->base);
    self->base.pVtable = (void **)gen_vtbl_point;
    self->base.pName = GS_PSNAME_POINT_GEN;
    self->flAccumulator = 0.0f;
    self->dwDiffuse = 0xFFFFFFFF;
}

/* PRESERVED: nothing past the base class is initialised. */
static void box_gen_construct(BoxGenerator *self)
{
    base_gen_construct(&self->base);
    self->base.pVtable = (void **)gen_vtbl_box;
    self->base.pName = GS_PSNAME_BOX_GEN;
}

static void std_gen_construct(StdGenerator *self)
{
    base_gen_construct(&self->base);
    self->base.pVtable = (void **)gen_vtbl_std;
    memset((BYTE *)self + sizeof(Generator), 0, sizeof(StdGenerator) - sizeof(Generator));
    self->base.pName = GS_PSNAME_STD_GEN;
    for (int i = 0; i < 200; i++)
        self->pEmitProb[i] = 0xFFFFFFFF;
}

static void xstd_gen_construct(XStdGenerator *self)
{
    std_gen_construct(&self->base);
    self->base.base.pVtable = (void **)gen_vtbl_xstd;
    memset(self->flPosOffset, 0, sizeof self->flPosOffset);
    memset(self->flVelOffset, 0, sizeof self->flVelOffset);
    self->base.base.pName = GS_PSNAME_XSTD_GEN;
}

/* DETERMINISM: builds the position table from 500 angles drawn uniformly by
 * uniform_fill, so constructing a CylinderGenerator consumes rand() calls; its
 * order relative to other construction matters for replays.
 *
 * PRESERVED: calls SetDirection(0,1,0) to set flDirection, then immediately
 * overwrites the matrix it built with plain identity — so flDirection reads
 * (0,1,0) but flMatrix does not reflect it until SetDirection is called again.
 */
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

template <typename T>
static Generator *gen_new(void (*construct)(T *))
{
    T *obj = (T *)::operator new(sizeof(T), std::nothrow);
    if (obj)
        construct(obj);
    return (Generator *)obj;
}

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

Generator *gen_clone(const Generator *src)
{
    return (Generator *)clone_by_name(gen_create(src->pName), src);
}

Environment *env_clone(const Environment *src)
{
    return (Environment *)clone_by_name(env_create(src->pName), src);
}

/* ─── Exports — vtable thunks ─── */

#define THISCALL __attribute__((thiscall))

extern "C" {

/* All six slots, so a plain Environment never falls through to unimplemented
 * behaviour either. */
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

/* Shared no-op bodies for the base classes' do-nothing slots; the argument
 * counts match so callee cleanup stays correct under thiscall. */
__declspec(dllexport) void THISCALL Gen_Nop1(void *, float)                      { }
__declspec(dllexport) void THISCALL Gen_Nop3(void *, float, float, float)        { }
__declspec(dllexport) void THISCALL Gen_Nop4(void *, float, float, float, float) { }
__declspec(dllexport) BOOL THISCALL Gen_ReturnTrue(void *, void *)               { return TRUE; }

__declspec(dllexport) BOOL THISCALL
Gen_AttachRing(Generator *self, RingBuffer *ring)        { return gen_attach_ring(self, ring); }

/* Called directly by explodedebris.cpp and theme.cpp — see
 * fill_gaussian_field. */
__declspec(dllexport) void THISCALL
Gen_FillGaussianField(ExplodeDebris *self, float mu, float sigma)
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

}  // extern "C"

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

/* Generators and Environments share slot 3 (Tick / emit) and both carry one of
 * the tables above, so this is a direct call into this DLL — no class switch
 * or identity lookup needed. */

typedef void (THISCALL *sim_tick_fn)(void *, float);

void sim_tick_slot3(void *obj, float dt)
{
    void **vtbl = *(void ***)obj;
    ((sim_tick_fn)vtbl[GEN_VT_TICK_SLOT])(obj, dt);
}
