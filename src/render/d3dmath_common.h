/* d3dmath_common.cpp -- the maths helpers that are pure data movement, and
 * the exported wrappers. */
#pragma once
#include <stdint.h>

#include "d3dmath.h"
#include "rendertypes.h"

void  m4_identity(Mat4 *d);
void  m4_translate(Mat4 *d, float x, float y, float z);
void  v3_sub(Vec3 *d, const Vec3 *a, const Vec3 *b);
void v3_set(Vec3 *d, float x, float y, float z);
void v3_div(Vec3 *d, const Vec3 *v, float s);
void v3_sub_inplace(Vec3 *d, const Vec3 *v);

/* One 0x20-byte FVF 0x1e2 vertex. */
void billboard_vertex(BbVertex *d, const Vec3 *pos, uint32_t diffuse,
                      uint32_t specular, float u, float v);

/* The exported vec3 helpers.  Each result is built in temporaries and then
 * stored, so an output that aliases an input is safe. */
 
/* Set and return `self`. */
  Vec3 * 
Math_Vec3Set(Vec3 *self, float x, float y, float z);
/* d = a - b, returns d. */
  Vec3 *  Math_Vec3Sub(Vec3 *d, const Vec3 *a, const Vec3 *b);
/* (x*x + y*y) + z*z. */
  double   Math_Vec3SqLen(const Vec3 *v);
/* (az*bz + ay*by) + ax*bx. */
  double   Math_Vec3Dot(const Vec3 *a, const Vec3 *b);
/* d = a x b, returns d. */
  Vec3 *  Math_Vec3Cross(Vec3 *d, const Vec3 *a, const Vec3 *b);
/* d = v / s, returns d. */
  Vec3 *  Math_Vec3Div(Vec3 *d, const Vec3 *v, float s);
/* All sixteen elements, row-major; returns self. */
  Mat4 * 
Math_Mat4Set(Mat4 *self, float m00, float m01, float m02, float m03,
             float m10, float m11, float m12, float m13,
             float m20, float m21, float m22, float m23,
             float m30, float m31, float m32, float m33);
/* Zero d (through a zeroed temporary), returns d. */
  Mat4 *  Math_Mat4Zero(Mat4 *d);
/* cdecl(out, dir by value, scale): the four
 * corners of a quad facing `dir`, centred on the origin -- out[0] = 0,
 * out[1] = u, out[2] = r, out[3] = r + u, then each minus (r + u) / 2,
 * where r = |dir x up| * scale, u = |dir x r| * scale and up is (1,0,0),
 * or (0,1,0) when dir is exactly (1,0,0).  Used for
 * RenderSceneObjects' billboards. */
  void  
Math_BuildBillboardQuad(Vec3 *out, float dx, float dy, float dz, float scale);

/* The matrix builders.  All cdecl, all return their first argument, all
 * build into a temporary and copy out (so an `out` aliasing an input is
 * safe). */
  Mat4 *  Math_Mat4Identity(Mat4 *out);
/* Both operands BY VALUE, and the product is
 * b * a -- the second argument times the first. */
  Mat4 *  Math_Mat4Mul(Mat4 *out, Mat4 a, Mat4 b);
/* out = (v, 1) * m, then xyz /= w unless w == 1.0 or NaN.  m and v by
 * value. */
  Vec3 *  Math_Vec3TransformPoint(Vec3 *out, Mat4 m, Vec3 v);
/* Identity with row 3 = (x, y, z, 1). */
  Mat4 *  Math_Mat4Translate(Mat4 *out, float x, float y, float z);
/* The layouts of m4_rot_x/y/z_std. */
  Mat4 *  Math_Mat4RotX(Mat4 *out, float angle);
  Mat4 *  Math_Mat4RotY(Mat4 *out, float angle);
  Mat4 *  Math_Mat4RotZ(Mat4 *out, float angle);
/* A ScreenVertex (FVF 0x1C4) from *pos and rhw, colour, specular, tu, tv,
 * stored as given; returns self. */
  ScreenVertex * 
Math_VertexSet(ScreenVertex *self, const Vec3 *pos, float rhw, uint32_t color,
               uint32_t specular, float tu, float tv);
/* self *= k in place, returns self. */
  Vec3 * 
Math_Vec3ScaleInPlace(Vec3 *self, float k);
/* sqrt((x*x + y*y) + z*z). */
  double   Math_Vec3Length(const Vec3 *v);
