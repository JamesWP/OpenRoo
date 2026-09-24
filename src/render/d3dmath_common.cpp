/* Math helpers with no arithmetic that could round differently -- pure data
 * movement.  The address in each comment is the original it mirrors.
 */
#include <math.h>
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

/* ─── The exported vec3 helpers (d3dmath_common.h) ─────────────────────────
 *
 * The originals keep SqLen/Dot in the x87's 80-bit registers; ours are
 * double.  Only the last bits of a float the callers immediately round can
 * differ, which CLAUDE.md accepts. */
extern "C" {

__declspec(dllexport) Vec3 *__attribute__((thiscall))
Math_Vec3Set(Vec3 *self, float x, float y, float z)
{
    v3_set(self, x, y, z);
    return self;
}

__declspec(dllexport) Vec3 *__cdecl
Math_Vec3Sub(Vec3 *d, const Vec3 *a, const Vec3 *b)
{
    Vec3 t;
    v3_sub(&t, a, b);
    *d = t;
    return d;
}

__declspec(dllexport) double __cdecl
Math_Vec3SqLen(const Vec3 *v)
{
    return ((double)v->x * v->x + (double)v->y * v->y) + (double)v->z * v->z;
}

__declspec(dllexport) double __cdecl
Math_Vec3Dot(const Vec3 *a, const Vec3 *b)
{
    return ((double)a->z * b->z + (double)a->y * b->y) + (double)a->x * b->x;
}

__declspec(dllexport) Vec3 *__cdecl
Math_Vec3Cross(Vec3 *d, const Vec3 *a, const Vec3 *b)
{
    Vec3 t;
    t.x = (float)((double)b->z * a->y - (double)a->z * b->y);
    t.y = (float)((double)a->z * b->x - (double)b->z * a->x);
    t.z = (float)((double)a->x * b->y - (double)b->x * a->y);
    *d = t;
    return d;
}

__declspec(dllexport) Vec3 *__cdecl
Math_Vec3Div(Vec3 *d, const Vec3 *v, float s)
{
    Vec3 t;
    v3_div(&t, v, s);
    *d = t;
    return d;
}

__declspec(dllexport) Mat4 *__attribute__((thiscall))
Math_Mat4Set(Mat4 *self, float m00, float m01, float m02, float m03,
             float m10, float m11, float m12, float m13,
             float m20, float m21, float m22, float m23,
             float m30, float m31, float m32, float m33)
{
    const float v[16] = { m00, m01, m02, m03, m10, m11, m12, m13,
                          m20, m21, m22, m23, m30, m31, m32, m33 };
    for (int i = 0; i < 16; i++)
        self->m[i] = v[i];
    return self;
}

__declspec(dllexport) Mat4 *__cdecl
Math_Mat4Zero(Mat4 *d)
{
    for (int i = 0; i < 16; i++)
        d->m[i] = 0.0f;
    return d;
}

/* Written from the listing; each step rounds to float where the original
 * stores to memory. */
__declspec(dllexport) void __cdecl
Math_BuildBillboardQuad(Vec3 *out, float dx, float dy, float dz, float scale)
{
    Vec3 up = { 1.0f, 0.0f, 0.0f };
    if (dx == 1.0f && dy == 0.0f && dz == 0.0f)
        up = (Vec3){ 0.0f, 1.0f, 0.0f };

    Vec3 d = { dx, dy, dz };
    double len = sqrt(Math_Vec3SqLen(&d));
    Vec3 n = { (float)(dx / len), (float)(dy / len), (float)(dz / len) };

    Vec3 *r = &out[2], *u = &out[1];
    r->x = (float)((double)up.z * n.y - (double)n.z * up.y);
    r->y = (float)((double)n.z * up.x - (double)up.z * n.x);
    r->z = (float)((double)up.y * n.x - (double)n.y * up.x);
    u->x = (float)((double)n.y * r->z - (double)n.z * r->y);
    u->y = (float)((double)n.z * r->x - (double)n.x * r->z);
    u->z = (float)((double)n.x * r->y - (double)n.y * r->x);
    out[0] = (Vec3){ 0.0f, 0.0f, 0.0f };

    Vec3 t;
    Math_Vec3Div(&t, r, (float)sqrt(Math_Vec3SqLen(r)));
    *r = (Vec3){ t.x * scale, t.y * scale, t.z * scale };
    Math_Vec3Div(&t, u, (float)sqrt(Math_Vec3SqLen(u)));
    *u = (Vec3){ t.x * scale, t.y * scale, t.z * scale };

    out[3] = (Vec3){ r->x + u->x, r->y + u->y, r->z + u->z };
    Vec3 c = { out[3].x * 0.5f, out[3].y * 0.5f, out[3].z * 0.5f };
    for (int i = 0; i < 4; i++)
        v3_sub_inplace(&out[i], &c);
}

}
