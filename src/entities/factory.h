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

extern "C" {
__declspec(dllexport) void *__cdecl Gen_FactoryCreate(const char *name);
__declspec(dllexport) void *__cdecl Env_FactoryCreate(const char *name);
__declspec(dllexport) void *__cdecl PS_FactoryCreate(const char *name);
}
