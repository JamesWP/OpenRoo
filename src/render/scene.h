/* scene.h -- the level's scene-object list: one static instance, built and
 * torn down by staticinit.cpp, filled per level by BuildSceneObjectList
 * (scene.cpp) from the level's .leo records. */
#pragma once

#include <stdint.h>
 
#include <vector>
#include "splinepath.h"
#include "model.h"
#include "texture.h"
#include "ani.h"

class RenderDevice;
struct ParticleSystem;
class ExtraObjects;

struct SceneObject {
    unsigned char  type;             /* ExtraObjectKind 0..2 */
    AnimatedMesh     *mesh;             /* model */
    ParticleSystem *particle;        /* particle system */
    float          billboardRadius;  /* billboard */
    uint32_t          animLoaded;       /* 1 when the .ani loaded */
    AnimTable      anim;             /* loaded from the .ani (ani.h) */
    float          pos[3];
    float          rot[3];
    Texture  *texture;
    uint32_t          srcBlend;
    uint32_t          destBlend;
    uint32_t          textureAddress;
    uint32_t          onPath;           /* splineMode != 0 */
    uint32_t          lit;              /* the .leo "lit" flag: draw as a
                                        framed (lit) model, not a plain mesh */
    unsigned char  splineMode;
    uint32_t          splineTime;
    SplinePath     spline;

    // Only the spline builds itself; the other fields are left as allocated.
    SceneObject() = default;
};


class Scene {
public:
     

    Scene();
    ~Scene();
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;
    void buildObjectList(RenderDevice *d3d, ExtraObjects *leo);

    int  segmentHitsModel(float px, float py, float pz, float dx,
                          float dy, float dz);

    const std::vector<SceneObject *> &objects() const { return objects_; }
    ModelManager     *models()        { return &models_; }
    TextureManager   *textures()      { return &textures_; }

private:
    void freeSceneObjects();

    std::vector<SceneObject *> objects_;  // owned
    ModelManager    models_;
    TextureManager  textures_;
     
};

extern Scene g_scene;

