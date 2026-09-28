/* Sets render state per sub-object before deciding whether to draw, so a later
 * sub-object's blend and texture state depends on every sub-object that came
 * before it in the array, whether or not that one drew anything. */

#include "quadbatch.h"
#include "renderdevice.h"
#include "d3dmath.h"
#include "levelobject.h"
#include "log.h"

#define QUAD_FVF       VertexFormat::Lit
#define QUAD_LOG_FIRST 8

/* Only the fields this file reads are declared; the bytes before them are
 * padding from the caller's larger structure. */
struct QuadVerts {
    BYTE  pad00[0x124];
    DWORD dwQuads;  // quads; 6 vertices each (two triangles)
    void *pData;    // raw vertex array, FVF 0x1e2
};
static_assert(offsetof(QuadVerts, dwQuads) == 0x124, "QuadVerts layout");
static_assert(offsetof(QuadVerts, pData)   == 0x128, "QuadVerts layout");

/* KAROO_QUAD_DUMP=<path> writes every vertex of one quad batch (the 200th draw) to
 * a file, once.  FVF 0x1e2 is a 32-byte vertex: xyz(12), reserved(4),
 * diffuse(4), specular(4), two texture-coordinate pairs(8). */
static void quad_dump(const void *data, DWORD quads)
{
    static LONG calls = 0;
    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("KAROO_QUAD_DUMP", path, sizeof(path)))
        return;
    // Dumps the 200th gated draw, not the first: if the buffer is filled
    // lazily, the first frame would show an empty one.
    if (InterlockedIncrement(&calls) != 200)
        return;
    log_write("quadbatch: dumping at call 200, pData=%p quads=%lu\n", data, quads);

    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("quadbatch: dump could not open %s (err=%lu)\n",
                  path, GetLastError());
        return;
    }
    const BYTE *v = (const BYTE *)data;
    char line[300];
    DWORD wr;
    int n = wsprintfA(line, "raw dump pData=%p quads=%lu\r\n", data, quads);
    WriteFile(f, line, n, &wr, NULL);
    // Written as raw hex, one vertex per line, rather than decoded fields.
    DWORD total = quads * 6 * 32;
    for (DWORD off = 0; off < total; off += 32) {
        n = 0;
        n += wsprintfA(line + n, "%06lX ", off);
        for (int b = 0; b < 32; b++)
            n += wsprintfA(line + n, "%02X", v[off + b]);
        n += wsprintfA(line + n, "\r\n");
        WriteFile(f, line, n, &wr, NULL);
    }
    CloseHandle(f);
    log_write("quadbatch: dumped %lu quads to %s\n", quads, path);
}

/* KAROO_QUAD_FX: noalpha forces ALPHABLENDENABLE off for every sub-object (no
 * visible effect where a sub-object's blend factors are already zero); nodraw
 * skips the DrawPrimitive, leaving every render state set as usual. */
enum QuadFxMode { QUAD_FX_OFF = 0, QUAD_FX_NOALPHA, QUAD_FX_NODRAW };

static QuadFxMode quad_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = QUAD_FX_OFF;
        if (GetEnvironmentVariableA("KAROO_QUAD_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "noalpha") == 0) cached = QUAD_FX_NOALPHA;
            else if (lstrcmpiA(buf, "nodraw") == 0) cached = QUAD_FX_NODRAW;
        }
        log_write("quadbatch: FX mode = %s\n",
                  cached == QUAD_FX_NOALPHA ? "noalpha" :
                  cached == QUAD_FX_NODRAW  ? "nodraw"  : "off");
    }
    return (QuadFxMode)cached;
}

  void  
QuadBatch_Draw(QuadVerts *verts, void *game, RenderDevice *d3d)
{
    DWORD nobj = *(DWORD *)((BYTE *)game + GAME_OFF_QUAD_COUNT);
    if (nobj == 0)
        return;

    for (DWORD i = 0; i < *(DWORD *)((BYTE *)game + GAME_OFF_QUAD_COUNT); i++) {
        BYTE  *obj  = lobj_at(game, i);
        DWORD  nsub = *(DWORD *)(obj + LOBJ_OFF_SUBOBJCOUNT);
        {  // KAROO_QUAD_DIAG=1: log each object's draw-kind and sub-object count.
            static LONG diag = 0;
            char dbuf[8];
            if (GetEnvironmentVariableA("KAROO_QUAD_DIAG", dbuf, sizeof(dbuf))
                && dbuf[0] != '0' && InterlockedIncrement(&diag) <= 24)
                log_write("quadbatch: diag obj=%lu kind=%lu nsub=%lu\n",
                          i, *(DWORD *)(obj + LOBJ_OFF_DRAWKIND), nsub);
        }
        if (nsub == 0)
            continue;

        for (DWORD s = 0; s < *(DWORD *)(obj + LOBJ_OFF_SUBOBJCOUNT); s++) {
            SceneSubObject *sub =
                (SceneSubObject *)(obj + LOBJ_OFF_SUBOBJECTS) + s;

            DWORD addr = sub->dwTexAddress ? sub->dwTexAddress : 3;
            d3d->SetRenderState(RS::TextureAddressU, addr);
            d3d->SetRenderState(RS::TextureAddressV, addr);

            if (sub->pTexture)
                d3d->SetTexture(0, sub->pTexture);

            // last_state and last_value let the alpha-off and dest-blend
            // branches share one SetRenderState call.
            RS last_state;
            DWORD              last_value;
            if (sub->dwBlendSrc && sub->dwBlendDst
                && quad_fx() != QUAD_FX_NOALPHA) {
                d3d->SetRenderState(RS::AlphaBlendEnable, 1);
                d3d->SetRenderState(RS::SrcBlend,
                                             sub->dwBlendSrc);
                last_state = RS::DestBlend;
                last_value = sub->dwBlendDst;
            } else {
                last_state = RS::AlphaBlendEnable;
                last_value = 0;
            }
            d3d->SetRenderState(last_state, last_value);

            // The draw itself only runs for draw-kind 2, but the
            // render state above it is set for every sub-object regardless;
            // hoisting this gate above the state changes would change what
            // state is left behind for whatever draws next.
            if (*(DWORD *)(obj + LOBJ_OFF_DRAWKIND) == 2) {
                d3d->SetTransform(Transform::World,
                                           &g_worldIdentity);
                quad_dump(verts->pData, verts->dwQuads);
                bool ok = true;
                if (quad_fx() != QUAD_FX_NODRAW)
                    ok = d3d->Draw(
                        Prim::TriangleList, QUAD_FVF, verts->pData,
                        verts->dwQuads * 6, 0);

                static LONG logged = 0;
                if (InterlockedIncrement(&logged) <= QUAD_LOG_FIRST)
                    log_write("quadbatch: obj=%lu sub=%lu tex=%p addr=%lu "
                              "src=%lu dst=%lu quads=%lu -> ok=%d\n",
                              i, s, sub->pTexture, addr, sub->dwBlendSrc,
                              sub->dwBlendDst, verts->dwQuads, ok);
            }
        }
    }
}
