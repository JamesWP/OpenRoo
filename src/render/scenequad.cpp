/* Scene_RenderSceneObjects's strided draw for kind-2 (quad) theme
 * objects, called from sceneobjects.cpp once per sub-object per visible tile.
 *
 * FVF 0x242 (XYZ | DIFFUSE | D3DFVF_TEX2) declares two texture coordinate
 * sets, but the caller's D3DDRAWPRIMITIVESTRIDEDDATA only ever fills set 0;
 * set 1 is left NULL.  A driver is entitled to read every set the FVF
 * declares, so an unfilled set is a wild read (CRASH.md).  This hook repairs that before the real draw call: every
 * declared set beyond the one the caller filled is pointed at set 0's array,
 * so it is always readable.  Only texture stage 0 is enabled for this draw, so
 * the repaired sets are never sampled -- this only stops the wild read.
 *
 * KAROO_SCENEQUAD_FX visual-proof modes (read by value, never by presence):
 *   drop  skip the draw entirely -- the animated billboard quads vanish;
 *   tint  force their vertex diffuse to magenta. */

#include "scenequad.h"
#include "com_proxy.h"
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

extern "C" __declspec(dllexport) HRESULT WINAPI
hooks_SceneQuadDrawStrided(IDirect3DDevice3 *dev, D3DPRIMITIVETYPE prim, DWORD fvf,
                           D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vert_count, DWORD flags)
{
    // Point every texture coordinate set the FVF declares, past the one the
    // caller filled, at set 0's array so the driver never reads unfilled data.
    DWORD ntex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (ntex > D3DDP_MAXTEXCOORD)
        ntex = D3DDP_MAXTEXCOORD;
    for (DWORD i = 1; i < ntex; i++)
        data->textureCoords[i] = data->textureCoords[0];

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= QUAD_LOG_FIRST)
        log_write("scenequad: dev=%p prim=%lu fvf=%03lX ntex=%lu pos=%p tex0=%p count=%lu\n",
                  dev, (DWORD)prim, fvf, ntex, data->position.lpvData,
                  data->textureCoords[0].lpvData, vert_count);

    QuadFx fx = quad_fx();
    if (fx == FX_DROP)
        return D3D_OK;
    if (fx == FX_TINT && data->diffuse.lpvData) {
        // The caller rebuilds this quad fresh for every draw, so overwriting
        // its diffuse colour here is safe and only affects this one draw.
        for (DWORD i = 0; i < vert_count; i++)
            *(DWORD *)((char *)data->diffuse.lpvData + i * data->diffuse.dwStride) = 0xFFFF00FF;
    }

    return dev->DrawPrimitiveStrided(prim, fvf, data, vert_count, flags);
}
