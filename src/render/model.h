/* model.h -- the owner header for model.cpp's exports: the .fkt importer and
 * the model cache the theme loader and BuildSceneObjectList share. */
#pragma once

#include <windows.h>
#include "layout.h"
#include "linkedlist.h"
#include "faktmesh.h"

struct GameLogger;

/* Load `path` into `self`; the low byte of the result is the
 * success flag. */
int Model_ImportSceneModels(CFaktMesh *self, const char *path);

/* ─── ModelManager -- the name-keyed CFaktMesh cache ───────────────────────
 *
 * The twin of TextureManager (scenetexture.h): same shape, same in-place
 * lowercasing lookup, "MM:" instead of "TM:" in its log lines. */
class __attribute__((packed)) ModelManager {
public:
    static const int ORIGIN = 0;

    void        *vtable;    // +0x00
    LinkedList   cache;     // +0x04  CFaktMesh *, game-heap nodes
    GameLogger  *pLogger;   // +0x14  NULL = silent
private:
    KAROO_LAYOUT_REGISTER(ModelManager);
};

KAROO_LAYOUT_CHECKS(ModelManager)
{
    KAROO_LAYOUT_AT(cache,   0x04);
    KAROO_LAYOUT_AT(pLogger, 0x14);
    KAROO_LAYOUT_SIZE(0x18);
}

extern ModelManager g_modelManager;

CFaktMesh *ModelManager_FindOrImport(ModelManager *self, char *name);
void ModelManager_ClearReleaseFree(ModelManager *self);

/* Constructor, destructor body and scalar deleting destructor (the one
 * vtable slot).  Instances: g_modelManager, constructed by staticinit.cpp,
 * and the Scene's (scene.h). */
ModelManager *ModelManager_Construct(ModelManager *self);
void ModelManager_Destruct(ModelManager *self);
ModelManager *
ModelManager_ScalarDestructor(ModelManager *self, unsigned char flags);
