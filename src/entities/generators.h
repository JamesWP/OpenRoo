/* Particle generators and environments: the emitters that fill a particle
 * system's ring with new particles, and the fields that age and retire them
 * once they are in it.  A Generator subclass owns Tick/emit/Save/Load/CopyFrom
 * through its own vtable; an Environment subclass owns the same five for
 * ageing, fading and killing particles already in the ring. */

#pragma once
#include <windows.h>
#include <stddef.h>
#include "particles.h"
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
struct Generator {
    void      **pVtable;  // ten-slot vtable: dtor, CopyFrom, AttachRing, Tick, Save, Load, SetPosition, ..., SetDirection, ...
    char       *pName;
    DWORD       dwEnabled;  // zero disables Tick/emit for this generator
    RingBuffer *pRing;
};

/* Base of every environment.  Same role as Generator for the fields it has;
 * there is no enable flag here, so an environment can only be turned off by
 * detaching or not ticking it. */
struct Environment {
    void      **pVtable;  // vtable, six slots: dtor, CopyFrom, AttachRing, Tick, Save, Load
    char       *pName;
    RingBuffer *pRing;
};

/* GravityEnvironment: a per-second gravity vector applied to live particles,
 * with a colour fade toward a target and up to three axis kill planes. */
struct GravityEnvironment {
    Environment base;
    float       flDirection[3];
    float       flMagnitude;
    float       flGravity[3];
    DWORD       dwTargetARGB;
    DWORD       dwTargetA;  // never read
    DWORD       dwTargetRGB[3];
    float       flFadeRate;
    DWORD       dwFadeThreshold;  // a fade step below this is not applied
    DWORD       dwClipEnable[3];
    float       flClipMax[3];
    float       flClipMin[3];
    float       flFadeAccum;
};

/* MagnetEnvironment: pulls live particles toward flCentre and retires any that
 * arrive within flHalfExtent of it, with the same colour-fade skeleton as
 * GravityEnvironment. */
struct MagnetEnvironment {
    Environment base;
    float       flCentre[3];
    float       flForce[3];
    float       flHalfExtent[3];
    float       flRange;    // serialised, unread by Tick
    DWORD       dwField34;  // serialised, unread by Tick
    DWORD       dwTargetRGB[3];
    float       flFadeRate;
    DWORD       dwFadeThreshold;
    float       flFadeAccum;
};

/* StdGenerator: the default emitter, used by most shipping effects.  Emit
 * never samples a distribution at runtime; Load fills the four tables below
 * once, by sampling with rand(), and Tick/emit only ever indexes them by the
 * four cursors at the end of the struct. */
struct StdGenerator {
    Generator base;
    float     flDtScale;
    DWORD     dwEmitMode;   // selects which of the Sph/Box parameter pairs below Load samples from
    float     flSphMin[3];  // consumed only by Load, to build the tables; emit never reads these
    float     flSphMax[3];
    float     flBoxMin[3];
    float     flBoxMax[3];
    float     flVelMin[3];
    float     flVelMax[3];
    float     flLifeMin;
    float     flLifeMax;
    float     flEmitRateMin;
    float     flEmitRateMax;
    void     *pTypeTable;  // heap block owned and freed by this object
    DWORD     dwTypeTableCount;
    float     flPosTable[1500];  // 500 samples of (x, y, z), read as node position
    float     flVelTable[1500];  // 500 samples of (x, y, z), read as node velocity
    float     pLifeTable[100];
    DWORD     pEmitProb[200];
    DWORD     dwCtr0;
    float     flAccumulator;
    DWORD     dwPosIdx;  // steps by 1, wraps at 500
    DWORD     dwVelIdx;
    DWORD     dwLifeIdx;
    DWORD     dwProbIdx;
};

/* XStdGenerator: StdGenerator plus a constant offset added to every sampled
 * position and velocity, used for thruster and flame effects.  Every field and
 * cursor through StdGenerator is inherited unchanged; only emit differs. */
struct XStdGenerator {
    StdGenerator base;
    float        flPosOffset[3];
    float        flVelOffset[3];
};

