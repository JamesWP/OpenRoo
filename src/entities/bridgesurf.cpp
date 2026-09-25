/* DrawBridgeSurfaces (0x408a00) reimplementation.
 *
 * __cdecl(Game *game, void *lvl, Direct3D *d3d, double t) — 0xbe5 bytes, one
 * E8 call site (0x4284B9 in RenderGameFrame).  The plan's estimate of "768
 * bytes, 12 dispatches" was low: it is ~3045 bytes, and the arguments are
 * *five* dwords, the last two being one double — not the three the closure
 * table listed.  The call site pushes, in order, EBP, EBX, EAX, 0x46c890,
 * [0x46c498]; the `add esp,0x14` after it confirms five dwords, __cdecl.
 *
 *   arg1  [0x46c498]   the Game singleton   (huge offsets: +0x170643 etc.)
 *   arg2  0x46c890     the level-object blob quadbatch/meshbatch also get
 *   arg3  [0x4e04ac]   g_pDirect3D
 *   arg4  EBX:EBP      a double — the animation clock, in milliseconds
 *
 * What it draws: one scrolling textured quad per "conveyor" object, as a
 * 4-vertex TRIANGLESTRIP with FVF 0x242 (XYZ | DIFFUSE | two texture
 * coordinate sets, so a 32-byte vertex).  The v coordinate of set 0 is
 * offset by
 *
 *     f = fmod(t * (double)0.001f / n, 1.0)          n = (int8)cv[0x45]
 *
 * so the surface scrolls along its length over time.  The quad runs from the
 * object's anchor point a to a distance `len` away, where len is the distance
 * to a second stored point b; it is one unit wide (+/- 0.5) across.
 *
 * Three loops, and the outer two are exactly what the Ghidra decompile gets
 * wrong (it models the whole frame off `local_40`, a phantom, and puts the
 * loop bases inside the world matrix).  These come from the disassembly:
 *
 *   A. over the 0x5dd-byte LevelObject array in `lvl` — count at +0x6c9b8,
 *      objects at +0x6c9bc.  Gated on obj->dwType == 2, exactly as
 *      DrawQuadBatch is.  Two per-object render states, then...
 *   B. over that object's SceneSubObject array — the same +0x3c1 count /
 *      +0x3c5 entries / 0x3c stride levelobject.h already declares, and the
 *      same four fields (pTexture, dwBlendSrc, dwBlendDst, dwTexAddress).
 *      The cursor in the binary is sub+0x0c, which is why the decompile
 *      shows pDVar13[-2] for pTexture.
 *   C. over game->field_0x170643[] (count byte at +0x170a43) — the pointer
 *      array HOOKS.md lists as the "switch trigger" objects.
 *
 * Loop C does not depend on A or B at all: every conveyor is redrawn once per
 * sub-object of every quad-batch object, with only the render state differing.
 * That is quadratic and almost certainly not what was intended, but it is
 * what the binary does, so it is reproduced.
 *
 * Loop C's objects are BridgeObjects (bridgeobject.h), and since COHESION
 * Band 4a the per-bridge vertex build is BridgeObject::buildSurface -- this
 * file keeps the render states and the draw.  The fields it reads, from the
 * disassembly:
 *   +0x25 float[3]  b — the far point (z is negated on read)
 *   +0x39 float[3]  a — the anchor    (z is negated on read)
 *   +0x45 int8      n — scroll divisor and texture-repeat multiplier
 *   +0x53 dword     draw gate (this or +0x58 non-zero)
 *   +0x57 int8      direction: > 0 extends forwards, <= 0 backwards
 *   +0x58 dword     draw gate
 *   +0x60 int8      axis: == 1 runs along X, otherwise along Z
 * And on a LevelObject: +0x5ad gates ZWRITEENABLE, +0x5b5 gates
 * SPECULARENABLE (together with the "Highlights" video option,
 * Game +0x2aa138 -- Game::videoHighlights(), config.h).
 *
 * Preserved oddities, deliberately not cleaned up:
 *   - The identity world matrix is built into scratch by the game's matrix
 *     helper and then *copied* to a second buffer before SetTransform.  We
 *     build one identity matrix; the bytes handed to D3D are the same.
 *   - The four vertices are zero-initialised (diffuse to 0xFFFFFFFF) and then
 *     every one of the 32 fields is overwritten in every branch.  The
 *     zero-init is dead, and is kept.
 *   - `az` is the *negated* stored z, and the quad is built around az, but
 *     the X-axis cases shift x by -0.5 at both ends instead of centring it
 *     the way the Z-axis cases do.  Asymmetric; preserved.
 *   - The device pointer is re-read from d3d before every dispatch.
 *   - Both inner loop bounds are re-read from memory each iteration.
 *   - SPECULARENABLE is turned *off* for every dwType==2 object, including
 *     ones that never turned it on, and ones with no sub-objects at all.
 *
 * The device is the com_proxy device proxy; calls go through it deliberately.
 *
 * KAROO_BRIDGE_FX visual-proof modes (read by value, never by presence):
 *   tint     — diffuse 0xFFFF00FF instead of 0xFFFFFFFF.  Only this function
 *              builds these vertices, so a magenta conveyor is ours.
 *   nodraw   — skip the DrawPrimitive, leaving every render state untouched.
 *   backward — negate the scroll phase f.  A *direction* change: it proves
 *              the fmod/clock arithmetic, which a colour cannot.
 */
