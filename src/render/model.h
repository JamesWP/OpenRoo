/* model.h -- the owner header for model.cpp's exports: the .fkt importer and
 * the model cache the theme loader and BuildSceneObjectList share. */
#pragma once

#include <windows.h>
 
#include "linkedlist.h"
#include "faktmesh.h"

class GameLogger;

/* ─── ModelManager -- the name-keyed CFaktMesh cache ───────────────────────
 *
 * The twin of TextureManager (scenetexture.h): same shape, same in-place
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

    GameLogger  *logger() const          { return pLogger_; }
    void         setLogger(GameLogger *l) { pLogger_ = l; }

private:
    LinkedList   cache_;     // +0x04  CFaktMesh *, game-heap nodes
    GameLogger  *pLogger_;   // +0x14  NULL = silent
     
};

 
extern ModelManager g_modelManager;

