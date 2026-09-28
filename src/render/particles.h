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
    /* Allocation and release of the nodes (particles.cpp). */
    void init();
    void release();
    void assignShapes(DWORD shapes);
    BOOL alloc(DWORD count, DWORD shapes);

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
 * ps_vtbl_* tables.  The tables are hand-built, not C++ virtuals: callers
 * dispatch through them by slot number with __thiscall, so each slot is a
 * static member taking the object, forwarding to an ordinary member. */

/* Base class, 0x28 bytes.  Its fields are protected: the three subclasses'
 * slots work on them. */
class ParticleSystem {
public:
    /* The factory: one of the four class names, allocated at its size,
     * constructed, and its vtable installed. */
    static ParticleSystem *create(const char *name);
    /* The two ways the game builds a system: clone, and load from a file. */
    ParticleSystem *clone() const;
    static ParticleSystem *loadFile(const char *path, struct GameLogger *log);

    /* Fill and draw for Render: one call each through the object's vtable
     * (slots 9 and 12). */
    void  fill();
    DWORD draw(RenderDevice *dev);

    /* The three non-virtual entry points. */
    Generator *getGenerator(const char *name);
    void enableRenderNode();
    void disableRenderNode();

    /* Virtual dispatch for callers outside particles.cpp: through the object's
     * own table, so each class's override runs.  The tick's argument is a
     * float in every slot 7 (the slots take it as DWORD bits; same ABI). */
    void vtick(float dt);
    void vsetVector(float x, float y, float z);
    void vrender(RenderDevice *dev);
    void vtransformCorners(float *matrix);

    void              **vtable() const      { return pVtable_; }
    const char         *name() const        { return pName_; }
    const RingBuffer   &ring() const        { return ring_; }
    Generator          *generator() const   { return pGenerator_; }
    Environment        *environment() const { return pEnvironment_; }

    /* The constructor create() runs. */
    void constructBase();

    /* The vtable slots. */
    static ParticleSystem *__attribute__((thiscall)) loadStream(void *fp, GameLogger *log);
    static void *__attribute__((thiscall)) baseDtor(ParticleSystem *self, unsigned flags);
    static void __attribute__((thiscall)) baseRelease(ParticleSystem *self, int flags);
    static BOOL __attribute__((thiscall)) baseCopyFrom(ParticleSystem *self, const ParticleSystem *src);
    static BOOL __attribute__((thiscall)) baseSetCapacity(ParticleSystem *self, DWORD n);
    static BOOL __attribute__((thiscall)) baseResize(ParticleSystem *self, DWORD n);
    static BOOL __attribute__((thiscall)) setGeneratorSlot(ParticleSystem *self, void *gen);
    static BOOL __attribute__((thiscall)) setEnvironmentSlot(ParticleSystem *self, void *env);
    static BOOL __attribute__((thiscall)) baseSave(ParticleSystem *self, void *fp, GameLogger *log);
    static BOOL __attribute__((thiscall)) baseLoad(ParticleSystem *self, void *fp, GameLogger *log);
    static void __attribute__((thiscall)) baseFill(ParticleSystem *);
    static DWORD __attribute__((thiscall)) baseDrawNull(ParticleSystem *, RenderDevice *);
    static void __attribute__((thiscall)) nopVec3(ParticleSystem *, float, float, float);
    static void __attribute__((thiscall)) nopPtr(ParticleSystem *, void *);
    static void __attribute__((thiscall)) quadReleaseSlot(ParticleSystem *self, int flags);
    static void __attribute__((thiscall)) baseTick(ParticleSystem *self, DWORD dt);
    static DWORD __attribute__((thiscall)) baseRender(ParticleSystem *self, RenderDevice *dev);

protected:
    template <typename EmitFn> DWORD fillRing(EmitFn emit);
    void release(int flags);
    void destructBase();
    BOOL setCapacity(DWORD count);
    BOOL resize(DWORD count);
    BOOL copyFrom(const ParticleSystem *src);
    BOOL serialize(void *fp, GameLogger *);
    BOOL deserialize(void *fp, GameLogger *log);
    void setRenderNode(DWORD enabled);
    void quadRelease(int flags);
    void quadDestruct(void *const *vtbl);
    void tick(float dt);

    void         **pVtable_;       // +0x00 → 15-slot vtable
    char          *pName_;         // +0x04
    RingBuffer     ring_;          // +0x08 handed to the generator and environment
    Generator     *pGenerator_;    // +0x1c
    Environment   *pEnvironment_;  // +0x20
    void          *pField24_;      // +0x24

private:
    static void checkLayout();
};

