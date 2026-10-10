/* The render-specific helpers (d3dmath.h): the identity world matrix and the
 * vertex builders.  The pure maths they sit beside is in vecmath.cpp. */
#include <stdint.h>
#include "d3dmath.h"

Mat4 g_worldIdentity;

/* +0x0c is written as a literal zero, not left alone: FVF 0x1e2 has a
 * reserved slot there that nothing else fills. */
void billboard_vertex(BbVertex *d, const Vec3 *pos, uint32_t diffuse,
                      uint32_t specular, float u, float v)
{
    d->x = pos->x;  d->y = pos->y;  d->z = pos->z;
    d->zero = 0;
    d->diffuse = diffuse;
    d->specular = specular;
    d->u = u;  d->v = v;
}

/* ─── RenderGameFrame's three small helpers ─────────────────────────────── */

ScreenVertex *
Math_VertexSet(ScreenVertex *self, const Vec3 *pos, float rhw, uint32_t color,
               uint32_t specular, float tu, float tv)
{
    self->sx = pos->x;
    self->sy = pos->y;
    self->sz = pos->z;
    self->rhw = rhw;
    self->color = color;
    self->specular = specular;
    self->tu = tu;
    self->tv = tv;
    return self;
}
