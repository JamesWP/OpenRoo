#pragma once
#include <stdint.h>

/* The pure 3D maths: the Vec3 and Mat4 types, the vector and matrix helpers
 * over them, and the scalar helpers the game uses directly.  Nothing here
 * depends on the renderer.
 *
 * Implemented in vecmath.cpp (plain float, the C library, no assembly).
 * Float rounding is not matched bit for bit and need not be: these matrices
 * feed rendering only, never the simulation.
 */

struct Mat4 { float m[16]; };   /* row-major, row vectors: v * M */
struct Vec3 { float x, y, z; };

/* The identity and data-movement helpers.  Pure data movement, no arithmetic
 * that could round differently. */
void  m4_identity(Mat4 *d);
void  m4_translate(Mat4 *d, float x, float y, float z);
void  v3_sub(Vec3 *d, const Vec3 *a, const Vec3 *b);
void v3_set(Vec3 *d, float x, float y, float z);
void v3_div(Vec3 *d, const Vec3 *v, float s);
void v3_sub_inplace(Vec3 *d, const Vec3 *v);

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

/* The exported vec3 helpers.  Each result is built in temporaries and then
 * stored, so an output that aliases an input is safe. */

/* Set and return `self`. */
Vec3 *Math_Vec3Set(Vec3 *self, float x, float y, float z);
/* d = a - b, returns d. */
Vec3 *Math_Vec3Sub(Vec3 *d, const Vec3 *a, const Vec3 *b);
/* (x*x + y*y) + z*z. */
double Math_Vec3SqLen(const Vec3 *v);
/* (az*bz + ay*by) + ax*bx. */
double Math_Vec3Dot(const Vec3 *a, const Vec3 *b);
/* d = a x b, returns d. */
Vec3 *Math_Vec3Cross(Vec3 *d, const Vec3 *a, const Vec3 *b);
/* d = v / s, returns d. */
Vec3 *Math_Vec3Div(Vec3 *d, const Vec3 *v, float s);
/* self *= k in place, returns self. */
Vec3 *Math_Vec3ScaleInPlace(Vec3 *self, float k);
/* sqrt((x*x + y*y) + z*z). */
double Math_Vec3Length(const Vec3 *v);
/* out = (v, 1) * m, then xyz /= w unless w == 1.0 or NaN.  m and v by
 * value. */
Vec3 *Math_Vec3TransformPoint(Vec3 *out, Mat4 m, Vec3 v);

/* All sixteen elements, row-major; returns self. */
Mat4 *Math_Mat4Set(Mat4 *self, float m00, float m01, float m02, float m03,
                   float m10, float m11, float m12, float m13,
                   float m20, float m21, float m22, float m23,
                   float m30, float m31, float m32, float m33);
/* Zero d (through a zeroed temporary), returns d. */
Mat4 *Math_Mat4Zero(Mat4 *d);
/* cdecl(out, dir by value, scale): the four
 * corners of a quad facing `dir`, centred on the origin -- out[0] = 0,
 * out[1] = u, out[2] = r, out[3] = r + u, then each minus (r + u) / 2,
 * where r = |dir x up| * scale, u = |dir x r| * scale and up is (1,0,0),
 * or (0,1,0) when dir is exactly (1,0,0).  Used for
 * RenderSceneObjects' billboards. */
void Math_BuildBillboardQuad(Vec3 *out, float dx, float dy, float dz, float scale);

/* The matrix builders.  All cdecl, all return their first argument, all
 * build into a temporary and copy out (so an `out` aliasing an input is
 * safe).
 *
 * The w == 1.0 test in Math_Vec3TransformPoint compares a float with a double
 * 1.0, so NaN skips the divide; a zero w divides to inf/NaN. */
Mat4 *Math_Mat4Identity(Mat4 *out);
/* Both operands BY VALUE, and the product is
 * b * a -- the second argument times the first. */
Mat4 *Math_Mat4Mul(Mat4 *out, Mat4 a, Mat4 b);
/* Identity with row 3 = (x, y, z, 1). */
Mat4 *Math_Mat4Translate(Mat4 *out, float x, float y, float z);
/* The layouts of m4_rot_x/y/z. */
Mat4 *Math_Mat4RotX(Mat4 *out, float angle);
Mat4 *Math_Mat4RotY(Mat4 *out, float angle);
Mat4 *Math_Mat4RotZ(Mat4 *out, float angle);
