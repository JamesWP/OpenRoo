/* Direct3D::CreateD3DDevice (0x412740) reimplementation.
 *
 * The last target on RENDER_PLAN.md's list, and the one it kept calling
 * "high value, high risk": 2112 bytes, 20 COM dispatches, and the function
 * that brings up DirectDraw, the display mode, the surface chain, Direct3D,
 * the device and the viewport.  Everything the rest of the render path
 * assumes exists is created here.
 *
 * THE SIGNATURE IS FOUR ARGUMENTS, NOT TWO.  Ghidra types it
 * `(Direct3D *this, int *param_1, GUID *param_2)` and then uses `param_1`
 * both as the window handle and as `(uint)param_1 & 0xff`, a list index, and
 * `param_2` both as a GUID pointer and as `(char)param_2`, a flag.  Neither
 * can be true.  The epilogue is `RET 0x10` — four stack dwords — and the
 * frame arithmetic (ESP sits 0x640 below the entry ESP after `SUB ESP,0x630`
 * plus four pushes, so the arguments are at ESP+0x644 .. ESP+0x650) places
 * them as:
 *
 *   arg1  +0x644   HWND     hWnd          stored to this->hWnd
 *   arg2  +0x648   GUID    *pDriverGuid   passed to DirectDrawCreate
 *   arg3  +0x64c   int      nModeIndex    index into this->modeList (low byte)
 *   arg4  +0x650   char     bHardware     HAL, or MMX-then-RGB
 *
 * The three WinMain call sites (0x42d362 / 0x42d386 / 0x42d3a0) confirm it:
 * each pushes four dwords, and together they form a retry ladder — first with
 * the driver GUID from the Game struct (game+0x2aa13e) and the configured
 * mode index (game+0x2aa14e), then with no GUID, then with no GUID and mode 0.
 *
 * bHardware IS 1 AT ALL THREE CALL SITES.  The MMX/RGB fallbacks and the
 * system-memory z-buffer below are therefore dead code in this build, the
 * same way BindTextureResource never runs (RENDER_PLAN.md, 2026-09-02).  They
 * are reimplemented anyway, but nothing at runtime exercises them, and a
 * green replay suite is not evidence about them.
 *
 * The three device GUIDs are decoded from .rdata rather than guessed:
 *   0x45daa8 = a4665c60-2673-11cf-a31a-00aa00b93356  IID_IDirect3DRGBDevice
 *   0x45dab8 = 84e63de0-46aa-11cf-816f-0000c020156e  IID_IDirect3DHALDevice
 *   0x45dac8 = 881949a1-d6f3-11d0-89ab-00a0c9054129  IID_IDirect3DMMXDevice
 * and 0x45da88 = bb223240-e72b-11d0-a9b4-00aa00c0993e = IID_IDirect3D3, the
 * IID both QueryInterface calls use.  0x45d768 is IID_IDirectDraw4.
 *
 * What Ghidra calls `DuplicateSurface` at slot 0x1c of the first, temporary
 * IDirect3D3 is IDirect3D3::FindDevice.  It searches for the HAL device
 * (D3DFINDDEVICESEARCH { dwSize 0x5c, dwFlags D3DFDS_GUID, guid HAL }) purely
 * to read `ddHwDesc.dwDeviceRenderBitDepth` out of the 0x20c-byte result and
 * park it in this->dwModeFilterFlags, which EnumDisplayModesCallback then
 * uses to filter modes.  That interface is queried off pDD4 and released
 * immediately; it is *not* this->pD3D, which is queried again later.
 *
 * Two callbacks stay game-owned and are passed at their original addresses:
 *   0x413000 EnumDisplayModesCallback   — builds this->modeList
 *   0x413100 EnumZBufferFormatsCallback — fills this->zbufFmt (DDPIXELFORMAT)
 *
 * The field names in direct3d.h are, for the record, wrong, and this function
 * is what proves it: +0x34 ("pBackBuffer") receives the CreateSurface'd
 * DDSCAPS_ZBUFFER surface, and +0x3c ("pZBuffer") receives the
 * GetAttachedSurface(DDSCAPS_BACKBUFFER) result — which is why
 * FlipPrimaryFrame Blts to "pZBuffer" and why ReleaseResources releases
 * "pBackBuffer" but not "pZBuffer".  They are left as they are because
 * ReleaseResources and FlipPrimaryFrame already ship against these names;
 * renaming is a separate mechanical change, not part of this one.
 *
 * Defects and oddities preserved deliberately:
 *   - `hwDesc` is only filled when the FindDevice result's ddHwDesc.dwFlags
 *     is non-zero.  When it is zero the function goes on to read
 *     dwDeviceRenderBitDepth out of *uninitialised stack* and store it in
 *     this->dwModeFilterFlags.  Reproduced: the local is left uninitialised.
 *   - The mode-list walk is `for (i = idx & 0xff; i--; n = n->pNextNode)`
 *     with no NULL check.  An index past the end of the list faults.
 *   - The "no mode specified" and "first mode" fallbacks null-check pHead and
 *     then dereference the resulting node pointer anyway, so an empty list
 *     faults on the very next instruction.
 *   - The z-buffer caps are computed as `0x20800 + (bHardware ? 0x3800 : 0)`
 *     — an *addition*, not an OR, which happens to turn
 *     ZBUFFER|SYSTEMMEMORY into ZBUFFER|VIDEOMEMORY.  Kept as the addition.
 *   - dvClipY is +aspect and dvClipHeight is 2*aspect, so the clip rectangle
 *     runs from +aspect downwards rather than being centred the way
 *     dvClipX/-1.0 and dvClipWidth/2.0 are.  Kept.
 *   - The DDSURFACEDESC2 is reused for the z-buffer without being re-zeroed,
 *     so dwBackBufferCount is still 1 from the primary.  DDSD_BACKBUFFERCOUNT
 *     is not set the second time, so DirectDraw ignores it.  Kept.
 *   - On success the low byte of the return is 1 and the upper three bytes
 *     are whatever SetCurrentViewport's HRESULT left in EAX.  Callers test AL
 *     only; the garbage is reproduced anyway.
 *   - Log() returns strlen(msg) & 0xffffff00, i.e. AL = 0, which is how every
 *     error path returns FALSE.  Its value is returned unchanged.
 *   - Two early failures (the first QueryInterface and FindDevice) return
 *     hr & 0xffffff00 with no log line at all, unlike every other error path.
 *
 * ReleaseResources is called through our own export, not 0x413180 — that
 * address is a UD2 safety stub.
 *
 * Every surface, device, viewport and DirectDraw interface produced here is
 * wrapped by the com_proxy layer on the way out, exactly as before: the
 * DirectDrawCreate IAT hook and the w4_CreateSurface / w3_CreateDevice /
 * w3_CreateViewport wrappers all sit underneath these calls.
 */
