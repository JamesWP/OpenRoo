/* Particle generators and environments: the emitters that put new particles
 * into a particle system's slots, and the fields that move, age and retire
 * them once they are there.  The particles are on the device
 * (particletypes.h); these keep the parameters and the CPU's bookkeeping, and
 * hand each step's share over as a ParticleEmit and a ParticleField. */

#pragma once

#include <vector>
#include <stdint.h>
#include <istream>
#include <stddef.h>
#include "particles.h"
struct ParticleEmit;
struct ParticleField;
 
#include "explodedebris.h"

/* Base of every emitter.  pName identifies the concrete class for Load
 * and for copyFrom's type check, and dwEnabled gates whether it emits at
 * all. */
class Generator {
public:
    /* The factory: the named class allocated and constructed, or NULL for a
     * name none of the generator classes has. */
    static Generator *create(const char *name);
    /* A new instance of this class, copied with copyFrom; NULL on failure. */
    Generator *clone() const;

    Generator();
    virtual ~Generator() = default;

    /* Gates on a matching type name; a subclass copies its own fields too. */
    virtual int copyFrom(const Generator *src);
    /* Counts what dt's emission is; takeEmit hands it over. */
    virtual void tick(float dt) { (void)dt; }
    /* What has been emitted since the last call, and the distributions it
     * is drawn from; out.count is 0 for nothing.  Starts the count again. */
    virtual void takeEmit(ParticleEmit &out);
    virtual int load(std::istream &in) { (void)in; return 1; }
    virtual void setPosition(float x, float y, float z)  { (void)x; (void)y; (void)z; }
    virtual void setVelocity(float x, float y, float z, float mag)
        { (void)x; (void)y; (void)z; (void)mag; }
    virtual void setDirection(float x, float y, float z) { (void)x; (void)y; (void)z; }
    virtual void setSpeed(float mag) { (void)mag; }

    const char *name() const            { return pName_; }
    uint32_t       enabled() const         { return dwEnabled_; }
    void        setEnabled(uint32_t e)     { dwEnabled_ = e; }

protected:
    char       *pName_;
    uint32_t       dwEnabled_;  // zero stops this generator emitting
};

/* Base of every environment: what acts on the particles once emitted.
 * There is no enable flag here.  The base class itself is a field that does
 * nothing: its particles never age. */
class Environment {
public:
    /* The factory, for the three environment class names. */
    static Environment *create(const char *name);
    Environment *clone() const;

    Environment();
    virtual ~Environment() = default;

    /* The base copyFrom only gates on type name. */
    virtual int copyFrom(const Environment *src) { return envSameName(src); }
    /* Counts dt's fade; takeField hands it over. */
    virtual void tick(float dt) { (void)dt; }
    /* The field, and the fade since the last call, which starts again. */
    virtual void takeField(ParticleField &out);
    virtual int load(std::istream &in) { (void)in; return 1; }

    const char *name() const   { return pName_; }

protected:
    int envSameName(const Environment *src) const;

    char       *pName_;
};

/* GravityEnvironment: a per-second gravity vector applied to live particles,
 * with a colour fade toward a target and up to three axis kill planes. */
class GravityEnvironment : public Environment {
public:
    GravityEnvironment();

    int copyFrom(const Environment *src) override;
    void tick(float dt) override;
    void takeField(ParticleField &out) override;
    int load(std::istream &in) override;

private:
    void gravitySetVector(const float dir[3], float mag);

    float       flGravity_[3] = {};
    uint32_t       dwTargetRGB_ = {};  // 0x00RRGGBB
    float       flFadeRate_ = {};
    uint32_t       dwFadeThreshold_ = 10;  // a fade step below this is not applied
    uint32_t       dwClipEnable_[3] = {};
    float       flClipMax_[3] = {};
    float       flClipMin_[3] = {};
    float       flFadeAccum_ = {};
    float       flFadePending_ = {};  // fade steps ticked, not yet taken
};

/* MagnetEnvironment: pulls live particles toward flCentre and retires any that
 * arrive within flHalfExtent of it, with the same colour fade as
 * GravityEnvironment. */
class MagnetEnvironment : public Environment {
public:
    MagnetEnvironment();

