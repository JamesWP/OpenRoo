/* SkyBackground::DrawSkyBackground (0x43cc00) reimplementation.
 *
 * __thiscall(this, IDirect3DDevice3 *dev, DWORD, DWORD, DWORD) -> float*.
 *
 * ── The signature, and how getting it wrong presents ───────────────────────
 * The decompile shows ONE stack parameter.  It is wrong: the original ends
 * `ret $0x10`, so it pops FOUR stack dwords.  The call site (0x427909) does
 * `SUB ESP,0xc` and fills three dwords from the globals 0x46c4a0/a4/a8 -- a
 * 12-byte struct passed by value -- and then pushes the device, so the stack
 * layout is (device, g0, g1, g2).  The function body never reads the three;
 * they still have to be popped.
 *
 * Declaring only (this, dev) built cleanly and ran, then killed the process
 * inside the first frame: each call left 12 bytes of stack behind until the
 * return path walked into `page fault on read access to 00000000`.  A no-op
 * body crashed identically, which is what proved it was the frame and not the
 * drawing.  Always read the original's `ret N` -- the decompiler's parameter
 * list is not evidence.
 * 522 bytes, five D3D dispatches, one E8 call site (0x42792A in
 * RenderGameFrame).
 *
 *   SetRenderState(D3DRENDERSTATE_ZENABLE, 0)      // sky ignores the z-buffer
 *   build a Y-rotation matrix from this->flYawAngle into this->WorldMatrix
 *   SetTransform(D3DTRANSFORMSTATE_WORLD, this->WorldMatrix)
 *   for i in 0..5:
 *       SetTexture(0, this->Textures[i].pTexture2)
 *       DrawPrimitive(D3DPT_TRIANGLESTRIP, 0x1e2, &this->QuadVerts[i*0x80],
 *                     4, D3DDP_DONOTUPDATEEXTENTS)
 *   SetRenderState(D3DRENDERSTATE_ZENABLE, 1)
 *
 * ── Establishing the layout ────────────────────────────────────────────────
 * Ghidra's decompile is unusable as written: it addresses the object through
 * `unaff_EBP` because the function reloads EBP from a saved copy of `this`
 * (`mov 0x14(%esp),%ebp` at 0x43cd91) and the decompiler does not connect the
 * two.  Typing `this` as SkyBackground (done in the Ghidra project) recovers
 * this->flYawAngle but not the EBP-based accesses.  EBP == this is confirmed
 * three independent ways:
 *   1. The struct tiles exactly: Textures end at 0xb0, the six 0x80-byte quad
 *      blocks run 0xb0..0x3b0, and the matrix sits at 0x3b0 — no gaps.
 *   2. `lea 0x20(%ebp),%esi` with a 0x1c stride matches HOOKS.md's
 *      independently-derived "six SceneTexture at this+0x08, stride 0x1C"
 *      (0x08 + 0x18 = 0x20 is each one's pTexture2).
 *   3. EBP is loaded from the stack slot holding `this`.
 *
 * ── A wrong call I made here, and the correction ──────────────────────────
 * The decompile renders the translation write as `uStack_9c = uStack_4`, which
 * looks like an uninitialised stack value landing in _41 of the second matrix.
 * I first dismissed it as a decompiler artifact and dropped it.  That produced
 * a visible skybox glitch (James spotted it) while the replay tests still
 * passed -- they assert game state, not pixels.
 *
 * It is real.  The original loads three consecutive dwords from the caller's
 * by-value struct (0x43ccd8/e6/f3: [esp+0xe4], [esp+0xe8], [esp+0xec]) and
 * stores them at offsets 0x30/0x34/0x38 of the matrix built at esp+0x1c --
 * i.e. _41/_42/_43, the translation row.  Ghidra mis-assigned the slots
 * because ESP moves between the lea and the stores.
 *
 * So the world matrix is Yrot * Translate(centre): the skybox is translated to
 * follow the viewer.  Dropping the translation leaves the sky pinned at the
 * world origin, which is exactly the glitch.
 *
 * Note the rotation is the transpose of the usual D3D Y-rotation: the original
 * sets _13 = +sin and _31 = -sin (from afStack_8c[2] and the -0x6c slot).
 * Reproduced as-is rather than "corrected".
 *
 * KAROO_SKY_FX=noskip draws only the first of the six sky quads, leaving the
 * rest of the sky absent — visual proof the sky pixels come from this code.
 */
#include "direct3d.h"
#include "sky.h"
#include "log.h"

#include <math.h>

#define SKY_FVF        0x1e2
#define SKY_QUADS      6
#define SKY_QUAD_BYTES 0x80   /* 4 verts * 32-byte FVF 0x1e2 stride */
#define SKY_LOG_FIRST  8

static bool fx_one_quad(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_SKY_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "noskip") == 0);
        log_write("sky: FX mode = %s\n", cached ? "noskip (1 quad only)" : "off");
    }
    return cached != 0;
}

extern "C" __declspec(dllexport) float * __attribute__((thiscall))
Sky_DrawSkyBackground(SkyBackground *self, IDirect3DDevice3 *dev,
                      float flCentreX, float flCentreY, float flCentreZ)
{
    dev->SetRenderState(D3DRENDERSTATE_ZENABLE, 0);

    const float c = (float)cos(self->flYawAngle);
    const float s = (float)sin(self->flYawAngle);

    float *m = self->WorldMatrix;
    for (int i = 0; i < 16; i++)
        m[i] = 0.0f;
    m[0]  = c;   m[2]  = s;     /* _11, _13 */
    m[5]  = 1.0f;               /* _22      */
    m[8]  = -s;  m[10] = c;     /* _31, _33 */
    m[15] = 1.0f;               /* _44      */

    /* Translation row: the three by-value floats the caller passes.  The
     * original writes them into _41/_42/_43 of the second matrix and then
     * computes Yrot * T; since Yrot's last row is (0,0,0,1) and T's upper 3x3
     * is identity, that product is exactly this rotation with the translation
     * row copied in, so it is written directly. */
    m[12] = flCentreX;          /* _41 */
    m[13] = flCentreY;          /* _42 */
    m[14] = flCentreZ;          /* _43 */

    dev->SetTransform(D3DTRANSFORMSTATE_WORLD, (D3DMATRIX *)m);

    const int nquads = fx_one_quad() ? 1 : SKY_QUADS;
    for (int i = 0; i < nquads; i++) {
        IDirect3DTexture2 *tex = self->Textures[i].pTexture2;
        dev->SetTexture(0, tex);
        HRESULT hr = dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, SKY_FVF,
                                        self->QuadVerts + i * SKY_QUAD_BYTES,
                                        4, 8);

        static LONG logged = 0;
        if (InterlockedIncrement(&logged) <= SKY_LOG_FIRST)
            log_write("sky: quad %d tex=%p yaw=%d/1000 -> hr=%08lX\n",
                      i, (void *)tex, (int)(self->flYawAngle * 1000.0f), hr);
    }

    dev->SetRenderState(D3DRENDERSTATE_ZENABLE, 1);
    return self->WorldMatrix;
}
