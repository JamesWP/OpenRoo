#include "com_proxy.h"

typedef HRESULT (WINAPI *DirectDrawCreate_t)(GUID *, LPDIRECTDRAW *, IUnknown *);

/* Extract the real IDirectDraw pointer from a proxy disguised as IDirectDraw. */
static inline IDirectDraw *real_dd(IDirectDraw *self)
{
    return (IDirectDraw *)((ComProxy *)self)->real;
}

#define NOINLINE __attribute__((noinline))

/* --- IDirectDraw wrapper functions (slots 0-22) --- */

static HRESULT WINAPI NOINLINE w_QueryInterface(IDirectDraw *s, REFIID r, void **p)
    { return real_dd(s)->QueryInterface(r, p); }
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
