#pragma once
#include "renderdevice.h"   /* LoadedImage (the 24-byte base variant) */

/* SceneTexture — the 28-byte extended LoadedImage variant: the base struct
 * plus an IDirect3DTexture2 at +0x18.  See HOOKS.md § LoadedImage struct —
 * two sizes.  The two variants really are distinct objects: CreateSurfaceDIB
 * and friends operate on 24-byte instances that have no +0x18 slot at all, so
 * anything touching pTexture2 must be handed the extended type. */
struct SceneTexture {
    LoadedImage        base;       // +0x00 (24 bytes)
    IDirect3DTexture2 *pTexture2;  // +0x18
};

static_assert(offsetof(SceneTexture, pTexture2) == 0x18, "SceneTexture layout");

/* textures\shadow.tga and textures\karoo128.tga, loaded once at startup
 * (renderstate.cpp). */
extern SceneTexture g_texShadow;
extern SceneTexture g_texKaroo128;
static_assert(sizeof(SceneTexture) == 0x1c, "SceneTexture stride mismatch");

/* ─── texture.cpp's exports other files call (COHESION_PLAN template 10) ───
 *
 * The LoadedImage ctor/dtor family.  scenetexture.cpp's SceneTexture family
 * is the only outside caller: its Constructor chains to the base ctor and its
 * DtorBody tail-calls the base dtor body, exactly as the originals do. */
extern "C" __declspec(dllexport) LoadedImage *__attribute__((thiscall))
Texture_ImageCtor(LoadedImage *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Texture_ImageDtorBody(LoadedImage *self);
extern "C" __declspec(dllexport) LoadedImage *__attribute__((thiscall))
Texture_ImageScalarDtor(LoadedImage *self, unsigned int flags);

/* Restore a lost surface and reload its image.
 * TextureManager_LoadAll (scenetexture.cpp) is the outside caller. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_Load(LoadedImage *self);

/* Release the IDirect3DTexture2 and both surfaces. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Texture_ReleaseD3DTexture(SceneTexture *self);

/* A palette from the DIB's colour table.  scenetexture.cpp's
 * BindTextureResource is the only caller. */
extern "C" __declspec(dllexport) IDirectDrawPalette *__stdcall
Texture_CreatePaletteFromDIB(IDirectDraw4 *dd, HBITMAP hbmp);

/* The vtable pointer LoadedImage's ctor and dtor body install, and the same
 * question for SceneTexture: our own one-slot table in this DLL. */
extern "C" __declspec(dllexport) void *Texture_ImageVtable(void);

/* KAROO_IMAGE_DIAG's first-call announcement, shared so the census covers all
 * six ctor/dtor entry points through one implementation. */
extern "C" __declspec(dllexport) void Texture_ImageFirstCall(const char *who,
                                                             unsigned long *seen);
