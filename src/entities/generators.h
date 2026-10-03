/* Particle generators and environments: the emitters that fill a particle
 * system's ring with new particles, and the fields that age and retire them
 * once they are in it.  A Generator subclass overrides tick/save/load/copyFrom;
 * an Environment subclass overrides the same four for ageing, fading and
 * killing particles already in the ring. */

#pragma once

#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include "particles.h"
 
#include "explodedebris.h"

/* Base of every emitter.  pName identifies the concrete class for Save/Load
 * and for copyFrom's type check, and dwEnabled gates whether tick runs at all.
 * pRing is the particle system's ring buffer, shared with the paired
 * Environment. */
class Generator {
public:
    /* The factory: the named class allocated and constructed, or NULL for a
     * name none of the six generator classes has. */
    static Generator *create(const char *name);
    /* A new instance of this class, copied with copyFrom; NULL on failure. */
    Generator *clone() const;

    Generator();
    virtual ~Generator() = default;

    /* Gates on a matching type name; a subclass copies its own fields too. */
    virtual int copyFrom(const Generator *src);
    /* Emits into the ring. */
    virtual void tick(float dt) { (void)dt; }
    virtual int save(FILE *fp) { (void)fp; return 1; }
    virtual int load(FILE *fp) { (void)fp; return 1; }
    virtual void setPosition(float x, float y, float z)  { (void)x; (void)y; (void)z; }
    virtual void setVelocity(float x, float y, float z, float mag)
        { (void)x; (void)y; (void)z; (void)mag; }
    virtual void setDirection(float x, float y, float z) { (void)x; (void)y; (void)z; }
    virtual void setSpeed(float mag) { (void)mag; }

    /* Refuses (and leaves pRing alone) a NULL ring. */
    int attachRing(RingBuffer *ring);

    const char *name() const            { return pName_; }
    uint32_t       enabled() const         { return dwEnabled_; }
    void        setEnabled(uint32_t e)     { dwEnabled_ = e; }

protected:
    char       *pName_;
    uint32_t       dwEnabled_;  // zero disables tick for this generator
    RingBuffer *pRing_;
};

 
/* Base of every environment.  Same role as Generator for the fields it has;
 * there is no enable flag here, so an environment can only be turned off by
 * detaching or not ticking it. */
class Environment {
public:
    /* The factory, for the three environment class names. */
    static Environment *create(const char *name);
    Environment *clone() const;

    Environment();
    virtual ~Environment() = default;

    /* The base copyFrom only gates on type name. */
    virtual int copyFrom(const Environment *src) { return envSameName(src); }
    virtual void tick(float dt) { (void)dt; }
    virtual int save(FILE *fp) { (void)fp; return 1; }
    virtual int load(FILE *fp) { (void)fp; return 1; }

    /* Refuses (and leaves pRing alone) a NULL ring. */
    int attachRing(RingBuffer *ring);

    const char *name() const   { return pName_; }

protected:
    int envSameName(const Environment *src) const;

    char       *pName_;
    RingBuffer *pRing_;
};

/* GravityEnvironment: a per-second gravity vector applied to live particles,
 * with a colour fade toward a target and up to three axis kill planes. */
class GravityEnvironment : public Environment {
public:
    GravityEnvironment();

    int copyFrom(const Environment *src) override;
    void tick(float dt) override;
    int save(FILE *fp) override;
    int load(FILE *fp) override;

private:
    void gravityTick(float dt);
    void gravitySetVector(const float dir[3], float mag);
    void gravitySetColour(uint32_t argb, float fade);

    float       flDirection_[3] = {};
    float       flMagnitude_ = {};
    float       flGravity_[3] = {};
    uint32_t       dwTargetARGB_ = {};
    uint32_t       dwTargetA_ = {};  // never read
    uint32_t       dwTargetRGB_[3] = {};
    float       flFadeRate_ = {};
    uint32_t       dwFadeThreshold_ = 10;  // a fade step below this is not applied
    uint32_t       dwClipEnable_[3] = {};
    float       flClipMax_[3] = {};
    float       flClipMin_[3] = {};
    float       flFadeAccum_ = {};

 
};

/* MagnetEnvironment: pulls live particles toward flCentre and retires any that
 * arrive within flHalfExtent of it, with the same colour-fade skeleton as
 * GravityEnvironment. */
class MagnetEnvironment : public Environment {
public:
    MagnetEnvironment();

    int copyFrom(const Environment *src) override;
    void tick(float dt) override;
    int save(FILE *fp) override;
    int load(FILE *fp) override;

private:
    void magnetTick(float dt);

    float       flCentre_[3] = {};
    float       flForce_[3] = {};
    float       flHalfExtent_[3] = {};
    float       flRange_ = {};    // serialised, unread by Tick
    uint32_t       dwField34_ = {};  // serialised, unread by Tick
    uint32_t       dwTargetRGB_[3] = {};
    float       flFadeRate_ = {};
    uint32_t       dwFadeThreshold_ = 10;
    float       flFadeAccum_ = {};

 
};

/* StdGenerator: the default emitter, used by most shipping effects.  Emit
 * never samples a distribution at runtime; Load fills the four tables below
 * once, by sampling with rand(), and Tick/emit only ever indexes them by the
 * four cursors at the end of the struct. */
class StdGenerator : public Generator {
public:
    StdGenerator();
    ~StdGenerator() override;

