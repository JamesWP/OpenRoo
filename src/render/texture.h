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

class __attribute__((packed)) LoadedImage {
public:
    /* ─── texture.cpp's members other files call ───────────────────────────
     *
     * The LoadedImage ctor/dtor family.  SceneTexture's (scenetexture.cpp) is
     * the only outside caller: its construct chains to the base's and its
     * dtorBody tail-calls the base's, exactly as the originals do. */
    LoadedImage *construct();

    void dtorBody();
    static LoadedImage * __attribute__((thiscall))
    scalarDtor(LoadedImage *self, unsigned int flags);

    /* Restore a lost surface and reload its image.
     * TextureManager::loadAll (scenetexture.cpp) is the outside caller. */
    unsigned int load();

    /* The vtable pointer LoadedImage's ctor and dtor body install, and the same
     * question for SceneTexture: our own one-slot table in this DLL. */
    static void *vtbl();

    void                *vtable() const { return unknown00_; }
    IDirectDrawSurface4 *textureSurface() const { return pTextureSurface_; }
    char                *imageName() const { return ImageName_; }

    void releaseSurfaces();

    /* For the DIB and TGA loaders (texturedib.cpp, texturetga.cpp), which fill
     * the surface and record how it was filled. */
    void setLoadStatus(int s)      { loadStatus_ = s; }
    void setLoadedState(int s)     { loadedState_ = s; }
    void setImageNamePtr(char *p)  { ImageName_ = p; }
    /* The surface's address, for CreateSurface's out-parameter; 4-aligned. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    IDirectDrawSurface4 **textureSurfaceSlot() { return &pTextureSurface_; }
#pragma GCC diagnostic pop

private:
    friend class SceneTexture;

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
