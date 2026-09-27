/* SceneMaterial -- the whole class (scenematerial.h). */

#include <string.h>
#include "scenematerial.h"
#include "log.h"
#include <stdlib.h>
SceneMaterial g_material;

extern "C" {

__declspec(dllexport) void __attribute__((thiscall))
SceneMaterial_Release(SceneMaterial *self)
{
    IDirect3DMaterial3 *p = self->pMaterial;
    if (p != NULL)
        p->Release();
    self->pMaterial = NULL;  // unconditional, unlike the light

    if (self->pHeapData != NULL)
        free(self->pHeapData);
    self->pHeapData = NULL;
}

/* Only the low byte is the result; callers test nothing else.  The upper three
 * bytes are the last HRESULT's: hr & 0xffffff00 on failure, (SetMaterial's hr
 * & 0xffffff00) | 1 on success. */
__declspec(dllexport) unsigned int __attribute__((thiscall))
SceneMaterial_Create(SceneMaterial *self, IDirect3D3 *pD3D,
                     IDirect3DDevice3 *pDevice)
{
    SceneMaterial_Release(self);

    HRESULT hr = pD3D->CreateMaterial(&self->pMaterial, NULL);
    if (hr < 0) {
        log_write("scenematerial: CreateMaterial failed hr=%08lX\n", hr);
        return (unsigned int)hr & 0xffffff00u;
    }

    hr = self->pMaterial->GetHandle(pDevice, &self->hMaterial);
    if (hr < 0) {
        log_write("scenematerial: GetHandle failed hr=%08lX\n", hr);
        return (unsigned int)hr & 0xffffff00u;
    }

    HRESULT hrSet = self->pMaterial->SetMaterial(&self->mat);
    log_write("scenematerial: Create this=%p pD3D=%p dev=%p mat=%p handle=%08lX "
              "setmaterial=%08lX\n",
              self, (void *)pD3D, (void *)pDevice, (void *)self->pMaterial,
              (unsigned long)self->hMaterial, hrSet);

    return ((unsigned int)hrSet & 0xffffff00u) | 1u;
}

/* ─── Construction and teardown ─────────────────────────────────────────────
 *
 * The one instance is g_material, built and torn down by staticinit.cpp.  The
 * scalar dtor is the one vtable slot and is never reached.
 *
 * The ctor zeroes the D3DMATERIAL and sets dwSize 80, diffuse and ambient
 * (0.5, 0.5, 0.5, 1), specular (1, 1, 1, 1), power 0, dwRampSize 1. */
SceneMaterial *__attribute__((thiscall)) SceneMaterial_ScalarDtor(SceneMaterial *self,
                                                                   unsigned char flags);

static void *const g_SceneMaterialVtable[1] = { (void *)&SceneMaterial_ScalarDtor };

__declspec(dllexport) SceneMaterial *__attribute__((thiscall))
SceneMaterial_Construct(SceneMaterial *self)
{
    self->pVtable   = (void *)g_SceneMaterialVtable;
    self->pHeapData = NULL;
    self->pMaterial = NULL;
    self->hMaterial = 0;
    memset(&self->mat, 0, sizeof self->mat);
    self->mat.dwSize = sizeof(D3DMATERIAL);
    self->mat.diffuse.a  = 1.0f;
    self->mat.ambient.a  = 1.0f;
    self->mat.specular.r = self->mat.specular.g =
    self->mat.specular.b = self->mat.specular.a = 1.0f;
    self->mat.power = 0.0f;
    self->mat.diffuse.r = self->mat.diffuse.g = self->mat.diffuse.b = 0.5f;
    self->mat.ambient.r = self->mat.ambient.g = self->mat.ambient.b = 0.5f;
    self->mat.dwRampSize = 1;
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
SceneMaterial_DtorBody(SceneMaterial *self)
{
    self->pVtable = (void *)g_SceneMaterialVtable;
    SceneMaterial_Release(self);
}

__declspec(dllexport) SceneMaterial *__attribute__((thiscall))
SceneMaterial_ScalarDtor(SceneMaterial *self, unsigned char flags)
{
    SceneMaterial_DtorBody(self);
    if (flags & 1)
        free(self);
    return self;
}

}  // extern "C"
