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

class LoadedImage {
public:
    LoadedImage();
    /* Frees the image name.  PRESERVED: it is not NULLed. */
    virtual ~LoadedImage();
    LoadedImage(const LoadedImage &) = delete;
    LoadedImage &operator=(const LoadedImage &) = delete;

    /* Restore a lost surface and reload its image.
     * TextureManager::loadAll (scenetexture.cpp) is the outside caller. */
    unsigned int load();

    IDirectDrawSurface4 *textureSurface() const { return pTextureSurface_; }
    char                *imageName() const { return ImageName_; }

    void releaseSurfaces();

    /* For the DIB and TGA loaders (texturedib.cpp, texturetga.cpp), which fill
     * the surface and record how it was filled. */
    void setLoadStatus(int s)      { loadStatus_ = s; }
    void setLoadedState(int s)     { loadedState_ = s; }
    void setImageNamePtr(char *p)  { ImageName_ = p; }
    /* The surface's address, for CreateSurface's out-parameter; 4-aligned. */
 
 
    IDirectDrawSurface4 **textureSurfaceSlot() { return &pTextureSurface_; }
 

private:
    friend class SceneTexture;

    IDirectDrawSurface4 *pTextureSurface_;
    IDirectDrawSurface4 *pTexturePalette_;
    char                *ImageName_;
    int                  loadStatus_;
    int                  loadedState_;
};

/* A palette from the DIB's colour table.  scenetexture.cpp's
 * BindTextureResource is the only caller. */
  IDirectDrawPalette *__stdcall
Texture_CreatePaletteFromDIB(IDirectDraw4 *dd, HBITMAP hbmp);

/* KAROO_IMAGE_DIAG's first-call announcement, shared so the census covers all
 * six ctor/dtor entry points through one implementation. */
  void Texture_ImageFirstCall(const char *who,
                                                             unsigned long *seen);
