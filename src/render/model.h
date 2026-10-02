/* model.h -- the owner header for model.cpp's exports: the .fkt importer and
 * the model cache the theme loader and BuildSceneObjectList share. */
#pragma once

 
#include "linkedlist.h"
#include "faktmesh.h"


/* ─── ModelManager -- the name-keyed CFaktMesh cache ───────────────────────
 *
 * The twin of TextureManager (texture.h): same shape, same in-place
 * lowercasing lookup, "MM:" instead of "TM:" in its log lines. */
class ModelManager {
public:
     

    CFaktMesh *findOrImport(char *name);
    void clearReleaseFree();

    /* Instances: g_modelManager and the Scene's (scene.h).  The destructor
     * empties the cache's nodes, not the meshes. */
    ModelManager();
    virtual ~ModelManager();
    ModelManager(const ModelManager &) = delete;
    ModelManager &operator=(const ModelManager &) = delete;

private:
    LinkedList   cache_;     // +0x04  CFaktMesh *, game-heap nodes
     
};

 
extern ModelManager g_modelManager;

