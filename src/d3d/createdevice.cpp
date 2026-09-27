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
 * enum_display_modes_cb applies.  It is released at once; the device's is queried
 * again later. */

#include "d3dnative.h"
#include "log.h"
#include "gamestr.h"
#include "gameglobals.h"
#include <stdio.h>
#include <string.h>

#include "ddrawdiag.h"
#include "nullddraw.h"

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
    static DWORD filterFlags(RenderDevice *self) { return self->modeFilterFlags_; }
    static void keepMode(RenderDevice *self, const DisplayMode &m) { self->modes_.push_back(m); }
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

/* The DirectDraw everything goes through: the in-DLL null device when
 * headless (KAROO_HEADLESS), else the real one from ddraw.dll.  This is the
 * single point where headless mode is decided -- every surface, device and
 * viewport descends from this object. */
static HRESULT create_directdraw(GUID *guid, LPDIRECTDRAW *out)
{
    *out = NULL;
    if (nulldd_enabled()) {
        *out = nulldd_create();
        return DD_OK;
    }

    // LoadLibrary, not GetModuleHandle: the executable does not import
    // ddraw.dll, so it may not be loaded yet.
    typedef HRESULT (WINAPI *create_fn)(GUID *, LPDIRECTDRAW *, IUnknown *);
    HMODULE ddraw = LoadLibraryA("ddraw.dll");
    create_fn create = ddraw
        ? (create_fn)(void (*)(void))GetProcAddress(ddraw, "DirectDrawCreate")
        : NULL;
    if (!create)
        return DDERR_GENERIC;
    return create(guid, out, NULL);
}

/* A mode is usable when its depth's bit is set in depths (the HAL device's
 * dwDeviceRenderBitDepth), its depth is at least 16, and -- unless anyAspect
 * -- its aspect ratio falls strictly inside (1.3, 1.4): 4:3 and nothing
 * else.  Create and EnumerateDisplayModes share it, so their indices agree. */
static bool mode_usable(const DDSURFACEDESC2 *d, DWORD depths, bool anyAspect)
{
    DWORD bpp = d->ddpfPixelFormat.dwRGBBitCount;
    if (bpp < 16)
        return false;
    if (bpp == 32 && !(depths & DDBD_32)) return false;
    if (bpp == 24 && !(depths & DDBD_24)) return false;
    if (bpp == 16 && !(depths & DDBD_16)) return false;
    float aspect = (float)((double)d->dwWidth / (double)d->dwHeight);
    return anyAspect || (aspect < 1.4f && aspect > 1.3f);
}

/* The HAL device's render depths, from a temporary IDirect3D3; 0 when there
 * is no HAL description. */
static bool hal_render_depths(IDirectDraw4 *dd, DWORD *depths)
{
    IDirect3D3 *d3d = NULL;
    if (FAILED(dd->QueryInterface(IID_IDirect3D3, (void **)&d3d)))
        return false;

    D3DFINDDEVICERESULT found;
    D3DFINDDEVICESEARCH search;
    memset(&found,  0, sizeof(found));
    memset(&search, 0, sizeof(search));
    found.dwSize   = sizeof(found);
    search.dwSize  = sizeof(search);
    search.dwFlags = D3DFDS_GUID;
    search.guid    = IID_IDirect3DHALDevice;

    HRESULT hr = d3d->FindDevice(&search, &found);
    ddiag_find_device(hr, &found);
    d3d->Release();
    if (FAILED(hr))
        return false;
    *depths = found.ddHwDesc.dwFlags != 0
            ? found.ddHwDesc.dwDeviceRenderBitDepth : 0;
    return true;
}

