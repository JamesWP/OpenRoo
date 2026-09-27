#pragma once
#include <windows.h>
#include <stddef.h>
class RenderDevice;

/* ParticleSystem hierarchy: the systems, their ring of particles, and the
 * vertex buffers they fill.  Implemented in particles.cpp. */

/* Ring node, 0x2C bytes.
 * The ring is ONE NULL-terminated doubly-linked list (not circular), split into
 * a live region [pRingHead, pRingCurrent) and a free region [pRingCurrent,
 * pRingTail].  Generators claim at pRingCurrent; environments age and retire. */
struct ParticleNode {
    ParticleNode *pPrev;
    ParticleNode *pNext;
    float         flX, flY, flZ;  // position
    float         flVel[3];       // velocity
    float         flLife;         // seconds remaining; < 0 retires
    DWORD         dwDiffuse;
    DWORD         dwShapeIndex;   // XFace corner-table index; alloc-time only
};

/* FVF 0x1e2 vertex, 0x20 bytes.  Fill only ever writes xyz + diffuse, leaving
 * psize and specular uninitialised.  u/v are not touched by Fill
 * either: the Face and XFace vertex allocators bake the texture corners in
 * once, at allocation (see quad_alloc_verts in particles.cpp). */
struct ParticleVertex {
    float flX, flY, flZ;
    float flPsize;         // (never written)
    DWORD dwDiffuse;
    DWORD dwSpecular;      // (never written)
    float flU, flV;        // (never written)
};
static_assert(sizeof(ParticleVertex) == 0x20, "ParticleVertex size");

/* The ring, as ONE struct.  It is embedded in ParticleSystem, and it is the
 * *same object* that the Generator's and the Environment's pRing point at:
 * SetGenerator/SetEnvironment attach both to `&ps->ring`.  Declaring it once means the
 * emitter, the integrator and the render fill all name the same fields.
 *
 *   pRingBase ─ … ─ pRingHead ─ … ─ pRingCurrent ─ … ─ pRingTail ─╴NULL
 *                     └─── live ───┘  └───── free ──────┘
 *
 * Empty when pRingHead == pRingCurrent; full when pRingCurrent == NULL. */
struct RingBuffer {
    DWORD         dwRingCount;
    ParticleNode *pRingBase;
    ParticleNode *pRingHead;      // oldest live particle
    ParticleNode *pRingTail;      // last free node
    ParticleNode *pRingCurrent;   // next node to emit into
};

struct Generator;
struct Environment;

/* Every constructor and destructor installs one of particles.cpp's
 * ps_vtbl_* tables. */

/* Base class. */
struct ParticleSystem {
    void         **pVtable;       // → 15-slot vtable
    char          *pName;
    RingBuffer     ring;          // handed to the generator and environment
    Generator     *pGenerator;
    Environment   *pEnvironment;
    void          *pField24;
};

struct PointParticleSystem {
    ParticleSystem  base;
    ParticleVertex *pVerts;        // scratch buffer
    DWORD           dwVertexCount; // 1 vertex per particle
};

struct FaceParticleSystem {
    ParticleSystem  base;
    ParticleVertex *pVerts;
    WORD            nVertexCount;   // 6 vertices per particle
    float           flCorner[6][3]; // six baked xyz corner offsets
    float           flScale;
};

/* Corner-table entry.  Tick accumulates flRotVel into flRotAccum each frame;
 * when an accumulated angle exceeds 0.01 it is baked into flCorner as an axis
 * rotation and reset. */
struct XFaceCornerEntry {
    float flCorner[6][3];  // six xyz corner offsets
    float flUnk48;         // never read by tick/fill
    float flRotVel[3];     // per-tick X/Y/Z rotation increments
    float flRotAccum[3];   // accumulated angles, reset when applied
};

struct XFaceParticleSystem {
    ParticleSystem    base;
    XFaceCornerEntry *pCornerTable;       // dwCornerTableCount entries
    ParticleVertex   *pVerts;
    WORD              nVertexCount;       // 6 vertices per particle
    DWORD             dwCornerTableCount;
    // (min, max) pairs: size, lifetime, speed, rotation.
    float             ranges[8];
};

/* ─── Internal (non-virtual) entry points ──────────────────────────────────
 *
 * Fill and draw for Render: one call each through the object's vtable
 * (slots 9 and 12). */
void  ps_fill(ParticleSystem *self);
DWORD ps_draw(ParticleSystem *self, RenderDevice *dev);

/* The factory: one of the four class names, allocated at its size,
 * constructed, and its vtable installed. */
ParticleSystem *ps_create(const char *name);

/* The two ways the game builds a system: clone, and load from a file. */
ParticleSystem *ps_clone(const ParticleSystem *src);
ParticleSystem *ps_load_file(const char *path, struct GameLogger *log);

ParticleSystem *Particle_CloneSystem(const ParticleSystem *);
ParticleSystem *Particle_LoadFromFile(const char *, struct GameLogger *);

/* ─── Vtable exports ───────────────────────────────────────────────────────
 *
 * The vtable slot functions.  Their signatures are the slot signatures and
 * must match the definitions in particles.cpp exactly. */
