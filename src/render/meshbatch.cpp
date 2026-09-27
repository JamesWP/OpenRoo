/* The mesh-batch pass: for every mesh-batch object and each of its
 * sub-objects, the same render-state block as DrawQuadBatch, then a two-way
 * draw.  A NULL mesh pointer draws the render context's flat quad batch under
 * the identity WORLD; otherwise the mesh is drawn under RotX(pi/2) with the
 * context's position as the translation.
 *
 * The translation comes from the render context, not the object, so it is the
 * same for every object in a frame.
 *
 * No recorded level has an object with a non-NULL mesh, so the rotation branch
 * is never exercised by the replay suite.  KAROO_MESHBATCH_FX=norot drops its
 * rotation (identity 3x3, translation kept); it too is only visible on such a
 * level. */

#include "meshbatch.h"
#include "renderdevice.h"
#include "d3dmath.h"
#include "levelobject.h"
#include "faktmesh.h"
#include "log.h"

#include <math.h>

#define MESH_QUAD_FVF   VertexFormat::Lit
#define MESH_LOG_FIRST  8

/* The loop cursor here is the object base (DrawQuadBatch's is the sub-object
 * count), so these offsets are 4 lower than levelobject.h's quad-batch ones.
 */
#define GAME_OFF_MESH_COUNT     0xebb8
#define GAME_OFF_MESH_OBJECTS   0xebc0
#define MOBJ_OFF_MESH           0x000  // CFaktMesh*, NULL = draw the flat quads
#define MOBJ_OFF_SUBOBJCOUNT    0x3bd
#define MOBJ_OFF_SUBOBJECTS     0x3c1

/* Render context fields this pass reads. */
#define CTX_OFF_QUAD_COUNT      0x80
#define CTX_OFF_QUAD_VERTS      0x84
#define CTX_OFF_POS_X           0x88
#define CTX_OFF_POS_Y           0x8c
#define CTX_OFF_POS_Z           0x90

#define g_flMeshBatchAngle 0x1.921fb6p+0  // (double)(float)(pi/2)

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
MeshBatch_Draw(void *ctx, void *game, RenderDevice *d3d)
{
    BYTE *c = (BYTE *)ctx;

    {  // Log: is this pass reached, and with what?
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
            d3d->SetRenderState(RS::TextureAddressU, addr);
            d3d->SetRenderState(RS::TextureAddressV, addr);

            if (sub->pTexture)
                d3d->SetTexture(0, sub->pTexture);

            // The two branches share the final SetRenderState call.
            RS last_state;
            DWORD              last_value;
            if (sub->dwBlendSrc && sub->dwBlendDst) {
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

            CFaktMesh *mesh = *(CFaktMesh **)(obj + MOBJ_OFF_MESH);
            if (mesh == NULL) {
                d3d->SetTransform(Transform::World,
                                           &g_worldIdentity);
                d3d->Draw(
                    Prim::TriangleList, MESH_QUAD_FVF,
                    *(void **)(c + CTX_OFF_QUAD_VERTS),
                    *(DWORD *)(c + CTX_OFF_QUAD_COUNT) * 6, 0);
            } else {
                const float cs = (float)cos(g_flMeshBatchAngle);
                const float sn = (float)sin(g_flMeshBatchAngle);

                // RotX(angle) with the translation row copied in: the
                // rotation's last row is (0,0,0,1) and the translation's 3x3
                // is identity, so the product is exact.
                float m[16];
                for (int k = 0; k < 16; k++)
                    m[k] = 0.0f;
                m[0] = 1.0f;
                if (fx_norot()) {
                    m[5] = 1.0f; m[10] = 1.0f;
                } else {
                    m[5] = cs;  m[6]  = -sn;
                    m[9] = sn;  m[10] = cs;
                }
                m[12] = *(float *)(c + CTX_OFF_POS_X);
                m[13] = *(float *)(c + CTX_OFF_POS_Y);
                m[14] = *(float *)(c + CTX_OFF_POS_Z);
                m[15] = 1.0f;

                d3d->SetTransform(Transform::World,
                                           (const Mat4 *)m);
                mesh->drawMeshBuffer(d3d, 0);

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
