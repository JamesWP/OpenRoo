/* The Vec3 and Mat4 types: the vocabulary rendertypes.h and renderdevice.h
 * speak in.  They live here so that renderapi depends on nothing; math's
 * vecmath.h includes this and adds the helpers over them. */
#pragma once

struct Mat4 { float m[16]; };   /* row-major, row vectors: v * M */
struct Vec3 { float x, y, z; };
