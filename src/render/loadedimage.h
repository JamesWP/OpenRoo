#pragma once
#include <stdint.h>

/* The backend objects an image holds; only the d3d files look inside. */
struct IDirectDrawSurface4;

/* LoadedImage: a bitmap on a DirectDraw surface, as RenderDevice::
 * PresentImage takes it. */
class LoadedImage;
class RenderDevice;

class LoadedImage {
public:
    LoadedImage();
    /* Frees the image name.  PRESERVED: it is not NULLed. */
    ~LoadedImage();
    LoadedImage(const LoadedImage &) = delete;
    LoadedImage &operator=(const LoadedImage &) = delete;

    IDirectDrawSurface4 *textureSurface() const { return pTextureSurface_; }
    char                *imageName() const { return ImageName_; }

    void releaseSurfaces();

    /* For the DIB loader (texturedib.cpp), which fills the surface and
     * records how it was filled. */
    void setLoadStatus(int s)      { loadStatus_ = s; }
    void setLoadedState(int s)     { loadedState_ = s; }
    void setImageNamePtr(char *p)  { ImageName_ = p; }
    /* The surface's address, for CreateSurface's out-parameter; 4-aligned. */
    IDirectDrawSurface4 **textureSurfaceSlot() { return &pTextureSurface_; }

private:
    IDirectDrawSurface4 *pTextureSurface_;
    char                *ImageName_;
    int                  loadStatus_;
    int                  loadedState_;
};
