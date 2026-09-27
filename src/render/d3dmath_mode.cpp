/* Public entry points for the maths layer, over d3dmath_std.cpp.
 *
 * The scalar helpers (m_sqrt / m_fmod / m_acos) are separate from the vector
 * and matrix routines because DrawSceneObjects uses them directly for the path
 * parameter, the animation frame and the tangent-derived heading.
 */
#include <math.h>
#include "d3dmath_mode.h"
#include "d3dmath_std.h"


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
