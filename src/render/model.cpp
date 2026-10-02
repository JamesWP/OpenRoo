/* ModelManager (model.h): the name-keyed CFaktMesh cache.  The .mdl reader
 * itself is CFaktMesh::importSceneModels (faktmesh.cpp). */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <new>
#include "model.h"
#include "logger.h"
#include <stdlib.h>
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

CFaktMesh *ModelManager::findOrImport(char *name)
{
    for (LinkedListNode *node = cache_.head(); node != NULL; ) {
        CFaktMesh *cached = (CFaktMesh *)node->value();
        node = node->next();
        mm_lower_inplace(name);
        mm_lower_inplace(cached->name());
        if (strcmp(cached->name(), name) == 0) {
            g_logger.logMessage(1, "MM: %s found", name);
            return cached;
        }
    }

    CFaktMesh *mesh = new (std::nothrow) CFaktMesh();
    if ((mesh->importSceneModels(name) & 0xff) == 0) {
        if (mesh != NULL)
            delete mesh;
        g_logger.logMessage(3, "MM: *ERROR* failed loading %s", name);
        return NULL;
    }
    g_logger.logMessage(1, "MM: %s loaded", name);
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
            delete mesh;
        }
    }
    cache_.clear();
}

/* ─── ModelManager lifecycle ───────────────────────────────────────────────
 *
 * Every instance is static (model.h), so nothing deletes one and the scalar
 * dtor's free is never reached. */
ModelManager::ModelManager()
{
}

ModelManager::~ModelManager()
{
}
