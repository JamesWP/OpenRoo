/* model.h -- the owner header for model.cpp's exports: the .fkt importer and
 * the model cache the theme loader and BuildSceneObjectList share. */
#pragma once

#include <windows.h>
#include "layout.h"
#include "linkedlist.h"
#include "faktmesh.h"

struct GameLogger;

/* 0x00437bc0 -- load `path` into `self`; the low byte of the result is the
 * success flag. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Model_ImportSceneModels(CFaktMesh *self, const char *path);

/* ─── ModelManager -- the name-keyed CFaktMesh cache ───────────────────────
 *
 * One instance at 0x004e03f0, game-constructed (Ghidra: AutoClass4).  The
 * twin of TextureManager (scenetexture.h): same shape, same in-place
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

static ModelManager *const GG_MODEL_MANAGER = (ModelManager *)0x004e03f0;

/* 0x004385b0 / 0x004386e0. */
extern "C" __declspec(dllexport) CFaktMesh *__attribute__((thiscall))
ModelManager_FindOrImport(ModelManager *self, char *name);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
ModelManager_ClearReleaseFree(ModelManager *self);
