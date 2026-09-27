/* SplinePath: a Bezier path through a list of control points, with a one-slot
 * vtable (the scalar deleting destructor).  One is embedded in the
 * ScriptPlayer (the flythrough camera) and one in every scene object (the
 * "usepath" animation). */

#pragma once

#include "layout.h"
#include "linkedlist.h"
class RenderDevice;


/* One control point, 12 bytes. */
struct __attribute__((packed)) SplineControlPoint {
    static const int ORIGIN = 0;
    float flX, flY, flZ;
private:
    KAROO_LAYOUT_REGISTER(SplineControlPoint);
};

KAROO_LAYOUT_CHECKS(SplineControlPoint)
{
    KAROO_LAYOUT_AT(flX, 0x00);
    KAROO_LAYOUT_AT(flY, 0x04);
    KAROO_LAYOUT_AT(flZ, 0x08);
    KAROO_LAYOUT_SIZE(12);
}

struct __attribute__((packed)) SplinePath {
    static const int ORIGIN = 0;

    void       **vtable;
    LinkedList   controlPointList;  // +0x04  head +0x08, count +0x10

private:
    KAROO_LAYOUT_REGISTER(SplinePath);
};

/* 20 bytes, which both embedders tile around; dsoscene.cpp reads the head at
 * +0x1c6+8 and the count at +0x1c6+0x10. */
KAROO_LAYOUT_CHECKS(SplinePath)
{
    KAROO_LAYOUT_AT(vtable,           0x00);
    KAROO_LAYOUT_AT(controlPointList, 0x04);
    KAROO_LAYOUT_SIZE(20);
}

/* Construct, the scalar deleting destructor, and the destructor (which purges
 * the points). */
SplinePath *Spline_Construct(SplinePath *self);

SplinePath *Spline_ScalarDestructor(SplinePath *self, unsigned char bFreeSelf);

void Spline_Destruct(SplinePath *self);

/* The point at t along the path, written to out; returns out. */
float *Spline_EvalBezierPath(SplinePath *self, float *out, float t);

/* Appends a control point. */
void Spline_AddControlPoint(SplinePath *self, float x, float y, float z);

/* Frees every control point and empties the list. */
void Spline_PurgeControlPoints(SplinePath *self);

/* Draws the path as numsegments line segments, and its control polygon. */
long Spline_DrawSplinePath(SplinePath *self, RenderDevice *dev,
                           unsigned int numsegments, unsigned long color);

long Spline_DrawControlPolygon(SplinePath *self, RenderDevice *dev,
                               unsigned long color);
