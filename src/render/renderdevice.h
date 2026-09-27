/* RenderDevice: the game's one rendering device.  Owns DirectDraw, Direct3D,
 * the device, the viewport, the primary/back/z-buffer surfaces and the
 * enumerated display modes.  One instance exists, at g_renderDevice.
 *
 * Device creation is in createdevice.cpp; presentation and teardown in
 * renderdevice.cpp. */

#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <vector>

/* One enumerated display mode. */
struct DisplayMode {
    DWORD dwWidth, dwHeight, dwBitDepth;
};

/* A bitmap on a DirectDraw surface, as PresentImage takes it. */
struct LoadedImage {
    void                *unknown00;
    IDirectDrawSurface4 *pTextureSurface;
    IDirectDrawSurface4 *pTexturePalette;
    char                *ImageName;
    int                  loadStatus;
    int                  loadedState;
};

class RenderDevice {
public:
    RenderDevice();
    ~RenderDevice();

    /* Brings up DirectDraw, the display mode, the surfaces, Direct3D, the
     * device and the viewport (createdevice.cpp).  On failure lastError()
     * says why. */
    bool Create(HWND hWnd, GUID *pDriverGuid, int nModeIndex, bool bHardware);

    /* Releases every interface and forgets the display modes. */
    void Release();

    /* Blts img over the back buffer and flips it to the screen. */
    void PresentImage(LoadedImage *img);

    const char *lastError() const { return lastError_; }

    IDirect3D3           *pD3D;
    IDirect3DViewport3   *pViewport;
    IDirect3DDevice3     *pDevice;
    DWORD                 dwModeFilterFlags;
    DDPIXELFORMAT         zbufFmt;
    IDirectDrawSurface4  *pPrimary;
    IDirectDrawSurface4  *pBackBuffer;
    IDirectDrawSurface4  *pZBuffer;
    std::vector<DisplayMode> modes;
    DisplayMode          *pSelectedMode;
    IDirectDraw4         *pDD4;
    HWND                  hWnd;

private:
    friend struct DeviceCreation;
    char lastError_[100];
};

extern RenderDevice *g_renderDevice;
