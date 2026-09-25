/* SplinePath -- ENDGAME_PLAN.md E1.  See splinepath.h for the function table,
 * the vtable ruling and the heap ruling.
 *
 * Taken as a class rather than a function because TickScriptPlayer 0x41d920,
 * the last real callback in E1 besides ParseExtraObjectEntry, calls
 * EvalBezierPath -- replacing the tick first would have swapped one callback
 * for two.  Same who-calls-whom ordering as the four cycles before it.
 *
 * ─── EvalBezierPath, and a Ghidra note that was wrong ────────────────────
 *
 * The plate comment said the Bezier parameter arrives "on the FPU stack left
 * by the caller".  It does not.  The function is `RET 8` and reads its
 * parameter with `FSUB float ptr [ESP+0x60]` / `FLD float ptr [ESP+0x60]` --
 * an ordinary stack float, the second of two dword arguments.  The decompile
 * dropped it (it models neither the argument nor the EAX return), which is
 * how the note came about: CLAUDE.md's "notes are reliable about structure,
 * wrong about meaning" in its usual form.
 *
 * So the real signature is (this, float (*out)[3], float t), returning `out`.
 *
 * The weight is the standard Bernstein term for degree n-1, with the
 * binomial computed by three running factorial loops rather than a table:
 *
 *     C(n-1, i) = (n-1)! / (i! * (n-1-i)!)
 *
 * Each loop multiplies 2,3,... and is skipped when its limit is below 2, so
 * 0! and 1! come out as the initial 1 without a special case.
 *
 * THE DIVISION IS UNSIGNED and the result is then used SIGNED: the original
 * does `DIV ESI` and then `FIMUL dword` (a signed 32-bit load).  Both are
 * kept.  They only differ from the obvious reading once (n-1)! passes 2^31,
 * which needs FOURTEEN control points (12! = 479001600 fits, 13! does not).
 * The game's own data runs it closer than that sounds: KAROO_SPLINE_DIAG
 * reports max points = 13 on every level the suite loads, one short of the
 * boundary.  "Which instruction the compiler picked" is semantics rather
 * than rounding, so it is reproduced rather than tidied.
 *
 * EMPTY LIST IS SAFE, and deliberately so: with dwCount == 0 the loop never
 * runs, nothing is dereferenced, and (0,0,0) is written out.  dsoscene.cpp
 * depends on knowing this -- its orientation branch samples the path twice
 * and differences the results, so an empty path divides 0 by 0 and NaNs the
 * whole world matrix instead of faulting.  That is why dsogolden.cpp installs
 * four real control points.
 *
 * Float rounding is NOT reproduced instruction for instruction (CLAUDE.md:
 * write simple C).  The original rounds the x and y products through a float
 * temporary but leaves z in an x87 register, so z carries extended precision
 * the other two do not; here all three go through float.  The difference is
 * in the last bits of a camera position and cannot change a branch.
 */

#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "splinepath.h"
#include <stdlib.h>
#include "log.h"

/* ─── KAROO_SIM_FX / KAROO_SPLINE_DIAG, read by value ─────────────────────
 * By VALUE, never by presence (RENDER_PLAN.md, 2026-09-02).
 *
 * splinerev reverses the path PARAMETER, t -> 1-t, at the single point it is
 * consumed.  That runs every path backwards -- the flythrough camera and
 * every "usepath" scene object -- while leaving the control points, the
 * weights and the vertex count alone: a DIRECTION change, not a perturbed
 * value, and the same shape as deckaxis and liftflip.
 *
 * It cannot reach the unbounded bridge/slide spawn scans: paths are consumed
 * during play and during scene drawing, never during level setup, and every
 * position it produces is a convex combination of the same control points it
 * always was.  So it is bounded by construction, not by luck. */
static int s_fx_splinerev = 0;
static int s_diag         = 0;
static int s_selfcheck    = 0;
static int s_init         = 0;

static unsigned s_evals = 0, s_adds = 0, s_purges = 0;
static unsigned s_ctors = 0, s_dtors = 0, s_draws = 0;
static unsigned s_eval_empty = 0, s_eval_maxpts = 0;

static void run_selfcheck(void);

static void fx_init(void)
{
    char  buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "splinerev") == 0) {
        s_fx_splinerev = 1;
        log_write("splinepath: KAROO_SIM_FX=splinerev -- the path parameter "
                  "is reversed (t -> 1-t) where it is consumed, so every "
                  "spline runs backwards\n");
    }

    /* Read by VALUE and the value is USED: KAROO_SPLINE_DIAG=N prints the
     * census every N evaluations.  A bare =1 therefore prints one line per
     * eval, which is what tells a recording that evaluates the path twice
     * apart from one that evaluates it two hundred times -- the distinction
     * a fixed interval hid on the first run of this census. */
    n = GetEnvironmentVariableA("KAROO_SPLINE_SELFCHECK", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_selfcheck = 1;

    n = GetEnvironmentVariableA("KAROO_SPLINE_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0) {
        s_diag = atoi(buf);
        if (s_diag < 1)
            s_diag = 1;
    }

    if (s_selfcheck)
        run_selfcheck();
}

