/* The 3D helpers' maths: plain float, the C library, no assembly.
 *
 * Float rounding is not matched bit for bit and need not be: these matrices
 * feed rendering only, never the simulation.
 *
 * m4_identity, m4_translate, v3_set, v3_sub, v3_div, v3_sub_inplace and
 * billboard_vertex are pure data movement and live in d3dmath_common.cpp.
 */
#include <math.h>
#include "d3dmath_std.h"

void m4_mul_std(Mat4 *d, const Mat4 *a, const Mat4 *b)
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

void m4_rot_x_std(Mat4 *d, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    m4_identity(d);
    d->m[5] = c;   d->m[6]  = -s;
    d->m[9] = s;   d->m[10] =  c;
}

void m4_rot_x_biased_std(Mat4 *d, float angle, float bias)
{
    m4_rot_x_std(d, angle + bias);
}

void m4_rot_y_std(Mat4 *d, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    m4_identity(d);
    d->m[0] =  c;  d->m[2]  = s;
    d->m[8] = -s;  d->m[10] = c;
}

void m4_rot_z_std(Mat4 *d, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    m4_identity(d);
    d->m[0] = c;   d->m[1] = -s;
    d->m[4] = s;   d->m[5] =  c;
}

float v3_len_sq_std(const Vec3 *v)
{
    return (float)((double)v->x * v->x + (double)v->y * v->y + (double)v->z * v->z);
}

/* Bernstein basis of degree n-1.  The binomial coefficient is built with the
 * multiplicative recurrence rather than three factorial loops -- it cannot
 * overflow and needs no division -- and the weights use powf. */
void bezier_eval_std(const ListNodeM *head, unsigned int n, float t, Vec3 *out)
{
    double ax = 0.0, ay = 0.0, az = 0.0;

    const ListNodeM *node = head;
    for (unsigned int i = 0; i < n; ++i) {
        const float *p = (const float *)node->pValue;
        node = node->pNext;

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
void billboard_corners_std(Vec3 dst[4], float dx, float dy, float dz, float size)
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
float v3_angle_between_std(const Vec3 *a, const Vec3 *b)
{
    float la = sqrtf(v3_len_sq_std(a));
    float lb = sqrtf(v3_len_sq_std(b));
    double dot = (double)a->x * b->x + (double)a->z * b->z;
    dot = dot + (double)a->y * b->y;
    return acosf((float)(dot / ((double)la * lb)));
}
