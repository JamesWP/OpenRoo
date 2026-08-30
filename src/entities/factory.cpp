/* Stage E1 — own Generator / Environment construction (PARTICLE_PLAN.md § 6.9).
 *
 * Replaces the two factories at their call sites (patch.py CALL_PATCHES):
 *
 *   0x4485d0 GeneratorFactoryCreate    → Gen_FactoryCreate    (2 E8 sites)
 *   0x4488f0 EnvironmentFactoryCreate  → Env_FactoryCreate    (2 E8 sites)
 *
 * Neither original is UD2-stubbed: we call straight through to it to do the
 * allocation and construction, then swap the finished object's vtable pointer
 * for a table of our own.  That is the whole point of the step — after it, the
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
#include "log.h"

/* The originals, called by absolute address.  They are left intact in the
 * binary (no UD2) precisely so we can delegate to them; movie.cpp already sets
 * the precedent for reaching into the image this way, and the game has no
 * relocations so these VAs are fixed. */
typedef void *(__cdecl *factory_fn)(const char *name);
#define ORIG_GENERATOR_FACTORY    ((factory_fn)0x004485d0)
#define ORIG_ENVIRONMENT_FACTORY  ((factory_fn)0x004488f0)

/* Generator vtables have 10 slots, Environment 6 (§ 6.2). */
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
}

struct SlotOverride {
    DWORD game_vtbl;   /* class identity */
    int   slot;
    void *impl;
};

static const SlotOverride g_override[] = {
    /* slot 3 = Tick(float dt) — the per-frame emit / integrate (§ 6.2) */
    { VTBL_GEN_STD,      GEN_VT_TICK_SLOT, (void *)Gen_StdEmit      },
    { VTBL_GEN_XSTD,     GEN_VT_TICK_SLOT, (void *)Gen_XStdEmit     },
    { VTBL_GEN_CYLINDER, GEN_VT_TICK_SLOT, (void *)Gen_CylinderEmit },
    { VTBL_ENV_GRAVITY,  GEN_VT_TICK_SLOT, (void *)Env_GravityTick  },
    { VTBL_ENV_MAGNET,   GEN_VT_TICK_SLOT, (void *)Env_MagnetTick   },
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
    void *obj = ORIG_GENERATOR_FACTORY(name);
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("factory: Gen_FactoryCreate active (first = \"%s\" -> %p)\n",
                  name ? name : "(null)", obj);
    adopt_vtable(obj, GEN_VTBL_SLOTS);
    return obj;
}

__declspec(dllexport) void *__cdecl
Env_FactoryCreate(const char *name)
{
    void *obj = ORIG_ENVIRONMENT_FACTORY(name);
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        log_write("factory: Env_FactoryCreate active (first = \"%s\" -> %p)\n",
                  name ? name : "(null)", obj);
    adopt_vtable(obj, ENV_VTBL_SLOTS);
    return obj;
}

} // extern "C"
