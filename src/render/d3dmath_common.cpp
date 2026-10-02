/* Maths helpers with no arithmetic that could round differently -- pure data
 * movement -- plus the exported wrappers over d3dmath_mode.cpp. */
#include <stdint.h>
#include <math.h>
#include "d3dmath_common.h"
#include "renderdevice.h"
Mat4 g_worldIdentity;

void m4_identity(Mat4 *d)
{
    for (int i = 0; i < 16; ++i)
        d->m[i] = 0.0f;
    d->m[0] = d->m[5] = d->m[10] = d->m[15] = 1.0f;
}

void m4_translate(Mat4 *d, float x, float y, float z)
{
    m4_identity(d);
    d->m[12] = x;  d->m[13] = y;  d->m[14] = z;
}

void v3_sub(Vec3 *d, const Vec3 *a, const Vec3 *b)
{
    d->x = a->x - b->x;
    d->y = a->y - b->y;
    d->z = a->z - b->z;
}

void v3_set(Vec3 *d, float x, float y, float z)
{
    d->x = x;  d->y = y;  d->z = z;
}

/* Componentwise divide by a scalar. */
void v3_div(Vec3 *d, const Vec3 *v, float s)
{
    d->x = v->x / s;
    d->y = v->y / s;
    d->z = v->z / s;
}

void v3_sub_inplace(Vec3 *d, const Vec3 *v)
{
    d->x = d->x - v->x;
    d->y = d->y - v->y;
    d->z = d->z - v->z;
}

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

/* ─── The exported vec3 helpers (d3dmath_common.h) ───────────────────────── */
 

  Vec3 * 
Math_Vec3Set(Vec3 *self, float x, float y, float z)
{
    v3_set(self, x, y, z);
    return self;
}

  Vec3 * 
Math_Vec3Sub(Vec3 *d, const Vec3 *a, const Vec3 *b)
{
    Vec3 t;
    v3_sub(&t, a, b);
    *d = t;
    return d;
}

  double  
Math_Vec3SqLen(const Vec3 *v)
{
    return ((double)v->x * v->x + (double)v->y * v->y) + (double)v->z * v->z;
}

  double  
Math_Vec3Dot(const Vec3 *a, const Vec3 *b)
{
    return ((double)a->z * b->z + (double)a->y * b->y) + (double)a->x * b->x;
}

  Vec3 * 
Math_Vec3Cross(Vec3 *d, const Vec3 *a, const Vec3 *b)
{
    Vec3 t;
    t.x = (float)((double)b->z * a->y - (double)a->z * b->y);
    t.y = (float)((double)a->z * b->x - (double)b->z * a->x);
    t.z = (float)((double)a->x * b->y - (double)b->x * a->y);
    *d = t;
    return d;
}

  Vec3 * 
Math_Vec3Div(Vec3 *d, const Vec3 *v, float s)
{
    Vec3 t;
    v3_div(&t, v, s);
    *d = t;
    return d;
}

  Mat4 * 
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

  Mat4 * 
Math_Mat4Zero(Mat4 *d)
{
    for (int i = 0; i < 16; i++)
        d->m[i] = 0.0f;
    return d;
}

  void  
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

/* ─── The matrix builders ──────────────────────────────────────────────────
 *
 * The w == 1.0 test compares a float with a double 1.0, so NaN skips the
 * divide; a zero w divides to inf/NaN. */

  Mat4 *  Math_Mat4Identity(Mat4 *out)
{
    m4_identity(out);
    return out;
}

  Mat4 *  Math_Mat4Mul(Mat4 *out, Mat4 a, Mat4 b)
{
    m4_mul(out, &a, &b);        /* b * a, as m4_mul_std sums it */
    return out;
}

  Vec3 *  Math_Vec3TransformPoint(Vec3 *out, Mat4 m, Vec3 v)
{
    const float in[4] = { v.x, v.y, v.z, 1.0f };
    float o[4];
    for (int c = 0; c < 4; c++) {
        double sum = 0.0;
        for (int k = 0; k < 4; k++)
            sum += (double)in[k] * (double)m.m[k * 4 + c];
        o[c] = (float)sum;
    }
    const float w = o[3];
    if (!(w == 1.0 || w != w)) {
        o[0] = o[0] / w;
        o[1] = o[1] / w;
        o[2] = o[2] / w;
    }
    out->x = o[0];  out->y = o[1];  out->z = o[2];
    return out;
}

  Mat4 *  Math_Mat4Translate(Mat4 *out, float x, float y, float z)
{
    Mat4 t;
    m4_translate(&t, x, y, z);
    *out = t;
    return out;
}

  Mat4 *  Math_Mat4RotX(Mat4 *out, float angle)
{
    Mat4 t;  m4_rot_x(&t, angle);  *out = t;  return out;
}

  Mat4 *  Math_Mat4RotY(Mat4 *out, float angle)
{
    Mat4 t;  m4_rot_y(&t, angle);  *out = t;  return out;
}

  Mat4 *  Math_Mat4RotZ(Mat4 *out, float angle)
{
    Mat4 t;  m4_rot_z(&t, angle);  *out = t;  return out;
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

  Vec3 * 
Math_Vec3ScaleInPlace(Vec3 *self, float k)
{
    self->x = self->x * k;
    self->y = self->y * k;
    self->z = self->z * k;
    return self;
}

  double  
Math_Vec3Length(const Vec3 *v)
{
    return sqrt(Math_Vec3SqLen(v));
}

