#pragma once
#include <windows.h>
#include <stddef.h>
#include "com_proxy.h"

/* ParticleSystem hierarchy — render-path structs.
 * Layouts from Ghidra (verified against the fill/draw disassembly; see
 * PARTICLE_PLAN.md § 1).  Only the fields the render path touches are
 * asserted; simulation state stays game-owned. */

/* Ring node, 0x2C bytes — layout complete as of Stage C (PARTICLE_PLAN.md § 4.3).
 * The ring is ONE NULL-terminated doubly-linked list (not circular), split into
 * a live region [pRingHead, pRingCurrent) and a free region [pRingCurrent,
 * pRingTail].  Generators claim at pRingCurrent; environments age and retire. */
struct ParticleNode {
    ParticleNode *pPrev;          // +0x00
    ParticleNode *pNext;          // +0x04
    float         flX, flY, flZ;  // +0x08..+0x10 position
    float         flVel[3];       // +0x14..+0x1c velocity
    float         flLife;         // +0x20 seconds remaining; < 0 retires
    DWORD         dwDiffuse;      // +0x24
    DWORD         dwShapeIndex;   // +0x28 XFace corner-table index; alloc-time only
};
static_assert(offsetof(ParticleNode, pNext)        == 0x04, "ParticleNode layout");
static_assert(offsetof(ParticleNode, flX)          == 0x08, "ParticleNode layout");
static_assert(offsetof(ParticleNode, flVel)        == 0x14, "ParticleNode layout");
static_assert(offsetof(ParticleNode, flLife)       == 0x20, "ParticleNode layout");
static_assert(offsetof(ParticleNode, dwDiffuse)    == 0x24, "ParticleNode layout");
static_assert(offsetof(ParticleNode, dwShapeIndex) == 0x28, "ParticleNode layout");
static_assert(sizeof(ParticleNode) == 0x2C, "ParticleNode size");

/* FVF 0x1e2 vertex, 0x20 bytes.  The originals only write xyz + diffuse;
 * psize/specular/u/v stay uninitialised — we preserve that. */
struct ParticleVertex {
    float flX, flY, flZ;   // +0x00..+0x08
    float flPsize;         // +0x0c  (never written)
    DWORD dwDiffuse;       // +0x10
    DWORD dwSpecular;      // +0x14  (never written)
    float flU, flV;        // +0x18, +0x1c (never written)
};
static_assert(sizeof(ParticleVertex) == 0x20, "ParticleVertex size");

/* The ring, as ONE struct.  It is embedded in ParticleSystem at +0x08, and it
 * is the *same object* that Generator+0x0C and Environment+0x08 point at:
 * AttachGeneratorRing (0x4483c0) / AttachEnvironmentRing (0x4484e0) are handed
 * `&ps->ring` by SetGenerator/SetEnvironment.  Declaring it once means the
 * emitter, the integrator and the render fill all name the same fields.
 *
 *   pRingBase ─ … ─ pRingHead ─ … ─ pRingCurrent ─ … ─ pRingTail ─╴NULL
 *                     └─── live ───┘  └───── free ──────┘
 *
 * Empty when pRingHead == pRingCurrent; full when pRingCurrent == NULL. */
struct RingBuffer {
    DWORD         dwRingCount;    // +0x00
    ParticleNode *pRingBase;      // +0x04
    ParticleNode *pRingHead;      // +0x08 oldest live particle
    ParticleNode *pRingTail;      // +0x0c last free node
    ParticleNode *pRingCurrent;   // +0x10 next node to emit into
};
static_assert(sizeof(RingBuffer) == 0x14, "RingBuffer size");
static_assert(offsetof(RingBuffer, pRingCurrent) == 0x10, "RingBuffer layout");

struct Generator;
struct Environment;

/* Class identity = the vtable address stored at +0x00.  The game image has no
 * relocations and is always mapped at 0x400000, so these are constants at
 * runtime; the dispatchers in particles.cpp / generators.cpp use them to
 * recognise a class without going back out through the vtable.
 *
 * They must stay in step with patch.py's VTABLE_PATCHES (file offset = VA -
 * 0x400000).  Anything not listed here falls back to a real virtual call. */
#define VTBL_PARTICLE_BASE   0x0045efb8
#define VTBL_PARTICLE_POINT  0x0045f140
#define VTBL_PARTICLE_FACE   0x0045f17c
#define VTBL_PARTICLE_XFACE  0x0045f1b8

