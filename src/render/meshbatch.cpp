/* DrawMeshBatch (0x408280) reimplementation.
 *
 * __cdecl(RenderCtx *ctx, Game *game, Direct3D *d3d), ret 0.  1153 bytes, ten
 * D3D dispatches, one E8 call site (0x4279CD in RenderGameFrame), no
 * raw-DWORD/vtable refs.  The caller pushes the same three globals
 * DrawQuadBatch gets: 0x4e0070, 0x46c890, g_pDirect3D.
 *
 * Structurally this is DrawQuadBatch's sibling on the mesh-batch array
 * (Game+0xebb8 count, objects at +0xebc0, stride 0x5dd), with the same
 * per-sub-object state block, and a two-way draw:
 *
 *   *(CFaktMesh **)obj == NULL -> SetTransform(WORLD, identity)
 *                                 DrawPrimitive(TRIANGLELIST, 0x1e2,
 *                                     ctx->meshVerts, ctx->meshQuads*6, 0)
 *   otherwise                  -> SetTransform(WORLD, Rx(angle) + translation)
 *                                 CFaktMesh::DrawMeshBuffer(mesh, dev, 0)
 *
 * ── What is verified, and what is NOT ─────────────────────────────────────
 * Ghidra mis-models this function's stack (iStack_2c / iStack_20 / iVar11 are
 * phantoms; two matrices overlap its variable map), so the matrix cannot be
 * read off the decompile.  KAROO_XFORM_DUMP=1 (com_proxy wd3_SetTransform)
 * was added to log every WORLD matrix as raw float bits, and the run was
 * compared before and after this replacement.
 *
 * VERIFIED — the NULL-mesh path.  Every WORLD matrix in a full castle-something
 * replay is bit-for-bit identical before and after, across 1000+ invocations of
 * this function, and the suite passes.
 *
 * NOT VERIFIED — the mesh path (the Rx + translation branch below).  Every
 * object in every recorded level has a NULL mesh pointer, so that branch has
 * never executed: the entry diagnostic reports nobj=1 and the per-mesh log line
 * never fires.  The rotated matrices seen in the capture therefore come from a
 * different SetTransform caller (FUN_00422b90 also reads the angle constant),
 * NOT from here — an earlier attempt to attribute them to this function was
 * wrong.
 *
 * So the rotation branch rests on the decompile's shape plus the constant at
 * 0x45d348 — a *double* (`fldl`, not `flds`) holding 1.5707963705062866, which
 * is float(pi/2) widened, giving cos ~0 and sin 1.  It is a reasoned
 * reconstruction, not a measured one.  Before relying on it, find a level with
 * a non-NULL mesh-batch object, run with KAROO_XFORM_DUMP=1, and diff the
 * matrices against the unpatched build the same way.
 *
 * The original builds the rotation and a translation matrix and multiplies
 * them.  Rx's last row is (0,0,0,1) and the translation matrix's upper 3x3 is
 * identity, so the product is exactly the rotation with the translation row
 * copied in (the same shortcut as sky.cpp, bit-identical for finite values).
 *
 * The translation source is ctx+0x88/+0x8c/+0x90, not a per-object field:
 * `mov 0x3f8(%esp),%eax` with a 0x3e4 frame plus four pushes resolves to
 * param_1, and param_2 at [esp+0x3ec] is confirmed by its Game+0xebb8 read.
 * It is therefore constant within a frame; the differing translations in the
 * capture are successive frames as the view moves.
 *
 * Note obj+0x00 is a CFaktMesh pointer, not a type tag: zero means "no mesh,
 * draw the flat quad batch".  DrawQuadBatch's gate compares the corresponding
 * slot against 2, which is why the two arrays' offsets differ by 4 — see
 * RENDER_PLAN.md.
 *
 * KAROO_MESHBATCH_FX=norot drops the rotation (identity upper 3x3, translation
 * kept), which would tip every batched mesh onto its side — but it can only be
 * seen on a level that actually has a non-NULL mesh-batch object, which none of
 * the recordings do.
 */
#include "direct3d.h"
#include "levelobject.h"
#include "faktmesh.h"
#include "log.h"

#include <math.h>

#define MESH_QUAD_FVF   0x1e2
#define MESH_LOG_FIRST  8

/* Mesh-batch array in the Game object.  The loop cursor here IS the object
 * base (unlike DrawQuadBatch, whose cursor is the sub-object count), so these
 * offsets are 4 lower than levelobject.h's quad-batch ones. */
#define GAME_OFF_MESH_COUNT     0xebb8
#define GAME_OFF_MESH_OBJECTS   0xebc0
#define MOBJ_OFF_MESH           0x000   /* CFaktMesh*, NULL = draw flat quads */
#define MOBJ_OFF_SUBOBJCOUNT    0x3bd
#define MOBJ_OFF_SUBOBJECTS     0x3c1

/* Render context fields this pass reads (the global at 0x4e0070). */
#define CTX_OFF_QUAD_COUNT      0x80
#define CTX_OFF_QUAD_VERTS      0x84
#define CTX_OFF_POS_X           0x88
#define CTX_OFF_POS_Y           0x8c
#define CTX_OFF_POS_Z           0x90

