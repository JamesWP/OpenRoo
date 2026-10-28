#pragma once

#include <memory>
#include <vector>
#include <stdint.h>
#include <istream>
#include <stddef.h>
class RenderDevice;

struct ParticleBuffer;
struct ParticleInstance;
struct ParticleLook;
struct ParticlePath;
struct ParticleShape;

/* ParticleSystem hierarchy: how many particles show at once, the generator
 * that emits them and the environment that moves, fades and retires them.
 * Implemented in particles.cpp.
 *
 * Nothing is stepped: a draw is a function of its ParticleClock.  The
 * generator emits at a steady rate from `start` until `end`; the device works
 * out where each particle is at `time` from its number and the seed
 * (particletypes.h). */

/* When a draw is: all in seconds on one clock. */
struct ParticleClock {
    double time = 0.0;                   // now
    double start = 0.0;                  // emission began
    double end = 1e300;                  // and stopped
    const ParticlePath *path = nullptr;  // where a moving emitter was
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

    /* Draw the particles as they are at the clock, with the device's
     * current state and world. */
    void render(RenderDevice *dev, const ParticleClock &clock, uint32_t seed = 0);
    /* The same, drawn at each of `count` placements in one draw. */
    void render(RenderDevice *dev, const ParticleClock &clock,
                const ParticleInstance *instances, uint32_t count);
    /* Face only: the quad's direction, and a transform of its corners. */
    virtual void  setVector(float, float, float) {}
    virtual void  transformCorners(float *) {}
    /* What transformCorners has made of the corners since setVector: a
     * row-major 4x4, row vectors. */
    virtual void  cornerTransform(float out[16]) const;

    Generator *getGenerator(const char *name);

    const char         *name() const        { return pName_; }
    Generator          *generator() const   { return pGenerator_; }
    Environment        *environment() const { return pEnvironment_; }

protected:
    ParticleSystem();
    static ParticleSystem *loadStream(std::istream &in);

    /* Drop the generator and environment (when flags is set) and the device
     * buffer. */
    virtual void release(int flags);
    /* Type-name gate, then the count, generator and environment like src's. */
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

    const char     *pName_;
    uint32_t        count_ = 0;  // the most that show at once
    Generator      *pGenerator_;
    Environment    *pEnvironment_;

    /* The device's copy, made at the first render. */
    ParticleBuffer *buffer_ = nullptr;
};

class FaceParticleSystem : public ParticleSystem {
public:
    FaceParticleSystem();

    void  setVector(float x, float y, float z) override;
    void  transformCorners(float *matrix) override;
    void  cornerTransform(float out[16]) const override;

protected:
    int  copyFrom(const ParticleSystem *src) override;
    int  load(std::istream &in) override;
    void look(ParticleLook &out) const override;

private:
    static const float FACE_UV[6][2];
    float           facing_[3] = { 0, 0, 1 };
    float           corners_[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    float           flScale_;
};

/* Each particle is drawn with one of dwCornerTableCount shapes, tumbling,
 * and two-sided. */
class XFaceParticleSystem : public ParticleSystem {
public:
    XFaceParticleSystem();
    ~XFaceParticleSystem() override;

protected:
    void  release(int flags) override;
    int  copyFrom(const ParticleSystem *src) override;
    int  load(std::istream &in) override;
    void look(ParticleLook &out) const override;
    void uploadShapes(RenderDevice *dev) override;

private:
    int buildCorners();

    static const float XFACE_UV[6][2];
    std::unique_ptr<ParticleShape[]> shapes_;  // dwCornerTableCount of them
    uint32_t             dwCornerTableCount_;
    float             ranges[8];
};
