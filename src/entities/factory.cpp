/* Stage E — the three factories (PARTICLE_PLAN.md § 6.9, § 6.11).
 *
 * patch.py rewrites the six call sites:
 *
 *   0x4485d0 GeneratorFactoryCreate      → Gen_FactoryCreate  (2 E8 sites)
 *   0x4488f0 EnvironmentFactoryCreate    → Env_FactoryCreate  (2 E8 sites)
 *   0x448ab0 ParticleSystemFactoryCreate → PS_FactoryCreate   (2 E8 sites)
 *
 * All three originals are UD2: the object, its constructor and its vtable are
 * ours, so these just forward to the create functions in generators.cpp and
 * particles.cpp and log the first call of each for the record.
 */
#include "factory.h"
#include "generators.h"
#include "particles.h"
#include "log.h"

static void log_first(LONG *once, const char *what, const char *name, void *obj)
{
    if (InterlockedExchange(once, 1) == 0)
        log_write("factory: %s active (first = \"%s\" -> %p)\n",
                  what, name ? name : "(null)", obj);
}

extern "C" {

__declspec(dllexport) void *__cdecl
Gen_FactoryCreate(const char *name)
{
    void *obj = gen_create(name);
    static LONG once = 0;
    log_first(&once, "Gen_FactoryCreate", name, obj);
    return obj;
}

__declspec(dllexport) void *__cdecl
Env_FactoryCreate(const char *name)
{
    void *obj = env_create(name);
    static LONG once = 0;
    log_first(&once, "Env_FactoryCreate", name, obj);
    return obj;
}

__declspec(dllexport) void *__cdecl
PS_FactoryCreate(const char *name)
{
    void *obj = ps_create(name);
    static LONG once = 0;
    log_first(&once, "PS_FactoryCreate", name, obj);
    return obj;
}

} // extern "C"
