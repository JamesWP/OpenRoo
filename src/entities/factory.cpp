/* Stage E1 — own Generator / Environment construction (PARTICLE_PLAN.md § 6.9).
 *
 * Replaces the two factories at their call sites (patch.py CALL_PATCHES):
 *
 *   0x4485d0 GeneratorFactoryCreate     → Gen_FactoryCreate  (2 E8 sites)
 *   0x4488f0 EnvironmentFactoryCreate   → Env_FactoryCreate  (2 E8 sites)
 *   0x448ab0 ParticleSystemFactoryCreate → PS_FactoryCreate   (2 E8 sites)
 *
 * Since E4 the Environment factory is wholly ours: env_create (generators.cpp)
 * allocates with our own new and constructs, and the original is UD2.  The
 * Generator and ParticleSystem factories still call straight through to the
 * original for allocation and construction.  All three then swap the finished
 * object's vtable pointer for a table of our own.  That is the whole point of the step — after it, the
 * game's virtual calls on these objects go through a table in this DLL, so
 * changing a slot no longer needs a binary patch.
 *
 * The clone starts as a straight copy of the game's vtable, then the slots we
 * have replaced are written into it from g_override[] below.  Since E2 those
 * five entries are the *only* thing installing our simulation code: patch.py no
 * longer patches a single generator or environment vtable, because it no longer
 * has to.  Adding a sixth replaced slot now means one line in that table and a
 * rebuild — no change to Karoo.exe at all.
 *
 * Classes we do not recognise (the base Generator/Environment, and the dead
 * PointGenerator / BoxGenerator) keep the game's own vtable untouched — they
 * simply fall out of the clone table below and are returned as-is.
 */
#include "factory.h"
#include "generators.h"
#include "particles.h"
#include "log.h"

/* The originals, called by absolute address.  They are left intact in the
 * binary (no UD2) precisely so we can delegate to them; movie.cpp already sets
 * the precedent for reaching into the image this way, and the game has no
 * relocations so these VAs are fixed. */
typedef void *(__cdecl *factory_fn)(const char *name);
#define ORIG_PARTICLESYSTEM_FACTORY ((factory_fn)0x00448ab0)

/* Generator vtables have 10 slots, Environment 6 (§ 6.2), ParticleSystem 15. */
#define GEN_VTBL_SLOTS  10
#define ENV_VTBL_SLOTS   6

/* Clone table.  Small and fixed: at most six generator classes and three
 * environment classes exist, and each gets one clone the first time it is
 * constructed.  Storage is static so nothing is allocated at load time and the
 * tables outlive every object that points at them. */
#define MAX_CLONES      12
#define MAX_CLONE_SLOTS 16

struct VtblClone {
    DWORD  game;                       /* the game vtable VA it was cloned from */
    void  *slot[MAX_CLONE_SLOTS];
};
static VtblClone g_clone[MAX_CLONES];
static int       g_clone_count;

static void apply_overrides(DWORD game_vtbl, void **slots, int count);

/* Return our clone of `game_vtbl`, creating it on first use.  Returns NULL if
 * the table is full or the slot count is out of range, in which case the caller
 * leaves the object on the game's own vtable — degraded but correct. */
static void **clone_vtable(DWORD game_vtbl, int slots)
{
    if (game_vtbl == 0 || slots <= 0 || slots > MAX_CLONE_SLOTS)
        return NULL;

    for (int i = 0; i < g_clone_count; i++)
        if (g_clone[i].game == game_vtbl)
            return g_clone[i].slot;

    if (g_clone_count >= MAX_CLONES) {
        log_write("factory: clone table full, leaving vtbl=%08lX game-owned\n",
                  game_vtbl);
        return NULL;
    }

    VtblClone *c = &g_clone[g_clone_count++];
    c->game = game_vtbl;
    for (int i = 0; i < slots; i++)
        c->slot[i] = ((void **)game_vtbl)[i];

    log_write("factory: cloned vtbl %08lX -> %p (%d slots)\n",
              game_vtbl, (void *)c->slot, slots);
    apply_overrides(game_vtbl, c->slot, slots);
    return c->slot;
}

/* Slots this DLL implements, keyed by the game vtable of the owning class.
 *
 * This replaces the VTABLE_PATCHES entries that used to install these five
 * functions by rewriting Karoo.exe.  Extending it costs one line; extending
 * VTABLE_PATCHES cost a binary patch and a patdiff audit. */
