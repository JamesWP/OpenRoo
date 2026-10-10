#pragma once
#include <stdint.h>
#include "vecmath.h"
#include "rendertypes.h"

/* The render-specific helpers over the pure maths in vecmath.h: the vertex
 * builders for the billboard and screen formats, and the identity world
 * matrix.  Implemented in d3dmath.cpp. */


/* The identity WORLD matrix, set at startup (renderstate.cpp) and re-applied
 * by the batch passes. */
extern Mat4 g_worldIdentity;

/* One 0x20-byte FVF 0x1e2 billboard vertex. */
struct BbVertex { float x, y, z; uint32_t zero; uint32_t diffuse, specular; float u, v; };

/* One 0x20-byte FVF 0x1e2 vertex. */
void billboard_vertex(BbVertex *d, const Vec3 *pos, uint32_t diffuse,
                      uint32_t specular, float u, float v);

/* A ScreenVertex (FVF 0x1C4) from *pos and rhw, colour, specular, tu, tv,
 * stored as given; returns self. */
ScreenVertex *
Math_VertexSet(ScreenVertex *self, const Vec3 *pos, float rhw, uint32_t color,
               uint32_t specular, float tu, float tv);
