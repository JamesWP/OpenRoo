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

/* One placed object; dsoscene.cpp draws them.  The type byte at +0
 * misaligns everything, hence packed. */
struct __attribute__((packed)) SceneObject {
     

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

     
};

/* The object is allocated at exactly 0x1da bytes. */
 

class __attribute__((packed)) Scene {
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

