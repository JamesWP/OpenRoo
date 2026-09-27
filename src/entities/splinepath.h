/* SplinePath: a Bezier path through a list of control points, with a one-slot
 * vtable (the scalar deleting destructor).  One is embedded in the
 * ScriptPlayer (the flythrough camera) and one in every scene object (the
 * "usepath" animation). */

#pragma once

#include "linkedlist.h"
class RenderDevice;

/* One control point, 12 bytes. */
struct SplineControlPoint {
    float flX, flY, flZ;
private:
};

struct SplinePath {

    void       **vtable;
    LinkedList   controlPointList;

private:
};

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
