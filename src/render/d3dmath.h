#pragma once
#include <windows.h>
#include <d3d.h>

/* Our own copies of the game's shared 3D math helpers.
 *
 * The render replacements must not call the originals, so these are written
 * from the disassembly.  Implemented in d3dmath_std.cpp (plain float, the C
 * library, no assembly) and d3dmath_common.cpp (pure data movement), with the
 * public names in d3dmath_mode.cpp.  Each file's functions are declared in
 * the header of the same name (COHESION_PLAN.md template point 11); this
 * header holds the shared types and includes the two public ones.  Not bit-exact against the originals, and
 * not meant to be: these feed rendering only, never the simulation.
 */

struct Mat4 { float m[16]; };   /* row-major, D3D convention */
struct Vec3 { float x, y, z; };

/* A control-point list node (SplinePath's list). */
struct ListNodeM { void *pValue; ListNodeM *pNext; };

/* One 0x20-byte FVF 0x1e2 billboard vertex. */
struct BbVertex { float x, y, z; DWORD zero; DWORD diffuse, specular; float u, v; };

#include "d3dmath_common.h"
#include "d3dmath_mode.h"