extern "C" {
    void __attribute__((thiscall)) Gen_StdEmit(void *, float);
    void __attribute__((thiscall)) Gen_XStdEmit(void *, float);
    void __attribute__((thiscall)) Gen_CylinderEmit(void *, float);
    void __attribute__((thiscall)) Env_GravityTick(void *, float);
    void __attribute__((thiscall)) Env_MagnetTick(void *, float);
    BOOL __attribute__((thiscall)) Env_GravityLoad(void *, void *);
    BOOL __attribute__((thiscall)) Env_MagnetLoad(void *, void *);
    void *__attribute__((thiscall)) Env_GravityDtor(void *, unsigned);
    void *__attribute__((thiscall)) Env_MagnetDtor(void *, unsigned);
    BOOL __attribute__((thiscall)) Env_GravityCopyFrom(void *, const void *);
    BOOL __attribute__((thiscall)) Env_MagnetCopyFrom(void *, const void *);
    BOOL __attribute__((thiscall)) Env_GravitySave(void *, void *);
    BOOL __attribute__((thiscall)) Env_MagnetSave(void *, void *);
    void *__attribute__((thiscall)) Env_BaseDtor(void *, unsigned);
    BOOL __attribute__((thiscall)) Env_BaseCopyFrom(void *, const void *);
    BOOL __attribute__((thiscall)) Env_AttachRing(void *, void *);
    void __attribute__((thiscall)) Env_BaseTick(void *, float);
    BOOL __attribute__((thiscall)) Env_BaseSave(void *, void *);
    BOOL __attribute__((thiscall)) Env_BaseLoad(void *, void *);

    void __attribute__((thiscall)) Gen_Nop1(void *, float);
    void __attribute__((thiscall)) Gen_Nop3(void *, float, float, float);
    void __attribute__((thiscall)) Gen_Nop4(void *, float, float, float, float);
    BOOL __attribute__((thiscall)) Gen_ReturnTrue(void *, void *);
    BOOL __attribute__((thiscall)) Gen_AttachRing(void *, void *);
    BOOL __attribute__((thiscall)) Gen_BaseCopyFrom(void *, const void *);
    void *__attribute__((thiscall)) Gen_BaseDtor(void *, unsigned);
    void *__attribute__((thiscall)) Gen_PointDtor(void *, unsigned);
    void *__attribute__((thiscall)) Gen_BoxDtor(void *, unsigned);
    void *__attribute__((thiscall)) Gen_StdDtor(void *, unsigned);
    void *__attribute__((thiscall)) Gen_XStdDtor(void *, unsigned);
    void __attribute__((thiscall)) Gen_PointEmit(void *, float);
    void __attribute__((thiscall)) Gen_BoxEmit(void *, float);
    BOOL __attribute__((thiscall)) Gen_StdCopyFrom(void *, const void *);
    BOOL __attribute__((thiscall)) Gen_StdSave(void *, void *);
    BOOL __attribute__((thiscall)) Gen_StdLoad(void *, void *);
    BOOL __attribute__((thiscall)) Gen_XStdCopyFrom(void *, const void *);
    BOOL __attribute__((thiscall)) Gen_XStdSave(void *, void *);
    BOOL __attribute__((thiscall)) Gen_XStdLoad(void *, void *);
    void __attribute__((thiscall)) Gen_XStdSetPosition(void *, float, float, float);
    void __attribute__((thiscall)) Gen_XStdSetVelocity(void *, float, float, float, float);
    void __attribute__((thiscall)) Gen_XStdSetDirection(void *, float, float, float);
    void __attribute__((thiscall)) Gen_XStdSetSpeed(void *, float);
    void *__attribute__((thiscall)) Gen_CylDtor(void *, unsigned);
    BOOL __attribute__((thiscall)) Gen_CylCopyFrom(void *, const void *);
    BOOL __attribute__((thiscall)) Gen_CylSave(void *, void *);
    BOOL __attribute__((thiscall)) Gen_CylLoad(void *, void *);
    void __attribute__((thiscall)) Gen_CylSetPosition(void *, float, float, float);
    void __attribute__((thiscall)) Gen_CylSetDirection(void *, float, float, float);
}

struct SlotOverride {
    DWORD game_vtbl;   /* class identity */
    int   slot;
    void *impl;
};

