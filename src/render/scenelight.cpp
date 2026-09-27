/* SceneSpotLight -- the whole class (scenelight.h). */

#include <math.h>
#include <string.h>
#include "scenelight.h"
#include "direct3d.h"
#include <stdlib.h>
#include "log.h"
SceneSpotLight g_light;

extern "C" {

__declspec(dllexport) void __attribute__((thiscall))
SceneLight_Release(SceneSpotLight *self)
{
    // Unlike SceneMaterial_Release, pLight is NULLed only inside the null
    // check (indistinguishable here), and bNotInScene is set whether or not a
    // light existed.
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

    IDirect3D3 *pD3D = d3d->pD3D;
    HRESULT hr = pD3D->CreateLight(&self->pLight, NULL);

    log_write("scenelight: Create this=%p d3d=%p pD3D=%p -> hr=%08lX light=%p\n",
              self, d3d, (void *)pD3D, hr, (void *)self->pLight);
    return hr >= 0;
}

/* ─── Construction and teardown ─────────────────────────────────────────────
 *
 * The one instance is g_light, built and torn down by staticinit.cpp.  The
 * scalar dtor is the one vtable slot; nothing deletes a global, so its free is
 * never reached.
 *
 * The ctor zeroes the D3DLIGHT2 and then sets: dwSize 80, dvRange
 * sqrt(FLT_MAX) (= D3DLIGHT_RANGE_MAX), falloff 1, attenuation0 1,
 * attenuation1/2 0 (already zero), flags 1 (D3DLIGHT_ACTIVE).  dltType is left
 * 0; renderstate.cpp fills it. */
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
        free(self);
    return self;
}

}  // extern "C"
