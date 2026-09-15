/* d3dmath_common.cpp -- the math helpers that are pure data movement.  The
 * address on each is the original it mirrors. */
#pragma once

#include "d3dmath.h"

/* MatrixSetIdentity, 0x413230 */
void  m4_identity(Mat4 *d);
/* BuildTranslateMatrix, 0x4232b0 */
void  m4_translate(Mat4 *d, float x, float y, float z);
/* VectorSubtract3, 0x403790 */
void  v3_sub(Vec3 *d, const Vec3 *a, const Vec3 *b);
/* 0x403770 */ void v3_set(Vec3 *d, float x, float y, float z);
/* 0x407f70 */ void v3_div(Vec3 *d, const Vec3 *v, float s);
/* 0x426c20 */ void v3_sub_inplace(Vec3 *d, const Vec3 *v);

/* BuildBillboardVertex, 0x421ef0 -- one 0x20-byte FVF 0x1e2 vertex. */
void billboard_vertex(BbVertex *d, const Vec3 *pos, DWORD diffuse,
                      DWORD specular, float u, float v);
