/* Sets render state per sub-object before deciding whether to draw, so a later
 * sub-object's blend and texture state depends on every sub-object that came
 * before it in the array, whether or not that one drew anything. */

#include <strings.h>
#include <atomic>
#include <stdio.h>
#include <stdint.h>
#include "quadbatch.h"
#include "sysdev.h"
#include <fstream>
#include "binio.h"
#include <errno.h>
#include "renderdevice.h"
#include "d3dmath.h"
#include "theme.h"
#include "levelplacements.h"
#include "logger.h"

#define QUAD_FVF       VertexFormat::Lit
#define QUAD_LOG_FIRST 8

/* KAROO_QUAD_DUMP=<path> writes every vertex of one quad batch (the 200th draw) to
 * a file, once.  FVF 0x1e2 is a 32-byte vertex: xyz(12), reserved(4),
 * diffuse(4), specular(4), two texture-coordinate pairs(8). */
static void quad_dump(const void *data, uint32_t quads)
{
    static std::atomic<long> calls = 0;
    char path[FILENAME_MAX];
    if (!sysdev::getEnv("KAROO_QUAD_DUMP", path, sizeof(path)))
        return;
    // Dumps the 200th gated draw, not the first: if the buffer is filled
    // lazily, the first frame would show an empty one.
    if (++calls != 200)
        return;
    g_logger.write("quadbatch: dumping at call 200, pData=%p quads=%lu\n", data, quads);

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        g_logger.write("quadbatch: dump could not open %s\n", path);
        return;
    }
    const uint8_t *v = (const uint8_t *)data;
    printTo(f, "raw dump pData=%p quads=%lu\r\n", data, (unsigned long)quads);
    // Written as raw hex, one vertex per line, rather than decoded fields.
    uint32_t total = quads * 6 * 32;
    for (uint32_t off = 0; off < total; off += 32) {
        printTo(f, "%06lX ", (unsigned long)off);
        for (int b = 0; b < 32; b++)
            printTo(f, "%02X", v[off + b]);
        f << "\r\n";
    }
    f.close();
    g_logger.write("quadbatch: dumped %lu quads to %s\n", quads, path);
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
        if (sysdev::getEnv("KAROO_QUAD_FX", buf, sizeof(buf))) {
            if (strcasecmp(buf, "noalpha") == 0) cached = QUAD_FX_NOALPHA;
            else if (strcasecmp(buf, "nodraw") == 0) cached = QUAD_FX_NODRAW;
        }
        g_logger.write("quadbatch: FX mode = %s\n",
                  cached == QUAD_FX_NOALPHA ? "noalpha" :
                  cached == QUAD_FX_NODRAW  ? "nodraw"  : "off");
    }
    return (QuadFxMode)cached;
}

void QuadBatch_Draw(const LevelPlacements *pl, const ThemeAssetBlock *theme,
                    RenderDevice *d3d)
{
    const ThemeObjectTypeSlot *slot = theme->slot(THEME_OBJ_SIDE);
    if (slot->instanceCount() == 0)
        return;

    for (uint32_t i = 0; i < slot->instanceCount(); i++) {
        const ThemeLevelObject *obj = &slot->records()[i];
        uint32_t nsub = obj->subObjectCount();
        {  // KAROO_QUAD_DIAG=1: log each object's draw-kind and sub-object count.
            static std::atomic<long> diag = 0;
            char dbuf[8];
            if (sysdev::getEnv("KAROO_QUAD_DIAG", dbuf, sizeof(dbuf))
                && dbuf[0] != '0' && ++diag <= 24)
                g_logger.write("quadbatch: diag obj=%lu kind=%lu nsub=%lu\n",
                          i, (uint32_t)obj->kind(), nsub);
        }
        if (nsub == 0)
            continue;

        for (uint32_t s = 0; s < obj->subObjectCount(); s++) {
            const SceneSubObject *sub = &obj->subObjects()[s];

            uint32_t addr = sub->dwTexAddress ? sub->dwTexAddress : 3;
            d3d->SetRenderState(RS::TextureAddressU, addr);
            d3d->SetRenderState(RS::TextureAddressV, addr);

            if (sub->pTexture)
                d3d->SetTexture(0, sub->pTexture);

            // last_state and last_value let the alpha-off and dest-blend
            // branches share one SetRenderState call.
            RS last_state;
            uint32_t              last_value;
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
            if (obj->kind() == THEME_KIND_FIELD) {
                d3d->SetTransform(Transform::World,
                                           &g_worldIdentity);
                quad_dump(pl->wallStripVerts(), (uint32_t)pl->wallStripCount());
                bool ok = true;
                if (quad_fx() != QUAD_FX_NODRAW)
                    ok = d3d->Draw(
                        Prim::TriangleList, QUAD_FVF, pl->wallStripVerts(),
                        (uint32_t)pl->wallStripCount() * 6, 0);

                static std::atomic<long> logged = 0;
                if (++logged <= QUAD_LOG_FIRST)
                    g_logger.write("quadbatch: obj=%lu sub=%lu tex=%p addr=%lu "
                              "src=%lu dst=%lu quads=%lu -> ok=%d\n",
                              i, s, sub->pTexture, addr, sub->dwBlendSrc,
                              sub->dwBlendDst, (uint32_t)pl->wallStripCount(), ok);
            }
        }
    }
}
