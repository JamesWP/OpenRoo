/* d3dmath_std.cpp -- the maths behind d3dmath_mode.cpp's entry points:
 * plain float, the C library, no assembly. */
#pragma once

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
