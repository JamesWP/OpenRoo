#include "com_proxy.h"

typedef HRESULT (WINAPI *DirectDrawCreate_t)(GUID *, LPDIRECTDRAW *, IUnknown *);

#define NOINLINE __attribute__((noinline))

/* IID_IDirectDraw4 = {9c59509a-39bd-11d1-8c4a-00c04fd930c5} */
static const GUID IID_IDirectDraw4_g = {
    0x9c59509a, 0x39bd, 0x11d1,
    {0x8c, 0x4a, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5}
};

/* Extract the real IDirectDraw4 pointer from a proxy. */
static inline IDirectDraw4 *real_dd4(IDirectDraw4 *self)
{
    return (IDirectDraw4 *)((ComProxy *)self)->real;
}

/* --- IDirectDraw4 wrapper functions (slots 0-27) --- */

static HRESULT WINAPI NOINLINE w4_QueryInterface(IDirectDraw4 *s, REFIID r, void **p)
    { return real_dd4(s)->QueryInterface(r, p); }
static ULONG   WINAPI NOINLINE w4_AddRef(IDirectDraw4 *s)
    { return real_dd4(s)->AddRef(); }
static ULONG   WINAPI NOINLINE w4_Release(IDirectDraw4 *s)
    { return real_dd4(s)->Release(); }
static HRESULT WINAPI NOINLINE w4_Compact(IDirectDraw4 *s)
    { return real_dd4(s)->Compact(); }
static HRESULT WINAPI NOINLINE w4_CreateClipper(IDirectDraw4 *s, DWORD f, LPDIRECTDRAWCLIPPER *pp, IUnknown *u)
    { return real_dd4(s)->CreateClipper(f, pp, u); }
static HRESULT WINAPI NOINLINE w4_CreatePalette(IDirectDraw4 *s, DWORD f, LPPALETTEENTRY pe, LPDIRECTDRAWPALETTE *pp, IUnknown *u)
    { return real_dd4(s)->CreatePalette(f, pe, pp, u); }
static HRESULT WINAPI NOINLINE w4_CreateSurface(IDirectDraw4 *s, LPDDSURFACEDESC2 d, LPDIRECTDRAWSURFACE4 *pp, IUnknown *u)
    { return real_dd4(s)->CreateSurface(d, pp, u); }
static HRESULT WINAPI NOINLINE w4_DuplicateSurface(IDirectDraw4 *s, LPDIRECTDRAWSURFACE4 src, LPDIRECTDRAWSURFACE4 *pp)
    { return real_dd4(s)->DuplicateSurface(src, pp); }
static HRESULT WINAPI NOINLINE w4_EnumDisplayModes(IDirectDraw4 *s, DWORD f, LPDDSURFACEDESC2 d, LPVOID ctx, LPDDENUMMODESCALLBACK2 cb)
    { return real_dd4(s)->EnumDisplayModes(f, d, ctx, cb); }
static HRESULT WINAPI NOINLINE w4_EnumSurfaces(IDirectDraw4 *s, DWORD f, LPDDSURFACEDESC2 d, LPVOID ctx, LPDDENUMSURFACESCALLBACK2 cb)
    { return real_dd4(s)->EnumSurfaces(f, d, ctx, cb); }
static HRESULT WINAPI NOINLINE w4_FlipToGDISurface(IDirectDraw4 *s)
    { return real_dd4(s)->FlipToGDISurface(); }
static HRESULT WINAPI NOINLINE w4_GetCaps(IDirectDraw4 *s, LPDDCAPS dc, LPDDCAPS hc)
    { return real_dd4(s)->GetCaps(dc, hc); }
static HRESULT WINAPI NOINLINE w4_GetDisplayMode(IDirectDraw4 *s, LPDDSURFACEDESC2 d)
    { return real_dd4(s)->GetDisplayMode(d); }