static const SlotOverride g_override[] = {
    /* Generator / Environment slot 3 = Tick(float dt) — emit / integrate. */
    { VTBL_GEN_STD,      GEN_VT_TICK_SLOT, (void *)Gen_StdEmit      },
    { VTBL_GEN_XSTD,     GEN_VT_TICK_SLOT, (void *)Gen_XStdEmit     },
    { VTBL_GEN_CYLINDER, GEN_VT_TICK_SLOT, (void *)Gen_CylinderEmit },
    { VTBL_ENV_GRAVITY,  GEN_VT_TICK_SLOT, (void *)Env_GravityTick  },
    { VTBL_ENV_MAGNET,   GEN_VT_TICK_SLOT, (void *)Env_MagnetTick   },

    /* Stage E4 — generators: all ten slots of all six classes are ours. */
#define GEN_ROW(v, s0, s1, s3, s4, s5, s6, s7, s8, s9) \
    { v, 0, (void *)s0 }, { v, 1, (void *)s1 }, { v, 2, (void *)Gen_AttachRing }, \
    { v, 3, (void *)s3 }, { v, 4, (void *)s4 }, { v, 5, (void *)s5 }, \
    { v, 6, (void *)s6 }, { v, 7, (void *)s7 }, { v, 8, (void *)s8 }, { v, 9, (void *)s9 }
    GEN_ROW(VTBL_GEN_BASE,  Gen_BaseDtor,  Gen_BaseCopyFrom, Gen_Nop1,
            Gen_ReturnTrue, Gen_ReturnTrue, Gen_Nop3, Gen_Nop4, Gen_Nop3, Gen_Nop1),
    GEN_ROW(VTBL_GEN_POINT, Gen_PointDtor, Gen_BaseCopyFrom, Gen_PointEmit,
            Gen_ReturnTrue, Gen_ReturnTrue, Gen_Nop3, Gen_Nop4, Gen_Nop3, Gen_Nop1),
    GEN_ROW(VTBL_GEN_BOX,   Gen_BoxDtor,   Gen_BaseCopyFrom, Gen_BoxEmit,
            Gen_ReturnTrue, Gen_ReturnTrue, Gen_Nop3, Gen_Nop4, Gen_Nop3, Gen_Nop1),
    GEN_ROW(VTBL_GEN_STD,   Gen_StdDtor,   Gen_StdCopyFrom,  Gen_StdEmit,
            Gen_StdSave, Gen_StdLoad, Gen_Nop3, Gen_Nop4, Gen_Nop3, Gen_Nop1),
    GEN_ROW(VTBL_GEN_XSTD,  Gen_XStdDtor,  Gen_XStdCopyFrom, Gen_XStdEmit,
            Gen_XStdSave, Gen_XStdLoad, Gen_XStdSetPosition, Gen_XStdSetVelocity,
            Gen_XStdSetDirection, Gen_XStdSetSpeed),
    GEN_ROW(VTBL_GEN_CYLINDER, Gen_CylDtor, Gen_CylCopyFrom, Gen_CylinderEmit,
            Gen_CylSave, Gen_CylLoad, Gen_CylSetPosition, Gen_Nop4,
            Gen_CylSetDirection, Gen_Nop1),
#undef GEN_ROW

    /* Stage E4 — every slot of all three environment classes is ours, so the
     * clone of an environment vtable holds no game address at all. */
    { VTBL_ENV_BASE,     GEN_VT_DTOR_SLOT, (void *)Env_BaseDtor        },
    { VTBL_ENV_BASE,     GEN_VT_COPY_SLOT, (void *)Env_BaseCopyFrom    },
    { VTBL_ENV_BASE,     2,                (void *)Env_AttachRing      },
    { VTBL_ENV_BASE,     GEN_VT_TICK_SLOT, (void *)Env_BaseTick        },
    { VTBL_ENV_BASE,     GEN_VT_SAVE_SLOT, (void *)Env_BaseSave        },
    { VTBL_ENV_BASE,     GEN_VT_LOAD_SLOT, (void *)Env_BaseLoad        },
    { VTBL_ENV_GRAVITY,  2,                (void *)Env_AttachRing      },
    { VTBL_ENV_MAGNET,   2,                (void *)Env_AttachRing      },
    { VTBL_ENV_GRAVITY,  GEN_VT_DTOR_SLOT, (void *)Env_GravityDtor     },
    { VTBL_ENV_GRAVITY,  GEN_VT_COPY_SLOT, (void *)Env_GravityCopyFrom },
    { VTBL_ENV_GRAVITY,  GEN_VT_SAVE_SLOT, (void *)Env_GravitySave     },
    { VTBL_ENV_GRAVITY,  GEN_VT_LOAD_SLOT, (void *)Env_GravityLoad     },
    { VTBL_ENV_MAGNET,   GEN_VT_DTOR_SLOT, (void *)Env_MagnetDtor      },
    { VTBL_ENV_MAGNET,   GEN_VT_COPY_SLOT, (void *)Env_MagnetCopyFrom  },
    { VTBL_ENV_MAGNET,   GEN_VT_SAVE_SLOT, (void *)Env_MagnetSave      },
    { VTBL_ENV_MAGNET,   GEN_VT_LOAD_SLOT, (void *)Env_MagnetLoad      },

    /* ParticleSystem — the Stage A/B render and tick path. */
    { VTBL_PARTICLE_BASE,  PS_VT_TICK,   (void *)Particle_BaseTick   },
    { VTBL_PARTICLE_BASE,  PS_VT_RENDER, (void *)Particle_BaseRender },

    { VTBL_PARTICLE_POINT, PS_VT_TICK,   (void *)Particle_BaseTick    },
    { VTBL_PARTICLE_POINT, PS_VT_RENDER, (void *)Particle_PointRender },
    { VTBL_PARTICLE_POINT, PS_VT_FILL,   (void *)Particle_PointFill   },
    { VTBL_PARTICLE_POINT, PS_VT_DRAW,   (void *)Particle_PointDraw   },

    { VTBL_PARTICLE_FACE,  PS_VT_TICK,   (void *)Particle_BaseTick             },
    { VTBL_PARTICLE_FACE,  PS_VT_RENDER, (void *)Particle_BaseRender           },
    { VTBL_PARTICLE_FACE,  PS_VT_FILL,   (void *)Particle_FaceFill             },
    { VTBL_PARTICLE_FACE,  PS_VT_SETVEC, (void *)Particle_FaceSetVector        },
    { VTBL_PARTICLE_FACE,  PS_VT_XFORM,  (void *)Particle_FaceTransformCorners },
    { VTBL_PARTICLE_FACE,  PS_VT_DRAW,   (void *)Particle_FaceDraw             },

    { VTBL_PARTICLE_XFACE, PS_VT_TICK,   (void *)Particle_XFaceTick  },
    { VTBL_PARTICLE_XFACE, PS_VT_RENDER, (void *)Particle_BaseRender },
    { VTBL_PARTICLE_XFACE, PS_VT_FILL,   (void *)Particle_XFaceFill  },
    { VTBL_PARTICLE_XFACE, PS_VT_DRAW,   (void *)Particle_XFaceDraw  },
};

