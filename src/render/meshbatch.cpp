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

#include <strings.h>
#include <atomic>
#include <stdint.h>
#include "meshbatch.h"
#include "sysdev.h"
#include "renderdevice.h"
#include "d3dmath.h"
#include "theme.h"
#include "levelplacements.h"
#include "faktmesh.h"
#include "logger.h"

#include <math.h>

#define MESH_LOG_FIRST  8

#define g_flMeshBatchAngle 0x1.921fb6p+0  // (double)(float)(pi/2)

static bool fx_norot(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (sysdev::getEnv("KAROO_MESHBATCH_FX", buf, sizeof buf))
            cached = (strcasecmp(buf, "norot") == 0);
        g_logger.write("meshbatch: FX mode = %s\n", cached ? "norot" : "off");
    }
    return cached != 0;
}

void MeshBatch_Draw(const LevelPlacements *pl, const ThemeAssetBlock *theme,
                    RenderDevice *d3d)
{
    const ThemeObjectTypeSlot *slot = theme->slot(THEME_OBJ_PLATE);

    {  // Log: is this pass reached, and with what?
        static std::atomic<long> e = 0;
        long k = ++e;
        if (k <= 3 || k % 500 == 0)
            g_logger.write("meshbatch: enter #%ld nobj=%lu quads=%lu\n", k,
                      slot->instanceCount(), (uint32_t)pl->kind01Count());
    }
    if (slot->instanceCount() == 0)
        return;

    for (uint32_t i = 0; i < slot->instanceCount(); i++) {
        const ThemeLevelObject *obj = &slot->records()[i];
        if (obj->subObjectCount() == 0)
            continue;

        for (uint32_t s = 0; s < obj->subObjectCount(); s++) {
            const SceneSubObject *sub = &obj->subObjects()[s];

            const AddressMode addr = addressFromTheme(sub->dwTexAddress);
            d3d->SetSamplerAddress(0, addr, addr);

            if (sub->pTexture)
                d3d->SetTexture(0, sub->pTexture);

            d3d->SetBlend(blendFromTheme(sub->dwBlendSrc, sub->dwBlendDst));

            CFaktMesh *mesh = obj->mesh();  // NULL: draw the flat quads
            if (mesh == NULL) {
                d3d->SetWorld(g_worldIdentity);
                d3d->DrawBuffer(
                    Prim::TriangleList, pl->kind01Buffer(d3d), 0,
                    pl->kind01Count() * 6);
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
                m[12] = pl->exitPos()[0];
                m[13] = pl->exitPos()[1];
                m[14] = pl->exitPos()[2];
                m[15] = 1.0f;

                d3d->SetWorld(*(const Mat4 *)m);
                mesh->drawMeshBuffer(d3d, 0);

                static std::atomic<long> logged = 0;
                if (++logged <= MESH_LOG_FIRST)
                    g_logger.write("meshbatch: obj=%lu sub=%lu mesh=%p "
                              "pos=%d,%d,%d (x1000)\n", i, s, mesh,
                              (int)(m[12] * 1000.0f), (int)(m[13] * 1000.0f),
                              (int)(m[14] * 1000.0f));
            }
        }
    }
}