inline void ParticleSystem::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(ParticleSystem, ring_)         == 0x08, "ParticleSystem layout");
    static_assert(offsetof(ParticleSystem, pGenerator_)   == 0x1c, "ParticleSystem layout");
    static_assert(offsetof(ParticleSystem, pEnvironment_) == 0x20, "ParticleSystem layout");
#pragma GCC diagnostic pop
}
static_assert(sizeof(ParticleSystem) == 0x28, "ParticleSystem size");

class PointParticleSystem : public ParticleSystem {      // 0x30 bytes
public:
    /* The constructor create() runs. */
    void pointConstruct();

    /* The vtable slots. */
    static void __attribute__((thiscall)) pointFillSlot(PointParticleSystem *self);
    static DWORD __attribute__((thiscall)) pointDrawSlot(PointParticleSystem *self, RenderDevice *dev);
    static DWORD __attribute__((thiscall)) pointRender(PointParticleSystem *self, RenderDevice *dev);
    static void *__attribute__((thiscall)) pointDtor(PointParticleSystem *self, unsigned flags);
    static BOOL __attribute__((thiscall)) pointCopyFromSlot(PointParticleSystem *self, const ParticleSystem *src);
    static BOOL __attribute__((thiscall)) pointSetCapacitySlot(PointParticleSystem *self, DWORD n);
    static BOOL __attribute__((thiscall)) pointResizeSlot(PointParticleSystem *self, DWORD n);
    static BOOL __attribute__((thiscall)) pointSave(PointParticleSystem *self, void *fp, GameLogger *log);
    static BOOL __attribute__((thiscall)) pointLoad(PointParticleSystem *self, void *fp, GameLogger *log);

private:
    void pointFill();
    DWORD pointDraw(RenderDevice *dev);
    BOOL pointAllocVerts();
    BOOL pointSetCapacity(DWORD count);
    BOOL pointResize(DWORD count);
    BOOL pointCopyFrom(const ParticleSystem *src);
    BOOL pointSerialize(void *fp, GameLogger *log);
    BOOL pointDeserialize(void *fp, GameLogger *log);

    ParticleVertex *pVerts_;        // +0x28 scratch buffer
    DWORD           dwVertexCount_; // +0x2c 1 vertex per particle

    static void checkLayout();
};

inline void PointParticleSystem::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(PointParticleSystem, pVerts_)        == 0x28, "Point layout");
    static_assert(offsetof(PointParticleSystem, dwVertexCount_) == 0x2c, "Point layout");
#pragma GCC diagnostic pop
}
static_assert(sizeof(PointParticleSystem) == 0x30, "Point size");

#pragma pack(push, 1)
class FaceParticleSystem : public ParticleSystem {       // 0x7a bytes, byte-packed (corners at +0x2e)
public:
    /* The constructor create() runs. */
    void faceConstruct();

    /* The vtable slots. */
    static void __attribute__((thiscall)) faceFillSlot(FaceParticleSystem *self);
    static DWORD __attribute__((thiscall)) faceDrawSlot(FaceParticleSystem *self, RenderDevice *dev);
    static void *__attribute__((thiscall)) faceDtor(FaceParticleSystem *self, unsigned flags);
    static BOOL __attribute__((thiscall)) faceCopyFromSlot(FaceParticleSystem *self, const FaceParticleSystem *src);
    static BOOL __attribute__((thiscall)) faceSetCapacitySlot(FaceParticleSystem *self, DWORD n);
    static BOOL __attribute__((thiscall)) faceResizeSlot(FaceParticleSystem *self, DWORD n);
    static BOOL __attribute__((thiscall)) faceSave(FaceParticleSystem *self, void *fp, GameLogger *log);
    static BOOL __attribute__((thiscall)) faceLoad(FaceParticleSystem *self, void *fp, GameLogger *log);
    static void __attribute__((thiscall)) faceSetVectorSlot(FaceParticleSystem *self, float x, float y, float z);
    static void __attribute__((thiscall)) faceTransformCorners(FaceParticleSystem *self, float *matrix);

private:
    void faceFill();
    DWORD faceDraw(RenderDevice *dev);
    BOOL faceAllocVerts();
    BOOL faceSetCapacity(DWORD count);
    BOOL faceCopyFrom(const FaceParticleSystem *src);
    BOOL faceResize(DWORD count);
    BOOL faceSerialize(void *fp, GameLogger *log);
    BOOL faceDeserialize(void *fp, GameLogger *log);
    void faceSetVector(float x, float y, float z);

    ParticleVertex *pVerts_;         // +0x28
    WORD            nVertexCount_;   // +0x2c 6 vertices per particle
    float           flCorner_[6][3]; // +0x2e six baked xyz corner offsets
    float           flScale_;        // +0x76