#include "direct3d.h"
#include "log.h"
#include "gamestr.h"
#include "gameglobals.h"
#include <string.h>

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Direct3D_ReleaseResources(Direct3D *self);

typedef HRESULT (WINAPI *ddcreate_fn)(GUID *, LPDIRECTDRAW *, IUnknown *);
#define ORIG_DIRECTDRAWCREATE ((ddcreate_fn)0x004417fe)

typedef unsigned (__attribute__((thiscall)) *d3dlog_fn)(Direct3D *, const char *);
#define ORIG_D3D_LOG ((d3dlog_fn)0x00412f80)

typedef int (__cdecl *sprintf_fn)(char *, const char *, ...);
#define ORIG_SPRINTF ((sprintf_fn)0x00450655)

typedef unsigned (__cdecl *fwrite_fn)(const char *, unsigned, unsigned, FILE *);
#define ORIG_FWRITE ((fwrite_fn)0x004513c7)

#define ORIG_ENUMDISPLAYMODES_CB ((LPDDENUMMODESCALLBACK2)0x00413000)
#define ORIG_ENUMZBUFFER_CB      ((LPD3DENUMPIXELFORMATSCALLBACK)0x00413100)

#define IID_D3D_RGB  (*(const GUID *)0x0045daa8)
#define IID_D3D_HAL  (*(const GUID *)0x0045dab8)
#define IID_D3D_MMX  (*(const GUID *)0x0045dac8)
#define IID_D3D3     (*(const IID  *)0x0045da88)
#define IID_DD4      (*(const IID  *)0x0045d768)

/* Format strings, at their original addresses — the German error messages go
 * to Direct3D::Log (which also copies them into this->pLastError), the
 * English progress lines to the image log. */

/* The original's log idiom: sprintf into a stack buffer, then write strlen
 * bytes of it to the image log.  Both helpers are shared and stay live. */
static void d3d_imagelog(const char *s)
{
    ORIG_FWRITE(s, (unsigned)lstrlenA(s), 1, GG_LOG_STREAM);
}

