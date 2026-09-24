/* scene.h -- the level's scene-object list (Ghidra: Scene, g_Scene at
 * 0x0046c458), ENDGAME_PLAN.md E4.
 *
 * One static instance, built by the CRT's static initialiser and torn down
 * by its atexit entry:
 *
 *   0x00420b50  thunk: call 0x420b60, jmp 0x420b70         (game's, live)
 *   0x00420b60  SceneStaticInit: mov ecx,g_Scene; jmp ctor  (game's, live)
 *   0x00420b70  atexit(0x420b80); 0x420b80 jmps to the dtor (game's, live)
 *   0x00420bf0  Scene::Constructor       ours: Scene_Construct
 *   0x00420b90  Scene::Destructor        ours: Scene_Destruct
 *   0x00420c50  BuildSceneObjectList     ours: Scene_BuildObjectList
 *   0x00420ee0  FreeSceneObjects         ours, internal
 *   0x00420f40  SceneObject dtor thunk   ours, internal (lea ecx,[ecx+0x1c6];
 *                                        jmp SplinePath::Destruct)
 *   0x00423240  any model hit by a segment    ours: Scene_SegmentHitsModel
 *   0x00422b90  segment vs one model's box    ours, internal
 *
 * The thunks stay the game's: they only load ECX and jump, and the two jumps
 * into the ctor and dtor are JMP_PATCHES.
 *
 * SceneObject lifetime is entirely ours: BuildSceneObjectList is the only
 * allocator of the 0x1da-byte block and FreeSceneObjects the only freer
 * (xref.py: operator new(0x1da) appears once; FreeSceneObjects has one
 * caller, BuildSceneObjectList).  So they come from our heap, not alloc.h.
 */
#pragma once

#include <windows.h>
#include "layout.h"
#include "linkedlist.h"
#include "splinepath.h"
#include "model.h"
#include "scenetexture.h"

struct Direct3D;
struct GameLogger;
class ExtraObjects;

/* One placed object.  Read field-by-field by dsoscene.cpp (O_* offsets),
 * which predates this struct.  The type byte at +0 misaligns everything. */
struct __attribute__((packed)) SceneObject {
    static const int ORIGIN = 0;

    unsigned char  type;             /* +0x000  ExtraObjectKind 0..2 */
    CFaktMesh     *mesh;             /* +0x001  model */
    void          *particle;         /* +0x005  particle system */
    float          billboardRadius;  /* +0x009  billboard */
    DWORD          animLoaded;       /* +0x00d  1 when the .ani loaded */
    unsigned char  anim[0x180];      /* +0x011  AnimTable (ani.h) */
    float          pos[3];           /* +0x191 */
    float          rot[3];           /* +0x19d */
    SceneTexture  *texture;          /* +0x1a9 */
    DWORD          srcBlend;         /* +0x1ad */
    DWORD          destBlend;        /* +0x1b1 */
    DWORD          textureAddress;   /* +0x1b5 */
    DWORD          onPath;           /* +0x1b9  splineMode != 0 */
    DWORD          lit;              /* +0x1bd */
    unsigned char  splineMode;       /* +0x1c1 */
    DWORD          splineTime;       /* +0x1c2 */
    SplinePath     spline;           /* +0x1c6 */

    KAROO_LAYOUT_REGISTER(SceneObject);
};

/* operator new(0x1da) in the original. */
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

static Scene *const GG_SCENE = (Scene *)0x0046c458;

/* The exports patch.py binds. */
extern "C" __declspec(dllexport) Scene *__attribute__((thiscall))
Scene_Construct(Scene *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Scene_Destruct(Scene *self);
extern "C" __declspec(dllexport) void __cdecl
Scene_BuildObjectList(Direct3D *d3d, ExtraObjects *leo, GameLogger *logger);
extern "C" __declspec(dllexport) int __cdecl
Scene_SegmentHitsModel(float px, float py, float pz,
                       float dx, float dy, float dz);
