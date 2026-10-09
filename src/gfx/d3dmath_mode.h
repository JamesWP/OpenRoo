/* d3dmath_mode.cpp -- the public entry points over d3dmath_std.cpp's maths. */
#pragma once

#include "d3dmath.h"

/* d = a * b, row-major: d[r][c] = SUM_k a[r][k]*b[k][c]. */
void  m4_mul(Mat4 *d, const Mat4 *a, const Mat4 *b);
void  m4_rot_x(Mat4 *d, float angle);
void  m4_rot_y(Mat4 *d, float angle);
void  m4_rot_z(Mat4 *d, float angle);
/* RotX of (angle + bias), as the original's `fld [angle]; fadd [bias]; fcos`. */
void  m4_rot_x_biased(Mat4 *d, float angle, float bias);

/* Scalar helpers.  Separate from the vector routines because
 * DrawSceneObjects uses them directly for the path parameter, the animation
 * frame and the tangent-derived heading. */
float  m_sqrt(float v);
/* Angle between two vectors, acos(dot / (|a||b|)). */
float  v3_angle_between(const Vec3 *a, const Vec3 *b);
double m_fmod(double a, double b);
float  m_acos(float v);

/* SUM of squares -- the game's "DotProduct3" is really a self-dot. */
float  v3_len_sq(const Vec3 *v);

/* Bernstein basis of degree count-1:
 *   p = SUM_i C(n-1,i) * t^i * (1-t)^(n-1-i) * P_i
 * An empty control-point list writes (0,0,0) and touches nothing else. */
void bezier_eval(const float *head, unsigned int count, float t, Vec3 *out);

/* Builds four corner offsets (dst[0..3])
 * for a camera-facing quad of the given size. */
void billboard_corners(Vec3 dst[4], float dx, float dy, float dz, float size);
