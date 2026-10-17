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
#include "particletypes.h"
 
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
    /* What has been emitted since the last call, and where it samples the
     * tables; out.count is 0 for nothing.  Starts the count again. */
    virtual void takeEmit(ParticleEmit &out) { out.count = 0; }
    /* The tables emission samples, or false for none. */
    virtual bool tables(ParticleTables &out) const { (void)out; return false; }
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
    virtual void takeField(ParticleField &out) { out.kind = ParticleFieldKind::None; }
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

/* What StdGenerator and CylinderGenerator share: emission never samples a
 * distribution at runtime.  Load fills the four tables once, by sampling with
 * rand(), and the device reads them from cursors that step by one per
 * particle (three for velocity) and wrap. */
class TableGenerator : public Generator {
public:
    void tick(float dt) override;
    void takeEmit(ParticleEmit &out) override;
    bool tables(ParticleTables &out) const override;

protected:
    /* Where a new particle's position and velocity go from the tables'. */
    virtual void placement(ParticleEmit &out) const = 0;

    static constexpr uint32_t POS_COUNT = 500, VEL_COUNT = 500, LIFE_COUNT = 100,
                              COLOUR_COUNT = 200;

    float     flDtScale_ = {};  // particles per second
    std::vector<uint32_t> typeTable_;  // dwTypeTableCount_ (colour, weight) pairs
    uint32_t     dwTypeTableCount_ = {};
    float     flPosTable_[POS_COUNT * 3] = {};  // (x, y, z) samples
    float     flVelTable_[VEL_COUNT * 3] = {};
    float     pLifeTable_[LIFE_COUNT] = {};
    uint32_t     pEmitProb_[COLOUR_COUNT] = {};  // colours
    float     flAccumulator_ = {};
    uint32_t     dwPosIdx_ = {};
    uint32_t     dwVelIdx_ = {};
    uint32_t     dwLifeIdx_ = {};
    uint32_t     dwProbIdx_ = {};
    /* Emitted since the last takeEmit, and the cursors the first read. */
    uint32_t     pendingCount_ = 0;
    uint32_t     pendingCursor_[4] = {};
};

/* StdGenerator: the default emitter, used by most shipping effects.  Its
 * positions are sampled from a sphere or a box. */
class StdGenerator : public TableGenerator {
public:
    StdGenerator();

    int copyFrom(const Generator *src) override;
    int load(std::istream &in) override;

protected:
    void placement(ParticleEmit &out) const override;
    void stdBuildSphere(const float *mn, const float *mx);
    void stdBuildBox(const float *mn, const float *mx);
    void stdBuildVelocity(const float *vmin, const float *vmax, float lmin,
                          float lmax);
    void stdBuildRate(float lo, float hi);
    void stdInterleavePos(const float *x, const float *y, const float *z);

    uint32_t     dwEmitMode_ = {};   // selects which of the Sph/Box parameter pairs below Load samples from
    float     flSphMin_[3] = {};  // consumed only by Load, to build the tables
    float     flSphMax_[3] = {};
    float     flBoxMin_[3] = {};
    float     flBoxMax_[3] = {};
    float     flVelMin_[3] = {};
    float     flVelMax_[3] = {};
    float     flLifeMin_ = {};
    float     flLifeMax_ = {};
    float     flEmitRateMin_ = {};
    float     flEmitRateMax_ = {};
};

/* XStdGenerator: StdGenerator plus a constant offset added to every sampled
 * position and velocity, used for thruster and flame effects. */
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

/* CylinderGenerator: samples a unit circle, then scales the position,
 * carries it through flMatrix and offsets it by flOrigin.  Velocity is taken
 * from its table unscaled and untransformed. */
class CylinderGenerator : public TableGenerator {
public:
    CylinderGenerator();

    int copyFrom(const Generator *src) override;
    int load(std::istream &in) override;
    void setPosition(float x, float y, float z) override;
    void setDirection(float x, float y, float z) override;

protected:
    void placement(ParticleEmit &out) const override;

private:
    void cylBuildVelocity(const float *vmin, const float *vmax, float lmin,
                          float lmax);
    void cylBuildRate(float lo, float hi);

    float     flOrigin_[3] = {};
    float     flDirection_[3] = {};
    float     flScale_ = {};       // scale applied to the sampled position, before the matrix
    float     flMatrix_[16] = {};  // rebuilt whenever SetDirection is called
    float     flVelMin_[3] = {};
    float     flVelMax_[3] = {};
    float     flLifeMin_ = {};
    float     flLifeMax_ = {};
    float     flEmitRateMin_ = {};
    float     flEmitRateMax_ = {};
};

/* Gen_FillGaussianField writes into an ExplodeDebris (explodedebris.h); its
 * layout and the offsets this relies on are asserted there. */
  void  
Gen_FillGaussianField(ExplodeDebris *self, float mu, float sigma);