static HRESULT WINAPI NOINLINE w4_GetFourCCCodes(IDirectDraw4 *s, LPDWORD pn, LPDWORD pc)
    { return real_dd4(s)->GetFourCCCodes(pn, pc); }
static HRESULT WINAPI NOINLINE w4_GetGDISurface(IDirectDraw4 *s, LPDIRECTDRAWSURFACE4 *pp)
    { return real_dd4(s)->GetGDISurface(pp); }
static HRESULT WINAPI NOINLINE w4_GetMonitorFrequency(IDirectDraw4 *s, LPDWORD p)
    { return real_dd4(s)->GetMonitorFrequency(p); }
static HRESULT WINAPI NOINLINE w4_GetScanLine(IDirectDraw4 *s, LPDWORD p)
    { return real_dd4(s)->GetScanLine(p); }
static HRESULT WINAPI NOINLINE w4_GetVerticalBlankStatus(IDirectDraw4 *s, LPBOOL p)
    { return real_dd4(s)->GetVerticalBlankStatus(p); }
static HRESULT WINAPI NOINLINE w4_Initialize(IDirectDraw4 *s, GUID *g)
    { return real_dd4(s)->Initialize(g); }
static HRESULT WINAPI NOINLINE w4_RestoreDisplayMode(IDirectDraw4 *s)
    { return real_dd4(s)->RestoreDisplayMode(); }
static HRESULT WINAPI NOINLINE w4_SetCooperativeLevel(IDirectDraw4 *s, HWND h, DWORD f)
    { return real_dd4(s)->SetCooperativeLevel(h, f); }
static HRESULT WINAPI NOINLINE w4_SetDisplayMode(IDirectDraw4 *s, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags)
    { return real_dd4(s)->SetDisplayMode(w, h, bpp, refresh, flags); }
static HRESULT WINAPI NOINLINE w4_WaitForVerticalBlank(IDirectDraw4 *s, DWORD f, HANDLE e)
    { return real_dd4(s)->WaitForVerticalBlank(f, e); }
static HRESULT WINAPI NOINLINE w4_GetAvailableVidMem(IDirectDraw4 *s, LPDDSCAPS2 caps, LPDWORD total, LPDWORD free_)
    { return real_dd4(s)->GetAvailableVidMem(caps, total, free_); }
static HRESULT WINAPI NOINLINE w4_GetSurfaceFromDC(IDirectDraw4 *s, HDC hdc, LPDIRECTDRAWSURFACE4 *pp)
    { return real_dd4(s)->GetSurfaceFromDC(hdc, pp); }
static HRESULT WINAPI NOINLINE w4_RestoreAllSurfaces(IDirectDraw4 *s)
    { return real_dd4(s)->RestoreAllSurfaces(); }
static HRESULT WINAPI NOINLINE w4_TestCooperativeLevel(IDirectDraw4 *s)
    { return real_dd4(s)->TestCooperativeLevel(); }
static HRESULT WINAPI NOINLINE w4_GetDeviceIdentifier(IDirectDraw4 *s, LPDDDEVICEIDENTIFIER p, DWORD f)
    { return real_dd4(s)->GetDeviceIdentifier(p, f); }

static void *s_dd4_vtable_data[28] = {
    (void*)w4_QueryInterface,
    (void*)w4_AddRef,
    (void*)w4_Release,
    (void*)w4_Compact,
    (void*)w4_CreateClipper,
    (void*)w4_CreatePalette,
    (void*)w4_CreateSurface,
    (void*)w4_DuplicateSurface,
    (void*)w4_EnumDisplayModes,
    (void*)w4_EnumSurfaces,
    (void*)w4_FlipToGDISurface,
    (void*)w4_GetCaps,
    (void*)w4_GetDisplayMode,
    (void*)w4_GetFourCCCodes,
    (void*)w4_GetGDISurface,
    (void*)w4_GetMonitorFrequency,
    (void*)w4_GetScanLine,
    (void*)w4_GetVerticalBlankStatus,
    (void*)w4_Initialize,
    (void*)w4_RestoreDisplayMode,
    (void*)w4_SetCooperativeLevel,
    (void*)w4_SetDisplayMode,
    (void*)w4_WaitForVerticalBlank,
    (void*)w4_GetAvailableVidMem,
    (void*)w4_GetSurfaceFromDC,
    (void*)w4_RestoreAllSurfaces,
    (void*)w4_TestCooperativeLevel,
    (void*)w4_GetDeviceIdentifier,
};