/* CylinderGenerator: samples position and velocity like StdGenerator, then
 * scales the position, carries it through flMatrix and offsets it by flOrigin.
 * Velocity is taken from its table unscaled and untransformed. */
struct CylinderGenerator {
    Generator base;
    float     flOrigin[3];
    float     flDirection[3];
    float     flScale;       // scale applied to the sampled position, before the matrix
    float     flMatrix[16];  // rebuilt whenever SetDirection is called
    float     flVelMin[3];
    float     flVelMax[3];
    float     flLifeMin;
    float     flLifeMax;
    float     flEmitRateMin;
    float     flEmitRateMax;
    float     flDtScale;
    void     *pTypeTable;
    DWORD     dwTypeTableCount;
    float     flAccumulator;
    float     flPosTable[1500];
    float     flVelTable[1500];
    float     pLifeTable[100];
    DWORD     pEmitProb[200];
    DWORD     dwPosIdx;
    DWORD     dwVelIdx;
    DWORD     dwLifeIdx;
    DWORD     dwProbIdx;
};

/* PointGenerator: emits every particle at a fixed position and colour; not
 * used by any shipping effect, so its tables are only ever whatever the
 * constructor leaves them as. */
struct PointGenerator {
    Generator base;
    float     flEmitPos[3];
    float     flVelBias[3];
    float     flEmitRate;
    BYTE      opaque2c[0x0c];
    DWORD     dwDiffuse;  // constructor sets this to 0xFFFFFFFF
    float     flAccumulator;
    BYTE      opaque40[0x04];
    float     flVelTable[1000];
    DWORD     dwLifeTable[100];
    DWORD     dwVelIdx[3];
    DWORD     dwLifeIdx;  // runs past dwLifeTable's declared length before wrapping
};

/* BoxGenerator: like PointGenerator, not used by any shipping effect and never
 * filled by a Load. */
struct BoxGenerator {
    Generator base;
    BYTE      opaque10[0x18];
    float     flVelBias[3];
    float     flEmitRate;
    float     flAccumulator;
    DWORD     dwPosX[500];
    DWORD     dwPosY[500];
    DWORD     dwPosZ[500];
    float     flVelTable[500];
    DWORD     dwLifeTable[100];
    DWORD     dwDiffuse[200];
    DWORD     dwPosIdx[3];
    DWORD     dwVelIdx[3];
    DWORD     dwLifeIdx;
    DWORD     dwDiffuseIdx;
};

/* Direct calls through the shared vtable slot layout above, used where the
 * caller only has a Generator pointer or an Environment pointer and needs one
 * virtual call. */
#define GEN_VT_TICK_SLOT 3
#define GEN_VT_DTOR_SLOT 0
#define GEN_VT_COPY_SLOT 1
#define GEN_VT_SAVE_SLOT 4
#define GEN_VT_LOAD_SLOT 5
#define GEN_VT_SETPOS_SLOT 6

static inline void gen_vset_position(Generator *g, float x, float y, float z)
{
    typedef void (*fn)(Generator *, float, float, float);
    ((fn)g->pVtable[GEN_VT_SETPOS_SLOT])(g, x, y, z);
}
#define GEN_VT_SETDIR_SLOT 8
static inline void gen_vset_direction(Generator *g, float x, float y, float z)
{
    typedef void (*fn)(Generator *, float, float, float);
    ((fn)g->pVtable[GEN_VT_SETDIR_SLOT])(g, x, y, z);
}

/* Allocates and constructs the named class, or returns NULL for a name none of
 * the classes answer to or a failed allocation. */
Environment *env_create(const char *name);

/* Same contract as env_create, for the six generator class names. */
Generator *gen_create(const char *name);

/* Builds a new instance of src's class and copies it with CopyFrom, freeing
 * the new instance and returning NULL if either step fails. */
Generator   *gen_clone(const Generator *src);
Environment *env_clone(const Environment *src);

/* Gen_FillGaussianField writes into an ExplodeDebris (explodedebris.h); its
 * layout and the offsets this relies on are asserted there. */
void Gen_FillGaussianField(ExplodeDebris *self, float mu, float sigma);

/* Both classes' Tick is slot GEN_VT_TICK_SLOT. */
void sim_tick_slot3(void *obj, float dt);
