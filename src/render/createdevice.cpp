/* Direct3D::CreateD3DDevice: brings up DirectDraw, the display mode, the
 * surface chain, Direct3D, the device and the viewport.  Everything the rest
 * of the render path assumes exists is created here.
 *
 * Arguments: hWnd (stored to this->hWnd), the driver GUID for
 * DirectDrawCreate, the index into this->modeList (low byte), and bHardware
 * (HAL, or MMX-then-RGB).  WinMain calls it as a retry ladder: the configured
 * driver and mode, then no GUID, then no GUID and mode 0.  bHardware is true
 * at all three, so the MMX/RGB fallbacks and the system-memory z-buffer are
 * never exercised by the replay suite.
 *
 * A temporary IDirect3D3 is used only for FindDevice(HAL), to read
 * ddHwDesc.dwDeviceRenderBitDepth into this->dwModeFilterFlags, the filter
 * EnumDisplayModesCallback applies.  It is released at once; this->pD3D is
 * queried again later.
 *
 * pBackBuffer receives the z-buffer surface and pZBuffer the back buffer --
 * the names are swapped (direct3d.h).
 *
 * Only the low byte of the result is tested.  Every error path returns
 * d3d_log()'s value, whose low byte is 0; on success the upper bytes are
 * SetCurrentViewport's HRESULT. */

#include "direct3d.h"
#include "createdevice.h"
#include "log.h"
#include "gamestr.h"
#include "gameglobals.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "com_proxy.h"
#define ORIG_DIRECTDRAWCREATE hooks_DirectDrawCreate

static unsigned __attribute__((thiscall)) d3d_log(Direct3D *self, const char *msg);
static HRESULT WINAPI d3d_enum_display_modes_cb(LPDDSURFACEDESC2 pDesc, LPVOID ctx);
static HRESULT WINAPI d3d_enum_zbuffer_cb(LPDDPIXELFORMAT pFmt, LPVOID ctx);

#define IID_D3D_RGB  IID_IDirect3DRGBDevice
#define IID_D3D_HAL  IID_IDirect3DHALDevice
#define IID_D3D_MMX  IID_IDirect3DMMXDevice
#define IID_D3D3     IID_IDirect3D3
#define IID_DD4      IID_IDirectDraw4

/* The German error messages go to d3d_log (which also copies them into
 * this->pLastError), the English progress lines to the image log. */

static void d3d_imagelog(const char *s)
{
    fwrite(s, (unsigned)lstrlenA(s), 1, stderr);
}

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
        log_write("direct3d: device FX mode = %s\n",
                  cached == DEVFX_HALFVP    ? "halfvp" :
                  cached == DEVFX_MODE0     ? "mode0"  :
                  cached == DEVFX_FIRSTZBUF ? "firstzbuf" : "off");
    }
    return cached;
}

/* KAROO_D3DDEV_DIAG=1: at the end of CreateD3DDevice, report how many times
 * each enumeration callback was entered and how many times it accepted.
 * Neither gate can otherwise tell "ran and agreed" from "never ran". */
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

/* A newline and then the message go to the image log, and the message, NUL
 * included, is copied into this->pLastError.  PRESERVED: the copy is not
 * bounded by the 100-byte field.  Returns (strlen+1) & 0xffffff00, so the low
 * byte is 0: how every error path returns FALSE. */
static unsigned __attribute__((thiscall)) d3d_log(Direct3D *self, const char *msg)
{
    d3d_imagelog(GS_FMT_NEWLINE);
    d3d_imagelog(msg);

    unsigned n = (unsigned)lstrlenA(msg) + 1;
    memcpy(self->pLastError, msg, n);

    g_devdiag.logLines++;
    return n & 0xffffff00u;
}

/* Context is the Direct3D object.  A mode survives when its depth's bit is set
 * in dwModeFilterFlags (0x100 = 32bpp, 0x200 = 24bpp, 0x400 = 16bpp), its
 * depth is at least 16, and its aspect ratio falls strictly inside (1.3, 1.4)
 * -- 4:3 and nothing else. */
