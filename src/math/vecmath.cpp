/* The pure 3D maths: plain float, the C library, no assembly.
 *
 * Float rounding is not matched bit for bit and need not be: these matrices
 * feed rendering only, never the simulation.
 */
#include <math.h>
#include "vecmath.h"

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

void m4_mul(Mat4 *d, const Mat4 *a, const Mat4 *b)
{
    Mat4 t;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k)
                sum += (double)b->m[r * 4 + k] * (double)a->m[k * 4 + c];
            t.m[r * 4 + c] = (float)sum;
        }
    }
    *d = t;
}

void m4_rot_x(Mat4 *d, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    m4_identity(d);
    d->m[5] = c;   d->m[6]  = -s;
    d->m[9] = s;   d->m[10] =  c;
}

void m4_rot_x_biased(Mat4 *d, float angle, float bias)
{
    m4_rot_x(d, angle + bias);
}

void m4_rot_y(Mat4 *d, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    m4_identity(d);
    d->m[0] =  c;  d->m[2]  = s;
    d->m[8] = -s;  d->m[10] = c;
}

void m4_rot_z(Mat4 *d, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    m4_identity(d);
    d->m[0] = c;   d->m[1] = -s;
    d->m[4] = s;   d->m[5] =  c;
}

float v3_len_sq(const Vec3 *v)
{
    return (float)((double)v->x * v->x + (double)v->y * v->y + (double)v->z * v->z);
}

/* Bernstein basis of degree n-1.  The binomial coefficient is built with the
 * multiplicative recurrence rather than three factorial loops -- it cannot
 * overflow and needs no division -- and the weights use powf. */
void bezier_eval(const float *pts, unsigned int n, float t, Vec3 *out)
{
    double ax = 0.0, ay = 0.0, az = 0.0;

    for (unsigned int i = 0; i < n; ++i) {
        const float *p = pts + 3 * i;

        double coeff = 1.0;                      /* C(n-1, i) */
        for (unsigned int k = 0; k < i; ++k)
            coeff = coeff * (double)(n - 1 - k) / (double)(k + 1);

        double w = coeff
                 * pow((double)t, (double)i)
                 * pow(1.0 - (double)t, (double)(n - 1 - i));

        ax += w * p[0];
        ay += w * p[1];
        az += w * p[2];
    }

    out->x = (float)ax;  out->y = (float)ay;  out->z = (float)az;
}

/* Camera-facing quad corners.  Same construction as the original -- pick an up
 * vector, two cross products, normalise, scale, centre -- but with an epsilon
 * on the degenerate test instead of the original's exact float comparison,
 * which is the one deliberate behavioural difference in this file. */
void billboard_corners(Vec3 dst[4], float dx, float dy, float dz, float size)
{
    Vec3 up;
    if (fabsf(dx - 1.0f) < 1e-6f && fabsf(dy) < 1e-6f && fabsf(dz) < 1e-6f)
        v3_set(&up, 0.0f, 1.0f, 0.0f);
    else
        v3_set(&up, 1.0f, 0.0f, 0.0f);

    double len = sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
    Vec3 fwd = { (float)(dx / len), (float)(dy / len), (float)(dz / len) };

    Vec3 right, upp;
    right.x = up.z * fwd.y - fwd.z * up.y;
    right.y = fwd.z * up.x - up.z * fwd.x;
    right.z = up.y * fwd.x - fwd.y * up.x;

    upp.x = fwd.y * right.z - fwd.z * right.y;
    upp.y = fwd.z * right.x - fwd.x * right.z;
    upp.z = fwd.x * right.y - fwd.y * right.x;

    float rl = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
    float ul = sqrtf(upp.x * upp.x + upp.y * upp.y + upp.z * upp.z);

    dst[0].x = dst[0].y = dst[0].z = 0.0f;
    v3_set(&dst[2], right.x / rl * size, right.y / rl * size, right.z / rl * size);
    v3_set(&dst[1], upp.x / ul * size,   upp.y / ul * size,   upp.z / ul * size);
    v3_set(&dst[3], dst[2].x + dst[1].x, dst[2].y + dst[1].y, dst[2].z + dst[1].z);

    Vec3 centre = { dst[3].x * 0.5f, dst[3].y * 0.5f, dst[3].z * 0.5f };
    for (int i = 0; i < 4; ++i)
        v3_sub_inplace(&dst[i], &centre);
}

