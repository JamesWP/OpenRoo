/* ASSET_PLAN.md Phase 3 — the .mdl reader.
 *
 *   0x437bc0  LoadedModel::ImportSceneModels(this, LPCSTR path) -> BOOL
 *             __thiscall, ret 4.  3 E8 call sites (0x426622, 0x426631 in
 *             ConfigureRenderState's model setup, 0x438655), no E9, no PUSH,
 *             no vtable slot.  UD2-stubbed.
 *
 * The object is the same CFaktMesh whose draw path we already own
 * (faktmesh.cpp); Ghidra names it LoadedModel here.  One struct, one header.
 * That is what makes this phase small: the destination was already
 * static_assert'd, so this is "fill a struct we own from a file".
 *
 * ─── Scope correction: .ani is NOT in this phase ──────────────────────────
 *
 * ASSET_PLAN.md paired .mdl and .ani, expecting "the same reader family".
 * There is no .ani reader to replace.  Phase 0's log showed the 18 .ani opens
 * coming from inside the .leo parser (FUN_00401070), and that function's
 * callee list is fopen/fclose plus the CRT's text-parsing helpers
 * (0x4505b7, 0x4506a7, 0x450743, 0x4507d0) -- it opens and parses the
 * animation inline, with no separable function.  .ani therefore moves to
 * Phase 4, with the .leo parser that contains it.
 *
 * ─── Calls into the game binary: the allocator, and only the allocator ────
 *
 * Four heap blocks are allocated here and freed by the game's FreeThing2
 * (0x437fb0), which uses FactAlloc::Free2.  They cross the ownership
 * boundary, so under ASSET_PLAN.md's no-callback rule they must come from the
 * game's heap: operator new (0x450e9d) is called for all four.  That is the
 * "allocator" kind, the one the rule keeps.  Everything else -- open, read,
 * close, the string copy, and the free path below -- is ours.
 *
 * FreeThing2 it(self) is NOT called and NOT stubbed: it stays live for its two
 * other referrers (0x437b36 JMP from the destructor, 0x4386f7 CALL).  Its
 * body is inlined below instead, which is four Free2 calls and five stores.
 *
 * ─── The file ─────────────────────────────────────────────────────────────
 *
 *   +0x00  WORD   frameCount     -> this->wFrameCount   (+0x10)
 *   +0x02  DWORD  vertexCount    -> this->dwVertexCount (+0x08)
 *   then, for each frame f:
 *          6 x DWORD             -> pFrameRecords + f*0x18
 *          for each vertex v:
 *              10 x DWORD        -> pVertexData + (f*vertexCount + v)*0x28
 *
 * Opened "rb" (0x465188) -- binary, unlike the Phase 2 player-state files.
 * Every read is a separate 4-byte fread; the header's two are 2 and 4 bytes.
 *
 * Three buffers are allocated before the read:
 *   pFrameRecords  frameCount * 0x18
 *   pVertexData    frameCount * vertexCount * 0x28   (zero-filled)
 *   pScratchVerts  vertexCount * 0x28                (zero-filled)
 * and one after it: pszName = strlen(path)+1 bytes, holding the path.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. NO READ IS CHECKED, and neither is the file's size.  A truncated .mdl
 *    leaves the remaining vertices at whatever the zero-fill left them, and
 *    the function still returns 1.  Only a failed open returns 0.
 * 2. pFrameRecords IS NOT ZERO-FILLED, while the other two buffers are.  A
 *    frame whose 0x18-byte record is not present in the file therefore reads
 *    as heap garbage rather than zeros.  Asymmetric in the original; kept.
 * 3. A NULL from operator new is stored as NULL and the code carries on to
 *    dereference it on the next read.  The original tests the result only to
 *    skip its zero-fill loop, never to bail out.
 * 4. THE FRAME COUNTER IS 16-BIT.  The outer loop compares AX against
 *    wFrameCount, so the count is masked to 16 bits; the vertex index is a
 *    full 32-bit compare against dwVertexCount.
 * 5. dwVertexCount IS RE-READ FROM THE OBJECT on every vertex iteration
 *    rather than hoisted.  Harmless -- nothing writes it during the loop --
 *    but reproduced.
 * 6. FreeThing2 runs BEFORE the open, so a failed open leaves the model
 *    cleared, not unchanged: any previously loaded mesh is already gone.
 *
 * ─── Visual proof ─────────────────────────────────────────────────────────
 *
 * KAROO_MDL_FX=scale halves every vertex position as it is read, so every
 * model in the game comes out at half size.  A geometry change, and one only
 * this code path can produce.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "faktmesh.h"
#include "log.h"
#include "alloc.h"

/* The game's heap.  Allocations here are freed by FreeThing2 (0x437fb0) via
 * FactAlloc::Free2, so they must come from the matching allocator. */

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

