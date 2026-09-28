/* SplinePath: a Bezier path through a list of control points, with a one-slot
 * vtable (the scalar deleting destructor).  One is embedded in the
 * ScriptPlayer (the flythrough camera) and one in every scene object (the
 * "usepath" animation). */

#pragma once

 
#include "linkedlist.h"
class RenderDevice;

/* One control point, 12 bytes. */
struct __attribute__((packed)) SplineControlPoint {
     
    float flX, flY, flZ;
private:
     
};

 
class __attribute__((packed)) SplinePath {
public:
     

    /* Construct, the scalar deleting destructor, and the destructor (which
     * purges the points). */
    SplinePath *construct();

    static SplinePath * 
    scalarDestructor(SplinePath *self, unsigned char bFreeSelf);

    void destruct();

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

    void       **vtable() const { return vtable_; }

    const LinkedList *controlPoints() const { return &controlPointList_; }

private:
    void       **vtable_;
    LinkedList   controlPointList_;  // +0x04  head +0x08, count +0x10
     
};

