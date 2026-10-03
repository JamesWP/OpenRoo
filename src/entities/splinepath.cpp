/* SplinePath: a Bezier curve through a linked list of control points, and the
 * two debug line-strip draws. */

#include <windows.h>
#include <stdint.h>
#include "sysdev.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

#include "splinepath.h"
#include <stdlib.h>
#include "logger.h"
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
    uint32_t n;

    if (s_init)
        return;
    s_init = 1;

    n = sysdev::getEnv("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "splinerev") == 0) {
        s_fx_splinerev = 1;
        g_logger.write("splinepath: KAROO_SIM_FX=splinerev -- the path parameter "
                  "is reversed (t -> 1-t) where it is consumed, so every "
                  "spline runs backwards\n");
    }

    n = sysdev::getEnv("KAROO_SPLINE_SELFCHECK", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_selfcheck = 1;

    n = sysdev::getEnv("KAROO_SPLINE_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0) {
        s_diag = atoi(buf);
        if (s_diag < 1)
            s_diag = 1;
    }

    if (s_selfcheck)
        run_selfcheck();
}

SplinePath::SplinePath()
{
    fx_init();
    if (s_diag)
        ++s_ctors;
}

SplinePath::~SplinePath()
{
    fx_init();
    if (s_diag)
        ++s_dtors;
    purgeControlPoints();
}

void SplinePath::addControlPoint(float x, float y, float z)
{
    fx_init();
    if (s_diag)
        ++s_adds;

    SplineControlPoint p;
    p.flX = x;
    p.flY = y;
    p.flZ = z;
    points_.push_back(p);
}

void SplinePath::purgeControlPoints()
{
    fx_init();
    if (s_diag)
        ++s_purges;

    points_.clear();
}

float *SplinePath::evalBezierPath(float *out, float t)
{
    unsigned int    n    = (unsigned int)points_.size();
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
            g_logger.write("splinepath: evals=%u (empty=%u, max points=%u) "
                      "adds=%u purges=%u ctors=%u dtors=%u draws=%u\n",
                      s_evals, s_eval_empty, s_eval_maxpts,
                      s_adds, s_purges, s_ctors, s_dtors, s_draws);
    }

    // Each weight is the Bernstein term C(n-1, i) (1-t)^(n-1-i) t^i, with the
    // binomial built from three factorial loops.  An empty list returns (0, 0,
    // 0); dsoscene relies on that.
    for (i = 0; i < n; ++i) {
        const SplineControlPoint &p = points_[i];
        unsigned int num = 1, k;
        int          di = 1, dn = 1;
        double       w;

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

        ax += (float)(w * p.flX);
        ay += (float)(w * p.flY);
        az += (float)(w * p.flZ);
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
        g_logger.write("splinepath: selfcheck ok   %s\n", what);
        return 0;
    }
    g_logger.write("splinepath: selfcheck FAIL %s -- want (%f %f %f) got "
              "(%f %f %f)\n", what, x, y, z, got[0], got[1], got[2]);
    return 1;
}