    int copyFrom(const Generator *src) override;
    void tick(float dt) override;
    int save(FILE *fp) override;
    int load(FILE *fp) override;

protected:
    void stdEmit(float dt, const float *pos_off, const float *vel_off);
    void stdCloneTypeTable(const uint32_t *src, uint32_t count);
    int stdSaveTypeTable(FILE *fp);
    int stdLoadTypeTable(FILE *fp);
    void stdInterleavePos(const float *x, const float *y, const float *z);
    void stdBuildSphere(const float *mn, const float *mx);
    void stdBuildBox(const float *mn, const float *mx);
    void stdBuildVelocity(const float *vmin, const float *vmax, float lmin,
                          float lmax);
    void stdBuildRate(float lo, float hi);

    float     flDtScale_ = {};
    uint32_t     dwEmitMode_ = {};   // selects which of the Sph/Box parameter pairs below Load samples from
    float     flSphMin_[3] = {};  // consumed only by Load, to build the tables; emit never reads these
    float     flSphMax_[3] = {};
    float     flBoxMin_[3] = {};
    float     flBoxMax_[3] = {};
    float     flVelMin_[3] = {};
    float     flVelMax_[3] = {};
    float     flLifeMin_ = {};
    float     flLifeMax_ = {};
    float     flEmitRateMin_ = {};
    float     flEmitRateMax_ = {};
    std::vector<uint32_t> typeTable_;  // dwTypeTableCount_ (colour, weight) pairs
    uint32_t     dwTypeTableCount_ = {};
    float     flPosTable_[1500] = {};  // 500 samples of (x, y, z), read as node position
    float     flVelTable_[1500] = {};  // 500 samples of (x, y, z), read as node velocity
    float     pLifeTable_[100] = {};
    uint32_t     pEmitProb_[200] = {};
    uint32_t     dwCtr0_ = {};
    float     flAccumulator_ = {};
    uint32_t     dwPosIdx_ = {};  // steps by 1, wraps at 500
    uint32_t     dwVelIdx_ = {};
    uint32_t     dwLifeIdx_ = {};
    uint32_t     dwProbIdx_ = {};

private:
 
};

/* XStdGenerator: StdGenerator plus a constant offset added to every sampled
 * position and velocity, used for thruster and flame effects.  Every field and
 * cursor through StdGenerator is inherited unchanged; only emit differs. */
class XStdGenerator : public StdGenerator {
public:
    XStdGenerator();

    int copyFrom(const Generator *src) override;
    void tick(float dt) override;
    int save(FILE *fp) override;
    int load(FILE *fp) override;
    void setPosition(float x, float y, float z) override;
    void setVelocity(float x, float y, float z, float mag) override;
    void setDirection(float x, float y, float z) override;
    void setSpeed(float mag) override;

private:
    void xstdStoreScaled(double x, double y, double z, double len, double mag);

    float        flPosOffset_[3] = {};
    float        flVelOffset_[3] = {};

 
};

/* CylinderGenerator: samples position and velocity like StdGenerator, then
 * scales the position, carries it through flMatrix and offsets it by flOrigin.
 * Velocity is taken from its table unscaled and untransformed. */
class CylinderGenerator : public Generator {
public:
    CylinderGenerator();
    ~CylinderGenerator() override;

    int copyFrom(const Generator *src) override;
    void tick(float dt) override;
    int save(FILE *fp) override;
    int load(FILE *fp) override;
    void setPosition(float x, float y, float z) override;
    void setDirection(float x, float y, float z) override;

private:
    void cylinderEmit(float dt);
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
    float     flDtScale_ = {};
    std::vector<uint32_t> typeTable_;  // dwTypeTableCount_ (colour, weight) pairs
    uint32_t     dwTypeTableCount_ = {};
    float     flAccumulator_ = {};
    float     flPosTable_[1500] = {};
    float     flVelTable_[1500] = {};
    float     pLifeTable_[100] = {};
    uint32_t     pEmitProb_[200] = {};
    uint32_t     dwPosIdx_ = {};
    uint32_t     dwVelIdx_ = {};
    uint32_t     dwLifeIdx_ = {};
    uint32_t     dwProbIdx_ = {};

 
};

/* PointGenerator: emits every particle at a fixed position and colour; not
 * used by any shipping effect, so its tables are only ever whatever the
 * constructor leaves them as. */
class PointGenerator : public Generator {
public:
    PointGenerator();

    void tick(float dt) override;

private:

    float     flEmitPos_[3];
    float     flVelBias_[3];
    float     flEmitRate_;
    uint8_t      opaque2c_[0x0c];
    uint32_t     dwDiffuse_;  // constructor sets this to 0xFFFFFFFF
    float     flAccumulator_;
    uint8_t      opaque40_[0x04];
    float     flVelTable_[1000];
    uint32_t     dwLifeTable_[100];
    uint32_t     dwVelIdx_[3];
    uint32_t     dwLifeIdx_;  // runs past dwLifeTable's declared length before wrapping

 
};

/* BoxGenerator: like PointGenerator, not used by any shipping effect and never
 * filled by a Load. */
class BoxGenerator : public Generator {
public:
    BoxGenerator();

    void tick(float dt) override;

private:

    uint8_t      opaque10_[0x18];
    float     flVelBias_[3];
    float     flEmitRate_;
    float     flAccumulator_;
    uint32_t     dwPosX_[500];
    uint32_t     dwPosY_[500];
    uint32_t     dwPosZ_[500];
    float     flVelTable_[500];
    uint32_t     dwLifeTable_[100];
    uint32_t     dwDiffuse_[200];
    uint32_t     dwPosIdx_[3];
    uint32_t     dwVelIdx_[3];
    uint32_t     dwLifeIdx_;
    uint32_t     dwDiffuseIdx_;

 
};
/* Gen_FillGaussianField writes into an ExplodeDebris (explodedebris.h); its
 * layout and the offsets this relies on are asserted there. */
  void  
Gen_FillGaussianField(ExplodeDebris *self, float mu, float sigma);

