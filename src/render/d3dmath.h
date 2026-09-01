#pragma once
#include <windows.h>
#include <d3d.h>

/* Our own copies of the game's shared 3D math helpers.
 *
 * The render replacements must not call the originals, so these are written
 * from the disassembly.  The originals stay live in the binary -- they have
 * many other callers -- these are simply our own equivalents.
 *
 * BIT-EXACTNESS IS THE WHOLE POINT, and it is not automatic:
 *
 *  - The originals do all float arithmetic on the x87 stack, accumulating in
 *    80 bits and rounding once on store.  A dot product written as
 *    `a*b + c*d + e*f` in C compiled for i386 without SSE does the same, since
 *    GCC keeps x87 intermediates in long double (FLT_EVAL_METHOD 2).  These
 *    functions rely on that, so this file must NOT be compiled with -msse2 or
 *    -ffloat-store.  d3dmath_selftest.cpp checks the results against the
 *    originals at runtime, so a toolchain change that breaks the assumption
 *    is caught rather than silently shipped.
 *  - cos and sin must be the x87 FCOS/FSIN instructions, not libm's cosf.
 *    They are reached through inline asm below for exactly that reason.
 *  - Where the original computes an angle in 80-bit and only then takes its
 *    cosine (e.g. `fld [angle]; fadd [pi/2]; fcos`), rounding the sum to
 *    float32 first can change the result.  m4_rot_x_biased exists to keep
 *    that addition in 80 bits.
 */

struct Mat4 { float m[16]; };   /* row-major, D3D convention */
struct Vec3 { float x, y, z; };

/* ─── x87 primitives ─────────────────────────────────────────────────────
 * `t` constrains the operand to st(0); "=t" returns in st(0).  FCOS/FSIN
 * replace their operand, so a single-input/single-output constraint is
 * exactly right and no explicit push/pop is needed. */
static inline long double x87_cos(long double a)
{
    long double r;
    __asm__ ("fcos" : "=t" (r) : "0" (a));
    return r;
}

static inline long double x87_sin(long double a)
{
    long double r;
    __asm__ ("fsin" : "=t" (r) : "0" (a));
    return r;
}

static inline long double x87_sqrt(long double a)
{
    long double r;
    __asm__ ("fsqrt" : "=t" (r) : "0" (a));
    return r;
}

/* The x87 has no acos.  MSVC's CRT builds it from FPATAN as
 * atan2(sqrt(1-x*x), x), which is what the original's acos call resolves to;
 * FPATAN is a single instruction, so this matches without a polynomial. */
static inline long double x87_atan2(long double y, long double x)
{
    long double r;
    __asm__ ("fpatan" : "=t" (r) : "0" (x), "u" (y) : "st(1)");
    return r;
}

static inline long double x87_acos(long double x)
{
    return x87_atan2(x87_sqrt(1.0L - x * x), x);
}

/* fmod: FPREM1 gives the IEEE remainder; the original's CRT fmod uses FPREM,
 * which truncates toward zero.  Loop until the reduction is complete (C2
 * clear), exactly as the CRT does. */
static inline long double x87_fmod(long double a, long double b)
{
    long double r = a;
    unsigned short sw;
    do {
        __asm__ ("fprem" : "=t" (r) : "0" (r), "u" (b));
        __asm__ ("fnstsw %0" : "=a" (sw));
    } while (sw & 0x0400);          /* C2 set => partial remainder */
    return r;
}

/* ─── matrix and vector ─────────────────────────────────────────────────── */

void  m4_identity(Mat4 *d);
/* d = a * b, row-major: d[r][c] = SUM_k a[r][k]*b[k][c].  Accumulated in
 * 80 bits per element, matching the original's FLD/FMUL/FADDP chain. */
void  m4_mul(Mat4 *d, const Mat4 *a, const Mat4 *b);
void  m4_rot_x(Mat4 *d, float angle);
void  m4_rot_y(Mat4 *d, float angle);
void  m4_rot_z(Mat4 *d, float angle);
/* RotX of (angle + bias) with the addition kept in 80 bits, matching
 * `fld [angle]; fadd [bias]; fcos`. */
void  m4_rot_x_biased(Mat4 *d, float angle, float bias);
void  m4_translate(Mat4 *d, float x, float y, float z);

/* x87 pow, matching the CRT's _CIpow (0x450c80): 2^(y*log2(x)) built from
 * FYL2X and F2XM1, with the integer and fractional parts of the exponent
 * separated by FRNDINT/FSCALE.  x <= 0 is not reached by any caller here. */
long double x87_pow(long double x, long double y);

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
