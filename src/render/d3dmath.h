#pragma once
#include <windows.h>
#include <d3d.h>

/* The shared 3D maths helpers.
 *
 * Implemented in d3dmath_std.cpp (plain float, the C library, no assembly)
 * and d3dmath_common.cpp (pure data movement), with the public names in
 * d3dmath_mode.cpp.  This header holds the shared types and includes the two
 * public headers.  Float rounding is not matched bit for bit: these feed
 * rendering only, never the simulation.
 */

struct Mat4 { float m[16]; };   /* row-major, D3D convention */
struct Vec3 { float x, y, z; };

/* The identity WORLD matrix, set at startup (renderstate.cpp) and re-applied
 * by the batch passes. */
extern D3DMATRIX g_worldIdentity;

/* A control-point list node (SplinePath's list). */
struct ListNodeM { void *pValue; ListNodeM *pNext; };

/* One 0x20-byte FVF 0x1e2 billboard vertex. */
struct BbVertex { float x, y, z; DWORD zero; DWORD diffuse, specular; float u, v; };

#include "d3dmath_common.h"
#include "d3dmath_mode.h"
