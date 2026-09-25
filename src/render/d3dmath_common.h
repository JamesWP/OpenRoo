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

/* The vec3 helpers of the CdThemes TU (0x403770..0x403890), exported for
 * the game's remaining call sites.  Each result is built in temporaries and
 * then stored, so an output that aliases an input is safe, as there. */
extern "C" {
/* 0x403770, thiscall, RET 0xc: set and return `self`. */
__declspec(dllexport) Vec3 *__attribute__((thiscall))
Math_Vec3Set(Vec3 *self, float x, float y, float z);
/* 0x403790, cdecl: d = a - b, returns d. */
__declspec(dllexport) Vec3 *__cdecl Math_Vec3Sub(Vec3 *d, const Vec3 *a, const Vec3 *b);
/* 0x4037e0, cdecl: (x*x + y*y) + z*z, in ST0. */
__declspec(dllexport) double __cdecl Math_Vec3SqLen(const Vec3 *v);
/* 0x403810, cdecl: (az*bz + ay*by) + ax*bx, in ST0. */
__declspec(dllexport) double __cdecl Math_Vec3Dot(const Vec3 *a, const Vec3 *b);
/* 0x403830, cdecl: d = a x b, returns d. */
__declspec(dllexport) Vec3 *__cdecl Math_Vec3Cross(Vec3 *d, const Vec3 *a, const Vec3 *b);
/* 0x407f70, cdecl: d = v / s, returns d. */
__declspec(dllexport) Vec3 *__cdecl Math_Vec3Div(Vec3 *d, const Vec3 *v, float s);
/* 0x406480, thiscall, RET 0x40: all sixteen elements, row-major, returns
 * self.  (Ghidra's "MatrixBuildIdentity" -- it builds whatever it is given.) */
__declspec(dllexport) Mat4 *__attribute__((thiscall))
Math_Mat4Set(Mat4 *self, float m00, float m01, float m02, float m03,
             float m10, float m11, float m12, float m13,
             float m20, float m21, float m22, float m23,
             float m30, float m31, float m32, float m33);
/* 0x406500, cdecl: zero d (through a zeroed temporary), returns d. */
__declspec(dllexport) Mat4 *__cdecl Math_Mat4Zero(Mat4 *d);
/* BuildBillboardMatrix 0x4268e0, cdecl(out, dir by value, scale): the four
 * corners of a quad facing `dir`, centred on the origin -- out[0] = 0,
 * out[1] = u, out[2] = r, out[3] = r + u, then each minus (r + u) / 2,
 * where r = |dir x up| * scale, u = |dir x r| * scale and up is (1,0,0),
 * or (0,1,0) when dir is exactly (1,0,0).  Callers: RenderSceneObjects'
 * billboards and 0x421064.  (0x426c20, its in-place subtract, is inlined.) */
__declspec(dllexport) void __cdecl
Math_BuildBillboardQuad(Vec3 *out, float dx, float dy, float dz, float scale);

/* ENDGAME #5: the Direct3D TU's matrix trio and Scene's four builders.  All
 * cdecl, all return their first argument, all build into a temporary and
 * copy out (so an `out` aliasing an input is safe, as there). */
/* MatrixSetIdentity 0x413230. */
__declspec(dllexport) Mat4 *__cdecl Math_Mat4Identity(Mat4 *out);
/* MatrixMultiply4x4 0x4132d0: both operands BY VALUE, and the product is
 * b * a -- the second argument times the first. */
__declspec(dllexport) Mat4 *__cdecl Math_Mat4Mul(Mat4 *out, Mat4 a, Mat4 b);
/* 0x413350: out = (v, 1) * m, then xyz /= w unless w == 1.0 (or unordered:
 * FCOMP sets C3 for NaN too, so a NaN w skips the divide).  m and v by
 * value. */
__declspec(dllexport) Vec3 *__cdecl Math_Vec3TransformPoint(Vec3 *out, Mat4 m, Vec3 v);
/* BuildTranslateMatrix 0x4232b0: identity with row 3 = (x, y, z, 1). */
__declspec(dllexport) Mat4 *__cdecl Math_Mat4Translate(Mat4 *out, float x, float y, float z);
/* BuildX/Y/ZRotationMatrix 0x423380 / 0x423420 / 0x4234c0 -- the layouts of
 * m4_rot_x/y/z_std (re-read from each listing's store offsets). */
__declspec(dllexport) Mat4 *__cdecl Math_Mat4RotX(Mat4 *out, float angle);
__declspec(dllexport) Mat4 *__cdecl Math_Mat4RotY(Mat4 *out, float angle);
__declspec(dllexport) Mat4 *__cdecl Math_Mat4RotZ(Mat4 *out, float angle);
/* 0x42cd60, thiscall, RET 0x18: a D3DTLVERTEX (FVF 0x1C4) from *pos and
 * rhw, colour, specular, tu, tv, stored as given -- the callers push rhw
 * 10.0f.  Returns self.  36 E8 sites, 17 of them in RenderGameFrame. */
__declspec(dllexport) D3DTLVERTEX *__attribute__((thiscall))
Math_VertexSet(D3DTLVERTEX *self, const Vec3 *pos, float rhw, D3DCOLOR color,
               D3DCOLOR specular, float tu, float tv);
/* 0x42cda0, thiscall, RET 4: self *= k in place, returns self. */
__declspec(dllexport) Vec3 *__attribute__((thiscall))
Math_Vec3ScaleInPlace(Vec3 *self, float k);
/* 0x42cdd0, cdecl: sqrt((x*x + y*y) + z*z), in ST0. */
__declspec(dllexport) double __cdecl Math_Vec3Length(const Vec3 *v);
}
