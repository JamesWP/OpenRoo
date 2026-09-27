/* DrawBridgeSurfaces: each bridge's deck as one scrolling textured quad, a
 * four-vertex strip (FVF 0x242: XYZ, diffuse, two texture-coordinate sets).
 * The deck's texture v scrolls by
 *     f = fmod(t * (double)0.001f / n, 1.0)      n = the deck length
 * so the surface moves along its length over time.  RenderGameFrame calls it
 * once a frame with the Game, the theme block, the Direct3D object and the
 * clock in ms.  BridgeObject::buildSurface builds each quad; this file sets
 * the render states and draws.
 *
 * Three nested loops:
 *   A. over the theme block's level objects, those of type 2 (as
 *      DrawQuadBatch), setting two render states each;
 *   B. over each one's sub-objects, binding its texture, blend and address
 *      mode;
 *   C. over every bridge.
 * PRESERVED: C does not depend on A or B, so every bridge is drawn once per
 * sub-object of every type-2 object, only the render state differing.
 * Quadratic, and surely not intended.
 *
 * On a level object, bNoZWrite gates ZWRITEENABLE and bSpecular gates
 * SPECULARENABLE, together with the Highlights video option
 * (Game::videoHighlights()).
 *
 * PRESERVED, too:
 *   - the vertices are zeroed (diffuse 0xFFFFFFFF) and then every field is
 *     overwritten;
 *   - the X-axis cases shift x by -0.5 at both ends instead of centring it,
 *     as the Z-axis cases do;
 *   - the device is re-read before every call, and both inner loop bounds
 *     every pass;
 *   - SPECULARENABLE is turned off for every type-2 object, including those
 *     that never turned it on and those with no sub-objects.
 *
 * KAROO_BRIDGE_FX, visual controls:
 *   tint      diffuse 0xFFFF00FF: only this code builds these vertices, so a
 *             magenta deck is ours;
 *   nodraw    skip the draw, leaving every render state as set;
 *   backward  negate the scroll: a direction change, so it proves the
 *             fmod and clock arithmetic. */

#include "bridgesurf.h"
#include "renderdevice.h"
#include "theme.h"
#include "game.h"
#include "bridgeobject.h"
#include "log.h"

#define BRIDGE_FVF        VertexFormat::Diffuse2
#define BRIDGE_LOG_FIRST  8

enum BridgeFxMode { BRIDGE_FX_OFF = 0, BRIDGE_FX_TINT, BRIDGE_FX_NODRAW,
                    BRIDGE_FX_BACKWARD };

static BridgeFxMode bridge_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = BRIDGE_FX_OFF;
        if (GetEnvironmentVariableA("KAROO_BRIDGE_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "tint") == 0)          cached = BRIDGE_FX_TINT;
            else if (lstrcmpiA(buf, "nodraw") == 0)   cached = BRIDGE_FX_NODRAW;
            else if (lstrcmpiA(buf, "backward") == 0) cached = BRIDGE_FX_BACKWARD;
        }
        log_write("bridgesurf: FX mode = %s\n",
                  cached == BRIDGE_FX_TINT     ? "tint" :
                  cached == BRIDGE_FX_NODRAW   ? "nodraw" :
                  cached == BRIDGE_FX_BACKWARD ? "backward" : "off");
    }
    return (BridgeFxMode)cached;
}

/* KAROO_BRIDGE_DIAG=1 logs each distinct (bridge, axis, direction, n) once
 * over the run, to show which orientations a level reaches, and so whether
 * backward reversed every one on screen. */
static void bridge_note_variant(DWORD k, int axis, int dir, int n,
                                float vnear, float vfar)
{
    static int enabled = -1;
    if (enabled < 0) {
        char b[8];
        enabled = (GetEnvironmentVariableA("KAROO_BRIDGE_DIAG", b, sizeof(b))
                   && b[0] != '0') ? 1 : 0;
    }
    if (!enabled)
        return;

    // Small and fixed; a level has at most a couple of dozen bridges.
    static DWORD seen[64];
    static int   nseen = 0;
    DWORD key = (k << 24) | ((DWORD)(axis & 0xff) << 16)
                | ((DWORD)(dir & 0xff) << 8) | (DWORD)(n & 0xff);
    for (int q = 0; q < nseen; q++)
        if (seen[q] == key)
            return;
    if (nseen >= (int)(sizeof(seen) / sizeof(seen[0])))
        return;
    seen[nseen++] = key;
    // The two v coordinates of texture set 0 are the whole animation; under
    // backward both must change sign, for every variant.
    log_write("bridgesurf: diag variant #%d cv=%lu axis=%d dir=%d n=%d "
              "(branch %s/%s) v[0]=%d/1000 v[1]=%d/1000\n",
              nseen, k, axis, dir, n,
              axis == 1 ? "X" : "Z", dir > 0 ? "fwd" : "back",
              (int)(vnear * 1000.0f), (int)(vfar * 1000.0f));
}

