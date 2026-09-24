/* SceneSpotLight -- the whole class.
 *
 *   0x425380 Construct          __thiscall(this) -> this, ret 0
 *   0x4253f0 DestructBody       __thiscall(this) -> void: vtable, then Release
 *   0x4253d0 ScalarDtor         __thiscall(this, flags) -> this, ret 4
 *   0x425400 CreateSceneLight   __thiscall(this, Direct3D *) -> bool, ret 4
 *   0x425430 ReleaseSceneLight  __thiscall(this)             -> void, ret 0
 *
 * Both signatures were checked against the originals' `ret N` rather than
 * taken from the decompiler's parameter list (see sky.cpp for why that
 * distinction cost a debugging session).
 *
 * Release is reached two ways: a direct E8 from CreateSceneLight, and an E9
 * tail-jump from the destructor thunk at 0x4253f0, which sets the vtable
 * pointer and falls straight into it.  patch.py handles the second through
 * JMP_PATCHES; CALL_PATCHES alone would leave the destructor running the
 * UD2-stubbed original.
 *
 * Two details preserved deliberately:
 *   - Release NULLs pLight only *inside* the null check, so a already-NULL
 *     pointer is left as it is (indistinguishable here, but the material
 *     version does it unconditionally — the asymmetry is the original's).
 *   - bNotInScene is set to 1 unconditionally, whether or not a light existed.
 */
#include <math.h>
#include <string.h>
#include "scenelight.h"
#include "direct3d.h"
#include "alloc.h"
#include "log.h"

extern "C" {

__declspec(dllexport) void __attribute__((thiscall))
SceneLight_Release(SceneSpotLight *self)
{
    IDirect3DLight *p = self->pLight;
    if (p != NULL) {
        p->Release();
        self->pLight = NULL;
    }
    self->bNotInScene = 1;
}

__declspec(dllexport) bool __attribute__((thiscall))
SceneLight_Create(SceneSpotLight *self, Direct3D *d3d)
{
    SceneLight_Release(self);

    IDirect3D3 *pD3D = d3d->pD3D;          /* *(param_1 + 4) */
    HRESULT hr = pD3D->CreateLight(&self->pLight, NULL);

    log_write("scenelight: Create this=%p d3d=%p pD3D=%p -> hr=%08lX light=%p\n",
              self, d3d, (void *)pD3D, hr, (void *)self->pLight);
    return hr >= 0;
}

/* ─── Construction and teardown (ENDGAME E4/E5) ────────────────────────────
 *
 * The one instance is the global at 0x46c830, built by the static-init thunk
 * at 0x425df0 and torn down by the atexit thunk at 0x425e10 -- both tail JMPs
 * with ECX loaded (JMP_PATCHES).  The scalar dtor is vtable slot 0 only and
 * nothing deletes a global, so its free is reimplemented but unreached -- on
 * the game heap, the LinkedList precedent.  Our own one-slot vtable; the
 * game's 0x45d454 is left pointing at the UD2 as a tripwire.
 *
 * The ctor zeroes the D3DLIGHT2 and then sets: dwSize 80, dvRange
 * sqrt(FLT_MAX) (= D3DLIGHT_RANGE_MAX; the original is FLD double / FSQRT /
 * FSTP float, and the double is exactly FLT_MAX so the float result is the
 * same), falloff 1, attenuation0 1, attenuation1/2 0 (already zero --
 * redundant stores in the original), flags 1 (D3DLIGHT_ACTIVE).  dltType is
 * left 0; CreateSceneLight's callers fill it. */
SceneSpotLight *__attribute__((thiscall)) SceneLight_ScalarDtor(SceneSpotLight *self,
                                                                 unsigned char flags);

static void *const g_SceneLightVtable[1] = { (void *)&SceneLight_ScalarDtor };

__declspec(dllexport) SceneSpotLight *__attribute__((thiscall))
SceneLight_Construct(SceneSpotLight *self)
{
    self->pVtable = (void *)g_SceneLightVtable;
    memset(&self->light, 0, sizeof self->light);
    self->light.dwSize        = sizeof(D3DLIGHT2);
    self->light.dvRange       = (float)sqrt(3.4028234663852886e38);
    self->light.dvFalloff     = 1.0f;
    self->light.dvAttenuation0 = 1.0f;
    self->light.dvAttenuation1 = 0.0f;
    self->light.dvAttenuation2 = 0.0f;
    self->pLight      = NULL;
    self->light.dwFlags = 1;
    self->bNotInScene = 1;
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
SceneLight_DtorBody(SceneSpotLight *self)
{
    self->pVtable = (void *)g_SceneLightVtable;
    SceneLight_Release(self);
}

__declspec(dllexport) SceneSpotLight *__attribute__((thiscall))
SceneLight_ScalarDtor(SceneSpotLight *self, unsigned char flags)
{
    SceneLight_DtorBody(self);
    if (flags & 1)
        game_free2(self);
    return self;
}

} // extern "C"
