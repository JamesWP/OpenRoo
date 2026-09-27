/* scene.h -- the level's scene-object list: one static instance, built and
 * torn down by staticinit.cpp, filled per level by BuildSceneObjectList
 * (scene.cpp) from the level's .leo records. */
#pragma once

#include <windows.h>
#include "layout.h"
#include "linkedlist.h"
#include "splinepath.h"
#include "model.h"
#include "scenetexture.h"
#include "ani.h"

struct Direct3D;
struct ParticleSystem;
struct GameLogger;
class ExtraObjects;

/* One placed object; dsoscene.cpp draws them.  The type byte at +0
 * misaligns everything, hence packed. */
struct __attribute__((packed)) SceneObject {
    static const int ORIGIN = 0;

    unsigned char  type;             /* +0x000  ExtraObjectKind 0..2 */
    CFaktMesh     *mesh;             /* +0x001  model */
    ParticleSystem *particle;        /* +0x005  particle system */
    float          billboardRadius;  /* +0x009  billboard */
    DWORD          animLoaded;       /* +0x00d  1 when the .ani loaded */
    AnimTable      anim;             /* +0x011  loaded from the .ani (ani.h) */
    float          pos[3];           /* +0x191 */
    float          rot[3];           /* +0x19d */
    SceneTexture  *texture;          /* +0x1a9 */
    DWORD          srcBlend;         /* +0x1ad */
    DWORD          destBlend;        /* +0x1b1 */
    DWORD          textureAddress;   /* +0x1b5 */
    DWORD          onPath;           /* +0x1b9  splineMode != 0 */
    DWORD          lit;              /* +0x1bd  the .leo "lit" flag: draw as a
                                                framed (lit) model, not a plain mesh */
    unsigned char  splineMode;       /* +0x1c1 */
    DWORD          splineTime;       /* +0x1c2 */
    SplinePath     spline;           /* +0x1c6 */

    KAROO_LAYOUT_REGISTER(SceneObject);
};

/* The object is allocated at exactly 0x1da bytes. */
KAROO_LAYOUT_CHECKS(SceneObject)
{
    KAROO_LAYOUT_AT(mesh,            0x001);
    KAROO_LAYOUT_AT(particle,        0x005);
    KAROO_LAYOUT_AT(billboardRadius, 0x009);
    KAROO_LAYOUT_AT(animLoaded,      0x00d);
    KAROO_LAYOUT_AT(anim,            0x011);
    KAROO_LAYOUT_AT(pos,             0x191);
    KAROO_LAYOUT_AT(rot,             0x19d);
    KAROO_LAYOUT_AT(texture,         0x1a9);
    KAROO_LAYOUT_AT(srcBlend,        0x1ad);
    KAROO_LAYOUT_AT(destBlend,       0x1b1);
    KAROO_LAYOUT_AT(textureAddress,  0x1b5);
    KAROO_LAYOUT_AT(onPath,          0x1b9);
    KAROO_LAYOUT_AT(lit,             0x1bd);
    KAROO_LAYOUT_AT(splineMode,      0x1c1);
    KAROO_LAYOUT_AT(splineTime,      0x1c2);
    KAROO_LAYOUT_AT(spline,          0x1c6);
    KAROO_LAYOUT_SIZE(0x1da);
}

struct __attribute__((packed)) Scene {
    static const int ORIGIN = 0;

    LinkedList      objects;    /* +0x00  SceneObject * */
    ModelManager    models;     /* +0x10 */
    TextureManager  textures;   /* +0x28 */

    KAROO_LAYOUT_REGISTER(Scene);
};

/* FreeSceneObjects zeroes exactly 0x10 dwords. */
KAROO_LAYOUT_CHECKS(Scene)
{
    KAROO_LAYOUT_AT(models,   0x10);
    KAROO_LAYOUT_AT(textures, 0x28);
    KAROO_LAYOUT_SIZE(0x40);
}

extern Scene g_scene;

extern "C" __declspec(dllexport) Scene *__attribute__((thiscall))
Scene_Construct(Scene *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Scene_Destruct(Scene *self);
extern "C" __declspec(dllexport) void __cdecl
Scene_BuildObjectList(Direct3D *d3d, ExtraObjects *leo, GameLogger *logger);
extern "C" __declspec(dllexport) int __cdecl
Scene_SegmentHitsModel(float px, float py, float pz,
                       float dx, float dy, float dz);
