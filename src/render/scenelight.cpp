/* SceneSpotLight::CreateSceneLight / ReleaseSceneLight reimplementation.
 *
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
#include "scenelight.h"
#include "direct3d.h"
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

} // extern "C"
