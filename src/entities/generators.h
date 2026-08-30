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