static HRESULT WINAPI d3d_enum_display_modes_cb(LPDDSURFACEDESC2 pDesc, LPVOID ctx)
{
    Direct3D *self = (Direct3D *)ctx;
    DWORD bpp   = pDesc->ddpfPixelFormat.dwRGBBitCount;
    DWORD flags = self->dwModeFilterFlags;

    g_devdiag.modesSeen++;

    // Width unsigned, height signed: the signedness changes the result.
    float aspect = (float)((double)(DWORD)pDesc->dwWidth /
                           (double)(int)pDesc->dwHeight);

    if (bpp == 0x20) {
        if (!(flags & 0x100)) return DDENUMRET_OK;
    } else if (bpp == 0x18) {
        if (!(flags & 0x200)) return DDENUMRET_OK;
    } else if (bpp == 0x10) {
        if (!(flags & 0x400)) return DDENUMRET_OK;
    } else if (bpp < 0x10) {
        return DDENUMRET_OK;
    }

    if (!(aspect < 1.4f && aspect > 1.3f))
        return DDENUMRET_OK;

    // Unchecked for NULL.
    DisplayModeNode *mode = (DisplayModeNode *)malloc(0xc);
    mode->dwWidth    = pDesc->dwWidth;
    mode->dwHeight   = pDesc->dwHeight;
    mode->dwBitDepth = bpp;

    char msg[256];
    sprintf(msg, GS_D3D_FOUND_MODE, mode->dwWidth, mode->dwHeight, bpp);
    d3d_imagelog(msg);

    LinkedList_Append(&self->modeList, mode);
    g_devdiag.modesKept++;
    return DDENUMRET_OK;
}

/* Context is &this->zbufFmt, zeroed by ReleaseResources before enumeration.
 * One is kept when it is a z-buffer and either nothing has been kept yet
 * (dwSize still != 0x20) or its Z and stencil depths are both >= the kept
 * one's; unsigned and non-strict, so on a tie the later format wins. */
static HRESULT WINAPI d3d_enum_zbuffer_cb(LPDDPIXELFORMAT pFmt, LPVOID ctx)
{
    const DWORD *src = (const DWORD *)pFmt;
    DWORD       *dst = (DWORD *)ctx;

    g_devdiag.zfmtSeen++;

    char msg[100];
    sprintf(msg, GS_D3D_ZBUF_FMT, src[3], src[4]);  // Z depth, stencil depth
    d3d_imagelog(msg);

    if (src[1] & 0x400) {  // DDPF_ZBUFFER
        bool take = (dst[0] != 0x20) ||
                    (src[3] >= dst[3] && src[4] >= dst[4]);
        if (devfx() == DEVFX_FIRSTZBUF)
            take = (dst[0] != 0x20);  // KAROO_D3DDEV_FX=firstzbuf
        if (take) {
            for (int i = 0; i < 8; i++)
                dst[i] = src[i];
            g_devdiag.zfmtKept++;
        }
    }
    return D3DENUMRET_OK;
}