static ComProxy s_dd4_proxy;

/* --- IDirectDraw (v1) proxy --- */

/* Extract the real IDirectDraw pointer from a proxy disguised as IDirectDraw. */
static inline IDirectDraw *real_dd(IDirectDraw *self)
{
    return (IDirectDraw *)((ComProxy *)self)->real;
}

/* --- IDirectDraw wrapper functions (slots 0-22) --- */

static HRESULT WINAPI NOINLINE w_QueryInterface(IDirectDraw *s, REFIID riid, void **ppv)
{
    HRESULT hr = real_dd(s)->QueryInterface(riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv &&
        memcmp(&riid, &IID_IDirectDraw4_g, sizeof(GUID)) == 0) {
        s_dd4_proxy.vtable = s_dd4_vtable_data;
        s_dd4_proxy.real   = (IUnknown *)*ppv;
        *ppv = &s_dd4_proxy;
    }
    return hr;
}
static ULONG   WINAPI NOINLINE w_AddRef(IDirectDraw *s)
    { return real_dd(s)->AddRef(); }
static ULONG   WINAPI NOINLINE w_Release(IDirectDraw *s)
    { return real_dd(s)->Release(); }
static HRESULT WINAPI NOINLINE w_Compact(IDirectDraw *s)
    { return real_dd(s)->Compact(); }
static HRESULT WINAPI NOINLINE w_CreateClipper(IDirectDraw *s, DWORD f, LPDIRECTDRAWCLIPPER *pp, IUnknown *u)
    { return real_dd(s)->CreateClipper(f, pp, u); }
static HRESULT WINAPI NOINLINE w_CreatePalette(IDirectDraw *s, DWORD f, LPPALETTEENTRY pe, LPDIRECTDRAWPALETTE *pp, IUnknown *u)
    { return real_dd(s)->CreatePalette(f, pe, pp, u); }
static HRESULT WINAPI NOINLINE w_CreateSurface(IDirectDraw *s, LPDDSURFACEDESC d, LPDIRECTDRAWSURFACE *pp, IUnknown *u)
    { return real_dd(s)->CreateSurface(d, pp, u); }
static HRESULT WINAPI NOINLINE w_DuplicateSurface(IDirectDraw *s, LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE *pp)
    { return real_dd(s)->DuplicateSurface(src, pp); }
static HRESULT WINAPI NOINLINE w_EnumDisplayModes(IDirectDraw *s, DWORD f, LPDDSURFACEDESC d, LPVOID ctx, LPDDENUMMODESCALLBACK cb)
    { return real_dd(s)->EnumDisplayModes(f, d, ctx, cb); }
static HRESULT WINAPI NOINLINE w_EnumSurfaces(IDirectDraw *s, DWORD f, LPDDSURFACEDESC d, LPVOID ctx, LPDDENUMSURFACESCALLBACK cb)
    { return real_dd(s)->EnumSurfaces(f, d, ctx, cb); }
static HRESULT WINAPI NOINLINE w_FlipToGDISurface(IDirectDraw *s)
    { return real_dd(s)->FlipToGDISurface(); }
static HRESULT WINAPI NOINLINE w_GetCaps(IDirectDraw *s, LPDDCAPS dc, LPDDCAPS hc)
    { return real_dd(s)->GetCaps(dc, hc); }
static HRESULT WINAPI NOINLINE w_GetDisplayMode(IDirectDraw *s, LPDDSURFACEDESC d)
    { return real_dd(s)->GetDisplayMode(d); }