/* Context is the RenderDevice. */
static HRESULT WINAPI enum_display_modes_cb(LPDDSURFACEDESC2 pDesc, LPVOID ctx)
{
    RenderDevice *self = (RenderDevice *)ctx;
    DWORD bpp = pDesc->ddpfPixelFormat.dwRGBBitCount;

    g_devdiag.modesSeen++;
    ddiag_mode(pDesc);
    if (!mode_usable(pDesc, DeviceCreation::filterFlags(self), false))
        return DDENUMRET_OK;

    DisplayMode mode = { pDesc->dwWidth, pDesc->dwHeight, bpp };

    char msg[256];
    sprintf(msg, GS_D3D_FOUND_MODE, mode.dwWidth, mode.dwHeight, bpp);
    imagelog(msg);

    DeviceCreation::keepMode(self, mode);
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
    ddiag_pixfmt("zbuffmt", pFmt);

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

bool RenderDevice::Create(HWND hWnd, GUID *pDriverGuid, int nModeIndex,
                          bool bHardware)
{
    char msg[256];

    Release();
    Native *n = native_;

    // ── DirectDraw, and the DirectDraw4 interface everything else uses ──
    LPDIRECTDRAW dd1 = NULL;
    HRESULT hr = create_directdraw(pDriverGuid, &dd1);
    if (FAILED(hr)) {
        hr = create_directdraw(NULL, &dd1);
        if (FAILED(hr))
            return DeviceCreation::fail(this, GS_D3D_ERR_DDRAW_CREATE);
    }

    hr = dd1->QueryInterface(IID_IDirectDraw4, (void **)&n->dd);
    dd1->Release();
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_DD4_IFACE);

    hr = n->dd->SetCooperativeLevel(hWnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN |
                                         DDSCL_FPUSETUP);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_COOP_LEVEL);

    // ── FindDevice(HAL), for dwDeviceRenderBitDepth only ──
    // REVIEW: a failure here used to return without a log line, and a HAL
    // result with no flags read the bit depth out of uninitialised stack; it
    // now fails with the Direct3D3 error / a zero filter.
    if (!hal_render_depths(n->dd, &modeFilterFlags_))
        return DeviceCreation::fail(this, GS_D3D_ERR_D3D3_IFACE);
    sprintf(msg, GS_D3D_RENDER_BITDEPTH, modeFilterFlags_);
    imagelog(msg);

    // ── Enumerate display modes ──
    imagelog(GS_D3D_START_ENUMMODES);
    hr = n->dd->EnumDisplayModes(0, NULL, this, enum_display_modes_cb);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_ENUMMODES);
    imagelog(GS_D3D_END_ENUMMODES);

    // ── Pick the mode, and set it ──
    // REVIEW: an out-of-range index used to walk off the list and fault, and
    // an empty list faulted on the fallbacks; the index now falls back to
    // the first mode, and no modes at all fails with the set-mode error.
    if (modes_.empty())
        return DeviceCreation::fail(this, GS_D3D_ERR_SET_MODE);
    if (devfx() == DEVFX_MODE0)
        nModeIndex = 0;

    if (nModeIndex >= 0 && (size_t)nModeIndex < modes_.size()) {
        DisplayMode *mode = &modes_[nModeIndex];
        sprintf(msg, GS_D3D_TRYING_MODE,
                mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
        imagelog(msg);
        hr = n->dd->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                  mode->dwBitDepth, 0, 0);
        if (SUCCEEDED(hr)) {
            mode_ = mode;
        } else {
            sprintf(msg, GS_D3D_FAILED_HR, hr);
            imagelog(msg);
            mode = &modes_[0];
            sprintf(msg, GS_D3D_TRYING_FIRST_MODE,
                    mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
            imagelog(msg);
        }
    } else {
        DisplayMode *mode = &modes_[0];
        sprintf(msg, GS_D3D_NO_MODE_SPECIFIED,
                mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
        imagelog(msg);
    }
    if (mode_ == NULL) {
        DisplayMode *mode = &modes_[0];
        hr = n->dd->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                  mode->dwBitDepth, 0, 0);
        if (FAILED(hr))
            return DeviceCreation::fail(this, GS_D3D_ERR_SET_MODE);
        mode_ = mode;
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
    hr = n->dd->CreateSurface(&dd, &n->primary, NULL);
    ddiag_create_surface(hr, &dd);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_PRIMARY);

    // REVIEW: only dwCaps used to be written; the rest was stack garbage.
    DDSCAPS2 caps;
    memset(&caps, 0, sizeof(caps));
    caps.dwCaps = DDSCAPS_BACKBUFFER;
    hr = n->primary->GetAttachedSurface(&caps, &n->backBuffer);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_BACKBUFFER);

    // ── Direct3D3, and the z-buffer pixel format ──
    hr = n->dd->QueryInterface(IID_IDirect3D3, (void **)&n->d3d);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_D3D3_IFACE);

    if (bHardware) {
        n->d3d->EnumZBufferFormats(IID_IDirect3DHALDevice, enum_zbuffer_cb, &n->zbufFmt);
    } else {
        hr = n->d3d->EnumZBufferFormats(IID_IDirect3DMMXDevice, enum_zbuffer_cb,
                                      &n->zbufFmt);
        if (FAILED(hr))
            n->d3d->EnumZBufferFormats(IID_IDirect3DRGBDevice, enum_zbuffer_cb,
                                     &n->zbufFmt);
    }
    if (n->zbufFmt.dwSize != sizeof(DDPIXELFORMAT))  // nothing was kept
        return DeviceCreation::fail(this, GS_D3D_ERR_ZBUF_FORMAT);

    // ── The z-buffer surface ──
    memset(&dd, 0, sizeof(dd));
    dd.dwSize          = sizeof(dd);
    dd.dwFlags         = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    dd.dwWidth         = mode_->dwWidth;
    dd.dwHeight        = mode_->dwHeight;
    dd.ddsCaps.dwCaps  = DDSCAPS_ZBUFFER |
                         (bHardware ? DDSCAPS_VIDEOMEMORY : DDSCAPS_SYSTEMMEMORY);
    dd.ddpfPixelFormat = n->zbufFmt;

    sprintf(msg, GS_D3D_ZBUF_BITDEPTH,    n->zbufFmt.dwZBufferBitDepth);
    imagelog(msg);
    sprintf(msg, GS_D3D_STENCIL_BITDEPTH, n->zbufFmt.dwStencilBitDepth);
    imagelog(msg);

    hr = n->dd->CreateSurface(&dd, &n->zBuffer, NULL);
    ddiag_create_surface(hr, &dd);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_ZBUF_SURFACE);

    hr = n->backBuffer->AddAttachedSurface(n->zBuffer);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_ATTACH_ZBUF);

    // ── The device, on the back buffer as render target ──
    if (bHardware) {
        hr = n->d3d->CreateDevice(IID_IDirect3DHALDevice, n->backBuffer, &n->device, NULL);
    } else {
        hr = n->d3d->CreateDevice(IID_IDirect3DMMXDevice, n->backBuffer, &n->device, NULL);
        if (FAILED(hr))
            hr = n->d3d->CreateDevice(IID_IDirect3DRGBDevice, n->backBuffer,
                                    &n->device, NULL);
    }
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_CREATE_DEVICE);

    // ── The viewport ── The clip volume spans x in [-1, 1] and
    // y in [-aspect, aspect] (dvClipY is its top edge).
    float aspect = (float)((double)mode_->dwHeight
                         / (double)mode_->dwWidth);

    D3DVIEWPORT2 vp;
    memset(&vp, 0, sizeof(vp));
    vp.dwSize       = sizeof(vp);
    vp.dwWidth      = mode_->dwWidth;
    vp.dwHeight     = mode_->dwHeight;
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

    hr = n->d3d->CreateViewport(&n->viewport, NULL);
    if (FAILED(hr))
        return DeviceCreation::fail(this, GS_D3D_ERR_CREATE_VP);

    n->device->AddViewport(n->viewport);
    n->viewport->SetViewport2(&vp);
    n->device->SetCurrentViewport(n->viewport);

    log_write("renderdevice: Create hwnd=%p guid=%p mode=%d hw=%s -> "
              "%lux%lux%lu dd4=%p d3d=%p dev=%p vp=%p primary=%p "
              "back=%p zbuf=%p filter=%08lX zdepth=%lu stencil=%lu\n",
              hWnd, pDriverGuid, nModeIndex, bHardware ? "TRUE" : "FALSE",
              mode_->dwWidth, mode_->dwHeight,
              mode_->dwBitDepth,
              n->dd, n->d3d, n->device, n->viewport, n->primary, n->backBuffer, n->zBuffer,
              modeFilterFlags_, n->zbufFmt.dwZBufferBitDepth,
              n->zbufFmt.dwStencilBitDepth);

    if (devdiag())
        log_write("renderdevice: DIAG modesSeen=%u modesKept=%u zfmtSeen=%u "
                  "zfmtKept=%u logLines=%u\n",
                  g_devdiag.modesSeen, g_devdiag.modesKept,
                  g_devdiag.zfmtSeen, g_devdiag.zfmtKept, g_devdiag.logLines);

    return true;
}

