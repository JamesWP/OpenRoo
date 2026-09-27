/* model.h -- the owner header for model.cpp's exports: the .fkt importer and
 * the model cache the theme loader and BuildSceneObjectList share. */
#pragma once

#include <windows.h>
#include "layout.h"
#include "linkedlist.h"
#include "faktmesh.h"

class GameLogger;


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

extern "C" __declspec(dllexport) CFaktMesh *__attribute__((thiscall))
ModelManager_FindOrImport(ModelManager *self, char *name);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
ModelManager_ClearReleaseFree(ModelManager *self);

/* Constructor, destructor body and scalar deleting destructor (the one
 * vtable slot).  Instances: g_modelManager, constructed by staticinit.cpp,
 * and the Scene's (scene.h). */
extern "C" __declspec(dllexport) ModelManager *__attribute__((thiscall))
ModelManager_Construct(ModelManager *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
ModelManager_Destruct(ModelManager *self);
extern "C" __declspec(dllexport) ModelManager *__attribute__((thiscall))
ModelManager_ScalarDestructor(ModelManager *self, unsigned char flags);
