/* SplinePath: a Bezier path through a list of control points.  It has a
 * virtual destructor (a one-slot vtable, as in the original layout), which
 * purges the points.  One is embedded in the
 * ScriptPlayer (the flythrough camera) and one in every scene object (the
 * "usepath" animation). */

#pragma once

 
#include <vector>
class RenderDevice;

/* One control point: three packed floats. */
struct SplineControlPoint {
     
    float flX, flY, flZ;
};
static_assert(sizeof(SplineControlPoint) == 3 * sizeof(float),
              "Bezier evaluation reads the points as packed floats");

 
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

    const std::vector<SplineControlPoint> &controlPoints() const { return points_; }

private:
    std::vector<SplineControlPoint> points_;
     
};