/* acos(dot / (|a||b|)).  Same summation order as the original for readability,
 * though nothing here depends on it. */
float v3_angle_between(const Vec3 *a, const Vec3 *b)
{
    float la = sqrtf(v3_len_sq(a));
    float lb = sqrtf(v3_len_sq(b));
    double dot = (double)a->x * b->x + (double)a->z * b->z;
    dot = dot + (double)a->y * b->y;
    return acosf((float)(dot / ((double)la * lb)));
}

float m_sqrt(float v)             { return sqrtf(v); }
double m_fmod(double a, double b) { return fmod(a, b); }
float m_acos(float v)             { return acosf(v); }

/* ─── The exported vec3 helpers (vecmath.h) ──────────────────────────────── */

Vec3 *Math_Vec3Set(Vec3 *self, float x, float y, float z)
{
    v3_set(self, x, y, z);
    return self;
}

Vec3 *Math_Vec3Sub(Vec3 *d, const Vec3 *a, const Vec3 *b)
{
    Vec3 t;
    v3_sub(&t, a, b);
    *d = t;
    return d;
}

double Math_Vec3SqLen(const Vec3 *v)
{
    return ((double)v->x * v->x + (double)v->y * v->y) + (double)v->z * v->z;
}

double Math_Vec3Dot(const Vec3 *a, const Vec3 *b)
{
    return ((double)a->z * b->z + (double)a->y * b->y) + (double)a->x * b->x;
}

Vec3 *Math_Vec3Cross(Vec3 *d, const Vec3 *a, const Vec3 *b)
{
    Vec3 t;
    t.x = (float)((double)b->z * a->y - (double)a->z * b->y);
    t.y = (float)((double)a->z * b->x - (double)b->z * a->x);
    t.z = (float)((double)a->x * b->y - (double)b->x * a->y);
    *d = t;
    return d;
}

Vec3 *Math_Vec3Div(Vec3 *d, const Vec3 *v, float s)
{
    Vec3 t;
    v3_div(&t, v, s);
    *d = t;
    return d;
}

Mat4 *Math_Mat4Set(Mat4 *self, float m00, float m01, float m02, float m03,
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

Mat4 *Math_Mat4Zero(Mat4 *d)
{
    for (int i = 0; i < 16; i++)
        d->m[i] = 0.0f;
    return d;
}

void Math_BuildBillboardQuad(Vec3 *out, float dx, float dy, float dz, float scale)
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

/* ─── The matrix builders ────────────────────────────────────────────────── */

Mat4 *Math_Mat4Identity(Mat4 *out)
{
    m4_identity(out);
    return out;
}

Mat4 *Math_Mat4Mul(Mat4 *out, Mat4 a, Mat4 b)
{
    m4_mul(out, &a, &b);        /* b * a, as the summation in m4_mul sums it */
    return out;
}

Vec3 *Math_Vec3TransformPoint(Vec3 *out, Mat4 m, Vec3 v)
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

Mat4 *Math_Mat4Translate(Mat4 *out, float x, float y, float z)
{
    Mat4 t;
    m4_translate(&t, x, y, z);
    *out = t;
    return out;
}

Mat4 *Math_Mat4RotX(Mat4 *out, float angle)
{
    Mat4 t;  m4_rot_x(&t, angle);  *out = t;  return out;
}

Mat4 *Math_Mat4RotY(Mat4 *out, float angle)
{
    Mat4 t;  m4_rot_y(&t, angle);  *out = t;  return out;
}

Mat4 *Math_Mat4RotZ(Mat4 *out, float angle)
{
    Mat4 t;  m4_rot_z(&t, angle);  *out = t;  return out;
}

Vec3 *Math_Vec3ScaleInPlace(Vec3 *self, float k)
{
    self->x = self->x * k;
    self->y = self->y * k;
    self->z = self->z * k;
    return self;
}

double Math_Vec3Length(const Vec3 *v)
{
    return sqrt(Math_Vec3SqLen(v));
}
