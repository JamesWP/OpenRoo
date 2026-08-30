#pragma once
#include <windows.h>
#include <stddef.h>
#include "particles.h"

/* Generator / Environment simulation structs — PARTICLE_PLAN.md § 4.
 *
 * Layouts from the corrected Ghidra project (see § 4.1 for the four facts that
 * were previously documented wrongly).  Only the fields the per-frame tick
 * touches are asserted; table construction (Save/Load/Copy) stays game-owned.
 */

/* Embedded at ParticleSystem+0x08; both Generator and Environment hold a
 * pointer to it (Generator+0x0C, Environment+0x08), installed by
 * AttachGeneratorRing (0x4483c0) / AttachEnvironmentRing (0x4484e0).
 *
 *   pRingBase ─ … ─ pRingHead ─ … ─ pRingCurrent ─ … ─ pRingTail ─╴NULL
 *                   └─── live ───┘  └───── free ──────┘
 */
struct RingBuffer {
    DWORD         dwRingCount;    // +0x00
    ParticleNode *pRingBase;      // +0x04
    ParticleNode *pRingHead;      // +0x08 oldest live particle
    ParticleNode *pRingTail;      // +0x0c last free node
    ParticleNode *pRingCurrent;   // +0x10 next node to emit into
};
static_assert(sizeof(RingBuffer) == 0x14, "RingBuffer size");
static_assert(offsetof(RingBuffer, pRingCurrent) == 0x10, "RingBuffer layout");

/* Base class of every emitter.  Note +0x0C is the ring back-pointer, NOT
 * padding, and dwEnabled exists only here — Environment has no such flag. */
struct Generator {
    void      **pVtable;          // +0x00 8-slot vtable
    char       *pName;            // +0x04
    DWORD       dwEnabled;        // +0x08 toggled by Enable/DisableRenderNode
    RingBuffer *pRing;            // +0x0c
};
static_assert(sizeof(Generator) == 0x10, "Generator size");
static_assert(offsetof(Generator, pRing) == 0x0c, "Generator layout");

/* Base class of every environment.  12 bytes: the ring pointer sits at +0x08,
 * where Generator keeps dwEnabled. */
struct Environment {
    void      **pVtable;          // +0x00 6-slot vtable (not 8)
    char       *pName;            // +0x04
    RingBuffer *pRing;            // +0x08
};
static_assert(sizeof(Environment) == 0x0c, "Environment size");
static_assert(offsetof(Environment, pRing) == 0x08, "Environment layout");

/* GravityEnvironment (0x6c) — vtable 0x45f110, tick 0x44c450.
 * Fields below +0x1C recovered from the tick; +0x0c..+0x1b is never read by
 * it and stays opaque until Save/Load (0x44c6c0/0x44c7f0) are decoded. */
struct GravityEnvironment {
    Environment base;             // +0x00
    BYTE        opaque0c[0x10];   // +0x0c serialised, unread by the tick
    float       flGravity[3];     // +0x1c acceleration per second
    BYTE        opaque28[0x08];   // +0x28 unread by the tick
    DWORD       dwTargetRGB[3];   // +0x30 fade targets: R, G, B
    float       flFadeRate;       // +0x3c
    DWORD       dwFadeThreshold;  // +0x40 step must exceed this to apply
    DWORD       dwClipEnable[3];  // +0x44 per-axis kill-plane enables
    float       flClipMax[3];     // +0x50
    float       flClipMin[3];     // +0x5c
    float       flFadeAccum;      // +0x68
};
static_assert(offsetof(GravityEnvironment, flGravity)    == 0x1c, "Gravity layout");
static_assert(offsetof(GravityEnvironment, dwTargetRGB)  == 0x30, "Gravity layout");
static_assert(offsetof(GravityEnvironment, flFadeRate)   == 0x3c, "Gravity layout");
static_assert(offsetof(GravityEnvironment, dwClipEnable) == 0x44, "Gravity layout");
static_assert(offsetof(GravityEnvironment, flClipMax)    == 0x50, "Gravity layout");
static_assert(offsetof(GravityEnvironment, flClipMin)    == 0x5c, "Gravity layout");
static_assert(offsetof(GravityEnvironment, flFadeAccum)  == 0x68, "Gravity layout");
static_assert(sizeof(GravityEnvironment) == 0x6c, "Gravity size");

