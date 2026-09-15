/* Math helpers with no arithmetic that could round differently -- pure data
 * movement.  The address in each comment is the original it mirrors.
 */
#include "d3dmath_common.h"

/* MatrixSetIdentity, 0x413230 */
void m4_identity(Mat4 *d)
{
    for (int i = 0; i < 16; ++i)
        d->m[i] = 0.0f;
    d->m[0] = d->m[5] = d->m[10] = d->m[15] = 1.0f;
}

/* BuildTranslateMatrix, 0x4232b0 */
void m4_translate(Mat4 *d, float x, float y, float z)
{
    m4_identity(d);
    d->m[12] = x;  d->m[13] = y;  d->m[14] = z;
}

/* VectorSubtract3, 0x403790 */
void v3_sub(Vec3 *d, const Vec3 *a, const Vec3 *b)
{
    d->x = a->x - b->x;
    d->y = a->y - b->y;
    d->z = a->z - b->z;
}

/* 0x403770 -- Vec3 setter. */
void v3_set(Vec3 *d, float x, float y, float z)
{
    d->x = x;  d->y = y;  d->z = z;
}

/* 0x407f70 -- componentwise divide by a scalar. */
void v3_div(Vec3 *d, const Vec3 *v, float s)
{
    d->x = v->x / s;
    d->y = v->y / s;
    d->z = v->z / s;
}

/* 0x426c20 -- in-place subtract. */
void v3_sub_inplace(Vec3 *d, const Vec3 *v)
{
    d->x = d->x - v->x;
    d->y = d->y - v->y;
    d->z = d->z - v->z;
}

/* BuildBillboardVertex, 0x421ef0.  +0x0c is written as a literal zero, not
 * left alone -- FVF 0x1e2 has a slot there the game never fills. */
void billboard_vertex(BbVertex *d, const Vec3 *pos, DWORD diffuse,
                      DWORD specular, float u, float v)
{
    d->x = pos->x;  d->y = pos->y;  d->z = pos->z;
    d->zero = 0;
    d->diffuse = diffuse;
    d->specular = specular;
    d->u = u;  d->v = v;
}
