#pragma once
#include <windows.h>

/* The backend objects a texture holds; only the texture files look inside. */
struct IDirectDraw4;
struct IDirectDrawSurface4;
struct IDirectDrawPalette;
struct IDirect3DTexture2;

/* LoadedImage: a bitmap on a DirectDraw surface, as RenderDevice::
 * PresentImage takes it. */
class LoadedImage;
class RenderDevice;

/* The two loaders that fill an image's surface, each in its own module
 * (texturedib.cpp, texturetga.cpp); friends of the class they fill. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_BlitToSurface(LoadedImage *self, HANDLE hbmp);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_CreateSurface(LoadedImage *self, RenderDevice *dev, LPCSTR name,
                         char bSysMem);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureTGA_Parse(LoadedImage *self, LPCSTR path);

class __attribute__((packed)) LoadedImage {
public:
    /* ─── texture.cpp's exports other files call (COHESION_PLAN template 10) ───
     *
     * The LoadedImage ctor/dtor family.  scenetexture.cpp's SceneTexture family
     * is the only outside caller: its Constructor chains to the base ctor and its
     * DtorBody tail-calls the base dtor body, exactly as the originals do. */
    LoadedImage *construct();

    void dtorBody();
    static LoadedImage * __attribute__((thiscall))
    scalarDtor(LoadedImage *self, unsigned int flags);

    /* Restore a lost surface and reload its image.
     * TextureManager_LoadAll (scenetexture.cpp) is the outside caller. */
    unsigned int load();

    /* The vtable pointer LoadedImage's ctor and dtor body install, and the same
     * question for SceneTexture: our own one-slot table in this DLL. */
    static void *vtbl();

    void                *vtable() const { return unknown00_; }
    IDirectDrawSurface4 *textureSurface() const { return pTextureSurface_; }
    char                *imageName() const { return ImageName_; }

    void releaseSurfaces();

private:
    friend class SceneTexture;
    friend unsigned int __attribute__((thiscall))
    TextureDIB_BlitToSurface(LoadedImage *self, HANDLE hbmp);
    friend unsigned int __attribute__((thiscall))
    TextureDIB_CreateSurface(LoadedImage *self, RenderDevice *dev, LPCSTR name,
                             char bSysMem);
    friend unsigned int __attribute__((thiscall))
    TextureTGA_Parse(LoadedImage *self, LPCSTR path);

    void                *unknown00_;
    IDirectDrawSurface4 *pTextureSurface_;
    IDirectDrawSurface4 *pTexturePalette_;
    char                *ImageName_;
    int                  loadStatus_;
    int                  loadedState_;
};

/* A palette from the DIB's colour table.  scenetexture.cpp's
 * BindTextureResource is the only caller. */
extern "C" __declspec(dllexport) IDirectDrawPalette *__stdcall
Texture_CreatePaletteFromDIB(IDirectDraw4 *dd, HBITMAP hbmp);

/* KAROO_IMAGE_DIAG's first-call announcement, shared so the census covers all
 * six ctor/dtor entry points through one implementation. */
extern "C" __declspec(dllexport) void Texture_ImageFirstCall(const char *who,
                                                             unsigned long *seen);
