/* Public entry points for the math layer.
 *
 * The implementation is d3dmath_std.cpp: plain float, the C library, no
 * assembly.  It is not bit-exact against the game's originals and does not
 * need to be -- it was accepted on the bar that matters, which is that the
 * game looks and plays the same.  Measured drift over the DrawSceneObjects
 * fixture was 6.3e-07 absolute on values of order 1, and the replay suite
 * passes with every asserted end-state field intact, because these matrices
 * feed rendering and never the simulation.
 *
 * The bit-exact backend and the verification build that compared it against
 * the originals are gone; see RENDER_PLAN.md, 2026-09-13.
 *
 * The scalar helpers (m_sqrt / m_fmod / m_acos) are separate from the vector
 * and matrix routines because DrawSceneObjects uses them directly for the path
 * parameter, the animation frame and the tangent-derived heading.
 */
#include <math.h>
#include "d3dmath.h"

void  m4_mul_std(Mat4 *, const Mat4 *, const Mat4 *);
void  m4_rot_x_std(Mat4 *, float);
void  m4_rot_y_std(Mat4 *, float);
void  m4_rot_z_std(Mat4 *, float);
void  m4_rot_x_biased_std(Mat4 *, float, float);
float v3_len_sq_std(const Vec3 *);
void  bezier_eval_std(const ListNodeM *, unsigned int, float, Vec3 *);
void  billboard_corners_std(Vec3 *, float, float, float, float);
float v3_angle_between_std(const Vec3 *, const Vec3 *);

#define DISPATCH(rt, name, params, args)                 \
    rt name params { return name##_std args; }

float  m_sqrt(float v)             { return sqrtf(v); }
double m_fmod(double a, double b)  { return fmod(a, b); }
float  m_acos(float v)             { return acosf(v); }

DISPATCH(void,  m4_mul,          (Mat4 *d, const Mat4 *a, const Mat4 *b), (d, a, b))
DISPATCH(void,  m4_rot_x,        (Mat4 *d, float a),                      (d, a))
DISPATCH(void,  m4_rot_y,        (Mat4 *d, float a),                      (d, a))
DISPATCH(void,  m4_rot_z,        (Mat4 *d, float a),                      (d, a))
DISPATCH(void,  m4_rot_x_biased, (Mat4 *d, float a, float bias),          (d, a, bias))
DISPATCH(float, v3_len_sq,       (const Vec3 *v),                         (v))
DISPATCH(void,  bezier_eval,     (const ListNodeM *h, unsigned int n, float t, Vec3 *o),
                                                                          (h, n, t, o))
DISPATCH(void,  billboard_corners, (Vec3 *d, float dx, float dy, float dz, float s),
                                                                          (d, dx, dy, dz, s))
DISPATCH(float, v3_angle_between, (const Vec3 *a, const Vec3 *b),         (a, b))
