/* Scene_RenderSceneObjects's strided draw for kind-2 (quad) theme
 * objects, called from sceneobjects.cpp once per sub-object per visible tile.
 *
 * The quads are stored as SceneQuadVertex (two UV pairs) but only the second
 * pair is ever sampled, so they are drawn strided as VertexFormat::Diffuse1
 * with that pair as set 0.  (Drawing them as Diffuse2 with set 1 unfilled
 * was a wild read -- CRASH.md.)
 *
 * KAROO_SCENEQUAD_FX visual-proof modes (read by value, never by presence):
 *   drop  skip the draw entirely -- the animated billboard quads vanish;
 *   tint  force their vertex diffuse to magenta. */

#include <strings.h>
#include <string.h>
#include "portable.h"
#include <stdint.h>
#include "scenequad.h"
#include "sysdev.h"
#include "logger.h"

#define QUAD_LOG_FIRST  8

enum QuadFx { FX_OFF = 0, FX_DROP, FX_TINT };

static QuadFx quad_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        buf[0] = 0;
        sysdev::getEnv("KAROO_SCENEQUAD_FX", buf, sizeof(buf));
        cached = FX_OFF;
        if (strcasecmp(buf, "drop") == 0)      cached = FX_DROP;
        else if (strcasecmp(buf, "tint") == 0) cached = FX_TINT;
        g_logger.write("scenequad: FX mode = %s (KAROO_SCENEQUAD_FX='%s')\n",
                  cached == FX_DROP ? "drop" : cached == FX_TINT ? "tint" : "off", buf);
    }
    return (QuadFx)cached;
}

bool SceneQuad_Draw(RenderDevice *dev, StridedVertices *v, uint32_t count)
{
    static AtomicInt logged = 0;
    if (atomicIncrement(&logged) <= QUAD_LOG_FIRST)
        g_logger.write("scenequad: dev=%p pos=%p tex0=%p count=%lu\n",
                  (void *)dev, v->position.data, v->texCoords[0].data,
                  (unsigned long)count);

    QuadFx fx = quad_fx();
    if (fx == FX_DROP)
        return true;
    if (fx == FX_TINT && v->diffuse.data) {
        // The caller rebuilds this quad fresh for every draw, so overwriting
        // its diffuse colour here is safe and only affects this one draw.
        const uint32_t tint = 0xFFFF00FF;
        for (uint32_t i = 0; i < count; i++)
            memcpy((char *)v->diffuse.data + i * v->diffuse.stride, &tint, sizeof(tint));
    }

    return dev->DrawStrided(Prim::TriangleStrip, VertexFormat::Diffuse1, v, count);
}
