/* ModelManager (model.h): the name-keyed CFaktMesh cache.  The .mdl reader
 * itself is CFaktMesh::importSceneModels (faktmesh.cpp). */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "model.h"
#include "log.h"
#include <stdlib.h>
#include "gamelog.h"
#include "gamestr.h"
ModelManager g_modelManager;

/* ─── ModelManager ──────────────────────────────────────────────────────────
 *
 * The lookup lowercases the caller's name and every cached name in place
 * before comparing, as TextureManager::getOrLoad does; the logged name is
 * therefore the lowercased one. */
static void mm_lower_inplace(char *s)
{
    for (; *s; s++)
        if (*s > '@' && *s < '[')
            *s += ' ';
}

typedef void *(__attribute__((thiscall)) *mm_scalar_dtor_fn)(void *self, unsigned int flags);

static void mm_delete(CFaktMesh *m)
{
    mm_scalar_dtor_fn dtor = *(mm_scalar_dtor_fn *)m->vtable();
    dtor(m, 1);
}

CFaktMesh *ModelManager::findOrImport(char *name)
{
    for (LinkedListNode *node = cache_.head(); node != NULL; ) {
        CFaktMesh *cached = (CFaktMesh *)node->value();
        node = node->next();
        mm_lower_inplace(name);
        mm_lower_inplace(cached->name());
        if (strcmp(cached->name(), name) == 0) {
            if (pLogger_ != NULL)
                pLogger_->logMessage(1, GS_MM_FOUND, name);
            return cached;
        }
    }

    void *mem = malloc(sizeof(CFaktMesh));
    CFaktMesh *mesh = (mem != NULL) ? ((CFaktMesh *)mem)->init() : NULL;
    if ((mesh->importSceneModels(name) & 0xff) == 0) {
        if (mesh != NULL)
            mm_delete(mesh);
        if (pLogger_ != NULL)
            pLogger_->logMessage(3, GS_MM_FAILED, name);
        return NULL;
    }
    if (pLogger_ != NULL)
        pLogger_->logMessage(1, GS_MM_LOADED, name);
    cache_.append(mesh);
    return mesh;
}

void ModelManager::clearReleaseFree()
{
    for (LinkedListNode *node = cache_.head(); node != NULL; ) {
        CFaktMesh *mesh = (CFaktMesh *)node->value();
        node = node->next();
        if (mesh != NULL) {
            mesh->releaseModelBuffers();
            mm_delete(mesh);
        }
    }
    cache_.clear();
}

/* ─── ModelManager lifecycle ───────────────────────────────────────────────
 *
 * Every instance is static (model.h), so nothing deletes one and the scalar
 * dtor's free is never reached. */
static void *const g_ModelManagerVtable[1] = { (void *)&ModelManager::scalarDestructor };

ModelManager *ModelManager::construct()
{
    cache_.init();
    vtable_  = (void *)g_ModelManagerVtable;
    pLogger_ = NULL;
    return this;
}

void ModelManager::destruct()
{
    vtable_ = (void *)g_ModelManagerVtable;
    cache_.destruct();
}

ModelManager * __attribute__((thiscall))
ModelManager::scalarDestructor(ModelManager *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}