/* ── KAROO_D3DDEV_FX — visual proof ──
 *
 * The UD2 stub at 0x412740 already proves the original never runs, but the
 * working practices want something visible on screen, and a colour tint has
 * nothing to tint here: this function draws no pixels, it builds the device
 * every other draw goes through.  So both modes change *geometry*, which a
 * passthrough could not fake:
 *
 *   halfvp — halve the viewport's dwWidth/dwHeight.  The whole scene renders
 *            into the top-left quarter of the screen.  Only this function
 *            builds the D3DVIEWPORT2 that SetViewport2 receives.
 *   mode0  — force nModeIndex to 0 regardless of what WinMain passed.  The
 *            game comes up in a *different screen resolution* than Karoo.cfg
 *            asks for, which proves the mode-list walk and the SetDisplayMode
 *            path, not merely that the function was entered.
 *
 * Read by value, never by presence (GetEnvironmentVariableA returns 0 for
 * empty and unset alike). */
enum { DEVFX_OFF = 0, DEVFX_HALFVP, DEVFX_MODE0 };

static int devfx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = DEVFX_OFF;
        if (GetEnvironmentVariableA("KAROO_D3DDEV_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "halfvp") == 0)     cached = DEVFX_HALFVP;
            else if (lstrcmpiA(buf, "mode0") == 0) cached = DEVFX_MODE0;
        }
        log_write("direct3d: device FX mode = %s\n",
                  cached == DEVFX_HALFVP ? "halfvp" :
                  cached == DEVFX_MODE0  ? "mode0"  : "off");
    }
    return cached;
}

