/* Particle generators and environments: the emitters that fill a particle
 * system's ring with new particles, and the fields that age and retire them
 * once they are in it.  A Generator subclass owns Tick/emit/Save/Load/CopyFrom
 * through its own vtable; an Environment subclass owns the same five for
 * ageing, fading and killing particles already in the ring.  Both bases stay
 * fixed size across builds, so every layout below is exact, not a guess: each
 * struct's static_asserts pin the offsets the .cpp indexes into. */

#pragma once
#include <windows.h>
#include <stddef.h>
#include "particles.h"
#include "layout.h"
#include "explodedebris.h"

/* Vtable slot counts for Generator and Environment; each class's vtable
 * (defined in generators.cpp) must have exactly this many entries. */
#define GEN_VTBL_SLOTS 10
#define ENV_VTBL_SLOTS  6

/* Base of every emitter.  A subclass installs its own vtable in its
 * constructor and restores this base one in its destructor; pName identifies
 * the concrete class for Save/Load and for CopyFrom's type check, and
 * dwEnabled gates whether Tick/emit runs at all.  pRing is the particle
 * system's ring buffer, shared with the paired Environment. */
class Generator {
public:
    /* The factory: the named class allocated and constructed, or NULL for a
     * name none of the six generator classes has. */
    static Generator *create(const char *name);
    /* A new instance of this class, copied with CopyFrom; NULL on failure. */
    Generator *clone() const;

    void vsetPosition(float x, float y, float z);
    void vsetDirection(float x, float y, float z);

    void      **vtable() const          { return pVtable_; }
    const char *name() const            { return pName_; }
    DWORD       enabled() const         { return dwEnabled_; }
    void        setEnabled(DWORD e)     { dwEnabled_ = e; }

    /* The constructor create() runs. */
    void baseGenConstruct();

    /* The vtable slots. */
    static BOOL __attribute__((thiscall)) attachRingSlot(Generator *self, RingBuffer *ring);
    static BOOL __attribute__((thiscall)) baseCopyFromSlot(Generator *self, const Generator *src);
    static void *__attribute__((thiscall)) baseDtorSlot(Generator *self, unsigned flags);
    static void __attribute__((thiscall)) nop1(void *, float);
    static void __attribute__((thiscall)) nop3(void *, float, float, float);
    static void __attribute__((thiscall)) nop4(void *, float, float, float, float);
    static BOOL __attribute__((thiscall)) returnTrue(void *, void *);

protected:
    void baseGenDestruct();
    BOOL genCopyBase(const Generator *src);
    BOOL genAttachRing(RingBuffer *ring);

    void      **pVtable_;  // ten-slot vtable: dtor, CopyFrom, AttachRing, Tick, Save, Load, SetPosition, ..., SetDirection, ...
    char       *pName_;
    DWORD       dwEnabled_;  // zero disables Tick/emit for this generator
    RingBuffer *pRing_;

private:
    static void checkLayout();
};

inline void Generator::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(Generator, pRing_) == 0x0c, "Generator layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(Generator) == 0x10, "Generator size");

/* Base of every environment.  Same role as Generator for the fields it has;
 * there is no enable flag here, so an environment can only be turned off by
 * detaching or not ticking it. */
class Environment {
public:
    /* The factory, for the three environment class names. */
    static Environment *create(const char *name);
    Environment *clone() const;

    void      **vtable() const { return pVtable_; }
    const char *name() const   { return pName_; }

    /* The constructor create() runs. */
    void baseEnvConstruct();

    /* The vtable slots. */
    static void *__attribute__((thiscall)) baseDtorSlot(Environment *self, unsigned flags);
    static BOOL __attribute__((thiscall)) baseCopyFromSlot(Environment *self, const Environment *src);
    static BOOL __attribute__((thiscall)) attachRingSlot(Environment *self, RingBuffer *ring);
    static void __attribute__((thiscall)) baseTickSlot(Environment *, float);
    static BOOL __attribute__((thiscall)) baseSaveSlot(Environment *, void *);
    static BOOL __attribute__((thiscall)) baseLoadSlot(Environment *, void *);

protected:
    BOOL envSameName(const Environment *src) const;
    void baseEnvDestruct();
    BOOL envAttachRing(RingBuffer *ring);

