#pragma once
#include <stdint.h>
#include "rendertypes.h"

/* The shared 3D maths helpers.
 *
 * Implemented in d3dmath_std.cpp (plain float, the C library, no assembly)
 * and d3dmath_common.cpp (pure data movement), with the public names in
 * d3dmath_mode.cpp.  This header holds the shared types and includes the two
 * public headers.  Float rounding is not matched bit for bit: these feed
 * rendering only, never the simulation.
 */


/* The identity WORLD matrix, set at startup (renderstate.cpp) and re-applied
 * by the batch passes. */
extern Mat4 g_worldIdentity;

/* One 0x20-byte FVF 0x1e2 billboard vertex. */
struct BbVertex { float x, y, z; uint32_t zero; uint32_t diffuse, specular; float u, v; };

#include "d3dmath_common.h"
#include "d3dmath_mode.h"
