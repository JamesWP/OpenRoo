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

    CFaktMesh *findOrImport(char *name);
    void clearReleaseFree();

    /* Constructor, destructor body and scalar deleting destructor (the one
     * vtable slot).  Instances: g_modelManager, constructed by staticinit.cpp,
     * and the Scene's (scene.h). */
    ModelManager *construct();

    void destruct();
    static ModelManager *__attribute__((thiscall))
    scalarDestructor(ModelManager *self, unsigned char flags);

    void        *vtable() const { return vtable_; }
    GameLogger  *logger() const          { return pLogger_; }
    void         setLogger(GameLogger *l) { pLogger_ = l; }

private:
    void        *vtable_;    // +0x00
    LinkedList   cache_;     // +0x04  CFaktMesh *, game-heap nodes
    GameLogger  *pLogger_;   // +0x14  NULL = silent
    KAROO_LAYOUT_REGISTER(ModelManager);
};

KAROO_LAYOUT_CHECKS(ModelManager)
{
    KAROO_LAYOUT_AT(cache_,   0x04);
    KAROO_LAYOUT_AT(pLogger_, 0x14);
    KAROO_LAYOUT_SIZE(0x18);
}

extern ModelManager g_modelManager;

