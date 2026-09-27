/* RenderDevice: presentation, teardown and lifecycle (renderdevice.h).
 * Device creation is in createdevice.cpp.
 *
 * KAROO_FLIP_FX=noblt makes PresentImage skip the Blt and flip whatever is
 * already on the back buffer: the loading/theme bitmap never appears. */

#include "renderdevice.h"
#include "log.h"
#include <string.h>

RenderDevice *g_renderDevice;

#define FLIP_LOG_FIRST 8

static bool fx_noblt(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_FLIP_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "noblt") == 0);
        log_write("renderdevice: flip FX mode = %s\n", cached ? "noblt" : "off");
    }
    return cached != 0;
}

RenderDevice::RenderDevice()
    : pD3D(NULL), pViewport(NULL), pDevice(NULL), dwModeFilterFlags(0),
      pPrimary(NULL), pBackBuffer(NULL), pZBuffer(NULL), pSelectedMode(NULL),
      pDD4(NULL), hWnd(NULL)
{
    memset(&zbufFmt, 0, sizeof(zbufFmt));
    lastError_[0] = '\0';
}

RenderDevice::~RenderDevice()
{
    Release();
}

void RenderDevice::PresentImage(LoadedImage *img)
{
    HRESULT hr_blt = S_OK;
    bool skipped = fx_noblt();

    if (!skipped)
        hr_blt = pBackBuffer->Blt(NULL, img->pTextureSurface, NULL,
                                  DDBLT_WAIT, NULL);

    HRESULT hr_flip = pPrimary->Flip(NULL, 1);

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= FLIP_LOG_FIRST) {
        char blt[16];
        if (skipped)
            lstrcpyA(blt, "skipped");
        else
            wsprintfA(blt, "%08lX", hr_blt);
        log_write("renderdevice: PresentImage img=%p src=%p back=%p primary=%p "
                  "blt=%s flip=%08lX\n",
                  img, img->pTextureSurface, pBackBuffer, pPrimary,
                  blt, hr_flip);
    }
}

/* The back buffer came from GetAttachedSurface, which AddRef'd it, so it is
 * Released like the rest.  (REVIEW: the original NULLed it without a
 * Release, leaking that reference.) */
void RenderDevice::Release()
{
    if (pViewport)   { pViewport->Release();   pViewport   = NULL; }
    if (pDevice)     { pDevice->Release();     pDevice     = NULL; }
    if (pZBuffer)    { pZBuffer->Release();    pZBuffer    = NULL; }
    if (pBackBuffer) { pBackBuffer->Release(); pBackBuffer = NULL; }
    if (pPrimary)    { pPrimary->Release();    pPrimary    = NULL; }
    if (pD3D)        { pD3D->Release();        pD3D        = NULL; }
    if (pDD4)        { pDD4->Release();        pDD4        = NULL; }

    modes.clear();
    pSelectedMode     = NULL;
    dwModeFilterFlags = 0;
    memset(&zbufFmt, 0, sizeof(zbufFmt));
}