#include "bridgesurf.h"
#include "direct3d.h"
#include "levelobject.h"
#include "game.h"
#include "bridgeobject.h"
#include "log.h"

#define BRIDGE_FVF        0x242
#define BRIDGE_LOG_FIRST  8

/* Bases inside the two argument blobs. */
#define LVL_OFF_BRIDGE_COUNT    0x6c9b8
#define LVL_OFF_BRIDGE_OBJECTS  0x6c9bc
#define LOBJ_OFF_ZWRITE_GATE    0x5ad
#define LOBJ_OFF_SPECULAR_GATE  0x5b5

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

/* KAROO_BRIDGE_DIAG=1 — which of the four orientation branches does a level
 * actually reach?  The first-8-draws log answers that for the first frame
 * only, which is not enough: the acceptance question "did every orientation
 * reverse under KAROO_BRIDGE_FX=backward" cannot be answered without knowing
 * how many orientations were on screen at all.  This logs each distinct
 * (conveyor, axis, dir, n) combination once, over the whole run. */
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

    /* Small fixed table; a level has at most a couple of dozen conveyors. */
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
    /* The two v coordinates of texture set 0 are the whole animation: if
     * KAROO_BRIDGE_FX=backward reverses the scroll, both must change sign
     * here, for every variant. */
    log_write("bridgesurf: diag variant #%d cv=%lu axis=%d dir=%d n=%d "
              "(branch %s/%s) v[0]=%d/1000 v[1]=%d/1000\n",
              nseen, k, axis, dir, n,
              axis == 1 ? "X" : "Z", dir > 0 ? "fwd" : "back",
              (int)(vnear * 1000.0f), (int)(vfar * 1000.0f));
}

