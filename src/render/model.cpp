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
 * before comparing, as TextureManager_GetOrLoad does; the logged name is
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

extern "C" __declspec(dllexport) CFaktMesh *__attribute__((thiscall))
ModelManager_FindOrImport(ModelManager *self, char *name)
{
    for (LinkedListNode *node = self->cache.head(); node != NULL; ) {
        CFaktMesh *cached = (CFaktMesh *)node->value();
        node = node->next();
        mm_lower_inplace(name);
        mm_lower_inplace(cached->name());
        if (strcmp(cached->name(), name) == 0) {
            if (self->pLogger != NULL)
                self->pLogger->logMessage(1, GS_MM_FOUND, name);
            return cached;
        }
    }

    void *mem = malloc(sizeof(CFaktMesh));
    CFaktMesh *mesh = (mem != NULL) ? ((CFaktMesh *)mem)->init() : NULL;
    if ((mesh->importSceneModels(name) & 0xff) == 0) {
        if (mesh != NULL)
            mm_delete(mesh);
        if (self->pLogger != NULL)
            self->pLogger->logMessage(3, GS_MM_FAILED, name);
        return NULL;
    }
    if (self->pLogger != NULL)
        self->pLogger->logMessage(1, GS_MM_LOADED, name);
    self->cache.append(mesh);
    return mesh;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
ModelManager_ClearReleaseFree(ModelManager *self)
{
    for (LinkedListNode *node = self->cache.head(); node != NULL; ) {
        CFaktMesh *mesh = (CFaktMesh *)node->value();
        node = node->next();
        if (mesh != NULL) {
            mesh->releaseModelBuffers();
            mm_delete(mesh);
        }
    }
    self->cache.clear();
}

/* ─── ModelManager lifecycle ───────────────────────────────────────────────
 *
 * Every instance is static (model.h), so nothing deletes one and the scalar
 * dtor's free is never reached. */
static void *const g_ModelManagerVtable[1] = { (void *)&ModelManager_ScalarDestructor };

extern "C" __declspec(dllexport) ModelManager *__attribute__((thiscall))
ModelManager_Construct(ModelManager *self)
{
    self->cache.init();
    self->vtable  = (void *)g_ModelManagerVtable;
    self->pLogger = NULL;
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
ModelManager_Destruct(ModelManager *self)
{
    self->vtable = (void *)g_ModelManagerVtable;
    self->cache.destruct();
}

extern "C" __declspec(dllexport) ModelManager *__attribute__((thiscall))
ModelManager_ScalarDestructor(ModelManager *self, unsigned char flags)
{
    ModelManager_Destruct(self);
    if (flags & 1)
        free(self);
    return self;
}
