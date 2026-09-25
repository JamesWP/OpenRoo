/* SceneMaterial -- the whole class.
 *
 *   0x42d690 Construct          __thiscall(this) -> this, ret 0
 *   0x42d720 DestructBody       __thiscall(this) -> void: vtable, then Release
 *   0x42d700 ScalarDtor         __thiscall(this, flags) -> this, ret 4
 *   0x42d760 CreateSceneMaterial
 *       __thiscall(this, IDirect3D3 *, IDirect3DDevice3 *) -> uint, ret 8
 *   0x42d730 ReleaseSceneMaterial
 *       __thiscall(this) -> void, ret 0
 *
 * ── The signature ──────────────────────────────────────────────────────────
 * The decompile shows ONE stack parameter plus a phantom `unaff_ESI` for the
 * device.  The original ends `ret $0x8`: two stack args.  The call site
 * (0x4266c5) does `PUSH ECX` (g_pDirect3D->pDevice) then `PUSH EDX`
 * (g_pDirect3D->pD3D), so the order is (this, pD3D, pDevice).  RENDER_PLAN's
 * "__thiscall(this, IDirect3D3*)" was incomplete.  Read the ret, not the
 * decompiler's parameter list.
 *
 * ── The return value ───────────────────────────────────────────────────────
 * The original returns a bool in AL but leaves the upper three bytes of EAX
 * holding whatever the last HRESULT had there:
 *     failure -> hr & 0xffffff00        (low byte 0)
 *     success -> (hr_setmaterial & 0xffffff00) | 1
 * That is reproduced exactly rather than cleaned up to a plain bool: callers
 * only test AL, but the garbage upper bytes are what the original produces.
 *
 * Release is reached both by an E8 from Create and by an E9 tail-jump from the
 * destructor thunk at 0x42d720; patch.py covers the latter via JMP_PATCHES.
 * Unlike the light, Release here NULLs pMaterial unconditionally.
 */
#include <string.h>
#include "scenematerial.h"
#include "log.h"
#include <stdlib.h>

/* FactAlloc::Free2 — __cdecl(void *), confirmed from the call site
 * (0x42d74e: push eax / call 0x4504c0 / add esp,4).  The original is left
 * intact in the binary, as factory.cpp already does for the factories. */

extern "C" {

__declspec(dllexport) void __attribute__((thiscall))
SceneMaterial_Release(SceneMaterial *self)
{
    IDirect3DMaterial3 *p = self->pMaterial;
    if (p != NULL)
        p->Release();
    self->pMaterial = NULL;          /* unconditional, unlike the light */

    if (self->pHeapData != NULL)
        free(self->pHeapData);
    self->pHeapData = NULL;
}

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

/* ─── Construction and teardown (ENDGAME E4/E5) ────────────────────────────
 *
 * The one instance is the global at 0x4e0390, built by the static-init thunk
 * at 0x425db0 and torn down by the atexit thunk at 0x425dd0 (tail JMPs,
 * JMP_PATCHES).  Same shape as the light: our own one-slot vtable, the
 * game's 0x45d4e8 left as a tripwire, the scalar dtor unreached.
 *
 * The ctor zeroes the D3DMATERIAL and sets dwSize 80, diffuse and ambient
 * (0.5, 0.5, 0.5, 1), specular (1, 1, 1, 1), power 0, dwRampSize 1.
 * pHeapData stays game-heap: nothing in the image writes it by absolute
 * address, but a pointer-relative writer is not ruled out -- ENDGAME_PLAN's
 * alloc.h table keeps it until that audit is done. */
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

} // extern "C"
