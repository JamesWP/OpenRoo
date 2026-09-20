#pragma once
#include <windows.h>
#include <stddef.h>
#include "particles.h"
#include "layout.h"
#include "shadowmesh.h"

/* Generator / Environment simulation structs — PARTICLE_PLAN.md § 4.
 *
 * Layouts from the corrected Ghidra project (see § 4.1 for the four facts that
 * were previously documented wrongly).  Since Stage E4 the lifecycle slots
 * (dtor / CopyFrom / Save / Load, and the table builders Load drives) are ours
 * too — see generators.cpp's header for the list.
 */

/* Slot counts, fixed by the game's own call sites (§ 6.2). */
#define GEN_VTBL_SLOTS 10
#define ENV_VTBL_SLOTS  6

/* The game vtable VAs these classes were built on.  Since E5 nothing stores
 * them — our constructors install our own tables (gen_vtbl_* / env_vtbl_* in
 * generators.cpp) — so these are documentation, and the addresses the UD2
 * stubs sit behind. */
#define VTBL_GEN_STD         0x0045f094
#define VTBL_GEN_XSTD        0x0045f0bc
#define VTBL_GEN_CYLINDER    0x0045f0e8
#define VTBL_ENV_GRAVITY     0x0045f110
#define VTBL_ENV_MAGNET      0x0045f128
#define VTBL_ENV_BASE        0x0045f01c   /* plain Environment; also every dtor's last store */
#define VTBL_GEN_BASE        0x0045eff4   /* plain Generator; also every gen dtor's last store */
#define VTBL_GEN_POINT       0x0045f044   /* PointGenerator — no shipping .par names it */
#define VTBL_GEN_BOX         0x0045f06c   /* BoxGenerator — no shipping .par names it */

/* RingBuffer now lives in particles.h — it is ParticleSystem::ring, and the
 * pointer below is that same object.  AttachGeneratorRing (0x4483c0) /
 * AttachEnvironmentRing (0x4484e0) install it. */

/* Base class of every emitter.  Note +0x0C is the ring back-pointer, NOT
 * padding, and dwEnabled exists only here — Environment has no such flag.
 *
 * Generator vtables have TEN slots, not eight (0x45f094..0x45f0bb, next vtable
 * at 0x45f0bc; gap / 4 = 10).  Slot map, recovered from the call sites:
 *
 *   0  ~dtor(int flags)         scalar deleting dtor, MSVC convention
 *   1  CopyFrom(Generator *src) from CloneGeneratorFromSource (0x4488b0)
 *   2  AttachRing(RingBuffer *) SHARED 0x4483c0; from ParticleSystem::SetGenerator
 *   3  Tick(float dt)           OURS
 *   4  Save(FILE *)             field-by-field fwrite
 *   5  Load(FILE *)             field-by-field fread + table builders
 *   6  SetPosition(vec3)       writes flOrigin (Cyl) / flPosOffset (XStd);
 *                              StdGenerator no-ops it (0x448470 = RET 0xc)
 *   7  unknown, 4 args         overridden by XStd only (0x44a850)
 *   8  SetDirection(vec3)      Cyl 0x44b0c0 writes the direction at +0x1c and
 *                              builds flMatrix at +0x2c; Std no-ops it
 *   9  unknown, 1 arg          overridden by XStd only (0x44a930)
 *
 * Slots 6-9 are NOT dead stubs — gameplay drives 6 and 8 through
 * ParticleSystem::GetGenerator (0x447dd0), which hands out the raw Generator*
 * purely so the caller can make a vtable call on it.
 *
 * The only non-vtable field accesses from outside the class are pName (+0x04,
 * read by GetGenerator / Serialize / CloneGeneratorFromSource) and dwEnabled
 * (+0x08, written directly by GeneratorEnableFlag 0x448450 /
 * GeneratorDisableFlag 0x448460 via ParticleSystem::Enable/DisableRenderNode).
 * No subclass field is ever touched from outside. See PARTICLE_PLAN.md 6.2a. */
struct Generator {
    void      **pVtable;          // +0x00 10-slot vtable
    char       *pName;            // +0x04
    DWORD       dwEnabled;        // +0x08 toggled by Enable/DisableRenderNode
    RingBuffer *pRing;            // +0x0c
};
static_assert(sizeof(Generator) == 0x10, "Generator size");
static_assert(offsetof(Generator, pRing) == 0x0c, "Generator layout");

/* Base class of every environment.  12 bytes: the ring pointer sits at +0x08,
 * where Generator keeps dwEnabled.
 *
 * Six slots (not 8), same meanings as the Generator map above for 0..5; slot 2
 * is the shared AttachEnvironmentRing (0x4484e0). */
struct Environment {
    void      **pVtable;          // +0x00 6-slot vtable (not 8)
    char       *pName;            // +0x04
    RingBuffer *pRing;            // +0x08
};
static_assert(sizeof(Environment) == 0x0c, "Environment size");
static_assert(offsetof(Environment, pRing) == 0x08, "Environment layout");