static HRESULT WINAPI NOINLINE w_GetFourCCCodes(IDirectDraw *s, LPDWORD pn, LPDWORD pc)
    { return real_dd(s)->GetFourCCCodes(pn, pc); }
static HRESULT WINAPI NOINLINE w_GetGDISurface(IDirectDraw *s, LPDIRECTDRAWSURFACE *pp)
    { return real_dd(s)->GetGDISurface(pp); }
static HRESULT WINAPI NOINLINE w_GetMonitorFrequency(IDirectDraw *s, LPDWORD p)
    { return real_dd(s)->GetMonitorFrequency(p); }
static HRESULT WINAPI NOINLINE w_GetScanLine(IDirectDraw *s, LPDWORD p)
    { return real_dd(s)->GetScanLine(p); }
static HRESULT WINAPI NOINLINE w_GetVerticalBlankStatus(IDirectDraw *s, LPBOOL p)
    { return real_dd(s)->GetVerticalBlankStatus(p); }
static HRESULT WINAPI NOINLINE w_Initialize(IDirectDraw *s, GUID *g)
    { return real_dd(s)->Initialize(g); }
static HRESULT WINAPI NOINLINE w_RestoreDisplayMode(IDirectDraw *s)
    { return real_dd(s)->RestoreDisplayMode(); }
static HRESULT WINAPI NOINLINE w_SetCooperativeLevel(IDirectDraw *s, HWND h, DWORD f)
    { return real_dd(s)->SetCooperativeLevel(h, f); }
static HRESULT WINAPI NOINLINE w_SetDisplayMode(IDirectDraw *s, DWORD w, DWORD h, DWORD bpp)
    { return real_dd(s)->SetDisplayMode(w, h, bpp); }
static HRESULT WINAPI NOINLINE w_WaitForVerticalBlank(IDirectDraw *s, DWORD f, HANDLE e)
    { return real_dd(s)->WaitForVerticalBlank(f, e); }

static void *s_dd_vtable[23] = {
    (void*)w_QueryInterface,
    (void*)w_AddRef,
    (void*)w_Release,
    (void*)w_Compact,
    (void*)w_CreateClipper,
    (void*)w_CreatePalette,
    (void*)w_CreateSurface,
    (void*)w_DuplicateSurface,
    (void*)w_EnumDisplayModes,
    (void*)w_EnumSurfaces,
    (void*)w_FlipToGDISurface,
    (void*)w_GetCaps,
    (void*)w_GetDisplayMode,
    (void*)w_GetFourCCCodes,
    (void*)w_GetGDISurface,
    (void*)w_GetMonitorFrequency,
    (void*)w_GetScanLine,
    (void*)w_GetVerticalBlankStatus,
    (void*)w_Initialize,
    (void*)w_RestoreDisplayMode,
    (void*)w_SetCooperativeLevel,
    (void*)w_SetDisplayMode,
    (void*)w_WaitForVerticalBlank,
};

static ComProxy s_dd_proxy;

extern "C" __declspec(dllexport) HRESULT WINAPI hooks_DirectDrawCreate(
        GUID *lpGUID, LPDIRECTDRAW *lplpDD, IUnknown *pUnkOuter)
{
    HMODULE ddraw = GetModuleHandleA("ddraw.dll");
    DirectDrawCreate_t real_fn = ddraw
        ? (DirectDrawCreate_t)GetProcAddress(ddraw, "DirectDrawCreate")
        : NULL;
    if (!real_fn) return DDERR_GENERIC;

    LPDIRECTDRAW real = NULL;
    HRESULT hr = real_fn(lpGUID, &real, pUnkOuter);
    if (FAILED(hr) || !real) {
        *lplpDD = NULL;
        return hr;
    }

    s_dd_proxy.vtable = s_dd_vtable;
    s_dd_proxy.real   = (IUnknown *)real;
    *lplpDD = (LPDIRECTDRAW)&s_dd_proxy;
    return hr;
}
