/* Runtime selection between the two math backends.
 *
 *   KAROO_MATH=exact     (default) bit-exact reproduction of the originals
 *   KAROO_MATH=standard  plain float / C library, readable, not bit-exact
 *
 * The public names below dispatch; callers never pick a backend themselves.
 * The mode is read once and logged, because "which maths was that run using?"
 * is the first question to ask of any surprising capture, and the answer must
 * be in the log rather than in someone's memory of the shell they typed.
 *
 * The branch costs nothing that matters: these are called a handful of times
 * per object per frame, not per vertex.
 */
#include "d3dmath.h"
#include "log.h"

void  m4_mul_exact(Mat4 *, const Mat4 *, const Mat4 *);
void  m4_rot_x_exact(Mat4 *, float);
void  m4_rot_y_exact(Mat4 *, float);
void  m4_rot_z_exact(Mat4 *, float);
void  m4_rot_x_biased_exact(Mat4 *, float, float);
float v3_len_sq_exact(const Vec3 *);
void  bezier_eval_exact(const ListNodeM *, unsigned int, float, Vec3 *);
void  billboard_corners_exact(Vec3 *, float, float, float, float);

void  m4_mul_std(Mat4 *, const Mat4 *, const Mat4 *);
void  m4_rot_x_std(Mat4 *, float);
void  m4_rot_y_std(Mat4 *, float);
void  m4_rot_z_std(Mat4 *, float);
void  m4_rot_x_biased_std(Mat4 *, float, float);
float v3_len_sq_std(const Vec3 *);
void  bezier_eval_std(const ListNodeM *, unsigned int, float, Vec3 *);
void  billboard_corners_std(Vec3 *, float, float, float, float);

int math_standard_mode(void)
{
    static int cached = -1;
    if (cached < 0) {
        char b[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_MATH", b, sizeof b) &&
            lstrcmpiA(b, "standard") == 0)
            cached = 1;
        log_write("d3dmath: mode = %s\n", cached ? "standard (not bit-exact)"
                                                 : "exact");
    }
    return cached;
}

#define DISPATCH(rt, name, params, args)                 \
    rt name params { return math_standard_mode()         \
        ? name##_std args : name##_exact args; }

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
