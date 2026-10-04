/* model.h -- the owner header for model.cpp's exports: the .fkt importer and
 * the model cache the theme loader and BuildSceneObjectList share. */
#pragma once

 
#include <vector>
#include "animatedmesh.h"


/* ─── ModelManager -- the name-keyed AnimatedMesh cache ───────────────────────
 *
 * The twin of TextureManager (texture.h): same shape, same in-place
 * lowercasing lookup, "MM:" instead of "TM:" in its log lines. */
class ModelManager {
public:
     

    AnimatedMesh *findOrImport(char *name);
    void clearReleaseFree();

    /* Instances: g_modelManager and the Scene's (scene.h).  The destructor
     * empties the cache's nodes, not the meshes. */
    ModelManager();
    virtual ~ModelManager();
    ModelManager(const ModelManager &) = delete;
    ModelManager &operator=(const ModelManager &) = delete;

private:
    std::vector<AnimatedMesh *> cache_;
     
};

 
extern ModelManager g_modelManager;

