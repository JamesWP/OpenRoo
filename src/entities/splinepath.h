/* SplinePath: a Bezier path through a list of control points.  It has a
 * virtual destructor (a one-slot vtable, as in the original layout), which
 * purges the points.  One is embedded in the
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

 
class SplinePath {
public:
     

    SplinePath();
    virtual ~SplinePath();
    SplinePath(const SplinePath &) = delete;
    SplinePath &operator=(const SplinePath &) = delete;

    /* The point at t along the path, written to out; returns out. */
    float *evalBezierPath(float *out, float t);

    /* Appends a control point. */
    void addControlPoint(float x, float y, float z);

    /* Frees every control point and empties the list. */
    void purgeControlPoints();

    /* Draws the path as numsegments line segments, and its control polygon. */
    long drawSplinePath(RenderDevice *dev, unsigned int numsegments,
                        unsigned long color);

    long drawControlPolygon(RenderDevice *dev, unsigned long color);

    const LinkedList *controlPoints() const { return &controlPointList_; }

private:
    LinkedList   controlPointList_;  // +0x04  head +0x08, count +0x10
     
};