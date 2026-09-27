#pragma once
#include <windows.h>
#include <stddef.h>
#include "com_proxy.h"

/* ParticleSystem hierarchy: the systems, their ring of particles, and the
 * vertex buffers they fill.  Implemented in particles.cpp. */

/* Ring node, 0x2C bytes.
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

/* FVF 0x1e2 vertex, 0x20 bytes.  Fill only ever writes xyz + diffuse, leaving
 * psize and specular uninitialised.  u/v are not touched by Fill
 * either: the Face and XFace vertex allocators bake the texture corners in
 * once, at allocation (see quad_alloc_verts in particles.cpp). */
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
 * SetGenerator/SetEnvironment attach both to `&ps->ring`.  Declaring it once means the
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

/* Every constructor and destructor installs one of particles.cpp's
 * ps_vtbl_* tables. */

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
    ParticleVertex *pVerts;        // +0x28 scratch buffer
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

/* Corner-table entry, 100 bytes (0x64).  Tick accumulates
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
 * Fill and draw for Render: one call each through the object's vtable
 * (slots 9 and 12). */
void  ps_fill(ParticleSystem *self);
DWORD ps_draw(ParticleSystem *self, IDirect3DDevice3 *dev);

/* The factory: one of the four class names, allocated at its size,
 * constructed, and its vtable installed. */
ParticleSystem *ps_create(const char *name);

/* The two ways the game builds a system: clone, and load from a file. */
ParticleSystem *ps_clone(const ParticleSystem *src);
ParticleSystem *ps_load_file(const char *path, struct GameLogger *log);

extern "C" {
__declspec(dllexport) ParticleSystem *__cdecl Particle_CloneSystem(const ParticleSystem *);
__declspec(dllexport) ParticleSystem *__cdecl Particle_LoadFromFile(const char *, struct GameLogger *);
}

/* ─── Vtable exports ───────────────────────────────────────────────────────
 *
 * The vtable slot functions.  Their signatures are the slot signatures and
 * must match the definitions in particles.cpp exactly. */
#define PS_THISCALL __attribute__((thiscall))
extern "C" {
void  PS_THISCALL Particle_BaseTick(ParticleSystem *self, DWORD dt);              /* slot 7 */
void  PS_THISCALL Particle_XFaceTick(XFaceParticleSystem *self, DWORD dt);        /* slot 7 */
DWORD PS_THISCALL Particle_BaseRender(ParticleSystem *self, IDirect3DDevice3 *d); /* slot 8 */
DWORD PS_THISCALL Particle_PointRender(PointParticleSystem *self, IDirect3DDevice3 *d);
void  PS_THISCALL Particle_PointFill(PointParticleSystem *self);                  /* slot 9 */
void  PS_THISCALL Particle_FaceFill(FaceParticleSystem *self);
void  PS_THISCALL Particle_XFaceFill(XFaceParticleSystem *self);
void  PS_THISCALL Particle_FaceSetVector(FaceParticleSystem *self,
                                         float x, float y, float z);              /* slot 10 */
void  PS_THISCALL Particle_FaceTransformCorners(FaceParticleSystem *self,
                                                float *matrix);                   /* slot 11 */
DWORD PS_THISCALL Particle_PointDraw(PointParticleSystem *self, IDirect3DDevice3 *d); /* slot 12 */
DWORD PS_THISCALL Particle_FaceDraw(FaceParticleSystem *self, IDirect3DDevice3 *d);
DWORD PS_THISCALL Particle_XFaceDraw(XFaceParticleSystem *self, IDirect3DDevice3 *d);

/* The lifecycle slots (0-6, 9, 12, 13, 14).  Slots 5 and 6 are one
 * function each across all four classes; slot 1 is shared by Point and Face. */
struct GameLogger;
void *PS_THISCALL Particle_BaseDtor(ParticleSystem *, unsigned);
void  PS_THISCALL Particle_BaseRelease(ParticleSystem *, int);
BOOL  PS_THISCALL Particle_BaseCopyFrom(ParticleSystem *, const ParticleSystem *);
BOOL  PS_THISCALL Particle_BaseSetCapacity(ParticleSystem *, DWORD);
BOOL  PS_THISCALL Particle_BaseResize(ParticleSystem *, DWORD);
BOOL  PS_THISCALL Particle_SetGenerator(ParticleSystem *, void *);
BOOL  PS_THISCALL Particle_SetEnvironment(ParticleSystem *, void *);
BOOL  PS_THISCALL Particle_BaseSave(ParticleSystem *, void *, GameLogger *);
BOOL  PS_THISCALL Particle_BaseLoad(ParticleSystem *, void *, GameLogger *);
void  PS_THISCALL Particle_BaseFill(ParticleSystem *);
DWORD PS_THISCALL Particle_BaseDrawNull(ParticleSystem *, IDirect3DDevice3 *);
void  PS_THISCALL Particle_QuadRelease(ParticleSystem *, int);
void  PS_THISCALL Particle_NopVec3(ParticleSystem *, float, float, float);
void  PS_THISCALL Particle_NopPtr(ParticleSystem *, void *);
struct Generator *PS_THISCALL Particle_GetGenerator(ParticleSystem *, const char *);
void  PS_THISCALL Particle_EnableRenderNode(ParticleSystem *);
void  PS_THISCALL Particle_DisableRenderNode(ParticleSystem *);

void *PS_THISCALL Particle_PointDtor(PointParticleSystem *, unsigned);
BOOL  PS_THISCALL Particle_PointCopyFrom(PointParticleSystem *, const ParticleSystem *);
BOOL  PS_THISCALL Particle_PointSetCapacity(PointParticleSystem *, DWORD);
BOOL  PS_THISCALL Particle_PointResize(PointParticleSystem *, DWORD);
BOOL  PS_THISCALL Particle_PointSave(PointParticleSystem *, void *, GameLogger *);
BOOL  PS_THISCALL Particle_PointLoad(PointParticleSystem *, void *, GameLogger *);

void *PS_THISCALL Particle_FaceDtor(FaceParticleSystem *, unsigned);
BOOL  PS_THISCALL Particle_FaceCopyFrom(FaceParticleSystem *, const FaceParticleSystem *);
BOOL  PS_THISCALL Particle_FaceSetCapacity(FaceParticleSystem *, DWORD);
BOOL  PS_THISCALL Particle_FaceResize(FaceParticleSystem *, DWORD);
BOOL  PS_THISCALL Particle_FaceSave(FaceParticleSystem *, void *, GameLogger *);
BOOL  PS_THISCALL Particle_FaceLoad(FaceParticleSystem *, void *, GameLogger *);

void *PS_THISCALL Particle_XFaceDtor(XFaceParticleSystem *, unsigned);
void  PS_THISCALL Particle_XFaceRelease(XFaceParticleSystem *, int);
BOOL  PS_THISCALL Particle_XFaceCopyFrom(XFaceParticleSystem *, const XFaceParticleSystem *);
BOOL  PS_THISCALL Particle_XFaceSetCapacity(XFaceParticleSystem *, DWORD);
BOOL  PS_THISCALL Particle_XFaceResize(XFaceParticleSystem *, DWORD);
BOOL  PS_THISCALL Particle_XFaceSave(XFaceParticleSystem *, void *, GameLogger *);
BOOL  PS_THISCALL Particle_XFaceLoad(XFaceParticleSystem *, void *, GameLogger *);
}

