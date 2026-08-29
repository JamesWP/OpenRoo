#pragma once
#include <windows.h>
#include <stddef.h>

/* ParticleSystem hierarchy — render-path structs.
 * Layouts from Ghidra (verified against the fill/draw disassembly; see
 * PARTICLE_PLAN.md § 1).  Only the fields the render path touches are
 * asserted; simulation state stays game-owned. */

/* Ring node, 0x2C bytes.  +0x14..+0x23 is generator-owned simulation state. */
struct ParticleNode {
    void         *unknown00;      // +0x00
    ParticleNode *pNext;          // +0x04
    float         flX, flY, flZ;  // +0x08..+0x10
    BYTE          sim14[0x10];    // +0x14 simulation state, not read here
    DWORD         dwDiffuse;      // +0x24
    DWORD         dwShapeIndex;   // +0x28 XFace corner-table index
};
static_assert(offsetof(ParticleNode, pNext)        == 0x04, "ParticleNode layout");
static_assert(offsetof(ParticleNode, flX)          == 0x08, "ParticleNode layout");
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

/* Base class, 0x28 bytes.  Ring walk: if head == current the ring is empty;
 * otherwise iterate node = node->pNext starting at head until node == current. */
struct ParticleSystem {
    void         **pVtable;       // +0x00 → 15-slot vtable
    char          *pName;         // +0x04
    DWORD          dwRingCount;   // +0x08
    ParticleNode  *pRingBase;     // +0x0c
    ParticleNode  *pRingHead;     // +0x10
    ParticleNode  *pRingTail;     // +0x14
    ParticleNode  *pRingCurrent;  // +0x18
    void          *pGenerator;    // +0x1c
    void          *pEnvironment;  // +0x20
    void          *pField24;      // +0x24
};
static_assert(sizeof(ParticleSystem) == 0x28, "ParticleSystem size");
static_assert(offsetof(ParticleSystem, pRingHead)    == 0x10, "ParticleSystem layout");
static_assert(offsetof(ParticleSystem, pRingCurrent) == 0x18, "ParticleSystem layout");

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
