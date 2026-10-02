/* LoadedImage's lifecycle (loadedimage.h). */

#include "loadedimage.h"
#include "d3dnative.h"
#include <stdlib.h>

LoadedImage::LoadedImage()
{
    pTextureSurface_ = NULL;
    ImageName_       = NULL;
    loadedState_     = 0;
    loadStatus_      = 0;
}

LoadedImage::~LoadedImage()
{
    if (ImageName_ != NULL)
        free(ImageName_);  // PRESERVED: not NULLed, so a second DtorBody double-frees
}

void LoadedImage::releaseSurfaces()
{
    IDirectDrawSurface4 *surf = pTextureSurface_;
    if (surf != NULL)
        surf->Release();
    pTextureSurface_ = NULL;  // unconditional

    if (ImageName_ != NULL) {
        free(ImageName_);
        ImageName_ = NULL;  // only inside the check
    }

    loadedState_ = 0;
    loadStatus_  = 0;
}
