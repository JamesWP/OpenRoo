/* sceneobjects -- the per-object draw dispatcher every theme object type goes
 * through: models (kind 1), quads (kind 2) and billboards (kind 3).
 *
 *  , twelve dwords (the callers ADD ESP,0x30):
 *   game, quad (four FVF 0x242 vertices the caller owns; kind 2 animates
 *   and draws them), positions[count], rotations[count], count, the theme
 *   slot, the Direct3D, now (a double), the animation time (a float), the
 *   animation code, and the frame's elapsed ms (unsigned).
 * Callers: RenderGameFrame's object passes, the lift and slide wrappers. */
#pragma once
#include <stdint.h>
#include "d3dmath.h"

class Game;
class ThemeObjectTypeSlot;
class RenderDevice;

/* The kind-2 quad vertex: position, diffuse and two UV pairs.  The game
 * animates the SECOND pair and draws it (strided, scenequad.cpp) as the only
 * set; the first is never sampled. */
struct SceneQuadVertex {                  /* naturally aligned; no packing needed */
    float x, y, z;
    uint32_t diffuse;
    float u0, v0;
    float u1, v1;
};

  void  
Scene_RenderSceneObjects(Game *game, SceneQuadVertex *quad, const Vec3 *positions,
                         const Vec3 *rotations, unsigned int count,
                         ThemeObjectTypeSlot *slot, RenderDevice *d3d, double now,
                         float animTime, unsigned int animCode, unsigned int dtMs);
