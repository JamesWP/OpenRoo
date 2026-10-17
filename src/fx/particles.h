#pragma once

#include <memory>
#include <vector>
#include <stdint.h>
#include <istream>
#include <stddef.h>
class RenderDevice;

#include "particletypes.h"

/* ParticleSystem hierarchy: a fixed number of particle slots on the device,
 * the generator that emits into them and the environment that moves, fades
 * and retires them.  Implemented in particles.cpp.
 *
 * The particles themselves live only on the device (particletypes.h): the
 * system's tick does the CPU's share -- how many to emit, how far colours
 * fade -- and the next render hands
 * that to the device as one step before drawing.  Ticks that come between
 * two renders are folded into one step. */

/* The slots, as the CPU knows them: how many, and the shape
 * each draws with (XFace only). */
struct ParticleSlots {
    void release();
    /* Give every slot a random shape index in [0, shapes-1]. */
    void assignShapes(uint32_t shapes);
    /* `count` slots, with no shapes until assignShapes; fewer than two is
     * refused. */
    int alloc(uint32_t count);

    uint32_t              count = 0;
    std::vector<uint32_t> shapes;
};

class Generator;
class Environment;

/* Base class.  Its fields are protected: the two subclasses work on them. */
class ParticleSystem {
public:
    /* The factory: one of the class names, allocated and constructed. */
    static ParticleSystem *create(const char *name);
    /* The two ways the game builds a system: clone, and load from a file. */
    ParticleSystem *clone() const;
    static ParticleSystem *loadFile(const char *path);

    virtual ~ParticleSystem();
    ParticleSystem(const ParticleSystem &) = delete;
    ParticleSystem &operator=(const ParticleSystem &) = delete;

    /* Advance by dt seconds: the CPU's share, which the next render sends. */
    virtual void tick(float dt);
    /* Run what has been ticked since the last render on the device, then
     * draw the particles with the device's current state and world. */
    void render(RenderDevice *dev);
    /* The same, drawn at each of `count` placements in one draw. */
    void render(RenderDevice *dev, const ParticleInstance *instances, uint32_t count);
    /* Face only: the quad's direction, and a transform of its corners. */
    virtual void  setVector(float, float, float) {}
    virtual void  transformCorners(float *) {}
    /* What transformCorners has made of the corners since setVector. */
    virtual Mat4  cornerTransform() const;

    Generator *getGenerator(const char *name);
    void enableRenderNode();
    void disableRenderNode();

    const char         *name() const        { return pName_; }
    Generator          *generator() const   { return pGenerator_; }
    Environment        *environment() const { return pEnvironment_; }

protected:
    ParticleSystem();
    static ParticleSystem *loadStream(std::istream &in);

    /* Drop the generator and environment (when flags is set), the slots and
     * the device buffer. */
    virtual void release(int flags);
    /* Type-name gate, then new slots, generator and environment like src's. */
    virtual int copyFrom(const ParticleSystem *src);
    virtual int load(std::istream &in);
    /* How render draws. */
    virtual void look(ParticleLook &out) const = 0;
    /* Hands the device what buffer_'s slots draw with, once it is made. */
    virtual void uploadShapes(RenderDevice *) {}

    /* Hand the slots to the new sub-object first; only if it accepts is the
     * old one dropped.  A NULL argument is refused. */
    int setGenerator(Generator *gen);
    int setEnvironment(Environment *env);

    void setRenderNode(uint32_t enabled);

    const char     *pName_;
    ParticleSlots   slots_;
    Generator      *pGenerator_;
    Environment    *pEnvironment_;

    /* The device's copy, made at the first render. */
    ParticleBuffer *buffer_ = nullptr;
    uint32_t        nextSlot_ = 0;     // where the next emission starts
    float           pendingDt_ = 0.0f; // ticked but not yet stepped
    float           age_ = 0.0f;       // seconds ticked
};

class FaceParticleSystem : public ParticleSystem {
public:
    FaceParticleSystem();

    void  setVector(float x, float y, float z) override;
    void  transformCorners(float *matrix) override;
    Mat4  cornerTransform() const override;

protected:
    int  copyFrom(const ParticleSystem *src) override;
    int  load(std::istream &in) override;
    void look(ParticleLook &out) const override;

private:
    static const float FACE_UV[6][2];
    Vec3            facing_ = { 0, 0, 1 };
    Mat4            corners_ = { { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 } };
    float           flScale_;
};

/* Each particle is drawn with one of dwCornerTableCount shapes, tumbling,
 * and two-sided. */
class XFaceParticleSystem : public ParticleSystem {
public:
    XFaceParticleSystem();

protected:
    void  release(int flags) override;
    int  copyFrom(const ParticleSystem *src) override;
    int  load(std::istream &in) override;
    void look(ParticleLook &out) const override;
    void uploadShapes(RenderDevice *dev) override;

private:
    int buildCorners();

    static const float XFACE_UV[6][2];
    std::vector<ParticleShape> shapes_;  // dwCornerTableCount of them
    uint32_t             dwCornerTableCount_;
    float             ranges[8];
};