/* MagnetEnvironment (0x50) — vtable 0x45f128, tick 0x44cca0.
 * Same life/fade/retire skeleton as Gravity; gravity acceleration is replaced
 * by attraction toward flCentre, and a particle that arrives inside the
 * half-extent box is retired.  flRange/dwField30 are serialised but never read
 * by the tick. */
struct MagnetEnvironment {
    Environment base;             // +0x00
    float       flCentre[3];      // +0x0c attraction target
    float       flForce[3];       // +0x18 per-axis force magnitude
    float       flHalfExtent[3];  // +0x24 arrival box; inside -> retire
    float       flRange;          // +0x30 unread by the tick
    DWORD       dwField34;        // +0x34 unread by the tick
    DWORD       dwTargetRGB[3];   // +0x38 fade targets: R, G, B
    float       flFadeRate;       // +0x44
    DWORD       dwFadeThreshold;  // +0x48
    float       flFadeAccum;      // +0x4c
};
static_assert(offsetof(MagnetEnvironment, flCentre)       == 0x0c, "Magnet layout");
static_assert(offsetof(MagnetEnvironment, flForce)        == 0x18, "Magnet layout");
static_assert(offsetof(MagnetEnvironment, flHalfExtent)   == 0x24, "Magnet layout");
static_assert(offsetof(MagnetEnvironment, dwTargetRGB)    == 0x38, "Magnet layout");
static_assert(offsetof(MagnetEnvironment, flFadeRate)     == 0x44, "Magnet layout");
static_assert(offsetof(MagnetEnvironment, dwFadeThreshold)== 0x48, "Magnet layout");
static_assert(offsetof(MagnetEnvironment, flFadeAccum)    == 0x4c, "Magnet layout");
static_assert(sizeof(MagnetEnvironment) == 0x50, "Magnet size");

/* StdGenerator (0x3420) — vtable 0x45f094, emit 0x449fe0.  The default emitter
 * (44 of the 62 shipping .par files).
 *
 * The four lookup tables tile the struct exactly from +0x78 to +0x3408 with no
 * gaps, which is what pins the layout: emit only ever indexes them, never
 * recomputes the values.  Table *construction* stays game-owned — Load (slot 5)
 * fills them from the .par parameters and we do not touch it.
 *
 * NOTE the names: flPosTable was "pSphTable" and flVelTable "pBoxTable" in
 * earlier RE, which had position and velocity the wrong way round.  Emit writes
 * flPosTable to node+0x08 and flVelTable to node+0x14, and the shipped render
 * fill reads node+0x08 as the position. */
struct StdGenerator {
    Generator base;               // +0x0000
    float     flDtScale;          // +0x0010 accumulator rate
    DWORD     dwEmitMode;         // +0x0014 0 = sphere, 1 = box (shapes the tables)
    float     flSphMin[3];        // +0x0018 } serialised emit-shape parameters,
    float     flSphMax[3];        // +0x0024 } consumed by Load when it builds
    float     flBoxMin[3];        // +0x0030 } the tables; emit never reads them
    float     flBoxMax[3];        // +0x003c }
    float     flVelMin[3];        // +0x0048 }
    float     flVelMax[3];        // +0x0054 }
    float     flLifeMin;          // +0x0060
    float     flLifeMax;          // +0x0064
    float     flEmitRateMin;      // +0x0068
    float     flEmitRateMax;      // +0x006c
    void     *pTypeTable;         // +0x0070 heap; owned by Load/Copy/dtor
    DWORD     dwTypeTableCount;   // +0x0074
    float     flPosTable[1500];   // +0x0078 500 x xyz emit positions
    float     flVelTable[1500];   // +0x17e8 500 x xyz emit velocities
    float     pLifeTable[100];    // +0x2f58
    DWORD     pEmitProb[200];     // +0x30e8 emitted diffuse colours
    DWORD     dwCtr0;             // +0x3408
    float     flAccumulator;      // +0x340c fractional emission carry
    DWORD     dwPosIdx;           // +0x3410 step 1, wrap 500
    DWORD     dwVelIdx;           // +0x3414 step 3, wrap 500
    DWORD     dwLifeIdx;          // +0x3418 step 1, wrap 100
    DWORD     dwProbIdx;          // +0x341c step 1, wrap 200
};
static_assert(offsetof(StdGenerator, flDtScale)     == 0x0010, "Std layout");
static_assert(offsetof(StdGenerator, flPosTable)    == 0x0078, "Std layout");
static_assert(offsetof(StdGenerator, flVelTable)    == 0x17e8, "Std layout");
static_assert(offsetof(StdGenerator, pLifeTable)    == 0x2f58, "Std layout");
static_assert(offsetof(StdGenerator, pEmitProb)     == 0x30e8, "Std layout");
static_assert(offsetof(StdGenerator, flAccumulator) == 0x340c, "Std layout");
static_assert(offsetof(StdGenerator, dwPosIdx)      == 0x3410, "Std layout");
static_assert(offsetof(StdGenerator, dwProbIdx)     == 0x341c, "Std layout");
static_assert(sizeof(StdGenerator) == 0x3420, "Std size");

