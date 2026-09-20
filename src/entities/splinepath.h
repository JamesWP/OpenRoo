/* SplinePath -- the game's Bezier control-point path (0x004022a0..0x00402700).
 *
 * A vtable pointer and a LinkedList of 12-byte control points.  One is
 * embedded in ScriptPlayer at +0x93d (the flythrough camera) and one in every
 * scene object at +0x1c6 (the "usepath" animation); both those owners still
 * construct and destroy it, so the ctor and dtor are reached from game code.
 *
 *   SplinePathCtor       0x4022a0  ctor: List_Init + vtable      RET 0
 *   SplinePathDtor       0x4022c0  vtable slot 0, scalar dtor    RET 4
 *   Destruct             0x4022e0  purge + List_Destruct         RET 0
 *   EvalBezierPath       0x402330  (out, t) -> out               RET 8
 *   AddControlPoint      0x4024b0  (x, y, z)                     RET 0xc
 *   PurgeControlPoints   0x4024e0  free the points, clear list   RET 0
 *   DrawSplinePath       0x402510  (dev, segments, colour)       RET 0xc
 *   DrawControlPolygon   0x402640  (dev, colour)                 RET 8
 *
 * THE VTABLE IS OURS, on the LinkedList precedent: the ctor and Destruct
 * install a one-slot table defined in splinepath.cpp rather than writing the
 * game's 0x0045d294 back.  Slot 0 is the scalar deleting destructor and that
 * is ours, so the table needs nothing from the game.  The game's table keeps
 * pointing at the UD2-stubbed 0x004022c0 and is a tripwire.
 *
 * The control points stay on the GAME heap (alloc.h).  Both sides are ours
 * now, but ENDGAME_PLAN is explicit that the switch to new/delete happens in
 * the cycle that proves the other side rather than ahead of it.
 */
#pragma once

#include "layout.h"
#include "linkedlist.h"

struct IDirect3DDevice3;

/* One control point: the 12 bytes AddControlPoint asks the game's operator
 * new for. */
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

    void       **vtable;             // +0x00
    LinkedList   controlPointList;   // +0x04  head +0x08, count +0x10

private:
    KAROO_LAYOUT_REGISTER(SplinePath);
};

/* 20 is what both embedders tile around: ScriptPlayer+0x93d..0x951 is exactly
 * 20 bytes, and dsoscene.cpp reads the head at +0x1c6+8 and the count at
 * +0x1c6+0x10. */
KAROO_LAYOUT_CHECKS(SplinePath)
{
    KAROO_LAYOUT_AT(vtable,           0x00);
    KAROO_LAYOUT_AT(controlPointList, 0x04);
    KAROO_LAYOUT_SIZE(20);
}

/* The exports patch.py binds. */
extern "C" __declspec(dllexport) SplinePath *__attribute__((thiscall))
Spline_Construct(SplinePath *self);

extern "C" __declspec(dllexport) SplinePath *__attribute__((thiscall))
Spline_ScalarDestructor(SplinePath *self, unsigned char bFreeSelf);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Spline_Destruct(SplinePath *self);

extern "C" __declspec(dllexport) float *__attribute__((thiscall))
Spline_EvalBezierPath(SplinePath *self, float *out, float t);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Spline_AddControlPoint(SplinePath *self, float x, float y, float z);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Spline_PurgeControlPoints(SplinePath *self);

extern "C" __declspec(dllexport) long __attribute__((thiscall))
Spline_DrawSplinePath(SplinePath *self, IDirect3DDevice3 *dev,
                      unsigned int numsegments, unsigned long color);

extern "C" __declspec(dllexport) long __attribute__((thiscall))
Spline_DrawControlPolygon(SplinePath *self, IDirect3DDevice3 *dev,
                          unsigned long color);