/* GravityEnvironment (0x6c) — vtable 0x45f110, tick 0x44c450, load 0x44c7f0.
 * +0x0c..+0x2c decoded from Load's two setters (0x44c320, 0x44c410): the file
 * holds a direction and a magnitude, flGravity is their product. */
struct GravityEnvironment {
    Environment base;             // +0x00
    float       flDirection[3];   // +0x0c as read, not normalised
    float       flMagnitude;      // +0x18
    float       flGravity[3];     // +0x1c acceleration per second
    DWORD       dwTargetARGB;     // +0x28 packed; unpacked into the next four
    DWORD       dwTargetA;        // +0x2c unread by the tick
    DWORD       dwTargetRGB[3];   // +0x30 fade targets: R, G, B
    float       flFadeRate;       // +0x3c
    DWORD       dwFadeThreshold;  // +0x40 step must exceed this to apply
    DWORD       dwClipEnable[3];  // +0x44 per-axis kill-plane enables
    float       flClipMax[3];     // +0x50
    float       flClipMin[3];     // +0x5c
    float       flFadeAccum;      // +0x68
};
static_assert(offsetof(GravityEnvironment, flMagnitude)  == 0x18, "Gravity layout");
static_assert(offsetof(GravityEnvironment, flGravity)    == 0x1c, "Gravity layout");
static_assert(offsetof(GravityEnvironment, dwTargetARGB) == 0x28, "Gravity layout");
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
 * recomputes the values.  Load (slot 5, ours since E4) fills them from the .par
 * parameters by sampling with the game's rand().
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

/* XStdGenerator (0x3438) — vtable 0x45f0bc, emit 0x44aac0.
 * Thrusters / flames (3 shipping .par files).
 *
 * Genuine inheritance: every field through +0x341c is laid out identically to
 * StdGenerator, and the 0x18-byte extension begins exactly where StdGenerator
 * ends.  Its ctor calls the StdGenerator ctor and its Save/Load/Copy chain into
 * StdGenerator's.  Emit is StdGenerator's with a constant bias added to the
 * sampled position and velocity.
 *
 * There is NO matrix transform here, despite earlier RE notes claiming
 * "StdGenerator + 4x4 matrix"; those notes also placed the extension at
 * +0x3410, which is the pData-relative offset, not the struct one. */
struct XStdGenerator {
    StdGenerator base;            // +0x0000
    float        flPosOffset[3];  // +0x3420 added to the sampled position
    float        flVelOffset[3];  // +0x342c added to the sampled velocity
};
static_assert(offsetof(XStdGenerator, flPosOffset) == 0x3420, "XStd layout");
static_assert(offsetof(XStdGenerator, flVelOffset) == 0x342c, "XStd layout");
static_assert(sizeof(XStdGenerator) == 0x3438, "XStd size");

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
    float     flOrigin[3];        // +0x0010 added after the transform; slot 6
    float     flDirection[3];     // +0x001c as given to slot 8 (SetDirection)
    float     flScale;            // +0x0028 applied before the transform
    float     flMatrix[16];       // +0x002c row-major 4x4, rebuilt by slot 8
    float     flVelMin[3];        // +0x006c } serialised; Load builds the
    float     flVelMax[3];        // +0x0078 } velocity table from these
    float     flLifeMin;          // +0x0084 } (speed spread)
    float     flLifeMax;          // +0x0088 }
    float     flEmitRateMin;      // +0x008c } and pLifeTable from these
    float     flEmitRateMax;      // +0x0090 }
    float     flDtScale;          // +0x0094
    void     *pTypeTable;         // +0x0098 (colour, weight) pairs
    DWORD     dwTypeTableCount;   // +0x009c
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

/* PointGenerator (0x1184) — vtable 0x45f044, emit 0x449200.  Dead content: no
 * shipping .par file names it, and it has no Load of its own (slot 5 is the
 * shared `return 1`), so its tables are never filled — its constructor sets
 * only flAccumulator and dwDiffuse.  Layout from the emit's operands. */
struct PointGenerator {
    Generator base;               // +0x0000
    float     flEmitPos[3];       // +0x0010 copied verbatim to the node position
    float     flVelBias[3];       // +0x001c added to the sampled velocity
    float     flEmitRate;         // +0x0028
    BYTE      opaque2c[0x0c];     // +0x002c never touched
    DWORD     dwDiffuse;          // +0x0038 ctor: 0xFFFFFFFF
    float     flAccumulator;      // +0x003c
    BYTE      opaque40[0x04];     // +0x0040 never touched
    float     flVelTable[1000];   // +0x0044 indexed per axis, steps 1/2/3
    DWORD     dwLifeTable[100];   // +0x0fe4 raw bits into flLife
    DWORD     dwVelIdx[3];        // +0x1174
    DWORD     dwLifeIdx;          // +0x1180 runs 0..101: reads two past the table
};
static_assert(offsetof(PointGenerator, flEmitRate)    == 0x0028, "Point layout");
static_assert(offsetof(PointGenerator, dwDiffuse)     == 0x0038, "Point layout");
static_assert(offsetof(PointGenerator, flVelTable)    == 0x0044, "Point layout");
static_assert(offsetof(PointGenerator, dwLifeTable)   == 0x0fe4, "Point layout");
static_assert(offsetof(PointGenerator, dwVelIdx)      == 0x1174, "Point layout");
static_assert(sizeof(PointGenerator) == 0x1184, "Point size");

