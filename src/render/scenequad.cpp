/* Scene_RenderSceneObjects's strided draw for kind-2 (quad) theme
 * objects, called from sceneobjects.cpp once per sub-object per visible tile.
 *
 * VertexFormat::Diffuse2 declares two texture-coordinate sets but the caller
 * only fills set 0; RenderDevice::DrawStrided points the unfilled set at set
 * 0's array so the driver never reads unfilled data (CRASH.md).
 *
 * KAROO_SCENEQUAD_FX visual-proof modes (read by value, never by presence):
 *   drop  skip the draw entirely -- the animated billboard quads vanish;
 *   tint  force their vertex diffuse to magenta. */

#include "scenequad.h"
#include "log.h"

#define QUAD_LOG_FIRST  8

enum QuadFx { FX_OFF = 0, FX_DROP, FX_TINT };

static QuadFx quad_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        buf[0] = 0;
        GetEnvironmentVariableA("KAROO_SCENEQUAD_FX", buf, sizeof(buf));
        cached = FX_OFF;
        if (lstrcmpiA(buf, "drop") == 0)      cached = FX_DROP;
        else if (lstrcmpiA(buf, "tint") == 0) cached = FX_TINT;
        log_write("scenequad: FX mode = %s (KAROO_SCENEQUAD_FX='%s')\n",
                  cached == FX_DROP ? "drop" : cached == FX_TINT ? "tint" : "off", buf);
    }
    return (QuadFx)cached;
}

bool SceneQuad_Draw(RenderDevice *dev, StridedVertices *v, uint32_t count)
{
    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= QUAD_LOG_FIRST)
        log_write("scenequad: dev=%p pos=%p tex0=%p count=%lu\n",
                  (void *)dev, v->position.data, v->texCoords[0].data,
                  (unsigned long)count);

    QuadFx fx = quad_fx();
    if (fx == FX_DROP)
        return true;
    if (fx == FX_TINT && v->diffuse.data) {
        // The caller rebuilds this quad fresh for every draw, so overwriting
        // its diffuse colour here is safe and only affects this one draw.
        for (uint32_t i = 0; i < count; i++)
            *(DWORD *)((char *)v->diffuse.data + i * v->diffuse.stride) = 0xFFFF00FF;
    }

    return dev->DrawStrided(Prim::TriangleStrip, VertexFormat::Diffuse2, v, count);
}
