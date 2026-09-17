#pragma once
#include <windows.h>

/* Stage E — owning construction (PARTICLE_PLAN.md § 6).
 *
 * The game reaches Generators, Environments and ParticleSystems through
 * exactly two things: the factory that builds them, and their vtable.  Taking
 * the factories over therefore took the vtables over too — since E6 all three
 * families are constructed by us (gen_create / env_create / ps_create) onto
 * static vtables we own outright, so there is nothing here but the three
 * __cdecl entry points patch.py routes the factory call sites to.
 *
 * What used to live here: a clone of each game vtable with our slots written
 * into it, and a vtbl_identity() map so dispatchers could recognise a class
 * after the swap.  Both are gone — our objects never carry a game vtable
 * address, and dispatch is a plain virtual call.
 */

/* The destruction counterpart: the tail every slot-0 scalar deleting dtor
 * ends with, for all three families.
 *
 * MSVC's scalar deleting destructor destructs, then frees the block only if
 * bit 0 of its flags is set, and returns `this` either way.  The three
 * families' create functions (gen_create / env_create / ps_create) allocate
 * with our own operator new, so the block goes back through our own delete.
 *
 * A template, not three copies: this was `gen_scalar_delete`,
 * `env_scalar_delete` and `ps_scalar_delete`, three identical bodies whose
 * only reason to take `void *` was that their callers pass six different
 * object types (COHESION_PLAN.md Band 7d).  The callers -- Gen_BaseDtor,
 * Gen_PointDtor, Particle_BaseDtor and the rest -- were already typed; only
 * this tail was not.  Header-defined, so `check_homes` sees it as inline and
 * does not look for a factory.cpp. */
template <class T>
inline void *scalar_delete(T *self, unsigned flags)
{
    if (flags & 1)
        ::operator delete(self);
    return self;
}

extern "C" {
__declspec(dllexport) void *__cdecl Gen_FactoryCreate(const char *name);
__declspec(dllexport) void *__cdecl Env_FactoryCreate(const char *name);
__declspec(dllexport) void *__cdecl PS_FactoryCreate(const char *name);
}