    void      **pVtable_;  // vtable, six slots: dtor, CopyFrom, AttachRing, Tick, Save, Load
    char       *pName_;
    RingBuffer *pRing_;

private:
    static void checkLayout();
};

inline void Environment::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(Environment, pRing_) == 0x08, "Environment layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(Environment) == 0x0c, "Environment size");

/* GravityEnvironment: a per-second gravity vector applied to live particles,
 * with a colour fade toward a target and up to three axis kill planes. */
class GravityEnvironment : public Environment {
public:
    /* The constructor create() runs. */
    void gravityEnvConstruct();

    /* The vtable slots. */
    static void *__attribute__((thiscall)) gravityDtorSlot(GravityEnvironment *self, unsigned flags);
    static BOOL __attribute__((thiscall)) gravityCopyFromSlot(GravityEnvironment *self, const GravityEnvironment *src);
    static BOOL __attribute__((thiscall)) gravitySaveSlot(GravityEnvironment *self, void *fp);
    static BOOL __attribute__((thiscall)) gravityLoadSlot(GravityEnvironment *self, void *fp);
    static void __attribute__((thiscall)) gravityTickSlot(GravityEnvironment *self, float dt);

private:
    void gravityTick(float dt);
    void gravityEnvTick(float dt);
    void gravitySetVector(const float dir[3], float mag);
    void gravitySetColour(DWORD argb, float fade);
    BOOL gravityEnvLoad(void *fp);
    BOOL gravityEnvSave(void *fp);
    BOOL gravityEnvCopyFrom(const GravityEnvironment *src);
    void gravityEnvDestruct();

    float       flDirection_[3];
    float       flMagnitude_;
    float       flGravity_[3];
    DWORD       dwTargetARGB_;
    DWORD       dwTargetA_;  // never read
    DWORD       dwTargetRGB_[3];
    float       flFadeRate_;
    DWORD       dwFadeThreshold_;  // a fade step below this is not applied
    DWORD       dwClipEnable_[3];
    float       flClipMax_[3];
    float       flClipMin_[3];
    float       flFadeAccum_;

    static void checkLayout();
};

