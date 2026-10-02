/* The three factories: each forwards to its family's create function
 * (generators.cpp, particles.cpp) and logs its first call. */

#include <windows.h>
#include "factory.h"
#include "generators.h"
#include "particles.h"
#include "logger.h"

static void log_first(LONG *once, const char *what, const char *name, void *obj)
{
    if (InterlockedExchange(once, 1) == 0)
        g_logger.write("factory: %s active (first = \"%s\" -> %p)\n",
                  what, name ? name : "(null)", obj);
}

 

  void * 
Gen_FactoryCreate(const char *name)
{
    void *obj = Generator::create(name);
    static LONG once = 0;
    log_first(&once, "Gen_FactoryCreate", name, obj);
    return obj;
}

  void * 
Env_FactoryCreate(const char *name)
{
    void *obj = Environment::create(name);
    static LONG once = 0;
    log_first(&once, "Env_FactoryCreate", name, obj);
    return obj;
}

  void * 
PS_FactoryCreate(const char *name)
{
    void *obj = ParticleSystem::create(name);
    static LONG once = 0;
    log_first(&once, "PS_FactoryCreate", name, obj);
    return obj;
}
