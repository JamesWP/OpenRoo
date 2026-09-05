#pragma once
#include "com_proxy.h"

/* KAROO_HEADLESS=1 replaces DirectDraw entirely with the in-DLL null device.
 * See nullddraw.cpp for what that means and what it is built from. */
bool nulldd_enabled(void);

/* Returns the null IDirectDraw (v1) — the object hooks_DirectDrawCreate hands
 * back in headless mode.  Never fails; the object is static. */
IDirectDraw *nulldd_create(void);