/* FreeThing2 (0x437fb0), inlined -- see the header comment for why it is not
 * called.  Frees the four heap fields, then resets the two scalars. */
static void model_release(CFaktMesh *m)
{
    if (m->pVertexData)   game_free2(m->pVertexData);
    m->pVertexData = NULL;
    if (m->pFrameRecords) game_free2(m->pFrameRecords);
    m->pFrameRecords = NULL;
    if (m->pScratchVerts) game_free2(m->pScratchVerts);
    m->pScratchVerts = NULL;
    if (m->pszName)       game_free2(m->pszName);
    m->pszName = NULL;
    m->dwVertexCount = 0;
    m->wFrameCount   = 1;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Model_ImportSceneModels(CFaktMesh *self, const char *path)
{
    FILE *fp;
    unsigned frames, verts, total;
    static int logged = 0;

    model_release(self);                       /* defect 6: before the open */

    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    fread(&self->wFrameCount,   2, 1, fp);
    fread(&self->dwVertexCount, 4, 1, fp);

    frames = self->wFrameCount;
    verts  = self->dwVertexCount;
    total  = frames * verts;

    /* Per-frame records: allocated, NOT zero-filled (defect 2). */
    self->pFrameRecords = game_operator_new(frames * MDL_FRAME_REC_SIZE);

    /* Vertex array: zero-filled, 10 dwords per vertex. */
    self->pVertexData = game_operator_new(total * MDL_VERTEX_SIZE);
    if (self->pVertexData != NULL && total != 0)
        memset(self->pVertexData, 0, total * MDL_VERTEX_SIZE);

    /* Scratch vertices: one frame's worth, zero-filled. */
    self->pScratchVerts = game_operator_new(verts * MDL_VERTEX_SIZE);
    if (self->pScratchVerts != NULL && verts != 0)
        memset(self->pScratchVerts, 0, verts * MDL_VERTEX_SIZE);

    for (unsigned f = 0; f < (frames & 0xffff); f++) {   /* defect 4 */
        unsigned char *rec = (unsigned char *)self->pFrameRecords
                           + f * MDL_FRAME_REC_SIZE;
        for (int i = 0; i < 6; i++)
            fread(rec + i * 4, 4, 1, fp);                /* defect 1 */

        for (unsigned v = 0; v < self->dwVertexCount; v++) {  /* defect 5 */
            unsigned char *vert = (unsigned char *)self->pVertexData
                                + (f * self->dwVertexCount + v) * MDL_VERTEX_SIZE;
            for (int i = 0; i < 10; i++)
                fread(vert + i * 4, 4, 1, fp);

            if (fx_scale()) {
                /* xyz are the first three dwords of the FVF 0x212 vertex. */
                ((float *)vert)[0] *= 0.5f;
                ((float *)vert)[1] *= 0.5f;
                ((float *)vert)[2] *= 0.5f;
            }
        }
    }

    fclose(fp);

    /* strdup onto the game's heap: FreeThing2 frees this pointer. */
    {
        unsigned n = (unsigned)strlen(path) + 1;
        char *name = (char *)game_operator_new(n);
        self->pszName = name;
        if (name != NULL)
            memcpy(name, path, n);
    }

    /* KAROO_MDL_DUMP=<path> -- one line per load: the two heap buffers hashed
     * (FNV-1a 32), so an independent parse of the same .mdl can be compared
     * against what actually landed in memory.  See ASSET_PLAN.md Phase 3. */
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