// ── Enumeration for the launcher ──

static BOOL WINAPI enum_adapters_cb(GUID *guid, LPSTR desc, LPSTR, LPVOID ctx)
{
    std::vector<Adapter> *out = (std::vector<Adapter> *)ctx;
    Adapter a;
    memset(&a, 0, sizeof(a));
    lstrcpynA(a.name, desc, sizeof(a.name));
    a.hasGuid = guid != NULL;
    if (guid)
        a.guid = *guid;
    out->push_back(a);
    return DDENUMRET_OK;
}

bool RenderDevice::EnumerateAdapters(std::vector<Adapter> &out)
{
    // LoadLibrary, not GetModuleHandle: the executable does not import
    // ddraw.dll, so it may not be loaded yet.
    typedef HRESULT (WINAPI *enum_fn)(LPDDENUMCALLBACKA, LPVOID);
    enum_fn enumerate = (enum_fn)(void (*)(void))
        GetProcAddress(LoadLibraryA("ddraw.dll"), "DirectDrawEnumerateA");
    return enumerate && SUCCEEDED(enumerate(enum_adapters_cb, &out));
}

struct ModeListCtx {
    std::vector<DisplayMode> *out;
    DWORD depths;
    bool  anyAspect;
};

static HRESULT WINAPI enum_mode_list_cb(LPDDSURFACEDESC2 d, LPVOID ctxp)
{
    ModeListCtx *ctx = (ModeListCtx *)ctxp;
    ddiag_mode(d);
    if (mode_usable(d, ctx->depths, ctx->anyAspect)) {
        DisplayMode m = { d->dwWidth, d->dwHeight, d->ddpfPixelFormat.dwRGBBitCount };
        ctx->out->push_back(m);
    }
    return DDENUMRET_OK;
}

/* Creates the adapter's DirectDraw (falling back to the default), finds its
 * HAL device's render depths and lists the usable modes.  REVIEW: the
 * launcher's copy of this leaked every interface on its failure paths. */
bool RenderDevice::EnumerateDisplayModes(const GUID *adapter,
                                         std::vector<DisplayMode> &out,
                                         bool anyAspect)
{
    LPDIRECTDRAW dd = NULL;
    if (FAILED(create_directdraw((GUID *)adapter, &dd))
        && FAILED(create_directdraw(NULL, &dd)))
        return false;
    IDirectDraw4 *dd4 = NULL;
    HRESULT hr = dd->QueryInterface(IID_IDirectDraw4, (void **)&dd4);
    dd->Release();
    if (FAILED(hr))
        return false;

    ModeListCtx ctx = { &out, 0, anyAspect };
    bool ok = hal_render_depths(dd4, &ctx.depths)
           && SUCCEEDED(dd4->EnumDisplayModes(0, NULL, &ctx, enum_mode_list_cb));
    dd4->Release();
    return ok;
}