static void run_selfcheck(void)
{
    SplinePath sp;
    float      out[3];
    int        bad = 0;
    float      t;

    g_logger.write("splinepath: KAROO_SPLINE_SELFCHECK -- EvalBezierPath against "
              "the closed-form Bernstein polynomial\n");

    out[0] = out[1] = out[2] = 12345.0f;
    sp.evalBezierPath(out, 0.5f);
    bad += check_point("n=0 empty path is (0,0,0)", out, 0, 0, 0);

    sp.addControlPoint(1.0f, 2.0f, 3.0f);
    for (t = 0.0f; t <= 1.0f; t += 0.5f) {
        sp.evalBezierPath(out, t);
        bad += check_point("n=1 is constant", out, 1, 2, 3);
    }

    sp.addControlPoint(5.0f, 6.0f, 7.0f);
    sp.evalBezierPath(out, 0.0f);
    bad += check_point("n=2 at t=0 is P0", out, 1, 2, 3);
    sp.evalBezierPath(out, 1.0f);
    bad += check_point("n=2 at t=1 is P1", out, 5, 6, 7);
    sp.evalBezierPath(out, 0.25f);
    bad += check_point("n=2 at t=0.25 is the quarter point", out, 2, 3, 4);

    sp.purgeControlPoints();
    sp.addControlPoint(0.0f, 0.0f, 0.0f);
    sp.addControlPoint(4.0f, 0.0f, 0.0f);
    sp.addControlPoint(4.0f, 4.0f, 0.0f);
    sp.evalBezierPath(out, 0.5f);
    bad += check_point("n=3 at t=0.5 is (3,1,0)", out, 3, 1, 0);
    sp.evalBezierPath(out, 1.0f);
    bad += check_point("n=3 at t=1 is P2", out, 4, 4, 0);

    sp.purgeControlPoints();
    sp.addControlPoint(0.0f, 0.0f, 0.0f);
    sp.addControlPoint(0.0f, 3.0f, 0.0f);
    sp.addControlPoint(3.0f, 3.0f, 0.0f);
    sp.addControlPoint(3.0f, 0.0f, 0.0f);
    sp.evalBezierPath(out, 0.5f);
    bad += check_point("n=4 at t=0.5 is (1.5,2.25,0)", out, 1.5f, 2.25f, 0.0f);
    sp.evalBezierPath(out, 0.0f);
    bad += check_point("n=4 at t=0 is P0", out, 0, 0, 0);

    g_logger.write("splinepath: selfcheck %s (%d failure%s)\n",
              bad ? "FAIL" : "PASS", bad, bad == 1 ? "" : "s");
}

typedef long (WINAPI *DrawP_fn)(void *, uint32_t, uint32_t, void *, uint32_t, uint32_t);
struct DevVtbl { void *slot[42]; };
struct DevShim { DevVtbl *lpVtbl; };

struct SplineVertex {
    float x, y, z;
    uint32_t zero;
    uint32_t diffuse, specular;
    float u, v;
};

static long draw_strip(void *dev, void *verts, uint32_t count)
{
    return ((DrawP_fn)((DevShim *)dev)->lpVtbl->slot[0x70 / 4])(
        dev, 3 , 0x1e2, verts, count, 0);  // Prim::LineStrip
}

long SplinePath::drawSplinePath(RenderDevice *dev,
                      unsigned int numsegments, unsigned long color)
{
    std::vector<SplineVertex> verts(numsegments + 1);
    // An unsigned conversion; numsegments == 0 divides by zero.
    float        step = (float)(1.0 / (double)numsegments);
    unsigned int i;
    long         hr;

    for (i = 0; i <= numsegments; ++i) {
        SplineVertex v;
        float        pt[3];

        evalBezierPath(pt, (float)((double)i * step));
        v.x = pt[0]; v.y = pt[1]; v.z = pt[2];
        v.zero = 0; v.diffuse = color; v.specular = 0;
        v.u = 0.0f; v.v = 0.0f;
        verts[i] = v;
    }

    if (s_diag)
        ++s_draws;
    hr = draw_strip(dev, verts.data(), numsegments + 1);
    return hr;
}

long SplinePath::drawControlPolygon(RenderDevice *dev,
                          unsigned long color)
{
    unsigned int    n     = (unsigned int)points_.size();
    std::vector<SplineVertex> verts(n);
    long            hr;

    for (unsigned int i = 0; i < n; ++i) {
        const SplineControlPoint &p = points_[i];
        SplineVertex v;

        v.x = p.flX; v.y = p.flY; v.z = p.flZ;
        v.zero = 0; v.diffuse = color; v.specular = 0;
        v.u = 0.0f; v.v = 0.0f;
        verts[i] = v;
    }

    if (s_diag)
        ++s_draws;
    hr = draw_strip(dev, verts.data(), n);
    return hr;
}