inline void GravityEnvironment::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(GravityEnvironment, flMagnitude_)  == 0x18, "Gravity layout");
    static_assert(offsetof(GravityEnvironment, flGravity_)    == 0x1c, "Gravity layout");
    static_assert(offsetof(GravityEnvironment, dwTargetARGB_) == 0x28, "Gravity layout");
    static_assert(offsetof(GravityEnvironment, dwTargetRGB_)  == 0x30, "Gravity layout");
    static_assert(offsetof(GravityEnvironment, flFadeRate_)   == 0x3c, "Gravity layout");
    static_assert(offsetof(GravityEnvironment, dwClipEnable_) == 0x44, "Gravity layout");
    static_assert(offsetof(GravityEnvironment, flClipMax_)    == 0x50, "Gravity layout");
    static_assert(offsetof(GravityEnvironment, flClipMin_)    == 0x5c, "Gravity layout");
    static_assert(offsetof(GravityEnvironment, flFadeAccum_)  == 0x68, "Gravity layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(GravityEnvironment) == 0x6c, "Gravity size");

/* MagnetEnvironment: pulls live particles toward flCentre and retires any that
 * arrive within flHalfExtent of it, with the same colour-fade skeleton as
 * GravityEnvironment. */
class MagnetEnvironment : public Environment {
public:
    /* The constructor create() runs. */
    void magnetEnvConstruct();

    /* The vtable slots. */
    static void *__attribute__((thiscall)) magnetDtorSlot(MagnetEnvironment *self, unsigned flags);
    static BOOL __attribute__((thiscall)) magnetCopyFromSlot(MagnetEnvironment *self, const MagnetEnvironment *src);
    static BOOL __attribute__((thiscall)) magnetSaveSlot(MagnetEnvironment *self, void *fp);
    static BOOL __attribute__((thiscall)) magnetLoadSlot(MagnetEnvironment *self, void *fp);
    static void __attribute__((thiscall)) magnetTickSlot(MagnetEnvironment *self, float dt);

private:
    void magnetTick(float dt);
    void magnetEnvTick(float dt);
    BOOL magnetEnvLoad(void *fp);
    BOOL magnetEnvSave(void *fp);
    BOOL magnetEnvCopyFrom(const MagnetEnvironment *src);
    void magnetEnvDestruct();

    float       flCentre_[3];
    float       flForce_[3];
    float       flHalfExtent_[3];
    float       flRange_;    // serialised, unread by Tick
    DWORD       dwField34_;  // serialised, unread by Tick
    DWORD       dwTargetRGB_[3];
    float       flFadeRate_;
    DWORD       dwFadeThreshold_;
    float       flFadeAccum_;

    static void checkLayout();
};

inline void MagnetEnvironment::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(MagnetEnvironment, flCentre_)       == 0x0c, "Magnet layout");
    static_assert(offsetof(MagnetEnvironment, flForce_)        == 0x18, "Magnet layout");
    static_assert(offsetof(MagnetEnvironment, flHalfExtent_)   == 0x24, "Magnet layout");
    static_assert(offsetof(MagnetEnvironment, dwTargetRGB_)    == 0x38, "Magnet layout");
    static_assert(offsetof(MagnetEnvironment, flFadeRate_)     == 0x44, "Magnet layout");
    static_assert(offsetof(MagnetEnvironment, dwFadeThreshold_)== 0x48, "Magnet layout");
    static_assert(offsetof(MagnetEnvironment, flFadeAccum_)    == 0x4c, "Magnet layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(MagnetEnvironment) == 0x50, "Magnet size");

/* StdGenerator: the default emitter, used by most shipping effects.  Emit
 * never samples a distribution at runtime; Load fills the four tables below
 * once, by sampling with rand(), and Tick/emit only ever indexes them by the
 * four cursors at the end of the struct. */
class StdGenerator : public Generator {
public:
    /* The constructor create() runs. */
    void stdGenConstruct();

    /* The vtable slots. */
    static void *__attribute__((thiscall)) stdDtorSlot(StdGenerator *self, unsigned flags);
    static BOOL __attribute__((thiscall)) stdCopyFromSlot(StdGenerator *self, const StdGenerator *src);
    static BOOL __attribute__((thiscall)) stdSaveSlot(StdGenerator *self, void *fp);
    static BOOL __attribute__((thiscall)) stdLoadSlot(StdGenerator *self, void *fp);
    static void __attribute__((thiscall)) stdEmitSlot(StdGenerator *self, float dt);

protected:
    void stdEmit(float dt, const float *pos_off, const float *vel_off);
    void stdGenTick(float dt);
    void stdCloneTypeTable(const DWORD *src, DWORD count);
    BOOL stdSaveTypeTable(void *fp);
    BOOL stdLoadTypeTable(void *fp);
    void stdInterleavePos(const float *x, const float *y, const float *z);
    void stdBuildSphere(const float *mn, const float *mx);
    void stdBuildBox(const float *mn, const float *mx);
    void stdBuildVelocity(const float *vmin, const float *vmax, float lmin, float lmax);
    void stdBuildRate(float lo, float hi);
    void stdGenDestruct();
    BOOL stdGenCopyFrom(const StdGenerator *src);
    BOOL stdGenSave(void *fp);
    BOOL stdGenLoad(void *fp);

    float     flDtScale_;
    DWORD     dwEmitMode_;   // selects which of the Sph/Box parameter pairs below Load samples from
    float     flSphMin_[3];  // consumed only by Load, to build the tables; emit never reads these
    float     flSphMax_[3];
    float     flBoxMin_[3];
    float     flBoxMax_[3];
    float     flVelMin_[3];
    float     flVelMax_[3];
    float     flLifeMin_;
    float     flLifeMax_;
    float     flEmitRateMin_;
    float     flEmitRateMax_;
    void     *pTypeTable_;  // heap block owned and freed by this object
    DWORD     dwTypeTableCount_;
    float     flPosTable_[1500];  // 500 samples of (x, y, z), read as node position
    float     flVelTable_[1500];  // 500 samples of (x, y, z), read as node velocity
    float     pLifeTable_[100];
    DWORD     pEmitProb_[200];
    DWORD     dwCtr0_;
    float     flAccumulator_;
    DWORD     dwPosIdx_;  // steps by 1, wraps at 500
    DWORD     dwVelIdx_;
    DWORD     dwLifeIdx_;
    DWORD     dwProbIdx_;

private:
    static void checkLayout();
};

inline void StdGenerator::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(StdGenerator, flDtScale_)     == 0x0010, "Std layout");
    static_assert(offsetof(StdGenerator, flPosTable_)    == 0x0078, "Std layout");
    static_assert(offsetof(StdGenerator, flVelTable_)    == 0x17e8, "Std layout");
    static_assert(offsetof(StdGenerator, pLifeTable_)    == 0x2f58, "Std layout");
    static_assert(offsetof(StdGenerator, pEmitProb_)     == 0x30e8, "Std layout");
    static_assert(offsetof(StdGenerator, flAccumulator_) == 0x340c, "Std layout");
    static_assert(offsetof(StdGenerator, dwPosIdx_)      == 0x3410, "Std layout");
    static_assert(offsetof(StdGenerator, dwProbIdx_)     == 0x341c, "Std layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(StdGenerator) == 0x3420, "Std size");

/* XStdGenerator: StdGenerator plus a constant offset added to every sampled
 * position and velocity, used for thruster and flame effects.  Every field and
 * cursor through StdGenerator is inherited unchanged; only emit differs. */
class XStdGenerator : public StdGenerator {
public:
    /* The constructor create() runs. */
    void xstdGenConstruct();

    /* The vtable slots. */
    static void *__attribute__((thiscall)) xStdDtorSlot(XStdGenerator *self, unsigned flags);
    static BOOL __attribute__((thiscall)) xStdCopyFromSlot(XStdGenerator *self, const XStdGenerator *src);
    static BOOL __attribute__((thiscall)) xStdSaveSlot(XStdGenerator *self, void *fp);
    static BOOL __attribute__((thiscall)) xStdLoadSlot(XStdGenerator *self, void *fp);
    static void __attribute__((thiscall)) xStdSetPositionSlot(XStdGenerator *self, float x, float y, float z);
    static void __attribute__((thiscall)) xStdSetVelocitySlot(XStdGenerator *self, float x, float y, float z, float m);
    static void __attribute__((thiscall)) xStdSetDirectionSlot(XStdGenerator *self, float x, float y, float z);
    static void __attribute__((thiscall)) xStdSetSpeedSlot(XStdGenerator *self, float m);
    static void __attribute__((thiscall)) xStdEmitSlot(XStdGenerator *self, float dt);

private:
    void xstdGenTick(float dt);
    void xstdGenDestruct();
    BOOL xstdGenCopyFrom(const XStdGenerator *src);
    BOOL xstdGenSave(void *fp);
    BOOL xstdGenLoad(void *fp);
    void xstdSetPosition(float x, float y, float z);
    void xstdStoreScaled(double x, double y, double z, double len, double mag);
    void xstdSetDirection(float x, float y, float z);
    void xstdSetVelocity(float x, float y, float z, float mag);
    void xstdSetSpeed(float mag);

    float        flPosOffset_[3];
    float        flVelOffset_[3];

    static void checkLayout();
};

inline void XStdGenerator::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(XStdGenerator, flPosOffset_) == 0x3420, "XStd layout");
    static_assert(offsetof(XStdGenerator, flVelOffset_) == 0x342c, "XStd layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(XStdGenerator) == 0x3438, "XStd size");

/* CylinderGenerator: samples position and velocity like StdGenerator, then
 * scales the position, carries it through flMatrix and offsets it by flOrigin.
 * Velocity is taken from its table unscaled and untransformed. */
class CylinderGenerator : public Generator {
public:
    /* The constructor create() runs. */
    void cylGenConstruct();

    /* The vtable slots. */
    static void *__attribute__((thiscall)) cylDtorSlot(CylinderGenerator *self, unsigned flags);
    static BOOL __attribute__((thiscall)) cylCopyFromSlot(CylinderGenerator *self, const CylinderGenerator *src);
    static BOOL __attribute__((thiscall)) cylSaveSlot(CylinderGenerator *self, void *fp);
    static BOOL __attribute__((thiscall)) cylLoadSlot(CylinderGenerator *self, void *fp);
    static void __attribute__((thiscall)) cylSetPositionSlot(CylinderGenerator *self, float x, float y, float z);
    static void __attribute__((thiscall)) cylSetDirectionSlot(CylinderGenerator *self, float x, float y, float z);
    static void __attribute__((thiscall)) cylinderEmitSlot(CylinderGenerator *self, float dt);

private:
    void cylinderEmit(float dt);
    void cylGenTick(float dt);
    void cylGenDestruct();
    void cylSetDirection(float x, float y, float z);
    void cylSetPosition(float x, float y, float z);
    void cylBuildVelocity(const float *vmin, const float *vmax, float lmin, float lmax);
    void cylBuildRate(float lo, float hi);
    BOOL cylGenCopyFrom(const CylinderGenerator *src);
    BOOL cylGenSave(void *fp);
    BOOL cylGenLoad(void *fp);

    float     flOrigin_[3];
    float     flDirection_[3];
    float     flScale_;       // scale applied to the sampled position, before the matrix
    float     flMatrix_[16];  // rebuilt whenever SetDirection is called
    float     flVelMin_[3];
    float     flVelMax_[3];
    float     flLifeMin_;
    float     flLifeMax_;
    float     flEmitRateMin_;
    float     flEmitRateMax_;
    float     flDtScale_;
    void     *pTypeTable_;
    DWORD     dwTypeTableCount_;
    float     flAccumulator_;
    float     flPosTable_[1500];
    float     flVelTable_[1500];
    float     pLifeTable_[100];
    DWORD     pEmitProb_[200];
    DWORD     dwPosIdx_;
    DWORD     dwVelIdx_;
    DWORD     dwLifeIdx_;
    DWORD     dwProbIdx_;

    static void checkLayout();
};

inline void CylinderGenerator::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(CylinderGenerator, flScale_)       == 0x0028, "Cyl layout");
    static_assert(offsetof(CylinderGenerator, flMatrix_)      == 0x002c, "Cyl layout");
    static_assert(offsetof(CylinderGenerator, flDtScale_)     == 0x0094, "Cyl layout");
    static_assert(offsetof(CylinderGenerator, flAccumulator_) == 0x00a0, "Cyl layout");
    static_assert(offsetof(CylinderGenerator, flPosTable_)    == 0x00a4, "Cyl layout");
    static_assert(offsetof(CylinderGenerator, flVelTable_)    == 0x1814, "Cyl layout");
    static_assert(offsetof(CylinderGenerator, pLifeTable_)    == 0x2f84, "Cyl layout");
    static_assert(offsetof(CylinderGenerator, pEmitProb_)     == 0x3114, "Cyl layout");
    static_assert(offsetof(CylinderGenerator, dwPosIdx_)      == 0x3434, "Cyl layout");
    static_assert(offsetof(CylinderGenerator, dwProbIdx_)     == 0x3440, "Cyl layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(CylinderGenerator) == 0x3444, "Cyl size");

/* PointGenerator: emits every particle at a fixed position and colour; not
 * used by any shipping effect, so its tables are only ever whatever the
 * constructor leaves them as. */
class PointGenerator : public Generator {
public:
    /* The constructor create() runs. */
    void pointGenConstruct();

    /* The vtable slots. */
    static void *__attribute__((thiscall)) pointDtorSlot(PointGenerator *self, unsigned flags);
    static void __attribute__((thiscall)) pointEmitSlot(PointGenerator *self, float dt);

private:
    void pointGenEmit(float dt);

    float     flEmitPos_[3];
    float     flVelBias_[3];
    float     flEmitRate_;
    BYTE      opaque2c_[0x0c];
    DWORD     dwDiffuse_;  // constructor sets this to 0xFFFFFFFF
    float     flAccumulator_;
    BYTE      opaque40_[0x04];
    float     flVelTable_[1000];
    DWORD     dwLifeTable_[100];
    DWORD     dwVelIdx_[3];
    DWORD     dwLifeIdx_;  // runs past dwLifeTable's declared length before wrapping

    static void checkLayout();
};

inline void PointGenerator::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(PointGenerator, flEmitRate_)    == 0x0028, "Point layout");
    static_assert(offsetof(PointGenerator, dwDiffuse_)     == 0x0038, "Point layout");
    static_assert(offsetof(PointGenerator, flVelTable_)    == 0x0044, "Point layout");
    static_assert(offsetof(PointGenerator, dwLifeTable_)   == 0x0fe4, "Point layout");
    static_assert(offsetof(PointGenerator, dwVelIdx_)      == 0x1174, "Point layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(PointGenerator) == 0x1184, "Point size");

/* BoxGenerator: like PointGenerator, not used by any shipping effect and never
 * filled by a Load. */
class BoxGenerator : public Generator {
public:
    /* The constructor create() runs. */
    void boxGenConstruct();

    /* The vtable slots. */
    static void *__attribute__((thiscall)) boxDtorSlot(BoxGenerator *self, unsigned flags);
    static void __attribute__((thiscall)) boxEmitSlot(BoxGenerator *self, float dt);

private:
    void boxGenEmit(float dt);

    BYTE      opaque10_[0x18];
    float     flVelBias_[3];
    float     flEmitRate_;
    float     flAccumulator_;
    DWORD     dwPosX_[500];
    DWORD     dwPosY_[500];
    DWORD     dwPosZ_[500];
    float     flVelTable_[500];
    DWORD     dwLifeTable_[100];
    DWORD     dwDiffuse_[200];
    DWORD     dwPosIdx_[3];
    DWORD     dwVelIdx_[3];
    DWORD     dwLifeIdx_;
    DWORD     dwDiffuseIdx_;

    static void checkLayout();
};

inline void BoxGenerator::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(BoxGenerator, flVelBias_)    == 0x0028, "Box layout");
    static_assert(offsetof(BoxGenerator, dwPosX_)       == 0x003c, "Box layout");
    static_assert(offsetof(BoxGenerator, flVelTable_)   == 0x17ac, "Box layout");
    static_assert(offsetof(BoxGenerator, dwDiffuse_)    == 0x210c, "Box layout");
    static_assert(offsetof(BoxGenerator, dwPosIdx_)     == 0x242c, "Box layout");
    static_assert(offsetof(BoxGenerator, dwDiffuseIdx_) == 0x2448, "Box layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(BoxGenerator) == 0x244c, "Box size");

/* Direct calls through the shared vtable slot layout above, used where the
 * caller only has a Generator pointer or an Environment pointer and needs one
 * virtual call. */
#define GEN_VT_TICK_SLOT 3
#define GEN_VT_DTOR_SLOT 0
#define GEN_VT_COPY_SLOT 1
#define GEN_VT_SAVE_SLOT 4
#define GEN_VT_LOAD_SLOT 5
#define GEN_VT_SETPOS_SLOT 6

inline void Generator::vsetPosition(float x, float y, float z)
{
    typedef void (__attribute__((thiscall)) *fn)(Generator *, float, float, float);
    ((fn)pVtable_[GEN_VT_SETPOS_SLOT])(this, x, y, z);
}
#define GEN_VT_SETDIR_SLOT 8
inline void Generator::vsetDirection(float x, float y, float z)
{
    typedef void (__attribute__((thiscall)) *fn)(Generator *, float, float, float);
    ((fn)pVtable_[GEN_VT_SETDIR_SLOT])(this, x, y, z);
}

/* Gen_FillGaussianField writes into an ExplodeDebris (explodedebris.h); its
 * layout and the offsets this relies on are asserted there. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Gen_FillGaussianField(ExplodeDebris *self, float mu, float sigma);

/* Both classes' Tick is slot GEN_VT_TICK_SLOT. */
void sim_tick_slot3(void *obj, float dt);
