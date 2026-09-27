/* RenderDevice::Create: brings up DirectDraw, the display mode, the surface
 * chain, Direct3D, the device and the viewport.  Everything the rest of the
 * render path assumes exists is created here.
 *
 * Arguments: hWnd, the driver GUID for DirectDrawCreate, the index into the
 * enumerated modes, and bHardware (HAL, or MMX-then-RGB).  WinMain calls it
 * as a retry ladder: the configured driver and mode, then no GUID, then no
 * GUID and mode 0.  bHardware is true at all three, so the MMX/RGB fallbacks
 * and the system-memory z-buffer are never exercised by the replay suite.
 *
 * A temporary IDirect3D3 is used only for FindDevice(HAL), to read
 * ddHwDesc.dwDeviceRenderBitDepth into dwModeFilterFlags, the filter
 * enum_display_modes_cb applies.  It is released at once; pD3D is queried
 * again later. */

#include "renderdevice.h"
#include "log.h"
#include "gamestr.h"
#include "gameglobals.h"
#include <stdio.h>
#include <string.h>

#include "com_proxy.h"

/* KAROO_D3DDEV_FX: controls that change geometry, which only this function
 * decides.
 *   halfvp    -- halve the viewport: the scene renders into the top-left
 *                quarter of the screen.
 *   mode0     -- force nModeIndex to 0: the game comes up in a different
 *                resolution from Karoo.cfg's.
 *   firstzbuf -- keep the first z-buffer format offered instead of the
 *                deepest. */
enum { DEVFX_OFF = 0, DEVFX_HALFVP, DEVFX_MODE0, DEVFX_FIRSTZBUF };

static int devfx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = DEVFX_OFF;
        if (GetEnvironmentVariableA("KAROO_D3DDEV_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "halfvp") == 0)     cached = DEVFX_HALFVP;
            else if (lstrcmpiA(buf, "mode0") == 0) cached = DEVFX_MODE0;
            else if (lstrcmpiA(buf, "firstzbuf") == 0) cached = DEVFX_FIRSTZBUF;
        }
        log_write("renderdevice: device FX mode = %s\n",
                  cached == DEVFX_HALFVP    ? "halfvp" :
                  cached == DEVFX_MODE0     ? "mode0"  :
                  cached == DEVFX_FIRSTZBUF ? "firstzbuf" : "off");
    }
    return cached;
}

/* KAROO_D3DDEV_DIAG=1: at the end of Create, report how many times each
 * enumeration callback was entered and how many times it accepted.  Neither
 * gate can otherwise tell "ran and agreed" from "never ran". */
static struct {
    unsigned modesSeen, modesKept, zfmtSeen, zfmtKept, logLines;
} g_devdiag;

static int devdiag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_D3DDEV_DIAG", buf, sizeof(buf)))
            cached = (buf[0] != '\0' && buf[0] != '0');
    }
    return cached;
}

/* The German error messages go to fail() (which also keeps them for
 * lastError()), the English progress lines to the image log. */
static void imagelog(const char *s)
{
    fwrite(s, (unsigned)lstrlenA(s), 1, stderr);
}

/* Create's helpers, befriended by RenderDevice. */
struct DeviceCreation {
    /* A newline and then the message go to the image log, and the message is
     * kept for lastError().  Returns false: how every error path fails. */
    static bool fail(RenderDevice *self, const char *msg)
    {
        imagelog(GS_FMT_NEWLINE);
        imagelog(msg);
        lstrcpynA(self->lastError_, msg, sizeof(self->lastError_));
        g_devdiag.logLines++;
        return false;
    }
};

/* Context is the RenderDevice.  A mode survives when its depth's bit is set
 * in dwModeFilterFlags, its depth is at least 16, and its aspect ratio falls
 * strictly inside (1.3, 1.4) -- 4:3 and nothing else. */
