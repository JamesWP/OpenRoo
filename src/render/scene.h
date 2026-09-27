/* scene.h -- the level's scene-object list: one static instance, built and
 * torn down by staticinit.cpp, filled per level by BuildSceneObjectList
 * (scene.cpp) from the level's .leo records. */
#pragma once

#include <windows.h>
#include "linkedlist.h"
#include "splinepath.h"
#include "model.h"
#include "scenetexture.h"
#include "ani.h"

class RenderDevice;
struct ParticleSystem;
struct GameLogger;
class ExtraObjects;

/* One placed object; dsoscene.cpp draws them. */
struct SceneObject {

    unsigned char  type;             /* ExtraObjectKind 0..2 */
    CFaktMesh     *mesh;             /* model */
    ParticleSystem *particle;        /* particle system */
    float          billboardRadius;  /* billboard */
    DWORD          animLoaded;       /* 1 when the .ani loaded */
    AnimTable      anim;             /* loaded from the .ani (ani.h) */
    float          pos[3];
    float          rot[3];
    SceneTexture  *texture;
    DWORD          srcBlend;
    DWORD          destBlend;
    DWORD          textureAddress;
    DWORD          onPath;           /* splineMode != 0 */
    DWORD          lit;              /* the .leo "lit" flag: draw as a
                                        framed (lit) model, not a plain mesh */
    unsigned char  splineMode;
    DWORD          splineTime;
    SplinePath     spline;

};

struct Scene {

    LinkedList      objects;    /* SceneObject * */
    ModelManager    models;
    TextureManager  textures;

};

/* FreeSceneObjects zeroes exactly 0x10 dwords. */

extern Scene g_scene;

Scene *Scene_Construct(Scene *self);
void Scene_Destruct(Scene *self);
void
Scene_BuildObjectList(RenderDevice *d3d, ExtraObjects *leo, GameLogger *logger);
int Scene_SegmentHitsModel(float px, float py, float pz,
                           float dx, float dy, float dz);