/* CylinderGenerator (0x3444) — vtable 0x45f0e8, emit 0x44ba70.
 * Portal / exit emitters (8 shipping .par files).
 *
 * Same claim-and-advance skeleton and the same four index cadences as
 * StdGenerator, but the sampled position is scaled, run through a 4x4 matrix
 * and then offset by flOrigin.  Velocity is taken straight from its table and
 * is NOT transformed.
 *
 * flMatrix is row-major and applied as the row vector (v,1) x M — the same
 * convention as Stage B's corner transforms, confirmed from the decompiled
 * inner loop (out[c] = sum_r M[r][c] * v[r]). */
struct CylinderGenerator {
    Generator base;               // +0x0000
    float     flOrigin[3];        // +0x0010 added after the transform
    BYTE      opaque1c[0x0c];     // +0x001c unread by emit
    float     flScale;            // +0x0028 applied before the transform
    float     flMatrix[16];       // +0x002c row-major 4x4
    BYTE      opaque6c[0x28];     // +0x006c unread by emit
    float     flDtScale;          // +0x0094
    BYTE      opaque98[0x08];     // +0x0098 unread by emit
    float     flAccumulator;      // +0x00a0
    float     flPosTable[1500];   // +0x00a4 500 x xyz
    float     flVelTable[1500];   // +0x1814 500 x xyz (untransformed)
    float     pLifeTable[100];    // +0x2f84
    DWORD     pEmitProb[200];     // +0x3114
    DWORD     dwPosIdx;           // +0x3434 step 1, wrap 500
    DWORD     dwVelIdx;           // +0x3438 step 3, wrap 500
    DWORD     dwLifeIdx;          // +0x343c step 1, wrap 100
    DWORD     dwProbIdx;          // +0x3440 step 1, wrap 200
};
static_assert(offsetof(CylinderGenerator, flScale)       == 0x0028, "Cyl layout");
static_assert(offsetof(CylinderGenerator, flMatrix)      == 0x002c, "Cyl layout");
static_assert(offsetof(CylinderGenerator, flDtScale)     == 0x0094, "Cyl layout");
static_assert(offsetof(CylinderGenerator, flAccumulator) == 0x00a0, "Cyl layout");
static_assert(offsetof(CylinderGenerator, flPosTable)    == 0x00a4, "Cyl layout");
static_assert(offsetof(CylinderGenerator, flVelTable)    == 0x1814, "Cyl layout");
static_assert(offsetof(CylinderGenerator, pLifeTable)    == 0x2f84, "Cyl layout");
static_assert(offsetof(CylinderGenerator, pEmitProb)     == 0x3114, "Cyl layout");
static_assert(offsetof(CylinderGenerator, dwPosIdx)      == 0x3434, "Cyl layout");
static_assert(offsetof(CylinderGenerator, dwProbIdx)     == 0x3440, "Cyl layout");
static_assert(sizeof(CylinderGenerator) == 0x3444, "Cyl size");