/* Our own one-slot vtable; see the header for why it is not the game's. */
static void *s_vtable[1] = { (void *)&Spline_ScalarDestructor };

/* ─── Construction and destruction ───────────────────────────────────────── */

SplinePath *__attribute__((thiscall)) Spline_Construct(SplinePath *self)
{
    fx_init();
    if (s_diag)
        ++s_ctors;
    List_Init(&self->controlPointList);
    self->vtable = s_vtable;
    return self;
}

/* The original wraps Destruct in an MSVC EH frame (0x0045bc6b) so that the
 * LinkedList is destructed if PurgeControlPoints throws.  Neither can throw --
 * the game's operator new returns NULL rather than throwing -- so the frame is
 * unobservable and is not reproduced. */
void __attribute__((thiscall)) Spline_Destruct(SplinePath *self)
{
    fx_init();
    if (s_diag)
        ++s_dtors;
    self->vtable = s_vtable;
    Spline_PurgeControlPoints(self);
    List_Destruct(&self->controlPointList);
}

SplinePath *__attribute__((thiscall))
Spline_ScalarDestructor(SplinePath *self, unsigned char bFreeSelf)
{
    Spline_Destruct(self);
    if (bFreeSelf & 1)
        free(self);
    return self;
}

/* ─── The control-point list ─────────────────────────────────────────────── */

void __attribute__((thiscall))
Spline_AddControlPoint(SplinePath *self, float x, float y, float z)
{
    SplineControlPoint *p =
        (SplineControlPoint *)malloc(sizeof(SplineControlPoint));

    fx_init();
    if (s_diag)
        ++s_adds;

    /* Unguarded, as the original: operator new returns NULL on failure and
     * the stores go straight through it. */
    p->flX = x;
    p->flY = y;
    p->flZ = z;
    List_Append(&self->controlPointList, p);
}

/* Walks with the next pointer read BEFORE the value is freed, so it is the
 * nodes' own order that drives it; Clear then frees the nodes themselves. */
void __attribute__((thiscall)) Spline_PurgeControlPoints(SplinePath *self)
{
    LinkedListNode *node = self->controlPointList.pHead;

    fx_init();
    if (s_diag)
        ++s_purges;

    while (node != 0) {
        void *value = node->pValue;
        node = node->pNextNode;
        if (value != 0)
            free(value);
    }
    List_Clear(&self->controlPointList);
}

/* ─── Evaluation ─────────────────────────────────────────────────────────── */

float *__attribute__((thiscall))
Spline_EvalBezierPath(SplinePath *self, float *out, float t)
{
    unsigned int    n    = (unsigned int)self->controlPointList.dwCount;
    LinkedListNode *node = self->controlPointList.pHead;
    float           ax = 0.0f, ay = 0.0f, az = 0.0f;
    unsigned int    i;

    fx_init();
    if (s_fx_splinerev)
        t = 1.0f - t;
    if (s_diag) {
        ++s_evals;
        if (n == 0)
            ++s_eval_empty;
        if (n > s_eval_maxpts)
            s_eval_maxpts = n;
        if ((s_evals % (unsigned)s_diag) == 0)
            log_write("splinepath: evals=%u (empty=%u, max points=%u) "
                      "adds=%u purges=%u ctors=%u dtors=%u draws=%u\n",
                      s_evals, s_eval_empty, s_eval_maxpts,
                      s_adds, s_purges, s_ctors, s_dtors, s_draws);
    }

    for (i = 0; i < n; ++i) {
        const float *p = (const float *)node->pValue;
        unsigned int num = 1, k;
        int          di = 1, dn = 1;
        double       w;

        node = node->pNextNode;

        for (k = 2; k < n; ++k)             /* (n-1)! */
            num *= k;
        for (k = 2; k < i + 1; ++k)         /* i! */
            di *= (int)k;
        for (k = 2; k < n - i; ++k)         /* (n-1-i)! */
            dn *= (int)k;

        /* Unsigned divide, signed use -- see the header comment. */
        /* Both exponents are `FILD qword` with the high dword zeroed, i.e.
         * UNSIGNED loads; the binomial that follows is `FIMUL dword`, a
         * signed one.  Neither can matter for the values the game feeds in,
         * but signedness is semantics (CLAUDE.md), so it is written down. */
        w = pow((double)(1.0f - t), (double)(n - 1 - i))
          * pow((double)t,          (double)i)
          * (double)(int)(num / (unsigned int)(dn * di));

        ax += (float)(w * p[0]);
        ay += (float)(w * p[1]);
        az += (float)(w * p[2]);
    }

    out[0] = ax;
    out[1] = ay;
    out[2] = az;
    return out;
}