extern "C" __declspec(dllexport) unsigned __attribute__((thiscall))
Direct3D_CreateD3DDevice(Direct3D *self, HWND hWnd, GUID *pDriverGuid,
                         int nModeIndex, bool bHardware)
{
    char msg[256];
    char msg2[260];

    Direct3D_ReleaseResources(self);
    self->hWnd = hWnd;

    // ── DirectDraw, and the DirectDraw4 interface everything else uses ──
    LPDIRECTDRAW dd1 = NULL;
    HRESULT hr = ORIG_DIRECTDRAWCREATE(pDriverGuid, &dd1, NULL);
    if (FAILED(hr)) {
        hr = ORIG_DIRECTDRAWCREATE(NULL, &dd1, NULL);
        if (FAILED(hr))
            return d3d_log(self, GS_D3D_ERR_DDRAW_CREATE);
    }

    hr = dd1->QueryInterface(IID_DD4, (void **)&self->pDD4);
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_DD4_IFACE);
    dd1->Release();

    hr = self->pDD4->SetCooperativeLevel(hWnd, 0x811);
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_COOP_LEVEL);

    // ── FindDevice(HAL), for dwDeviceRenderBitDepth only ──
    IDirect3D3 *d3dTmp = NULL;
    hr = self->pDD4->QueryInterface(IID_D3D3, (void **)&d3dTmp);
    if (FAILED(hr))
        return (unsigned)hr & 0xffffff00u;  // PRESERVED: no log line

    D3DFINDDEVICERESULT found;
    D3DFINDDEVICESEARCH search;
    memset(&found,  0, 0x20c);
    memset(&search, 0, 0x5c);
    found.dwSize   = 0x20c;
    search.dwSize  = 0x5c;
    search.dwFlags = D3DFDS_GUID;
    search.guid    = IID_D3D_HAL;

    hr = d3dTmp->FindDevice(&search, &found);
    if (FAILED(hr))
        return (unsigned)hr & 0xffffff00u;  // PRESERVED: no log line

    // PRESERVED: when the HAL result carries no flags hwDesc is never written,
    // and dwDeviceRenderBitDepth is read out of uninitialised stack
    // regardless.
    D3DDEVICEDESC hwDesc;
    if (found.ddHwDesc.dwFlags != 0)
        memcpy(&hwDesc, &found.ddHwDesc, 0xfc);

    d3dTmp->Release();

    self->dwModeFilterFlags = hwDesc.dwDeviceRenderBitDepth;
    sprintf(msg, GS_D3D_RENDER_BITDEPTH, self->dwModeFilterFlags);
    d3d_imagelog(msg);

    // ── Enumerate display modes into self->modeList ──
    sprintf(msg, GS_D3D_START_ENUMMODES);
    d3d_imagelog(msg);
    hr = self->pDD4->EnumDisplayModes(0, NULL, self, d3d_enum_display_modes_cb);
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_ENUMMODES);
    sprintf(msg, GS_D3D_END_ENUMMODES);
    d3d_imagelog(msg);

    // ── Pick the mode, and set it ── PRESERVED: the walk has no NULL check;
    // an index past the end faults.
    if (devfx() == DEVFX_MODE0)
        nModeIndex = 0;
    LinkedListNode *node = self->modeList.pHead;
    for (int i = nModeIndex & 0xff; i != 0; i--)
        node = node->pNextNode;

    DisplayModeNode *mode = (DisplayModeNode *)node->pValue;
    self->pSelectedMode = mode;

    if (mode != NULL) {
        sprintf(msg2, GS_D3D_TRYING_MODE,
                     mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
        d3d_imagelog(msg2);

        mode = self->pSelectedMode;
        hr = self->pDD4->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                        mode->dwBitDepth, 0, 0);
        if (FAILED(hr)) {
            sprintf(msg2, GS_D3D_FAILED_HR, hr);
            d3d_imagelog(msg2);

            // PRESERVED: both fallbacks test pHead and then dereference the
            // mode anyway, so an empty list faults.
            mode = self->modeList.pHead
                 ? (DisplayModeNode *)self->modeList.pHead->pValue : NULL;
            sprintf(msg, GS_D3D_TRYING_FIRST_MODE,
                         mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
            d3d_imagelog(msg);
            hr = self->pDD4->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                            mode->dwBitDepth, 0, 0);
            if (FAILED(hr))
                return d3d_log(self, GS_D3D_ERR_SET_MODE);
            self->pSelectedMode = mode;
        }
    } else {
        mode = self->modeList.pHead
             ? (DisplayModeNode *)self->modeList.pHead->pValue : NULL;
        sprintf(msg, GS_D3D_NO_MODE_SPECIFIED,
                     mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
        d3d_imagelog(msg);
        hr = self->pDD4->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                        mode->dwBitDepth, 0, 0);
        if (FAILED(hr))
            return d3d_log(self, GS_D3D_ERR_SET_MODE);
        self->pSelectedMode = mode;
    }
    d3d_imagelog(GS_D3D_DONE);

    // ── Primary (flipping, complex, 3D) + its attached back buffer ──
    DDSURFACEDESC2 dd;
    memset(&dd, 0, 0x7c);
    dd.dwSize            = 0x7c;
    dd.dwFlags           = 0x21;  // CAPS | BACKBUFFERCOUNT
    dd.ddsCaps.dwCaps    = 0x2218;
    dd.dwBackBufferCount = 1;
    hr = self->pDD4->CreateSurface(&dd, &self->pPrimary, NULL);
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_PRIMARY);

    // PRESERVED: only dwCaps is written; the rest of the DDSCAPS2 is whatever
    // the stack held.
    DDSCAPS2 caps;
    caps.dwCaps = 4;  // DDSCAPS_BACKBUFFER
    hr = self->pPrimary->GetAttachedSurface(&caps, &self->pZBuffer);
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_BACKBUFFER);

    // ── Direct3D3, and the z-buffer pixel format ──
    hr = self->pDD4->QueryInterface(IID_D3D3, (void **)&self->pD3D);
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_D3D3_IFACE);

    if (bHardware) {
        self->pD3D->EnumZBufferFormats(IID_D3D_HAL, d3d_enum_zbuffer_cb,
                                       self->zbufFmt);
    } else {
        hr = self->pD3D->EnumZBufferFormats(IID_D3D_MMX, d3d_enum_zbuffer_cb,
                                            self->zbufFmt);
        if (FAILED(hr))
            self->pD3D->EnumZBufferFormats(IID_D3D_RGB, d3d_enum_zbuffer_cb,
                                           self->zbufFmt);
    }
    if (self->zbufFmt[0] != 0x20)  // DDPIXELFORMAT.dwSize: nothing was kept
        return d3d_log(self, GS_D3D_ERR_ZBUF_FORMAT);

    // ── The z-buffer surface ── The descriptor is reused without re-zeroing,
    // so dwBackBufferCount is still 1; DDSD_BACKBUFFERCOUNT is not set, so
    // DirectDraw ignores it.
    dd.dwSize         = 0x7c;
    dd.dwFlags        = 0x1007;  // CAPS|WIDTH|HEIGHT|PIXELFORMAT
    dd.dwWidth        = self->pSelectedMode->dwWidth;
    dd.dwHeight       = self->pSelectedMode->dwHeight;
    dd.ddsCaps.dwCaps = (bHardware ? 0x3800 : 0) + 0x20800;  // PRESERVED: add, not OR
    memcpy(&dd.ddpfPixelFormat, self->zbufFmt, 8 * sizeof(DWORD));

    sprintf(msg, GS_D3D_ZBUF_BITDEPTH,    self->zbufFmt[3]);
    d3d_imagelog(msg);
    sprintf(msg, GS_D3D_STENCIL_BITDEPTH, self->zbufFmt[4]);
    d3d_imagelog(msg);

    hr = self->pDD4->CreateSurface(&dd, &self->pBackBuffer, NULL);
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_ZBUF_SURFACE);

    hr = self->pZBuffer->AddAttachedSurface(self->pBackBuffer);
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_ATTACH_ZBUF);

    // ── The device, on the back buffer as render target ──
    if (bHardware) {
        hr = self->pD3D->CreateDevice(IID_D3D_HAL, self->pZBuffer,
                                      &self->pDevice, NULL);
    } else {
        hr = self->pD3D->CreateDevice(IID_D3D_MMX, self->pZBuffer,
                                      &self->pDevice, NULL);
        if (FAILED(hr))
            hr = self->pD3D->CreateDevice(IID_D3D_RGB, self->pZBuffer,
                                          &self->pDevice, NULL);
    }
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_CREATE_DEVICE);

    // ── The viewport ── aspect = height / width, both loaded as integers.
    float aspect = (float)((double)(int)self->pSelectedMode->dwHeight
                         / (double)(int)self->pSelectedMode->dwWidth);

    D3DVIEWPORT2 vp;
    memset(&vp, 0, 0x2c);
    vp.dwSize       = 0x2c;
    vp.dwWidth      = self->pSelectedMode->dwWidth;
    vp.dwHeight     = self->pSelectedMode->dwHeight;
    vp.dvClipX      = -1.0f;
    vp.dvClipWidth  = 2.0f;
    vp.dvMinZ       = 0.0f;
    vp.dvMaxZ       = 1.0f;
    vp.dvClipY      = aspect;  // PRESERVED: +aspect, so the clip is not centred
    vp.dvClipHeight = aspect + aspect;

    if (devfx() == DEVFX_HALFVP) {
        vp.dwWidth  /= 2;
        vp.dwHeight /= 2;
    }

    hr = self->pD3D->CreateViewport(&self->pViewport, NULL);
    if (FAILED(hr))
        return d3d_log(self, GS_D3D_ERR_CREATE_VP);

    self->pDevice->AddViewport(self->pViewport);
    self->pViewport->SetViewport2(&vp);
    hr = self->pDevice->SetCurrentViewport(self->pViewport);

    log_write("direct3d: CreateD3DDevice hwnd=%p guid=%p mode=%d hw=%s -> "
              "%lux%lux%lu dd4=%p d3d=%p dev=%p vp=%p primary=%p "
              "surf34=%p surf3c=%p filter=%08lX zdepth=%lu stencil=%lu\n",
              hWnd, pDriverGuid, nModeIndex & 0xff, bHardware?"TRUE":"FALSE",
              self->pSelectedMode->dwWidth, self->pSelectedMode->dwHeight,
              self->pSelectedMode->dwBitDepth,
              self->pDD4, self->pD3D, self->pDevice, self->pViewport,
              self->pPrimary, self->pBackBuffer, self->pZBuffer,
              self->dwModeFilterFlags, self->zbufFmt[3], self->zbufFmt[4]);

    if (devdiag())
        log_write("direct3d: DIAG modesSeen=%u modesKept=%u zfmtSeen=%u "
                  "zfmtKept=%u logLines=%u\n",
                  g_devdiag.modesSeen, g_devdiag.modesKept,
                  g_devdiag.zfmtSeen, g_devdiag.zfmtKept, g_devdiag.logLines);

    return ((unsigned)hr & 0xffffff00u) | 1u;
}
