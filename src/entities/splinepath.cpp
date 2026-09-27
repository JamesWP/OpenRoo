/* SplinePath: a Bezier curve through a linked list of control points, and the
 * two debug line-strip draws. */

#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "splinepath.h"
#include <stdlib.h>
#include "log.h"
#include "renderdevice.h"

/* KAROO_SIM_FX=splinerev evaluates every path backwards (t -> 1 - t);
 * KAROO_SPLINE_DIAG=N logs a census every N evaluations;
 * KAROO_SPLINE_SELFCHECK=1 runs the closed-form check below once. */
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

static void *s_vtable[1] = { (void *)&Spline_ScalarDestructor };

SplinePath *__attribute__((thiscall)) Spline_Construct(SplinePath *self)
{
    fx_init();
    if (s_diag)
        ++s_ctors;
    List_Init(&self->controlPointList);
    self->vtable = s_vtable;
    return self;
}

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

void __attribute__((thiscall))
Spline_AddControlPoint(SplinePath *self, float x, float y, float z)
{
    SplineControlPoint *p =
        (SplineControlPoint *)malloc(sizeof(SplineControlPoint));

    fx_init();
    if (s_diag)
        ++s_adds;

    p->flX = x;  // PRESERVED: a failed malloc is not checked.
    p->flY = y;
    p->flZ = z;
    List_Append(&self->controlPointList, p);
}

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

    // Each weight is the Bernstein term C(n-1, i) (1-t)^(n-1-i) t^i, with the
    // binomial built from three factorial loops.  An empty list returns (0, 0,
    // 0); dsoscene relies on that.
    for (i = 0; i < n; ++i) {
        const float *p = (const float *)node->pValue;
        unsigned int num = 1, k;
        int          di = 1, dn = 1;
        double       w;

        node = node->pNextNode;

        for (k = 2; k < n; ++k)
            num *= k;
        for (k = 2; k < i + 1; ++k)
            di *= (int)k;
        for (k = 2; k < n - i; ++k)
            dn *= (int)k;

        // PRESERVED: the factorial quotient is divided unsigned and used
        // signed; the two differ only from 14 control points, and the game
        // uses at most 13.  The sums are float, not extended precision: only
        // the last bits of a position differ.
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

/* The gates do not observe spline drawing, so this checks EvalBezierPath
 * against the closed-form polynomials for 0 to 4 control points. */
static int check_point(const char *what, const float *got, float x, float y,
                       float z)
{
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

    Spline_Construct(&sp);
    out[0] = out[1] = out[2] = 12345.0f;
    Spline_EvalBezierPath(&sp, out, 0.5f);
    bad += check_point("n=0 empty path is (0,0,0)", out, 0, 0, 0);

    Spline_AddControlPoint(&sp, 1.0f, 2.0f, 3.0f);
    for (t = 0.0f; t <= 1.0f; t += 0.5f) {
        Spline_EvalBezierPath(&sp, out, t);
        bad += check_point("n=1 is constant", out, 1, 2, 3);
    }

    Spline_AddControlPoint(&sp, 5.0f, 6.0f, 7.0f);
    Spline_EvalBezierPath(&sp, out, 0.0f);
    bad += check_point("n=2 at t=0 is P0", out, 1, 2, 3);
    Spline_EvalBezierPath(&sp, out, 1.0f);
    bad += check_point("n=2 at t=1 is P1", out, 5, 6, 7);
    Spline_EvalBezierPath(&sp, out, 0.25f);
    bad += check_point("n=2 at t=0.25 is the quarter point", out, 2, 3, 4);

    Spline_PurgeControlPoints(&sp);
    Spline_AddControlPoint(&sp, 0.0f, 0.0f, 0.0f);
    Spline_AddControlPoint(&sp, 4.0f, 0.0f, 0.0f);
    Spline_AddControlPoint(&sp, 4.0f, 4.0f, 0.0f);
    Spline_EvalBezierPath(&sp, out, 0.5f);
    bad += check_point("n=3 at t=0.5 is (3,1,0)", out, 3, 1, 0);
    Spline_EvalBezierPath(&sp, out, 1.0f);
    bad += check_point("n=3 at t=1 is P2", out, 4, 4, 0);

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

typedef long (WINAPI *DrawP_fn)(void *, DWORD, DWORD, void *, DWORD, DWORD);
struct DevVtbl { void *slot[42]; };
struct DevShim { DevVtbl *lpVtbl; };

struct SplineVertex {
    float x, y, z;
    DWORD zero;
    DWORD diffuse, specular;
    float u, v;
};

static long draw_strip(void *dev, void *verts, DWORD count)
{
    return ((DrawP_fn)((DevShim *)dev)->lpVtbl->slot[0x70 / 4])(
        dev, 3 , 0x1e2, verts, count, 0);  // Prim::LineStrip
}

/* On a failed allocation the original writes through the null pointer and
 * faults; that cannot be expressed in C, so the stores are guarded and only
 * the null buffer reaches DrawPrimitive.  No gate reaches the path. */
long __attribute__((thiscall))
Spline_DrawSplinePath(SplinePath *self, RenderDevice *dev,
                      unsigned int numsegments, unsigned long color)
{
    SplineVertex *verts =
        (SplineVertex *)malloc((numsegments + 1) * 32);
    // An unsigned conversion; numsegments == 0 divides by zero.
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

long __attribute__((thiscall))
Spline_DrawControlPolygon(SplinePath *self, RenderDevice *dev,
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
