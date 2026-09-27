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
class GameLogger;
class ExtraObjects;

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


class Scene {
public:
     

    Scene *construct();
    void destruct();
    void buildObjectList(RenderDevice *d3d, ExtraObjects *leo,
                         GameLogger *logger);

    int  segmentHitsModel(float px, float py, float pz, float dx,
                          float dy, float dz);

    const LinkedList *objects() const { return &objects_; }
    ModelManager     *models()        { return &models_; }
    TextureManager   *textures()      { return &textures_; }

private:
    void freeSceneObjects();

    LinkedList      objects_;    /* +0x00  SceneObject * */
    ModelManager    models_;     /* +0x10 */
    TextureManager  textures_;   /* +0x28 */
     
};

extern Scene g_scene;