void  Particle_BaseTick(ParticleSystem *self, DWORD dt);              /* slot 7 */
void  Particle_XFaceTick(XFaceParticleSystem *self, DWORD dt);        /* slot 7 */
DWORD Particle_BaseRender(ParticleSystem *self, RenderDevice *d); /* slot 8 */
DWORD Particle_PointRender(PointParticleSystem *self, RenderDevice *d);
void  Particle_PointFill(PointParticleSystem *self);                  /* slot 9 */
void  Particle_FaceFill(FaceParticleSystem *self);
void  Particle_XFaceFill(XFaceParticleSystem *self);
void  Particle_FaceSetVector(FaceParticleSystem *self,
                             float x, float y, float z);              /* slot 10 */
void  Particle_FaceTransformCorners(FaceParticleSystem *self,
                                    float *matrix);                   /* slot 11 */
DWORD Particle_PointDraw(PointParticleSystem *self, RenderDevice *d); /* slot 12 */
DWORD Particle_FaceDraw(FaceParticleSystem *self, RenderDevice *d);
DWORD Particle_XFaceDraw(XFaceParticleSystem *self, RenderDevice *d);

/* The lifecycle slots (0-6, 9, 12, 13, 14).  Slots 5 and 6 are one
 * function each across all four classes; slot 1 is shared by Point and Face. */
struct GameLogger;
void *Particle_BaseDtor(ParticleSystem *, unsigned);
void  Particle_BaseRelease(ParticleSystem *, int);
BOOL  Particle_BaseCopyFrom(ParticleSystem *, const ParticleSystem *);
BOOL  Particle_BaseSetCapacity(ParticleSystem *, DWORD);
BOOL  Particle_BaseResize(ParticleSystem *, DWORD);
BOOL  Particle_SetGenerator(ParticleSystem *, void *);
BOOL  Particle_SetEnvironment(ParticleSystem *, void *);
BOOL  Particle_BaseSave(ParticleSystem *, void *, GameLogger *);
BOOL  Particle_BaseLoad(ParticleSystem *, void *, GameLogger *);
void  Particle_BaseFill(ParticleSystem *);
DWORD Particle_BaseDrawNull(ParticleSystem *, RenderDevice *);
void  Particle_QuadRelease(ParticleSystem *, int);
void  Particle_NopVec3(ParticleSystem *, float, float, float);
void  Particle_NopPtr(ParticleSystem *, void *);
struct Generator *Particle_GetGenerator(ParticleSystem *, const char *);
void  Particle_EnableRenderNode(ParticleSystem *);
void  Particle_DisableRenderNode(ParticleSystem *);

void *Particle_PointDtor(PointParticleSystem *, unsigned);
BOOL  Particle_PointCopyFrom(PointParticleSystem *, const ParticleSystem *);
BOOL  Particle_PointSetCapacity(PointParticleSystem *, DWORD);
BOOL  Particle_PointResize(PointParticleSystem *, DWORD);
BOOL  Particle_PointSave(PointParticleSystem *, void *, GameLogger *);
BOOL  Particle_PointLoad(PointParticleSystem *, void *, GameLogger *);

void *Particle_FaceDtor(FaceParticleSystem *, unsigned);
BOOL  Particle_FaceCopyFrom(FaceParticleSystem *, const FaceParticleSystem *);
BOOL  Particle_FaceSetCapacity(FaceParticleSystem *, DWORD);
BOOL  Particle_FaceResize(FaceParticleSystem *, DWORD);
BOOL  Particle_FaceSave(FaceParticleSystem *, void *, GameLogger *);
BOOL  Particle_FaceLoad(FaceParticleSystem *, void *, GameLogger *);

void *Particle_XFaceDtor(XFaceParticleSystem *, unsigned);
void  Particle_XFaceRelease(XFaceParticleSystem *, int);
BOOL  Particle_XFaceCopyFrom(XFaceParticleSystem *, const XFaceParticleSystem *);
BOOL  Particle_XFaceSetCapacity(XFaceParticleSystem *, DWORD);
BOOL  Particle_XFaceResize(XFaceParticleSystem *, DWORD);
BOOL  Particle_XFaceSave(XFaceParticleSystem *, void *, GameLogger *);
BOOL  Particle_XFaceLoad(XFaceParticleSystem *, void *, GameLogger *);

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
    typedef void (*fn)(ParticleSystem *, float);
    ((fn)ps->pVtable[PS_VT_TICK])(ps, dt);
}
static inline void ps_vset_vector(ParticleSystem *ps, float x, float y, float z)
{
    typedef void (*fn)(ParticleSystem *, float, float, float);
    ((fn)ps->pVtable[PS_VT_SETVEC])(ps, x, y, z);
}
static inline void ps_vrender(ParticleSystem *ps, RenderDevice *dev)
{
    typedef DWORD (*fn)(ParticleSystem *, RenderDevice *);
    ((fn)ps->pVtable[PS_VT_RENDER])(ps, dev);
}
static inline void ps_vtransform_corners(ParticleSystem *ps, float *matrix)
{
    typedef void (*fn)(ParticleSystem *, float *);
    ((fn)ps->pVtable[PS_VT_XFORM])(ps, matrix);
}
