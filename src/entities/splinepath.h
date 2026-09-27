/* SplinePath: a Bezier path through a list of control points, with a one-slot
 * vtable (the scalar deleting destructor).  One is embedded in the
 * ScriptPlayer (the flythrough camera) and one in every scene object (the
 * "usepath" animation). */

#pragma once

#include "layout.h"
#include "linkedlist.h"
class RenderDevice;

struct IDirect3DDevice3;

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
extern "C" __declspec(dllexport) SplinePath *__attribute__((thiscall))
Spline_Construct(SplinePath *self);

extern "C" __declspec(dllexport) SplinePath *__attribute__((thiscall))
Spline_ScalarDestructor(SplinePath *self, unsigned char bFreeSelf);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Spline_Destruct(SplinePath *self);

/* The point at t along the path, written to out; returns out. */
extern "C" __declspec(dllexport) float *__attribute__((thiscall))
Spline_EvalBezierPath(SplinePath *self, float *out, float t);

/* Appends a control point. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Spline_AddControlPoint(SplinePath *self, float x, float y, float z);

/* Frees every control point and empties the list. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Spline_PurgeControlPoints(SplinePath *self);

/* Draws the path as numsegments line segments, and its control polygon. */
extern "C" __declspec(dllexport) long __attribute__((thiscall))
Spline_DrawSplinePath(SplinePath *self, RenderDevice *dev,
                      unsigned int numsegments, unsigned long color);

extern "C" __declspec(dllexport) long __attribute__((thiscall))
Spline_DrawControlPolygon(SplinePath *self, RenderDevice *dev,
                          unsigned long color);
