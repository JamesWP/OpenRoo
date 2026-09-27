/* The factories for generators, environments and particle systems
 * (factory.cpp): each builds an object of the class a data file names. */

#pragma once
#include <windows.h>

/* The tail every scalar deleting destructor of the three families ends with:
 * free the block when bit 0 of flags is set, and return self either way.  The
 * create functions allocate with operator new, so it goes back through
 * operator delete.  Defined here, so check_homes treats it as inline. */
template <class T>
inline void *scalar_delete(T *self, unsigned flags)
{
    if (flags & 1)
        ::operator delete(self);
    return self;
}

void *Gen_FactoryCreate(const char *name);
void *Env_FactoryCreate(const char *name);
void *PS_FactoryCreate(const char *name);