    static void checkLayout();
};

inline void FaceParticleSystem::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(FaceParticleSystem, nVertexCount_) == 0x2c, "Face layout");
    static_assert(offsetof(FaceParticleSystem, flCorner_)     == 0x2e, "Face layout");
#pragma GCC diagnostic pop
}
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

class XFaceParticleSystem : public ParticleSystem {      // 0x96 bytes; only replaced-path fields typed
public:
    /* The constructor create() runs. */
    void xfaceConstruct();

    /* The vtable slots. */
    static void __attribute__((thiscall)) xfaceFillSlot(XFaceParticleSystem *self);
    static DWORD __attribute__((thiscall)) xfaceDrawSlot(XFaceParticleSystem *self, RenderDevice *dev);
    static void *__attribute__((thiscall)) xfaceDtor(XFaceParticleSystem *self, unsigned flags);
    static void __attribute__((thiscall)) xfaceReleaseSlot(XFaceParticleSystem *self, int flags);
    static BOOL __attribute__((thiscall)) xfaceCopyFromSlot(XFaceParticleSystem *self, const XFaceParticleSystem *src);
    static BOOL __attribute__((thiscall)) xfaceSetCapacitySlot(XFaceParticleSystem *self, DWORD n);
    static BOOL __attribute__((thiscall)) xfaceResizeSlot(XFaceParticleSystem *self, DWORD n);
    static BOOL __attribute__((thiscall)) xfaceSave(XFaceParticleSystem *self, void *fp, GameLogger *log);
    static BOOL __attribute__((thiscall)) xfaceLoad(XFaceParticleSystem *self, void *fp, GameLogger *log);
    static void __attribute__((thiscall)) xfaceTickSlot(XFaceParticleSystem *self, DWORD dt);

private:
    void xfaceFill();
    DWORD xfaceDraw(RenderDevice *dev);
    BOOL xfaceAllocVerts();
    void xfaceRelease(int flags);
    void xfaceDestruct();
    BOOL xfaceBuildCorners();
    BOOL xfaceSetCapacity(DWORD count);
    BOOL xfaceCopyFrom(const XFaceParticleSystem *src);
    BOOL xfaceResize(DWORD count);
    BOOL xfaceSerialize(void *fp, GameLogger *log);
    BOOL xfaceDeserialize(void *fp, GameLogger *log);
    void xfaceTick(float dt);

    XFaceCornerEntry *pCornerTable_; // +0x28 dwCornerTableCount × 100-byte entries
    ParticleVertex   *pVerts_;       // +0x2c
    WORD              nVertexCount_; // +0x30 6 vertices per particle
    BYTE              gap32_[0x40];  // +0x32 uninitialised gap (never touched)
    DWORD             dwCornerTableCount_; // +0x72
    BYTE              simParams_[0x20];    // +0x76 size/lifetime/speed/rot ranges

    static void checkLayout();
};

inline void XFaceParticleSystem::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(XFaceParticleSystem, pCornerTable_)       == 0x28, "XFace layout");
    static_assert(offsetof(XFaceParticleSystem, pVerts_)             == 0x2c, "XFace layout");
    static_assert(offsetof(XFaceParticleSystem, nVertexCount_)       == 0x30, "XFace layout");
    static_assert(offsetof(XFaceParticleSystem, dwCornerTableCount_) == 0x72, "XFace layout");
#pragma GCC diagnostic pop
}
static_assert(sizeof(XFaceParticleSystem) == 0x96, "XFace size");
#pragma pack(pop)

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

/* ParticleSystem's virtual dispatch, through the object's own table. */
inline void ParticleSystem::vtick(float dt)
{
    typedef void (__attribute__((thiscall)) *fn)(ParticleSystem *, float);
    ((fn)pVtable_[PS_VT_TICK])(this, dt);
}
inline void ParticleSystem::vsetVector(float x, float y, float z)
{
    typedef void (__attribute__((thiscall)) *fn)(ParticleSystem *, float, float, float);
    ((fn)pVtable_[PS_VT_SETVEC])(this, x, y, z);
}
inline void ParticleSystem::vrender(RenderDevice *dev)
{
    typedef DWORD (__attribute__((thiscall)) *fn)(ParticleSystem *, RenderDevice *);
    ((fn)pVtable_[PS_VT_RENDER])(this, dev);
}
inline void ParticleSystem::vtransformCorners(float *matrix)
{
    typedef void (__attribute__((thiscall)) *fn)(ParticleSystem *, float *);
    ((fn)pVtable_[PS_VT_XFORM])(this, matrix);
}
