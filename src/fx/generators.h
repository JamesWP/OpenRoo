/* Particle generators and environments: the emitters a particle system's
 * particles come from, and the fields that move, fade and retire them.  The
 * particles are worked out on the device (particletypes.h); these keep the
 * parameters and describe them as a ParticleEmit and a ParticleField. */

#pragma once

#include <vector>
#include <stdint.h>
#include <istream>
#include <stddef.h>
#include "particles.h"
struct ParticleEmit;
struct ParticleField;
 

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
    /* Particles a second; 0 emits nothing. */
    virtual float rate() const { return 0.0f; }
    /* The distributions new particles are drawn from (not when: that is the
     * clock's). */
    virtual void describe(ParticleEmit &out) const { (void)out; }
    virtual int load(std::istream &in) { (void)in; return 1; }
    virtual void setPosition(float x, float y, float z)  { (void)x; (void)y; (void)z; }
    virtual void setVelocity(float x, float y, float z, float mag)
        { (void)x; (void)y; (void)z; (void)mag; }
    virtual void setDirection(float x, float y, float z) { (void)x; (void)y; (void)z; }
    virtual void setSpeed(float mag) { (void)mag; }

    const char *name() const            { return pName_; }

protected:
    char       *pName_;
    uint32_t       dwEnabled_;  // loaded and copied, but never cleared
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
    /* The field. */
    virtual void describe(ParticleField &out) const;
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
    void describe(ParticleField &out) const override;
    int load(std::istream &in) override;

private:
    void gravitySetVector(const float dir[3], float mag);

    float       flGravity_[3] = {};
    uint32_t       dwTargetRGB_ = {};  // 0x00RRGGBB
    float       flFadeRate_ = {};
    uint32_t       dwFadeThreshold_ = 10;  // loaded; the fade's rate is the same either way
    uint32_t       dwClipEnable_[3] = {};
    float       flClipMax_[3] = {};
    float       flClipMin_[3] = {};
};

/* MagnetEnvironment: pulls live particles toward flCentre and retires any that
 * arrive within flHalfExtent of it, with the same colour fade as
 * GravityEnvironment. */
class MagnetEnvironment : public Environment {
public:
    MagnetEnvironment();

    int copyFrom(const Environment *src) override;
    void describe(ParticleField &out) const override;
    int load(std::istream &in) override;

private:
    float       flCentre_[3] = {};
    float       flForce_[3] = {};
    float       flHalfExtent_[3] = {};
    uint32_t       dwTargetRGB_ = {};  // never loaded: black, unless copied
    float       flFadeRate_ = {};
    uint32_t       dwFadeThreshold_ = 10;
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

    float rate() const override { return dwEnabled_ ? rate_ : 0.0f; }
    void describe(ParticleEmit &out) const override;

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


