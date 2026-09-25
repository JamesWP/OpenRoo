/* sceneobjects -- Game::RenderSceneObjects 0x4095f0 (the levelplacements
 * Game TU), the per-object draw dispatcher every theme object type goes
 * through: models (kind 1), quads (kind 2) and billboards (kind 3).
 *
 * __cdecl, twelve dwords (the callers ADD ESP,0x30):
 *   game, quad (four FVF 0x242 vertices the caller owns; kind 2 animates
 *   and draws them), positions[count], rotations[count], count, the theme
 *   slot, the Direct3D, now (a double), the animation time (a float), the
 *   animation code, and the frame's elapsed ms (unsigned).
 * Callers: RenderGameFrame's object passes, the lift and slide wrappers
 * 0x408870 / 0x408920. */
#pragma once
#include <windows.h>
#include "d3dmath.h"

class Game;
class ThemeObjectTypeSlot;
struct Direct3D;

/* The kind-2 quad vertex: FVF 0x242, XYZ | DIFFUSE | TEX2.  The game
 * animates and draws the SECOND coordinate pair as set 0. */
struct SceneQuadVertex {                  /* naturally aligned; no packing needed */
    float x, y, z;
    DWORD diffuse;
    float u0, v0;
    float u1, v1;
};
static_assert(sizeof(SceneQuadVertex) == 0x20, "SceneQuadVertex stride");

extern "C" __declspec(dllexport) void __cdecl
Scene_RenderSceneObjects(Game *game, SceneQuadVertex *quad, const Vec3 *positions,
                         const Vec3 *rotations, unsigned int count,
                         ThemeObjectTypeSlot *slot, Direct3D *d3d, double now,
                         float animTime, unsigned int animCode, unsigned int dtMs);