/* Base class, 0x28 bytes. */
struct ParticleSystem {
    void         **pVtable;       // +0x00 → 15-slot vtable
    char          *pName;         // +0x04
    RingBuffer     ring;          // +0x08 handed to the generator and environment
    Generator     *pGenerator;    // +0x1c
    Environment   *pEnvironment;  // +0x20
    void          *pField24;      // +0x24
};
static_assert(sizeof(ParticleSystem) == 0x28, "ParticleSystem size");
static_assert(offsetof(ParticleSystem, ring)         == 0x08, "ParticleSystem layout");
static_assert(offsetof(ParticleSystem, pGenerator)   == 0x1c, "ParticleSystem layout");
static_assert(offsetof(ParticleSystem, pEnvironment) == 0x20, "ParticleSystem layout");

struct PointParticleSystem {      // 0x30 bytes
    ParticleSystem  base;
    ParticleVertex *pVerts;        // +0x28 scratch buffer (game-allocated)
    DWORD           dwVertexCount; // +0x2c 1 vertex per particle
};
static_assert(offsetof(PointParticleSystem, pVerts)        == 0x28, "Point layout");
static_assert(offsetof(PointParticleSystem, dwVertexCount) == 0x2c, "Point layout");
static_assert(sizeof(PointParticleSystem) == 0x30, "Point size");

#pragma pack(push, 1)
struct FaceParticleSystem {       // 0x7a bytes, byte-packed (corners at +0x2e)
    ParticleSystem  base;
    ParticleVertex *pVerts;        // +0x28
    WORD            nVertexCount;  // +0x2c 6 vertices per particle
    float           flCorner[6][3];// +0x2e six baked xyz corner offsets
    float           flScale;       // +0x76
};
static_assert(offsetof(FaceParticleSystem, nVertexCount) == 0x2c, "Face layout");
static_assert(offsetof(FaceParticleSystem, flCorner)     == 0x2e, "Face layout");
static_assert(sizeof(FaceParticleSystem) == 0x7a, "Face size");

/* Corner-table entry, 100 bytes (0x64).  Tick (0x44ee90) accumulates
 * flRotVel into flRotAccum each frame; when an accumulated angle exceeds
 * 0.01 it is baked into flCorner as an axis rotation and reset. */
struct XFaceCornerEntry {
    float flCorner[6][3];  // +0x00..0x44 six xyz corner offsets
    float flUnk48;         // +0x48 never read by tick/fill
    float flRotVel[3];     // +0x4c per-tick X/Y/Z rotation increments
    float flRotAccum[3];   // +0x58 accumulated angles, reset when applied
};
static_assert(offsetof(XFaceCornerEntry, flRotVel)   == 0x4c, "entry layout");
static_assert(offsetof(XFaceCornerEntry, flRotAccum) == 0x58, "entry layout");
static_assert(sizeof(XFaceCornerEntry) == 100, "entry size");

struct XFaceParticleSystem {      // 0x96 bytes; only replaced-path fields typed
    ParticleSystem   base;
    XFaceCornerEntry *pCornerTable; // +0x28 dwCornerTableCount × 100-byte entries
    ParticleVertex   *pVerts;       // +0x2c
    WORD              nVertexCount; // +0x30 6 vertices per particle
    BYTE              gap32[0x40];  // +0x32 uninitialised gap (never touched)
    DWORD             dwCornerTableCount; // +0x72
    BYTE              simParams[0x20];    // +0x76 size/lifetime/speed/rot ranges
};
static_assert(offsetof(XFaceParticleSystem, pCornerTable)       == 0x28, "XFace layout");
static_assert(offsetof(XFaceParticleSystem, pVerts)             == 0x2c, "XFace layout");
static_assert(offsetof(XFaceParticleSystem, nVertexCount)       == 0x30, "XFace layout");
static_assert(offsetof(XFaceParticleSystem, dwCornerTableCount) == 0x72, "XFace layout");
static_assert(sizeof(XFaceParticleSystem) == 0x96, "XFace size");
#pragma pack(pop)

/* ─── Internal (non-virtual) entry points ──────────────────────────────────
 *
 * Every one of the three concrete fill/draw methods is ours, so Render does
 * not need to leave the DLL to reach them.  These resolve the class from the
 * vtable slot the original Render *would* have called: if the slot holds one
 * of our exports we call the implementation directly; anything else is still
 * dispatched indirectly, so an unreplaced or future override keeps working.
 *
 * The two entry paths (game vtable → export, and Render → here) run the same
 * function, so logging and FX behave identically whichever way in. */
void  ps_fill(ParticleSystem *self);
DWORD ps_draw(ParticleSystem *self, IDirect3DDevice3 *dev);
