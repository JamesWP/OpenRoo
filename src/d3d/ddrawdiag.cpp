/* KAROO_DDRAW_DIAG's logging (ddrawdiag.h). */

#include "ddrawdiag.h"
#include "logger.h"

bool ddiag_on(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[8];
        cached = GetEnvironmentVariableA("KAROO_DDRAW_DIAG", buf, sizeof(buf))
                 && buf[0] != '0';
        if (cached)
            g_logger.write("ddraw_diag: active\n");
    }
    return cached != 0;
}

/* DWORD-wise dump: the structures below are read by the game as dword arrays
 * (D3DDEVICEDESC is 0xfc bytes = 0x3f dwords), so dwords are the useful unit. */
static void ddiag_dwords(const char *tag, const void *p, unsigned nbytes)
{
    const DWORD *d = (const DWORD *)p;
    unsigned n = nbytes / 4;
    for (unsigned i = 0; i < n; i += 8) {
        char line[256];
        int o = wsprintfA(line, "ddraw_diag: %s[%02u]", tag, i);
        for (unsigned j = i; j < i + 8 && j < n; j++)
            o += wsprintfA(line + o, " %08lX", (unsigned long)d[j]);
        g_logger.write("%s\n", line);
    }
}

void ddiag_pixfmt(const char *tag, const DDPIXELFORMAT *pf)
{
    if (!ddiag_on())
        return;
    g_logger.write("ddraw_diag: %s size=%lu flags=%08lX fourcc=%08lX bits=%lu "
              "r=%08lX g=%08lX b=%08lX a=%08lX\n",
              tag, (unsigned long)pf->dwSize, (unsigned long)pf->dwFlags,
              (unsigned long)pf->dwFourCC, (unsigned long)pf->dwRGBBitCount,
              (unsigned long)pf->dwRBitMask, (unsigned long)pf->dwGBitMask,
              (unsigned long)pf->dwBBitMask, (unsigned long)pf->dwRGBAlphaBitMask);
}

void ddiag_mode(const DDSURFACEDESC2 *d)
{
    if (!ddiag_on())
        return;
    g_logger.write("ddraw_diag: mode %lux%lux%lu pitch=%ld refresh=%lu flags=%08lX caps=%08lX\n",
              (unsigned long)d->dwWidth, (unsigned long)d->dwHeight,
              (unsigned long)d->ddpfPixelFormat.dwRGBBitCount,
              (long)d->lPitch, (unsigned long)d->dwRefreshRate,
              (unsigned long)d->dwFlags, (unsigned long)d->ddsCaps.dwCaps);
    ddiag_pixfmt("  mode.pf", &d->ddpfPixelFormat);
}

void ddiag_find_device(HRESULT hr, const D3DFINDDEVICERESULT *result)
{
    static LONG times = 0;
    if (!ddiag_on() || InterlockedIncrement(&times) != 1)
        return;
    g_logger.write("ddraw_diag: FindDevice hr=%08lX result=%p\n",
              (unsigned long)hr, result);
    if (SUCCEEDED(hr) && result)
        ddiag_dwords("finddev", result, 0x20c);
}

void ddiag_device_caps(HRESULT hr, const void *hal, const void *hel)
{
    static LONG times = 0;
    if (!ddiag_on() || InterlockedIncrement(&times) != 1)
        return;
    g_logger.write("ddraw_diag: Device3::GetCaps hr=%08lX hal=%p hel=%p\n",
              (unsigned long)hr, hal, hel);
    if (SUCCEEDED(hr)) {
        ddiag_dwords("devdesc.hal", hal, 0xfc);
        ddiag_dwords("devdesc.hel", hel, 0xfc);
    }
}

void ddiag_create_surface(HRESULT hr, const DDSURFACEDESC2 *d)
{
    if (!ddiag_on())
        return;
    g_logger.write("ddraw_diag: CreateSurface hr=%08lX flags=%08lX caps=%08lX "
              "%lux%lu bbc=%lu stage=%lu\n",
              (unsigned long)hr, (unsigned long)d->dwFlags,
              (unsigned long)d->ddsCaps.dwCaps,
              (unsigned long)d->dwWidth, (unsigned long)d->dwHeight,
              (unsigned long)d->dwBackBufferCount,
              (unsigned long)d->dwTextureStage);
    if (d->dwFlags & DDSD_PIXELFORMAT)
        ddiag_pixfmt("  cs.pf", &d->ddpfPixelFormat);
}

void ddiag_surface_desc(HRESULT hr, const DDSURFACEDESC2 *d)
{
    static LONG times = 0;
    if (!ddiag_on() || FAILED(hr) || InterlockedIncrement(&times) > 8)
        return;
    g_logger.write("ddraw_diag: GetSurfaceDesc flags=%08lX caps=%08lX %lux%lu pitch=%ld\n",
              (unsigned long)d->dwFlags, (unsigned long)d->ddsCaps.dwCaps,
              (unsigned long)d->dwWidth, (unsigned long)d->dwHeight,
              (long)d->lPitch);
    ddiag_pixfmt("  gsd.pf", &d->ddpfPixelFormat);
}

void ddiag_lock(HRESULT hr, DWORD flags, const DDSURFACEDESC2 *d)
{
    static LONG times = 0;
    if (!ddiag_on() || FAILED(hr) || InterlockedIncrement(&times) > 8)
        return;
    g_logger.write("ddraw_diag: Lock flags=%08lX -> dflags=%08lX %lux%lu "
              "pitch=%ld bits=%p\n",
              (unsigned long)flags, (unsigned long)d->dwFlags,
              (unsigned long)d->dwWidth, (unsigned long)d->dwHeight,
              (long)d->lPitch, d->lpSurface);
    ddiag_pixfmt("  lock.pf", &d->ddpfPixelFormat);
}