static HRESULT WINAPI enum_display_modes_cb(LPDDSURFACEDESC2 pDesc, LPVOID ctx)
{
    RenderDevice *self = (RenderDevice *)ctx;
    DWORD bpp   = pDesc->ddpfPixelFormat.dwRGBBitCount;
    DWORD flags = self->dwModeFilterFlags;

    g_devdiag.modesSeen++;

    float aspect = (float)((double)pDesc->dwWidth / (double)pDesc->dwHeight);

    if (bpp == 32) {
        if (!(flags & DDBD_32)) return DDENUMRET_OK;
    } else if (bpp == 24) {
        if (!(flags & DDBD_24)) return DDENUMRET_OK;
    } else if (bpp == 16) {
        if (!(flags & DDBD_16)) return DDENUMRET_OK;
    } else if (bpp < 16) {
        return DDENUMRET_OK;
    }

    if (!(aspect < 1.4f && aspect > 1.3f))
        return DDENUMRET_OK;

    DisplayMode mode = { pDesc->dwWidth, pDesc->dwHeight, bpp };

    char msg[256];
    sprintf(msg, GS_D3D_FOUND_MODE, mode.dwWidth, mode.dwHeight, bpp);
    imagelog(msg);

    self->modes.push_back(mode);
    g_devdiag.modesKept++;
    return DDENUMRET_OK;
}

/* Context is the kept DDPIXELFORMAT, zeroed by Release before enumeration.
 * One is kept when it is a z-buffer and either nothing has been kept yet or
 * its Z and stencil depths are both >= the kept one's; non-strict, so on a
 * tie the later format wins. */
static HRESULT WINAPI enum_zbuffer_cb(LPDDPIXELFORMAT pFmt, LPVOID ctx)
{
    DDPIXELFORMAT *kept = (DDPIXELFORMAT *)ctx;

    g_devdiag.zfmtSeen++;

    char msg[100];
    sprintf(msg, GS_D3D_ZBUF_FMT, pFmt->dwZBufferBitDepth, pFmt->dwStencilBitDepth);
    imagelog(msg);

    if (pFmt->dwFlags & DDPF_ZBUFFER) {
        bool none = kept->dwSize != sizeof(DDPIXELFORMAT);
        bool take = none ||
                    (pFmt->dwZBufferBitDepth >= kept->dwZBufferBitDepth &&
                     pFmt->dwStencilBitDepth >= kept->dwStencilBitDepth);
        if (devfx() == DEVFX_FIRSTZBUF)
            take = none;  // KAROO_D3DDEV_FX=firstzbuf
        if (take) {
            *kept = *pFmt;
            g_devdiag.zfmtKept++;
        }
    }
    return D3DENUMRET_OK;
}