    int copyFrom(const Environment *src) override;
    void tick(float dt) override;
    void takeField(ParticleField &out) override;
    int load(std::istream &in) override;

private:
    float       flCentre_[3] = {};
    float       flForce_[3] = {};
    float       flHalfExtent_[3] = {};
    uint32_t       dwTargetRGB_ = {};  // never loaded: black, unless copied
    float       flFadeRate_ = {};
    uint32_t       dwFadeThreshold_ = 10;
    float       flFadeAccum_ = {};
    float       flFadePending_ = {};
};

/* What StdGenerator and CylinderGenerator share: a rate, and the
 * distributions every new particle's velocity, life and colour are drawn
 * from, on the device.  Each subclass says where they start.
 *
 * The files' names for these are not what they do; as loaded:
 *   VelMin/VelMax    the direction's mean and variance, per axis
 *   LifeMin/LifeMax  the speed's mean and variance
 *   RateMin/RateMax  the life's mean and variance, in seconds
 *   DtScale          the rate, particles a second */
class EmitterGenerator : public Generator {
public:
    EmitterGenerator();

    void tick(float dt) override;
    void takeEmit(ParticleEmit &out) override;

protected:
    /* Where a new particle starts. */
    virtual void placement(ParticleEmit &out) const = 0;
    /* The distributions and the colours, in the order the files have them. */
    bool loadMotion(std::istream &in);
    bool loadColours(std::istream &in);

    float     rate_ = {};
    float     dirMean_[3] = {}, dirVariance_[3] = {};
    float     speedMean_ = {}, speedVariance_ = {};
    float     lifeMean_ = {}, lifeVariance_ = {};
    std::vector<uint32_t> colours_;  // (0xAARRGGBB, weight) pairs
    float     accumulator_ = {};
    uint32_t  pendingCount_ = 0;     // emitted since the last takeEmit
    uint32_t  seed_;                 // this generator's, stepped every take
};

/* StdGenerator: the default emitter, used by most shipping effects.  New
 * particles start about a point (normal per axis) or in a box. */
class StdGenerator : public EmitterGenerator {
public:
    StdGenerator();

    int copyFrom(const Generator *src) override;
    int load(std::istream &in) override;

protected:
    void placement(ParticleEmit &out) const override;

    uint32_t  emitMode_ = {};     // 0: about a point, 1: in a box
    float     boxMax_[3] = {}, boxMin_[3] = {};
    float     posMean_[3] = {}, posVariance_[3] = {};
};

/* XStdGenerator: StdGenerator plus a constant offset added to every new
 * particle's position and velocity, used for thruster and flame effects. */
class XStdGenerator : public StdGenerator {
public:
    XStdGenerator();

    int copyFrom(const Generator *src) override;
    int load(std::istream &in) override;
    void setPosition(float x, float y, float z) override;
    void setVelocity(float x, float y, float z, float mag) override;
    void setDirection(float x, float y, float z) override;
    void setSpeed(float mag) override;

protected:
    void placement(ParticleEmit &out) const override;

private:
    void xstdStoreScaled(double x, double y, double z, double len, double mag);

    float        flPosOffset_[3] = {};
    float        flVelOffset_[3] = {};
};

/* CylinderGenerator: new particles start on a ring: the unit circle scaled,
 * turned to flDirection and moved to flOrigin.  Velocity is not turned. */
class CylinderGenerator : public EmitterGenerator {
public:
    CylinderGenerator();

    int copyFrom(const Generator *src) override;
    int load(std::istream &in) override;
    void setPosition(float x, float y, float z) override;
    void setDirection(float x, float y, float z) override;

protected:
    void placement(ParticleEmit &out) const override;

private:
    float     flOrigin_[3] = {};
    float     flDirection_[3] = {};
    float     flScale_ = {};       // the ring's radius
    float     flMatrix_[16] = {};  // rebuilt whenever SetDirection is called
};

/* Gen_FillGaussianField writes into an ExplodeDebris (explodedebris.h); its
 * layout and the offsets this relies on are asserted there. */
  void  
Gen_FillGaussianField(ExplodeDebris *self, float mu, float sigma);