/* Apply every override registered for `game_vtbl` to a freshly cloned table. */
static void apply_overrides(DWORD game_vtbl, void **slots, int count)
{
    for (unsigned i = 0; i < sizeof g_override / sizeof g_override[0]; i++) {
        const SlotOverride *o = &g_override[i];
        if (o->game_vtbl != game_vtbl || o->slot >= count)
            continue;
        slots[o->slot] = o->impl;
        log_write("factory:   slot %d -> %p (ours)\n", o->slot, o->impl);
    }
}

/* Give `obj` our own vtable.  No-op when the class is one we do not clone. */
static void adopt_vtable(void *obj, int slots)
{
    if (obj == NULL)
        return;
    void **ours = clone_vtable(*(DWORD *)obj, slots);
    if (ours)
        *(void ***)obj = ours;
}

DWORD vtbl_identity(const void *vtbl)
{
    for (int i = 0; i < g_clone_count; i++)
        if ((const void *)g_clone[i].slot == vtbl)
            return g_clone[i].game;
    return (DWORD)vtbl;
}

/* ─── Exports ─── */

extern "C" {

__declspec(dllexport) void *__cdecl
Gen_FactoryCreate(const char *name)
{
    /* Ours since E4: gen_create builds with our own new (the original is UD2);
     * our slot-0 dtors free with our own delete. */
    void *obj = gen_create(name);
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("factory: Gen_FactoryCreate active (first = \"%s\" -> %p)\n",
                  name ? name : "(null)", obj);
    adopt_vtable(obj, GEN_VTBL_SLOTS);
    return obj;
}

__declspec(dllexport) void *__cdecl
PS_FactoryCreate(const char *name)
{
    void *obj = ORIG_PARTICLESYSTEM_FACTORY(name);
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("factory: PS_FactoryCreate active (first = \"%s\" -> %p)\n",
                  name ? name : "(null)", obj);
    adopt_vtable(obj, PS_VTBL_SLOTS);
    return obj;
}

__declspec(dllexport) void *__cdecl
Env_FactoryCreate(const char *name)
{
    /* Ours since E4: no call into the game factory (now UD2).  env_create
     * builds with our own new; our slot-0 dtor frees with our own delete. */
    void *obj = env_create(name);
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("factory: Env_FactoryCreate active (first = \"%s\" -> %p)\n",
                  name ? name : "(null)", obj);
    adopt_vtable(obj, ENV_VTBL_SLOTS);
    return obj;
}

} // extern "C"