bool RenderDevice::Create(HWND hWnd_, GUID *pDriverGuid, int nModeIndex,
                          bool bHardware)
{
    char msg[256];

    Release();
    hWnd = hWnd_;

    // ── DirectDraw, and the DirectDraw4 interface everything else uses ──
    LPDIRECTDRAW dd1 = NULL;
    HRESULT hr = hooks_DirectDrawCreate(pDriverGuid, &dd1, NULL);
    if (FAILED(hr)) {
        hr = hooks_DirectDrawCreate(NULL, &dd1, NULL);
        if (FAILED(hr))
            return DeviceCreation::fail(this, GS_D3D_ERR_DDRAW_CREATE);
    }

    hr = dd1->QueryInterface(IID_IDirectDraw4, (void **)&pDD4);
    dd1->Release();
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_DD4_IFACE);

    hr = pDD4->SetCooperativeLevel(hWnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN |
                                         DDSCL_FPUSETUP);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_COOP_LEVEL);

    // ── FindDevice(HAL), for dwDeviceRenderBitDepth only ──
    // REVIEW: these two failures used to return without a log line, and a
    // HAL result with no flags read the bit depth out of uninitialised stack;
    // both now fail with the Direct3D3 error / a zero filter.
    IDirect3D3 *d3dTmp = NULL;
    hr = pDD4->QueryInterface(IID_IDirect3D3, (void **)&d3dTmp);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_D3D3_IFACE);

    D3DFINDDEVICERESULT found;
    D3DFINDDEVICESEARCH search;
    memset(&found,  0, sizeof(found));
    memset(&search, 0, sizeof(search));
    found.dwSize   = sizeof(found);
    search.dwSize  = sizeof(search);
    search.dwFlags = D3DFDS_GUID;
    search.guid    = IID_IDirect3DHALDevice;

    hr = d3dTmp->FindDevice(&search, &found);
    d3dTmp->Release();
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_D3D3_IFACE);

    dwModeFilterFlags = found.ddHwDesc.dwFlags != 0
                      ? found.ddHwDesc.dwDeviceRenderBitDepth : 0;
    sprintf(msg, GS_D3D_RENDER_BITDEPTH, dwModeFilterFlags);
    imagelog(msg);

    // ── Enumerate display modes ──
    imagelog(GS_D3D_START_ENUMMODES);
    hr = pDD4->EnumDisplayModes(0, NULL, this, enum_display_modes_cb);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_ENUMMODES);
    imagelog(GS_D3D_END_ENUMMODES);

    // ── Pick the mode, and set it ──
    // REVIEW: an out-of-range index used to walk off the list and fault, and
    // an empty list faulted on the fallbacks; the index now falls back to
    // the first mode, and no modes at all fails with the set-mode error.
    if (modes.empty())
        return DeviceCreation::fail(this, GS_D3D_ERR_SET_MODE);
    if (devfx() == DEVFX_MODE0)
        nModeIndex = 0;

    if (nModeIndex >= 0 && (size_t)nModeIndex < modes.size()) {
        DisplayMode *mode = &modes[nModeIndex];
        sprintf(msg, GS_D3D_TRYING_MODE,
                mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
        imagelog(msg);
        hr = pDD4->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                  mode->dwBitDepth, 0, 0);
        if (SUCCEEDED(hr)) {
            pSelectedMode = mode;
        } else {
            sprintf(msg, GS_D3D_FAILED_HR, hr);
            imagelog(msg);
            mode = &modes[0];
            sprintf(msg, GS_D3D_TRYING_FIRST_MODE,
                    mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
            imagelog(msg);
        }
    } else {
        DisplayMode *mode = &modes[0];
        sprintf(msg, GS_D3D_NO_MODE_SPECIFIED,
                mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
        imagelog(msg);
    }
    if (pSelectedMode == NULL) {
        DisplayMode *mode = &modes[0];
        hr = pDD4->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                  mode->dwBitDepth, 0, 0);
        if (FAILED(hr))
            return DeviceCreation::fail(this, GS_D3D_ERR_SET_MODE);
        pSelectedMode = mode;
    }
    imagelog(GS_D3D_DONE);

    // ── Primary (flipping, complex, 3D) + its attached back buffer ──
    DDSURFACEDESC2 dd;
    memset(&dd, 0, sizeof(dd));
    dd.dwSize            = sizeof(dd);
    dd.dwFlags           = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    dd.ddsCaps.dwCaps    = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP |
                           DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE;
    dd.dwBackBufferCount = 1;
    hr = pDD4->CreateSurface(&dd, &pPrimary, NULL);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_PRIMARY);

    // REVIEW: only dwCaps used to be written; the rest was stack garbage.
    DDSCAPS2 caps;
    memset(&caps, 0, sizeof(caps));
    caps.dwCaps = DDSCAPS_BACKBUFFER;
    hr = pPrimary->GetAttachedSurface(&caps, &pBackBuffer);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_BACKBUFFER);

    // ── Direct3D3, and the z-buffer pixel format ──
    hr = pDD4->QueryInterface(IID_IDirect3D3, (void **)&pD3D);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_D3D3_IFACE);

    if (bHardware) {
        pD3D->EnumZBufferFormats(IID_IDirect3DHALDevice, enum_zbuffer_cb, &zbufFmt);
    } else {
        hr = pD3D->EnumZBufferFormats(IID_IDirect3DMMXDevice, enum_zbuffer_cb,
                                      &zbufFmt);
        if (FAILED(hr))
            pD3D->EnumZBufferFormats(IID_IDirect3DRGBDevice, enum_zbuffer_cb,
                                     &zbufFmt);
    }
    if (zbufFmt.dwSize != sizeof(DDPIXELFORMAT))  // nothing was kept
        return DeviceCreation::fail(this, GS_D3D_ERR_ZBUF_FORMAT);

    // ── The z-buffer surface ──
    memset(&dd, 0, sizeof(dd));
    dd.dwSize          = sizeof(dd);
    dd.dwFlags         = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    dd.dwWidth         = pSelectedMode->dwWidth;
    dd.dwHeight        = pSelectedMode->dwHeight;
    dd.ddsCaps.dwCaps  = DDSCAPS_ZBUFFER |
                         (bHardware ? DDSCAPS_VIDEOMEMORY : DDSCAPS_SYSTEMMEMORY);
    dd.ddpfPixelFormat = zbufFmt;

    sprintf(msg, GS_D3D_ZBUF_BITDEPTH,    zbufFmt.dwZBufferBitDepth);
    imagelog(msg);
    sprintf(msg, GS_D3D_STENCIL_BITDEPTH, zbufFmt.dwStencilBitDepth);
    imagelog(msg);

    hr = pDD4->CreateSurface(&dd, &pZBuffer, NULL);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_ZBUF_SURFACE);

    hr = pBackBuffer->AddAttachedSurface(pZBuffer);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_ATTACH_ZBUF);

    // ── The device, on the back buffer as render target ──
    if (bHardware) {
        hr = pD3D->CreateDevice(IID_IDirect3DHALDevice, pBackBuffer, &pDevice, NULL);
    } else {
        hr = pD3D->CreateDevice(IID_IDirect3DMMXDevice, pBackBuffer, &pDevice, NULL);
        if (FAILED(hr))
            hr = pD3D->CreateDevice(IID_IDirect3DRGBDevice, pBackBuffer,
                                    &pDevice, NULL);
    }
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_CREATE_DEVICE);

    // ── The viewport ── The clip volume spans x in [-1, 1] and
    // y in [-aspect, aspect] (dvClipY is its top edge).
    float aspect = (float)((double)pSelectedMode->dwHeight
                         / (double)pSelectedMode->dwWidth);

    D3DVIEWPORT2 vp;
    memset(&vp, 0, sizeof(vp));
    vp.dwSize       = sizeof(vp);
    vp.dwWidth      = pSelectedMode->dwWidth;
    vp.dwHeight     = pSelectedMode->dwHeight;
    vp.dvClipX      = -1.0f;
    vp.dvClipWidth  = 2.0f;
    vp.dvMinZ       = 0.0f;
    vp.dvMaxZ       = 1.0f;
    vp.dvClipY      = aspect;
    vp.dvClipHeight = aspect + aspect;

    if (devfx() == DEVFX_HALFVP) {
        vp.dwWidth  /= 2;
        vp.dwHeight /= 2;
    }

    hr = pD3D->CreateViewport(&pViewport, NULL);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_CREATE_VP);

    pDevice->AddViewport(pViewport);
    pViewport->SetViewport2(&vp);
    pDevice->SetCurrentViewport(pViewport);

    log_write("renderdevice: Create hwnd=%p guid=%p mode=%d hw=%s -> "
              "%lux%lux%lu dd4=%p d3d=%p dev=%p vp=%p primary=%p "
              "back=%p zbuf=%p filter=%08lX zdepth=%lu stencil=%lu\n",
              hWnd, pDriverGuid, nModeIndex, bHardware ? "TRUE" : "FALSE",
              pSelectedMode->dwWidth, pSelectedMode->dwHeight,
              pSelectedMode->dwBitDepth,
              pDD4, pD3D, pDevice, pViewport, pPrimary, pBackBuffer, pZBuffer,
              dwModeFilterFlags, zbufFmt.dwZBufferBitDepth,
              zbufFmt.dwStencilBitDepth);

    if (devdiag())
        log_write("renderdevice: DIAG modesSeen=%u modesKept=%u zfmtSeen=%u "
                  "zfmtKept=%u logLines=%u\n",
                  g_devdiag.modesSeen, g_devdiag.modesKept,
                  g_devdiag.zfmtSeen, g_devdiag.zfmtKept, g_devdiag.logLines);

    return true;
}