extern "C" __declspec(dllexport) void __cdecl
Direct3D_DrawBridgeSurfaces(Game *game, void *lvl, Direct3D *d3d, double t)
{
    D3DMATRIX world;
    ZeroMemory(&world, sizeof(world));
    world._11 = world._22 = world._33 = world._44 = 1.0f;
    d3d->pDevice->SetTransform(D3DTRANSFORMSTATE_WORLD, &world);

    const DWORD diffuse =
        bridge_fx() == BRIDGE_FX_TINT ? 0xFFFF00FF : 0xFFFFFFFF;

    if (*(DWORD *)((BYTE *)lvl + LVL_OFF_BRIDGE_COUNT) == 0)
        return;

    for (DWORD i = 0;
         i < *(DWORD *)((BYTE *)lvl + LVL_OFF_BRIDGE_COUNT); i++) {
        BYTE *obj = (BYTE *)lvl + LVL_OFF_BRIDGE_OBJECTS + i * LOBJ_STRIDE;

        if (*(DWORD *)(obj + LOBJ_OFF_DRAWKIND) != 2)
            continue;

        if (*(DWORD *)(obj + LOBJ_OFF_SPECULAR_GATE) != 0
            && game->videoHighlights() != 0)
            d3d->pDevice->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 1);

        d3d->pDevice->SetRenderState(
            D3DRENDERSTATE_ZWRITEENABLE,
            *(DWORD *)(obj + LOBJ_OFF_ZWRITE_GATE) != 0 ? 0 : 1);

        if (*(DWORD *)(obj + LOBJ_OFF_SUBOBJCOUNT) != 0) {
            for (DWORD s = 0;
                 s < *(DWORD *)(obj + LOBJ_OFF_SUBOBJCOUNT); s++) {
                SceneSubObject *sub =
                    (SceneSubObject *)(obj + LOBJ_OFF_SUBOBJECTS) + s;

                /* Unlike DrawQuadBatch, SetTexture happens unconditionally
                 * here: a null pTexture binds NULL rather than leaving the
                 * previously bound texture in place. */
                d3d->pDevice->SetTexture(
                    0, sub->pTexture
                       ? sub->pTexture->pTexture2
                       : NULL);

                /* One tail call in the original, state/value by the branch. */
                D3DRENDERSTATETYPE last_state;
                DWORD              last_value;
                if (sub->dwBlendSrc && sub->dwBlendDst) {
                    d3d->pDevice->SetRenderState(
                        D3DRENDERSTATE_ALPHABLENDENABLE, 1);
                    d3d->pDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND,
                                                 sub->dwBlendSrc);
                    last_state = D3DRENDERSTATE_DESTBLEND;
                    last_value = sub->dwBlendDst;
                } else {
                    last_state = D3DRENDERSTATE_ALPHABLENDENABLE;
                    last_value = 0;
                }
                d3d->pDevice->SetRenderState(last_state, last_value);

                {
                    DWORD addr = sub->dwTexAddress ? sub->dwTexAddress : 3;
                    d3d->pDevice->SetRenderState(
                        D3DRENDERSTATE_TEXTUREADDRESSU, addr);
                    d3d->pDevice->SetRenderState(
                        D3DRENDERSTATE_TEXTUREADDRESSV, addr);
                }

                for (DWORD k = 0;
                     k < game->bridgeCount();
                     k++) {
                    const BridgeObject *cv = game->bridgeSlot(k);

                    /* The vertex build is the bridge's own -- see
                     * BridgeObject::buildSurface. */
                    BridgeVertex v[4];
                    BridgeSurfaceInfo info;
                    if (!cv->buildSurface(v, t,
                                          bridge_fx() == BRIDGE_FX_BACKWARD,
                                          &info))
                        continue;
                    for (int q = 0; q < 4; q++)
                        v[q].diffuse = diffuse;

                    HRESULT hr = S_OK;
                    if (bridge_fx() != BRIDGE_FX_NODRAW)
                        hr = d3d->pDevice->DrawPrimitive(
                            D3DPT_TRIANGLESTRIP, BRIDGE_FVF, v, 4, 0);

                    static LONG logged = 0;
                    if (InterlockedIncrement(&logged) <= BRIDGE_LOG_FIRST)
                        log_write("bridgesurf: obj=%lu sub=%lu cv=%lu axis=%d "
                                  "dir=%d n=%d len=%d f=%d/1000 -> hr=%08lX\n",
                                  i, s, k, info.axis, info.dir, info.n,
                                  (int)(info.len * 1000.0f),
                                  (int)(info.f * 1000.0f), hr);

                    bridge_note_variant(k, info.axis, info.dir, info.n,
                                        v[0].v0, v[1].v0);
                }
            }
        }

        d3d->pDevice->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 0);
    }
}
