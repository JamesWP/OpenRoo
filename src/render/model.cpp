/* The .mdl reader (ImportSceneModels) and ModelManager (model.h).
 *
 * FORMAT: the .mdl file.
 *   +0x00  WORD   frameCount     -> this->wFrameCount
 *   +0x02  DWORD  vertexCount    -> this->dwVertexCount
 *   then, for each frame f:
 *          6 x DWORD             -> pFrameRecords + f*0x18
 *          for each vertex v:
 *              10 x DWORD        -> pVertexData + (f*vertexCount + v)*0x28
 * Opened "rb".  Every read is a separate 4-byte fread; the header's two are 2
 * and 4 bytes.
 *
 * KAROO_MDL_FX=scale halves every vertex position as it is read, so every
 * model comes out at half size. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "model.h"
#include "log.h"
#include <stdlib.h>
#include "gamelog.h"
#include "gamestr.h"
ModelManager g_modelManager;

#define MDL_FRAME_REC_SIZE 0x18
#define MDL_VERTEX_SIZE    0x28
#define MDL_LOG_FIRST      8

static bool fx_scale(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_MDL_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "scale") == 0);
        log_write("model: FX mode = %s\n", cached ? "scale" : "off");
    }
    return cached != 0;
}

static unsigned long fnv1a(const void *p, unsigned len)
{
    const unsigned char *b = (const unsigned char *)p;
    unsigned long h = 2166136261UL;
    if (b == NULL) return 0;
    for (unsigned i = 0; i < len; i++) { h ^= b[i]; h *= 16777619UL; }
    return h;
}

static void model_release(CFaktMesh *m)
{
    FaktMesh_ReleaseModelBuffers(m);
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Model_ImportSceneModels(CFaktMesh *self, const char *path)
{
    FILE *fp;
    unsigned frames, verts, total;
    static int logged = 0;

    // PRESERVED: the release runs before the open, so a failed open leaves the
    // mesh cleared, not unchanged.
    model_release(self);

    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    // PRESERVED: no read is checked, and neither is the file's size: a
    // truncated .mdl leaves the rest of the vertices zero and still returns 1.
    // A NULL allocation is stored and then read into.
    fread(&self->wFrameCount,   2, 1, fp);
    fread(&self->dwVertexCount, 4, 1, fp);

    frames = self->wFrameCount;
    verts  = self->dwVertexCount;
    total  = frames * verts;

    // PRESERVED: the frame records are not zero-filled, unlike the two vertex
    // buffers, so a record missing from the file reads as heap garbage.
    self->pFrameRecords = malloc(frames * MDL_FRAME_REC_SIZE);

    self->pVertexData = malloc(total * MDL_VERTEX_SIZE);
    if (self->pVertexData != NULL && total != 0)
        memset(self->pVertexData, 0, total * MDL_VERTEX_SIZE);

    self->pScratchVerts = malloc(verts * MDL_VERTEX_SIZE);
    if (self->pScratchVerts != NULL && verts != 0)
        memset(self->pScratchVerts, 0, verts * MDL_VERTEX_SIZE);

    for (unsigned f = 0; f < (frames & 0xffff); f++) {
        unsigned char *rec = (unsigned char *)self->pFrameRecords
                           + f * MDL_FRAME_REC_SIZE;
        for (int i = 0; i < 6; i++)
            fread(rec + i * 4, 4, 1, fp);

        for (unsigned v = 0; v < self->dwVertexCount; v++) {
            unsigned char *vert = (unsigned char *)self->pVertexData
                                + (f * self->dwVertexCount + v) * MDL_VERTEX_SIZE;
            for (int i = 0; i < 10; i++)
                fread(vert + i * 4, 4, 1, fp);

            if (fx_scale()) {
                ((float *)vert)[0] *= 0.5f;
                ((float *)vert)[1] *= 0.5f;
                ((float *)vert)[2] *= 0.5f;
            }
        }
    }

    fclose(fp);

    // pszName is the path, freed by FaktMesh_ReleaseModelBuffers.
    {
        unsigned n = (unsigned)strlen(path) + 1;
        char *name = (char *)malloc(n);
        self->pszName = name;
        if (name != NULL)
            memcpy(name, path, n);
    }

    // KAROO_MDL_DUMP=<path>: one line per load, the two heap buffers hashed
    // (FNV-1a 32), to compare an independent parse of the same .mdl against
    // what landed in memory.
    {
        char dump[MAX_PATH];
        if (GetEnvironmentVariableA("KAROO_MDL_DUMP", dump, sizeof(dump))) {
            HANDLE h = CreateFileA(dump, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                char line[512];
                int n = wsprintfA(line, "%s frames=%u verts=%u rec=%08lx vtx=%08lx\r\n",
                                  path, frames, verts,
                                  fnv1a(self->pFrameRecords, frames * MDL_FRAME_REC_SIZE),
                                  fnv1a(self->pVertexData,   total  * MDL_VERTEX_SIZE));
                DWORD w = 0;
                WriteFile(h, line, (DWORD)n, &w, NULL);
                CloseHandle(h);
            }
        }
    }

    if (logged < MDL_LOG_FIRST) {
        logged++;
        log_write("model: '%s' frames=%u verts=%u\n", path, frames, verts);
    }
    return 1;
}

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
    mm_scalar_dtor_fn dtor = *(mm_scalar_dtor_fn *)m->unknown00;
    dtor(m, 1);
}

extern "C" __declspec(dllexport) CFaktMesh *__attribute__((thiscall))
ModelManager_FindOrImport(ModelManager *self, char *name)
{
    for (LinkedListNode *node = self->cache.pHead; node != NULL; ) {
        CFaktMesh *cached = (CFaktMesh *)node->pValue;
        node = node->pNextNode;
        mm_lower_inplace(name);
        mm_lower_inplace(cached->pszName);
        if (strcmp(cached->pszName, name) == 0) {
            if (self->pLogger != NULL)
                GameLog_LogMessage(self->pLogger, 1, GS_MM_FOUND, name);
            return cached;
        }
    }

    void *mem = malloc(sizeof(CFaktMesh));
    CFaktMesh *mesh = (mem != NULL) ? FaktMesh_Init((CFaktMesh *)mem) : NULL;
    if ((Model_ImportSceneModels(mesh, name) & 0xff) == 0) {
        if (mesh != NULL)
            mm_delete(mesh);
        if (self->pLogger != NULL)
            GameLog_LogMessage(self->pLogger, 3, GS_MM_FAILED, name);
        return NULL;
    }
    if (self->pLogger != NULL)
        GameLog_LogMessage(self->pLogger, 1, GS_MM_LOADED, name);
    LinkedList_Append(&self->cache, mesh);
    return mesh;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
ModelManager_ClearReleaseFree(ModelManager *self)
{
    for (LinkedListNode *node = self->cache.pHead; node != NULL; ) {
        CFaktMesh *mesh = (CFaktMesh *)node->pValue;
        node = node->pNextNode;
        if (mesh != NULL) {
            FaktMesh_ReleaseModelBuffers(mesh);
            mm_delete(mesh);
        }
    }
    LinkedList_Clear(&self->cache);
}

/* ─── ModelManager lifecycle ───────────────────────────────────────────────
 *
 * Every instance is static (model.h), so nothing deletes one and the scalar
 * dtor's free is never reached. */
static void *const g_ModelManagerVtable[1] = { (void *)&ModelManager_ScalarDestructor };

extern "C" __declspec(dllexport) ModelManager *__attribute__((thiscall))
ModelManager_Construct(ModelManager *self)
{
    List_Init(&self->cache);
    self->vtable  = (void *)g_ModelManagerVtable;
    self->pLogger = NULL;
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
ModelManager_Destruct(ModelManager *self)
{
    self->vtable = (void *)g_ModelManagerVtable;
    List_Destruct(&self->cache);
}

extern "C" __declspec(dllexport) ModelManager *__attribute__((thiscall))
ModelManager_ScalarDestructor(ModelManager *self, unsigned char flags)
{
    ModelManager_Destruct(self);
    if (flags & 1)
        free(self);
    return self;
}
