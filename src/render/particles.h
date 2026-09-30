#pragma once
#include <windows.h>
#include <stddef.h>
class RenderDevice;

/* ParticleSystem hierarchy: the systems, their ring of particles, and the
 * vertex buffers they fill.  Implemented in particles.cpp. */

/* Ring node, 0x2C bytes. The ring is ONE NULL-terminated doubly-linked list
 * (not circular), split into a live region [pRingHead, pRingCurrent) and a free
 * region [pRingCurrent, pRingTail].  Generators claim at pRingCurrent;
 * environments age and retire. */
struct ParticleNode {
    /* The diffuse the fills write: dwDiffuse, or the KAROO_PARTICLE_FX=tint
     * colour. */
    DWORD colour() const;

    ParticleNode *pPrev;          // +0x00
    ParticleNode *pNext;          // +0x04
    float         flX, flY, flZ;  // +0x08..+0x10 position
    float         flVel[3];       // +0x14..+0x1c velocity
    float         flLife;         // +0x20 seconds remaining; < 0 retires
    DWORD         dwDiffuse;      // +0x24
    DWORD         dwShapeIndex;   // +0x28 XFace corner-table index; alloc-time only
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
    /* Allocation and release of the nodes (particles.cpp). */
    void release();
    void assignShapes(DWORD shapes);
    BOOL alloc(DWORD count, DWORD shapes);

    DWORD         dwRingCount  = 0;
    ParticleNode *pRingBase    = NULL;
    ParticleNode *pRingHead    = NULL;  // oldest live particle
    ParticleNode *pRingTail    = NULL;  // last free node
    ParticleNode *pRingCurrent = NULL;  // next node to emit into
};

class Generator;
class Environment;
struct GameLogger;

/* Base class.  Its fields are protected: the three subclasses work on them. */
class ParticleSystem {
public:
    /* The factory: one of the four class names, allocated and constructed. */
    static ParticleSystem *create(const char *name);
    /* The two ways the game builds a system: clone, and load from a file. */
    ParticleSystem *clone() const;
    static ParticleSystem *loadFile(const char *path, GameLogger *log);

    ParticleSystem();
    virtual ~ParticleSystem();
    ParticleSystem(const ParticleSystem &) = delete;
    ParticleSystem &operator=(const ParticleSystem &) = delete;

    /* Simulate and draw one frame.  render fills the vertex buffer, then draws
     * it; the point, face and xface classes supply fill and draw. */
    virtual void  tick(float dt);
    virtual DWORD render(RenderDevice *dev);
    virtual void  fill() {}
    virtual DWORD draw(RenderDevice *) { return 0; }
    /* Face only: the quad's direction, and a transform of its corners. */
    virtual void  setVector(float, float, float) {}
    virtual void  transformCorners(float *) {}

    Generator *getGenerator(const char *name);
    void enableRenderNode();
    void disableRenderNode();

    const char         *name() const        { return pName_; }
    const RingBuffer   &ring() const        { return ring_; }
    RingBuffer         &ring()              { return ring_; }
    Generator          *generator() const   { return pGenerator_; }
    Environment        *environment() const { return pEnvironment_; }

protected:
    static ParticleSystem *loadStream(void *fp, GameLogger *log);

    /* Drop the generator and environment (when flags is set) and the ring. */
    virtual void release(int flags);
    /* Type-name gate, then a new ring, generator and environment like src's. */
    virtual BOOL copyFrom(const ParticleSystem *src);
    virtual BOOL setCapacity(DWORD count);
    virtual BOOL resize(DWORD count);
    virtual BOOL save(void *fp, GameLogger *log);
    virtual BOOL load(void *fp, GameLogger *log);

    /* Hand the ring to the new sub-object first; only if it accepts is the old
     * one dropped.  A NULL argument is refused. */
    BOOL setGenerator(Generator *gen);
    BOOL setEnvironment(Environment *env);

    template <typename EmitFn> DWORD fillRing(EmitFn emit);
    void setRenderNode(DWORD enabled);

    /* The scratch vertex buffer the subclasses fill: perNode vertices per
     * ring node, zeroed, with the texture corners baked in from uv (NULL for
     * none).  Frees the old buffer first. */
    BOOL allocVerts(unsigned perNode, const float (*uv)[2] = NULL);

    char           *pName_;
    RingBuffer      ring_;          // handed to the generator and environment
    Generator      *pGenerator_;
    Environment    *pEnvironment_;
    ParticleVertex *pVerts_;
    DWORD           dwVertexCount_;  // vertices the last fill wrote
};

class PointParticleSystem : public ParticleSystem {
public:
    PointParticleSystem();

    void  fill() override;
    DWORD draw(RenderDevice *dev) override;

protected:
    BOOL  copyFrom(const ParticleSystem *src) override;
    BOOL  setCapacity(DWORD count) override;
    BOOL  resize(DWORD count) override;
    BOOL  save(void *fp, GameLogger *log) override;
    BOOL  load(void *fp, GameLogger *log) override;
};

class FaceParticleSystem : public ParticleSystem {
public:
    FaceParticleSystem();

    void  fill() override;
    DWORD draw(RenderDevice *dev) override;
    void  setVector(float x, float y, float z) override;
    void  transformCorners(float *matrix) override;

protected:
    BOOL  copyFrom(const ParticleSystem *src) override;
    BOOL  setCapacity(DWORD count) override;
    BOOL  resize(DWORD count) override;
    BOOL  save(void *fp, GameLogger *log) override;
    BOOL  load(void *fp, GameLogger *log) override;

private:
    BOOL allocVerts() { return ParticleSystem::allocVerts(6, FACE_UV); }

    static const float FACE_UV[6][2];
    float           flCorner_[6][3]; // six baked xyz corner offsets
    float           flScale_;
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

class XFaceParticleSystem : public ParticleSystem {
public:
    XFaceParticleSystem();
    ~XFaceParticleSystem() override;

    void  tick(float dt) override;
    void  fill() override;
    DWORD draw(RenderDevice *dev) override;

protected:
    void  release(int flags) override;
    BOOL  copyFrom(const ParticleSystem *src) override;
    BOOL  setCapacity(DWORD count) override;
    BOOL  resize(DWORD count) override;
    BOOL  save(void *fp, GameLogger *log) override;
    BOOL  load(void *fp, GameLogger *log) override;

private:
    BOOL allocVerts() { return ParticleSystem::allocVerts(6, XFACE_UV); }
    BOOL buildCorners();

    static const float XFACE_UV[6][2];
    XFaceCornerEntry *pCornerTable_; // dwCornerTableCount entries
    DWORD             dwCornerTableCount_;
    float             ranges[8];
};