extern "C" __declspec(dllexport) unsigned __attribute__((thiscall))
Direct3D_CreateD3DDevice(Direct3D *self, HWND hWnd, GUID *pDriverGuid,
                         int nModeIndex, char bHardware)
{
    char msg[256];
    char msg2[260];

    Direct3D_ReleaseResources(self);
    self->hWnd = hWnd;

    /* ── DirectDraw, and the DirectDraw4 interface everything else uses ── */
    LPDIRECTDRAW dd1 = NULL;
    HRESULT hr = ORIG_DIRECTDRAWCREATE(pDriverGuid, &dd1, NULL);
    if (FAILED(hr)) {
        hr = ORIG_DIRECTDRAWCREATE(NULL, &dd1, NULL);
        if (FAILED(hr))
            return ORIG_D3D_LOG(self, GS_D3D_ERR_DDRAW_CREATE);
    }

    hr = dd1->QueryInterface(IID_DD4, (void **)&self->pDD4);
    if (FAILED(hr))
        return ORIG_D3D_LOG(self, GS_D3D_ERR_DD4_IFACE);
    dd1->Release();

    hr = self->pDD4->SetCooperativeLevel(hWnd, 0x811);
    if (FAILED(hr))
        return ORIG_D3D_LOG(self, GS_D3D_ERR_COOP_LEVEL);

    /* ── FindDevice(HAL), for dwDeviceRenderBitDepth only ── */
    IDirect3D3 *d3dTmp = NULL;
    hr = self->pDD4->QueryInterface(IID_D3D3, (void **)&d3dTmp);
    if (FAILED(hr))
        return (unsigned)hr & 0xffffff00u;    /* AL = 0, no log line */

    D3DFINDDEVICERESULT found;
    D3DFINDDEVICESEARCH search;
    memset(&found,  0, 0x20c);                /* 0x83 dwords, rep stosd */
    memset(&search, 0, 0x5c);                 /* 0x17 dwords, rep stosd */
    found.dwSize   = 0x20c;
    search.dwSize  = 0x5c;
    search.dwFlags = D3DFDS_GUID;             /* 2 */
    search.guid    = IID_D3D_HAL;

    hr = d3dTmp->FindDevice(&search, &found);
    if (FAILED(hr))
        return (unsigned)hr & 0xffffff00u;    /* AL = 0, no log line */

    /* Deliberately uninitialised — see the header comment.  When the HAL
     * result carries no flags the original never writes this buffer, and then
     * reads dwDeviceRenderBitDepth out of it regardless. */
    D3DDEVICEDESC hwDesc;
    if (found.ddHwDesc.dwFlags != 0)
        memcpy(&hwDesc, &found.ddHwDesc, 0xfc);   /* 0x3f dwords */

    d3dTmp->Release();

    self->dwModeFilterFlags = hwDesc.dwDeviceRenderBitDepth;
    ORIG_SPRINTF(msg, GS_D3D_RENDER_BITDEPTH, self->dwModeFilterFlags);
    d3d_imagelog(msg);

    /* ── Enumerate display modes into self->modeList ── */
    ORIG_SPRINTF(msg, GS_D3D_START_ENUMMODES);
    d3d_imagelog(msg);
    hr = self->pDD4->EnumDisplayModes(0, NULL, self, ORIG_ENUMDISPLAYMODES_CB);
    if (FAILED(hr))
        return ORIG_D3D_LOG(self, GS_D3D_ERR_ENUMMODES);
    ORIG_SPRINTF(msg, GS_D3D_END_ENUMMODES);
    d3d_imagelog(msg);

    /* ── Pick the mode, and set it ──
     * The walk is unguarded: no NULL check on pNextNode.  Preserved. */
    if (devfx() == DEVFX_MODE0)
        nModeIndex = 0;
    LinkedListNode *node = self->modeList.pHead;
    for (int i = nModeIndex & 0xff; i != 0; i--)
        node = node->pNextNode;

    DisplayModeNode *mode = (DisplayModeNode *)node->pValue;
    self->pSelectedMode = mode;

    if (mode != NULL) {
        ORIG_SPRINTF(msg2, GS_D3D_TRYING_MODE,
                     mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
        d3d_imagelog(msg2);

        mode = self->pSelectedMode;           /* re-read; the original does */
        hr = self->pDD4->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                        mode->dwBitDepth, 0, 0);
        if (FAILED(hr)) {
            ORIG_SPRINTF(msg2, GS_D3D_FAILED_HR, hr);
            d3d_imagelog(msg2);

            mode = self->modeList.pHead
                 ? (DisplayModeNode *)self->modeList.pHead->pValue : NULL;
            ORIG_SPRINTF(msg, GS_D3D_TRYING_FIRST_MODE,
                         mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
            d3d_imagelog(msg);
            hr = self->pDD4->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                            mode->dwBitDepth, 0, 0);
            if (FAILED(hr))
                return ORIG_D3D_LOG(self, GS_D3D_ERR_SET_MODE);
            self->pSelectedMode = mode;
        }
    } else {
        mode = self->modeList.pHead
             ? (DisplayModeNode *)self->modeList.pHead->pValue : NULL;
        ORIG_SPRINTF(msg, GS_D3D_NO_MODE_SPECIFIED,
                     mode->dwWidth, mode->dwHeight, mode->dwBitDepth);
        d3d_imagelog(msg);
        hr = self->pDD4->SetDisplayMode(mode->dwWidth, mode->dwHeight,
                                        mode->dwBitDepth, 0, 0);
        if (FAILED(hr))
            return ORIG_D3D_LOG(self, GS_D3D_ERR_SET_MODE);
        self->pSelectedMode = mode;
    }
    d3d_imagelog(GS_D3D_DONE);

    /* ── Primary (flipping, complex, 3D) + its attached back buffer ── */
    DDSURFACEDESC2 dd;
    memset(&dd, 0, 0x7c);                     /* 0x1f dwords */
    dd.dwSize            = 0x7c;
    dd.dwFlags           = 0x21;              /* CAPS | BACKBUFFERCOUNT */
    dd.ddsCaps.dwCaps    = 0x2218;
    dd.dwBackBufferCount = 1;
    hr = self->pDD4->CreateSurface(&dd, &self->pPrimary, NULL);
    if (FAILED(hr))
        return ORIG_D3D_LOG(self, GS_D3D_ERR_PRIMARY);

    /* Only dwCaps is written; the rest of the DDSCAPS2 is whatever the stack
     * held.  Preserved. */
    DDSCAPS2 caps;
    caps.dwCaps = 4;                          /* DDSCAPS_BACKBUFFER */
    hr = self->pPrimary->GetAttachedSurface(&caps, &self->pZBuffer);
    if (FAILED(hr))
        return ORIG_D3D_LOG(self, GS_D3D_ERR_BACKBUFFER);

    /* ── Direct3D3, and the z-buffer pixel format ── */
    hr = self->pDD4->QueryInterface(IID_D3D3, (void **)&self->pD3D);
    if (FAILED(hr))
        return ORIG_D3D_LOG(self, GS_D3D_ERR_D3D3_IFACE);

    if (bHardware) {
        self->pD3D->EnumZBufferFormats(IID_D3D_HAL, ORIG_ENUMZBUFFER_CB,
                                       self->zbufFmt);
    } else {
        hr = self->pD3D->EnumZBufferFormats(IID_D3D_MMX, ORIG_ENUMZBUFFER_CB,
                                            self->zbufFmt);
        if (FAILED(hr))
            self->pD3D->EnumZBufferFormats(IID_D3D_RGB, ORIG_ENUMZBUFFER_CB,
                                           self->zbufFmt);
    }
    if (self->zbufFmt[0] != 0x20)             /* DDPIXELFORMAT.dwSize */
        return ORIG_D3D_LOG(self, GS_D3D_ERR_ZBUF_FORMAT);

    /* ── The z-buffer surface ── */
    dd.dwSize         = 0x7c;
    dd.dwFlags        = 0x1007;               /* CAPS|WIDTH|HEIGHT|PIXELFORMAT */
    dd.dwWidth        = self->pSelectedMode->dwWidth;
    dd.dwHeight       = self->pSelectedMode->dwHeight;
    dd.ddsCaps.dwCaps = (bHardware ? 0x3800 : 0) + 0x20800;  /* add, not OR */
    memcpy(&dd.ddpfPixelFormat, self->zbufFmt, 8 * sizeof(DWORD));

    ORIG_SPRINTF(msg, GS_D3D_ZBUF_BITDEPTH,    self->zbufFmt[3]);  /* +0x20 */
    d3d_imagelog(msg);
    ORIG_SPRINTF(msg, GS_D3D_STENCIL_BITDEPTH, self->zbufFmt[4]);  /* +0x24 */
    d3d_imagelog(msg);

    hr = self->pDD4->CreateSurface(&dd, &self->pBackBuffer, NULL);
    if (FAILED(hr))
        return ORIG_D3D_LOG(self, GS_D3D_ERR_ZBUF_SURFACE);

    hr = self->pZBuffer->AddAttachedSurface(self->pBackBuffer);
    if (FAILED(hr))
        return ORIG_D3D_LOG(self, GS_D3D_ERR_ATTACH_ZBUF);

    /* ── The device, on the back buffer as render target ── */
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
        return ORIG_D3D_LOG(self, GS_D3D_ERR_CREATE_DEVICE);

    /* ── The viewport ──
     * aspect = (double)height / (double)width, via FILD/FIDIV: both operands
     * are loaded as integers, so this is an integer-to-double divide, not a
     * float divide of two already-converted values. */
    float aspect = (float)((double)(int)self->pSelectedMode->dwHeight
                         / (double)(int)self->pSelectedMode->dwWidth);

    D3DVIEWPORT2 vp;
    memset(&vp, 0, 0x2c);                     /* 0xb dwords */
    vp.dwSize       = 0x2c;
    vp.dwWidth      = self->pSelectedMode->dwWidth;
    vp.dwHeight     = self->pSelectedMode->dwHeight;
    vp.dvClipX      = -1.0f;
    vp.dvClipWidth  = 2.0f;
    vp.dvMinZ       = 0.0f;
    vp.dvMaxZ       = 1.0f;
    vp.dvClipY      = aspect;                 /* not -aspect; preserved */
    vp.dvClipHeight = aspect + aspect;

    if (devfx() == DEVFX_HALFVP) {
        vp.dwWidth  /= 2;
        vp.dwHeight /= 2;
    }

    hr = self->pD3D->CreateViewport(&self->pViewport, NULL);
    if (FAILED(hr))
        return ORIG_D3D_LOG(self, GS_D3D_ERR_CREATE_VP);

    self->pDevice->AddViewport(self->pViewport);
    self->pViewport->SetViewport2(&vp);
    hr = self->pDevice->SetCurrentViewport(self->pViewport);

    log_write("direct3d: CreateD3DDevice hwnd=%p guid=%p mode=%d hw=%d -> "
              "%lux%lux%lu dd4=%p d3d=%p dev=%p vp=%p primary=%p "
              "surf34=%p surf3c=%p filter=%08lX zdepth=%lu stencil=%lu\n",
              hWnd, pDriverGuid, nModeIndex & 0xff, (int)bHardware,
              self->pSelectedMode->dwWidth, self->pSelectedMode->dwHeight,
              self->pSelectedMode->dwBitDepth,
              self->pDD4, self->pD3D, self->pDevice, self->pViewport,
              self->pPrimary, self->pBackBuffer, self->pZBuffer,
              self->dwModeFilterFlags, self->zbufFmt[3], self->zbufFmt[4]);

    /* AL = 1; the upper three bytes are SetCurrentViewport's HRESULT. */
    return ((unsigned)hr & 0xffffff00u) | 1u;
}