#define g_dwWorldIdentity (*(D3DMATRIX *)0x004e0440)
#define g_flMeshBatchAngle (*(const double *)0x0045d348)

extern "C" HRESULT __attribute__((thiscall))
FaktMesh_DrawMeshBuffer(CFaktMesh *self, IDirect3DDevice3 *dev, DWORD frame);

static bool fx_norot(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_MESHBATCH_FX", buf, sizeof buf))
            cached = (lstrcmpiA(buf, "norot") == 0);
        log_write("meshbatch: FX mode = %s\n", cached ? "norot" : "off");
    }
    return cached != 0;
}

extern "C" __declspec(dllexport) void __cdecl
Direct3D_DrawMeshBatch(void *ctx, void *game, Direct3D *d3d)
{
    BYTE *c = (BYTE *)ctx;

    {   /* entry diagnostic: is this pass reached at all, and with what? */
        static LONG e = 0;
        LONG k = InterlockedIncrement(&e);
        if (k <= 3 || k % 500 == 0)
            log_write("meshbatch: enter #%ld nobj=%lu quads=%lu\n", k,
                      *(DWORD *)((BYTE *)game + GAME_OFF_MESH_COUNT),
                      *(DWORD *)(c + CTX_OFF_QUAD_COUNT));
    }
    if (*(DWORD *)((BYTE *)game + GAME_OFF_MESH_COUNT) == 0)
        return;

    for (DWORD i = 0;
         i < *(DWORD *)((BYTE *)game + GAME_OFF_MESH_COUNT); i++) {
        BYTE *obj = (BYTE *)game + GAME_OFF_MESH_OBJECTS + i * LOBJ_STRIDE;
        if (*(DWORD *)(obj + MOBJ_OFF_SUBOBJCOUNT) == 0)
            continue;

        for (DWORD s = 0;
             s < *(DWORD *)(obj + MOBJ_OFF_SUBOBJCOUNT); s++) {
            SceneSubObject *sub =
                (SceneSubObject *)(obj + MOBJ_OFF_SUBOBJECTS) + s;

            DWORD addr = sub->dwTexAddress ? sub->dwTexAddress : 3;
            d3d->pDevice->SetRenderState(D3DRENDERSTATE_TEXTUREADDRESSU, addr);
            d3d->pDevice->SetRenderState(D3DRENDERSTATE_TEXTUREADDRESSV, addr);

            if (sub->pTexture)
                d3d->pDevice->SetTexture(
                    0, *(IDirect3DTexture2 **)((BYTE *)sub->pTexture + 0x18));

            /* One tail call in the original, state/value picked by the branch. */
            D3DRENDERSTATETYPE last_state;
            DWORD              last_value;
            if (sub->dwBlendSrc && sub->dwBlendDst) {
                d3d->pDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
                d3d->pDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND,
                                             sub->dwBlendSrc);
                last_state = D3DRENDERSTATE_DESTBLEND;
                last_value = sub->dwBlendDst;
            } else {
                last_state = D3DRENDERSTATE_ALPHABLENDENABLE;
                last_value = 0;
            }
            d3d->pDevice->SetRenderState(last_state, last_value);

            CFaktMesh *mesh = *(CFaktMesh **)(obj + MOBJ_OFF_MESH);
            if (mesh == NULL) {
                d3d->pDevice->SetTransform(D3DTRANSFORMSTATE_WORLD,
                                           &g_dwWorldIdentity);
                d3d->pDevice->DrawPrimitive(
                    D3DPT_TRIANGLELIST, MESH_QUAD_FVF,
                    *(void **)(c + CTX_OFF_QUAD_VERTS),
                    *(DWORD *)(c + CTX_OFF_QUAD_COUNT) * 6, 0);
            } else {
                const float cs = (float)cos(g_flMeshBatchAngle);
                const float sn = (float)sin(g_flMeshBatchAngle);

                float m[16];
                for (int k = 0; k < 16; k++)
                    m[k] = 0.0f;
                m[0] = 1.0f;                        /* _11            */
                if (fx_norot()) {
                    m[5] = 1.0f; m[10] = 1.0f;      /* identity 3x3   */
                } else {
                    m[5] = cs;  m[6]  = -sn;        /* _22, _23       */
                    m[9] = sn;  m[10] = cs;         /* _32, _33       */
                }
                m[12] = *(float *)(c + CTX_OFF_POS_X);  /* _41 */
                m[13] = *(float *)(c + CTX_OFF_POS_Y);  /* _42 */
                m[14] = *(float *)(c + CTX_OFF_POS_Z);  /* _43 */
                m[15] = 1.0f;

                d3d->pDevice->SetTransform(D3DTRANSFORMSTATE_WORLD,
                                           (D3DMATRIX *)m);
                FaktMesh_DrawMeshBuffer(mesh, d3d->pDevice, 0);

                static LONG logged = 0;
                if (InterlockedIncrement(&logged) <= MESH_LOG_FIRST)
                    log_write("meshbatch: obj=%lu sub=%lu mesh=%p "
                              "pos=%d,%d,%d (x1000)\n", i, s, mesh,
                              (int)(m[12] * 1000.0f), (int)(m[13] * 1000.0f),
                              (int)(m[14] * 1000.0f));
            }
        }
    }
}
