#pragma once
#include <windows.h>
#include <d3d.h>

/* Our own copies of the game's shared 3D math helpers.
 *
 * The render replacements must not call the originals, so these are written
 * from the disassembly.  Implemented in d3dmath_std.cpp (plain float, the C
 * library, no assembly) and d3dmath_common.cpp (pure data movement), with the
 * public names in d3dmath_mode.cpp.  Not bit-exact against the originals, and
 * not meant to be: these feed rendering only, never the simulation.
 */

struct Mat4 { float m[16]; };   /* row-major, D3D convention */
struct Vec3 { float x, y, z; };

/* ─── matrix and vector ─────────────────────────────────────────────────── */

void  m4_identity(Mat4 *d);
/* d = a * b, row-major: d[r][c] = SUM_k a[r][k]*b[k][c]. */
void  m4_mul(Mat4 *d, const Mat4 *a, const Mat4 *b);
void  m4_rot_x(Mat4 *d, float angle);
void  m4_rot_y(Mat4 *d, float angle);
void  m4_rot_z(Mat4 *d, float angle);
/* RotX of (angle + bias), as the original's `fld [angle]; fadd [bias]; fcos`. */
void  m4_rot_x_biased(Mat4 *d, float angle, float bias);
void  m4_translate(Mat4 *d, float x, float y, float z);

/* Scalar helpers (d3dmath_mode.cpp).  Separate from
 * the vector routines because DrawSceneObjects uses them directly for the path
 * parameter, the animation frame and the tangent-derived heading. */
float  m_sqrt(float v);
/* Angle between two vectors, acos(dot / (|a||b|)). */
float v3_angle_between(const Vec3 *a, const Vec3 *b);
double m_fmod(double a, double b);
float  m_acos(float v);

/* SUM of squares -- the game's "DotProduct3" is really a self-dot. */
float v3_len_sq(const Vec3 *v);
void  v3_sub(Vec3 *d, const Vec3 *a, const Vec3 *b);
/* 0x403770 */ void v3_set(Vec3 *d, float x, float y, float z);
/* 0x407f70 */ void v3_div(Vec3 *d, const Vec3 *v, float s);
/* 0x426c20 */ void v3_sub_inplace(Vec3 *d, const Vec3 *v);

/* SplinePath::EvalBezierPath, 0x402330.  Bernstein basis of degree count-1:
 *   p = SUM_i C(n-1,i) * t^i * (1-t)^(n-1-i) * P_i
 * An empty control-point list writes (0,0,0) and touches nothing else. */
struct ListNodeM { void *pValue; ListNodeM *pNext; };
void bezier_eval(const ListNodeM *head, unsigned int count, float t, Vec3 *out);

/* BuildBillboardMatrix, 0x4268e0 -- builds four corner offsets (dst[0..3])
 * for a camera-facing quad of the given size. */
void billboard_corners(Vec3 dst[4], float dx, float dy, float dz, float size);

/* BuildBillboardVertex, 0x421ef0 -- one 0x20-byte FVF 0x1e2 vertex. */
struct BbVertex { float x, y, z; DWORD zero; DWORD diffuse, specular; float u, v; };
void billboard_vertex(BbVertex *d, const Vec3 *pos, DWORD diffuse,
                      DWORD specular, float u, float v);