void BridgeSurf_Draw(Game *game, ThemeAssetBlock *theme, RenderDevice *d3d,
                     double t)
{
    Mat4 world = {};
    world.m[0] = world.m[5] = world.m[10] = world.m[15] = 1.0f;
    d3d->SetTransform(Transform::World, &world);

    const DWORD diffuse =
        bridge_fx() == BRIDGE_FX_TINT ? 0xFFFF00FF : 0xFFFFFFFF;

    ThemeObjectTypeSlot *slot = theme->slot(THEME_OBJ_BRIDGE);
    if (slot->instanceCount() == 0)
        return;

    for (DWORD i = 0; i < slot->instanceCount(); i++) {
        ThemeLevelObject *obj = &slot->records()[i];

        if (obj->kind() != THEME_KIND_FIELD)
            continue;

        if (obj->specular() != 0 && game->videoHighlights() != 0)
            d3d->SetRenderState(RS::SpecularEnable, 1);

        d3d->SetRenderState(RS::ZWriteEnable, obj->noZWrite() != 0 ? 0 : 1);

        if (obj->subObjectCount() != 0) {
            for (DWORD s = 0; s < obj->subObjectCount(); s++) {
                SceneSubObject *sub = &obj->subObjects()[s];

                // Unlike DrawQuadBatch, SetTexture runs unconditionally: a
                // NULL texture binds NULL rather than leaving the last one
                // bound.
                d3d->SetTexture(0, sub->pTexture);

                // One call, the state and value chosen by the branch.
                RS last_state;
                DWORD              last_value;
                if (sub->dwBlendSrc && sub->dwBlendDst) {
                    d3d->SetRenderState(
                        RS::AlphaBlendEnable, 1);
                    d3d->SetRenderState(RS::SrcBlend,
                                                 sub->dwBlendSrc);
                    last_state = RS::DestBlend;
                    last_value = sub->dwBlendDst;
                } else {
                    last_state = RS::AlphaBlendEnable;
                    last_value = 0;
                }
                d3d->SetRenderState(last_state, last_value);

                {
                    DWORD addr = sub->dwTexAddress ? sub->dwTexAddress : 3;
                    d3d->SetRenderState(
                        RS::TextureAddressU, addr);
                    d3d->SetRenderState(
                        RS::TextureAddressV, addr);
                }

                for (DWORD k = 0;
                     k < game->bridgeCount();
                     k++) {
                    const BridgeObject *cv = game->bridgeSlot(k);

                    // BridgeObject::buildSurface builds the vertices.
                    BridgeVertex v[4];
                    BridgeSurfaceInfo info;
                    if (!cv->buildSurface(v, t,
                                          bridge_fx() == BRIDGE_FX_BACKWARD,
                                          &info))
                        continue;
                    for (int q = 0; q < 4; q++)
                        v[q].diffuse = diffuse;

                    bool ok = true;
                    if (bridge_fx() != BRIDGE_FX_NODRAW)
                        ok = d3d->Draw(
                            Prim::TriangleStrip, BRIDGE_FVF, v, 4, 0);

                    static LONG logged = 0;
                    if (InterlockedIncrement(&logged) <= BRIDGE_LOG_FIRST)
                        log_write("bridgesurf: obj=%lu sub=%lu cv=%lu axis=%d "
                                  "dir=%d n=%d len=%d f=%d/1000 -> ok=%d\n",
                                  i, s, k, info.axis, info.dir, info.n,
                                  (int)(info.len * 1000.0f),
                                  (int)(info.f * 1000.0f), ok);

                    bridge_note_variant(k, info.axis, info.dir, info.n,
                                        v[0].v0, v[1].v0);
                }
            }
        }

        d3d->SetRenderState(RS::SpecularEnable, 0);
    }
}