/* ParticleSystem vtable slot numbers (15-slot table).
 *
 * Slots 0-6 and 13/14 are the lifecycle half, the same shape in all four
 * vtables:
 *   0  ~dtor(flags)            MSVC scalar deleting
 *   1  Release(flags)          drop generator + environment, free the ring
 *   2  CopyFrom(src)           type-name gate, then clone both sub-objects
 *   3  SetCapacity(n)          size the ring
 *   4  Resize(n)               free and re-make the ring and the vertex buffer
 *   5  SetGenerator(gen)       one function for all four classes
 *   6  SetEnvironment(env)     one function for all four classes
 *  13  Save(FILE *, log)       names of both sub-objects, then their Save
 *  14  Load(FILE *, log)       names, factory, their Load, then attach */
#define PS_VT_DTOR    0
#define PS_VT_RELEASE 1
#define PS_VT_COPY    2
#define PS_VT_SETCAP  3
#define PS_VT_RESIZE  4
#define PS_VT_SETGEN  5
#define PS_VT_SETENV  6
#define PS_VT_SAVE   13
#define PS_VT_LOAD   14
#define PS_VT_TICK    7
#define PS_VT_RENDER  8
#define PS_VT_FILL    9
#define PS_VT_SETVEC 10
#define PS_VT_XFORM  11
#define PS_VT_DRAW   12
#define PS_VTBL_SLOTS 15

/* Virtual dispatch for callers outside particles.cpp: through the object's
 * own table, so each class's override runs.  The tick's argument is a
 * float in every slot 7 (the exports take it as DWORD bits; same ABI). */
static inline void ps_vtick(ParticleSystem *ps, float dt)
{
    typedef void (__attribute__((thiscall)) *fn)(ParticleSystem *, float);
    ((fn)ps->pVtable[PS_VT_TICK])(ps, dt);
}
static inline void ps_vset_vector(ParticleSystem *ps, float x, float y, float z)
{
    typedef void (__attribute__((thiscall)) *fn)(ParticleSystem *, float, float, float);
    ((fn)ps->pVtable[PS_VT_SETVEC])(ps, x, y, z);
}
static inline void ps_vrender(ParticleSystem *ps, IDirect3DDevice3 *dev)
{
    typedef DWORD (__attribute__((thiscall)) *fn)(ParticleSystem *, IDirect3DDevice3 *);
    ((fn)ps->pVtable[PS_VT_RENDER])(ps, dev);
}
static inline void ps_vtransform_corners(ParticleSystem *ps, float *matrix)
{
    typedef void (__attribute__((thiscall)) *fn)(ParticleSystem *, float *);
    ((fn)ps->pVtable[PS_VT_XFORM])(ps, matrix);
}