/* ─── KAROO_SPLINE_SELFCHECK ──────────────────────────────────────────────
 *
 * NEITHER GATE CAN SEE THIS CLASS, and that is structural rather than thin
 * coverage.  Both recordings' assertions are simulation state only
 * (lives, score, gems, vitality, ...); the spline drives the flythrough
 * camera and scene-object path animation, which reach no asserted field.
 * KAROO_SIM_FX=splinerev duly passes 16/16 AND levelreport, with the census
 * showing hundreds of evaluations per recording -- live, entirely unobserved.
 * The fixture that once covered this path, dsogolden, was deleted when
 * DrawSceneObjects was replaced (it drove the ORIGINAL), so there is no
 * rendering oracle left in the tree at all.
 *
 * So the maths gets its own oracle rather than borrowing one.  This checks
 * EvalBezierPath against the closed-form Bernstein polynomial for degrees
 * 0..3 -- values derived independently of the implementation, not captured
 * from it, so it is a real assertion and not a snapshot.  It runs inside the
 * DLL under the real ABI on the real game heap.
 *
 * It has been watched to fail: with `t = 1.0f - t` forced on, cases 2-4
 * report FAIL with their expected and actual values, and case 1 (a constant
 * path) correctly does not. */

static int check_point(const char *what, const float *got, float x, float y,
                       float z)
{
    /* Loose: the original accumulates in float with one term at x87 extended
     * precision, so agreement to ~1e-5 is all that is meaningful. */
    const float tol = 1e-5f;
    float dx = got[0] - x, dy = got[1] - y, dz = got[2] - z;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dz < 0) dz = -dz;

    if (dx <= tol && dy <= tol && dz <= tol) {
        log_write("splinepath: selfcheck ok   %s\n", what);
        return 0;
    }
    log_write("splinepath: selfcheck FAIL %s -- want (%f %f %f) got "
              "(%f %f %f)\n", what, x, y, z, got[0], got[1], got[2]);
    return 1;
}

static void run_selfcheck(void)
{
    SplinePath sp;
    float      out[3];
    int        bad = 0;
    float      t;

    log_write("splinepath: KAROO_SPLINE_SELFCHECK -- EvalBezierPath against "
              "the closed-form Bernstein polynomial\n");

    /* Case 0: the empty path dereferences nothing and yields the origin.
     * dsoscene.cpp's orientation branch depends on this. */
    Spline_Construct(&sp);
    out[0] = out[1] = out[2] = 12345.0f;
    Spline_EvalBezierPath(&sp, out, 0.5f);
    bad += check_point("n=0 empty path is (0,0,0)", out, 0, 0, 0);

    /* Case 1: one point -- constant for every t. */
    Spline_AddControlPoint(&sp, 1.0f, 2.0f, 3.0f);
    for (t = 0.0f; t <= 1.0f; t += 0.5f) {
        Spline_EvalBezierPath(&sp, out, t);
        bad += check_point("n=1 is constant", out, 1, 2, 3);
    }

    /* Case 2: two points -- the straight line (1-t)P0 + tP1. */
    Spline_AddControlPoint(&sp, 5.0f, 6.0f, 7.0f);
    Spline_EvalBezierPath(&sp, out, 0.0f);
    bad += check_point("n=2 at t=0 is P0", out, 1, 2, 3);
    Spline_EvalBezierPath(&sp, out, 1.0f);
    bad += check_point("n=2 at t=1 is P1", out, 5, 6, 7);
    Spline_EvalBezierPath(&sp, out, 0.25f);
    bad += check_point("n=2 at t=0.25 is the quarter point", out, 2, 3, 4);

    /* Case 3: three points -- (1-t)^2 P0 + 2t(1-t) P1 + t^2 P2.
     * At t=0.5 with P=(0,0,0),(4,0,0),(4,4,0) that is (3,1,0). */
    Spline_PurgeControlPoints(&sp);
    Spline_AddControlPoint(&sp, 0.0f, 0.0f, 0.0f);
    Spline_AddControlPoint(&sp, 4.0f, 0.0f, 0.0f);
    Spline_AddControlPoint(&sp, 4.0f, 4.0f, 0.0f);
    Spline_EvalBezierPath(&sp, out, 0.5f);
    bad += check_point("n=3 at t=0.5 is (3,1,0)", out, 3, 1, 0);
    Spline_EvalBezierPath(&sp, out, 1.0f);
    bad += check_point("n=3 at t=1 is P2", out, 4, 4, 0);

    /* Case 4: four points -- the cubic.  With P=(0,0,0),(0,3,0),(3,3,0),
     * (3,0,0) the midpoint is (1.5, 2.25, 0), and the weights there are the
     * 1:3:3:1 row the three factorial loops must produce. */
    Spline_PurgeControlPoints(&sp);
    Spline_AddControlPoint(&sp, 0.0f, 0.0f, 0.0f);
    Spline_AddControlPoint(&sp, 0.0f, 3.0f, 0.0f);
    Spline_AddControlPoint(&sp, 3.0f, 3.0f, 0.0f);
    Spline_AddControlPoint(&sp, 3.0f, 0.0f, 0.0f);
    Spline_EvalBezierPath(&sp, out, 0.5f);
    bad += check_point("n=4 at t=0.5 is (1.5,2.25,0)", out, 1.5f, 2.25f, 0.0f);
    Spline_EvalBezierPath(&sp, out, 0.0f);
    bad += check_point("n=4 at t=0 is P0", out, 0, 0, 0);

    Spline_Destruct(&sp);
    log_write("splinepath: selfcheck %s (%d failure%s)\n",
              bad ? "FAIL" : "PASS", bad, bad == 1 ? "" : "s");
}