/* BoxGenerator (0x244c) — vtable 0x45f06c, emit 0x449420.  Dead content like
 * Point: no .par names it, no Load, and its constructor initialises nothing
 * past the base.  Layout from the emit's operands. */
struct BoxGenerator {
    Generator base;               // +0x0000
    BYTE      opaque10[0x18];     // +0x0010 never touched
    float     flVelBias[3];       // +0x0028
    float     flEmitRate;         // +0x0034
    float     flAccumulator;      // +0x0038
    DWORD     dwPosX[500];        // +0x003c raw bits into the node position
    DWORD     dwPosY[500];        // +0x080c
    DWORD     dwPosZ[500];        // +0x0fdc
    float     flVelTable[500];    // +0x17ac indexed per axis, steps 1/2/3
    DWORD     dwLifeTable[100];   // +0x1f7c
    DWORD     dwDiffuse[200];     // +0x210c
    DWORD     dwPosIdx[3];        // +0x242c
    DWORD     dwVelIdx[3];        // +0x2438
    DWORD     dwLifeIdx;          // +0x2444
    DWORD     dwDiffuseIdx;       // +0x2448
};
static_assert(offsetof(BoxGenerator, flVelBias)    == 0x0028, "Box layout");
static_assert(offsetof(BoxGenerator, dwPosX)       == 0x003c, "Box layout");
static_assert(offsetof(BoxGenerator, flVelTable)   == 0x17ac, "Box layout");
static_assert(offsetof(BoxGenerator, dwDiffuse)    == 0x210c, "Box layout");
static_assert(offsetof(BoxGenerator, dwPosIdx)     == 0x242c, "Box layout");
static_assert(offsetof(BoxGenerator, dwDiffuseIdx) == 0x2448, "Box layout");
static_assert(sizeof(BoxGenerator) == 0x244c, "Box size");

/* ─── Internal (non-virtual) entry points ──────────────────────────────────
 *
 * Every live Generator and Environment class is ours (PARTICLE_PLAN.md § 4.8:
 * Std / XStd / Cylinder, Gravity / Magnet; PointGenerator and BoxGenerator are
 * in the binary but instantiated by no .par file).  So Particle_BaseTick's
 * `pGenerator->vtbl[3](dt)` always left the DLL through a patched game vtable
 * only to come straight back.  These call the implementation directly when the
 * slot holds one of our exports, and fall back to real virtual dispatch
 * otherwise — so the dead classes, or anything we have not replaced, still
 * work exactly as before. */
/* Slot 3 = Tick(float dt) for both Generator and Environment. */
#define GEN_VT_TICK_SLOT 3
/* Lifecycle slots, same for both (§ 6.2): 0 = scalar deleting dtor(flags),
 * 1 = BOOL CopyFrom(src), 4 = BOOL Save(FILE *), 5 = BOOL Load(FILE *). */
#define GEN_VT_DTOR_SLOT 0
#define GEN_VT_COPY_SLOT 1
#define GEN_VT_SAVE_SLOT 4
#define GEN_VT_LOAD_SLOT 5

/* EnvironmentFactoryCreate (0x4488f0), ours: allocate with our own new and
 * construct the named class, or NULL for an unknown name.  The object comes
 * back carrying its *game* vtable VA; factory.cpp swaps in our table. */
Environment *env_create(const char *name);

/* GeneratorFactoryCreate (0x4485d0), ours: the same six names and sizes, our
 * own new, our constructors.  Game vtable VA on return, like env_create. */
Generator *gen_create(const char *name);

/* CloneGeneratorFromSource (0x4488b0) / the environment twin (0x448a70):
 * build one of the same class, CopyFrom the source, and drop it again if
 * either step fails.  ParticleSystem::CopyFrom is the only caller. */
Generator   *gen_clone(const Generator *src);
Environment *env_clone(const Environment *src);

/* The object 0x438170 is a method of is `ShadowMesh` (shadowmesh.h), whose
 * constructor, both destructors and buffer release were replaced in
 * ENDGAME_PLAN.md E2.  This header used to carry a two-field `GaussianFieldHost`
 * modelled from those fifteen instructions alone; the full layout, the
 * evidence for the name and the class's own layout checks now live in
 * shadowmesh.h, and there is one definition rather than two views of it.
 *
 * The one fact this file still relies on is unchanged and still asserted
 * there: the 30-float table at +0x14 and its DWORD cursor at +0x8c tile
 * exactly, 0x14 + 30*4 == 0x8c. */

/* 0x438170 — the Gaussian sampler's only caller outside the particle code.
 * Lives here because it is a thin wrapper over this file's gauss_fill. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Gen_FillGaussianField(ShadowMesh *self, float mu, float sigma);

/* Tick a Generator or an Environment: one virtual call through slot 3.  The
 * table is ours, so this lands straight on our implementation. */
void sim_tick_slot3(void *obj, float dt);