/* ─── The two debug draws ────────────────────────────────────────────────── */

typedef long (WINAPI *DrawP_fn)(void *, DWORD, DWORD, void *, DWORD, DWORD);
struct DevVtbl { void *slot[42]; };
struct DevShim { DevVtbl *lpVtbl; };

/* One 0x20-byte FVF 0x1e2 vertex, the same layout quadbatch.cpp draws. */
struct SplineVertex {
    float x, y, z;
    DWORD zero;
    DWORD diffuse, specular;
    float u, v;
};

static long draw_strip(void *dev, void *verts, DWORD count)
{
    return ((DrawP_fn)((DevShim *)dev)->lpVtbl->slot[0x70 / 4])(
        dev, 3 /* D3DPT_LINESTRIP */, 0x1e2, verts, count, 0);
}

/* numsegments+1 samples at t = i/numsegments.
 *
 * ONE DELIBERATE DEVIATION: on a NULL allocation the original stores the
 * vertices through the null pointer and faults.  Here the stores are guarded
 * and only the NULL buffer reaches DrawPrimitive, as the original's does.
 * Writing through NULL is undefined in C rather than merely wrong, so it is
 * not something a reimplementation can faithfully express; the path is the
 * out-of-memory one and no gate reaches it. */
long __attribute__((thiscall))
Spline_DrawSplinePath(SplinePath *self, IDirect3DDevice3 *dev,
                      unsigned int numsegments, unsigned long color)
{
    SplineVertex *verts =
        (SplineVertex *)malloc((numsegments + 1) * 32);
    /* `FILD qword` over numsegments with a zeroed high dword -- unsigned,
     * and a zero divides by zero here exactly as it does in the original. */
    float        step = (float)(1.0 / (double)numsegments);
    unsigned int i;
    long         hr;

    for (i = 0; i <= numsegments; ++i) {
        SplineVertex v;
        float        pt[3];

        Spline_EvalBezierPath(self, pt, (float)((double)i * step));
        v.x = pt[0]; v.y = pt[1]; v.z = pt[2];
        v.zero = 0; v.diffuse = color; v.specular = 0;
        v.u = 0.0f; v.v = 0.0f;
        if (verts != 0)
            verts[i] = v;
    }

    if (s_diag)
        ++s_draws;
    hr = draw_strip(dev, verts, numsegments + 1);
    if (verts != 0)
        free(verts);
    return hr;
}

/* The control polygon: one vertex per control point, no evaluation. */
long __attribute__((thiscall))
Spline_DrawControlPolygon(SplinePath *self, IDirect3DDevice3 *dev,
                          unsigned long color)
{
    unsigned int    n     = (unsigned int)self->controlPointList.dwCount;
    SplineVertex   *verts = (SplineVertex *)malloc(n * 32);
    LinkedListNode *node  = self->controlPointList.pHead;
    unsigned int    i     = 0;
    long            hr;

    while (node != 0) {
        const float *p = (const float *)node->pValue;
        SplineVertex v;

        node = node->pNextNode;
        v.x = p[0]; v.y = p[1]; v.z = p[2];
        v.zero = 0; v.diffuse = color; v.specular = 0;
        v.u = 0.0f; v.v = 0.0f;
        if (verts != 0)
            verts[i] = v;
        ++i;
    }

    if (s_diag)
        ++s_draws;
    hr = draw_strip(dev, verts, n);
    if (verts != 0)
        free(verts);
    return hr;
}
