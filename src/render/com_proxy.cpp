#include "com_proxy.h"

/* ── helpers ─────────────────────────────────────────────────────────────── */

ComProxy *make_proxy(void **vtable, IUnknown *real)
{
    ComProxy *p = (ComProxy *)HeapAlloc(GetProcessHeap(), 0, sizeof(ComProxy));
    if (p) { p->vtable = vtable; p->real = real; }
    return p;
}

#define REAL ((IDirectDraw *)((ComProxy *)self)->real)

/* ── IDirectDraw wrapper vtable ───────────────────────────────────────────
   One thin __stdcall stub per slot.  No logging — GDB breaks on the name. */

static HRESULT __stdcall wrapper_IDirectDraw_QueryInterface(
        IDirectDraw *self, REFIID riid, void **ppv)
{
    return REAL->QueryInterface(riid, ppv);
}

static ULONG __stdcall wrapper_IDirectDraw_AddRef(IDirectDraw *self)
{
    return REAL->AddRef();
}

static ULONG __stdcall wrapper_IDirectDraw_Release(IDirectDraw *self)
{
    IDirectDraw *real = REAL;
    ULONG refs = real->Release();
    if (refs == 0)
        HeapFree(GetProcessHeap(), 0, (ComProxy *)self);
    return refs;
}

static HRESULT __stdcall wrapper_IDirectDraw_Compact(IDirectDraw *self)
{
    return REAL->Compact();
}

static HRESULT __stdcall wrapper_IDirectDraw_CreateClipper(
        IDirectDraw *self, DWORD flags, LPDIRECTDRAWCLIPPER *out, IUnknown *unk)
{
    return REAL->CreateClipper(flags, out, unk);
}

static HRESULT __stdcall wrapper_IDirectDraw_CreatePalette(
        IDirectDraw *self, DWORD flags, LPPALETTEENTRY table,
        LPDIRECTDRAWPALETTE *out, IUnknown *unk)
{
    return REAL->CreatePalette(flags, table, out, unk);
}

static HRESULT __stdcall wrapper_IDirectDraw_CreateSurface(
        IDirectDraw *self, LPDDSURFACEDESC desc,
        LPDIRECTDRAWSURFACE *out, IUnknown *unk)
{
    return REAL->CreateSurface(desc, out, unk);
}

static HRESULT __stdcall wrapper_IDirectDraw_DuplicateSurface(
        IDirectDraw *self, LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE *out)
{
    return REAL->DuplicateSurface(src, out);
}

static HRESULT __stdcall wrapper_IDirectDraw_EnumDisplayModes(
        IDirectDraw *self, DWORD flags, LPDDSURFACEDESC desc,
        LPVOID ctx, LPDDENUMMODESCALLBACK cb)
{
    return REAL->EnumDisplayModes(flags, desc, ctx, cb);
}

static HRESULT __stdcall wrapper_IDirectDraw_EnumSurfaces(
        IDirectDraw *self, DWORD flags, LPDDSURFACEDESC desc,
        LPVOID ctx, LPDDENUMSURFACESCALLBACK cb)
{
    return REAL->EnumSurfaces(flags, desc, ctx, cb);
}

static HRESULT __stdcall wrapper_IDirectDraw_FlipToGDISurface(IDirectDraw *self)
{
    return REAL->FlipToGDISurface();
}

static HRESULT __stdcall wrapper_IDirectDraw_GetCaps(
        IDirectDraw *self, LPDDCAPS driver, LPDDCAPS hel)
{
    return REAL->GetCaps(driver, hel);
}

static HRESULT __stdcall wrapper_IDirectDraw_GetDisplayMode(
        IDirectDraw *self, LPDDSURFACEDESC desc)
{
    return REAL->GetDisplayMode(desc);
}

static HRESULT __stdcall wrapper_IDirectDraw_GetFourCCCodes(
        IDirectDraw *self, LPDWORD num, LPDWORD codes)
{
    return REAL->GetFourCCCodes(num, codes);
}

static HRESULT __stdcall wrapper_IDirectDraw_GetGDISurface(
        IDirectDraw *self, LPDIRECTDRAWSURFACE *out)
{
    return REAL->GetGDISurface(out);
}

static HRESULT __stdcall wrapper_IDirectDraw_GetMonitorFrequency(
        IDirectDraw *self, LPDWORD freq)
{
    return REAL->GetMonitorFrequency(freq);
}

static HRESULT __stdcall wrapper_IDirectDraw_GetScanLine(
        IDirectDraw *self, LPDWORD line)
{
    return REAL->GetScanLine(line);
}

static HRESULT __stdcall wrapper_IDirectDraw_GetVerticalBlankStatus(
        IDirectDraw *self, WINBOOL *in_vb)
{
    return REAL->GetVerticalBlankStatus(in_vb);
}

static HRESULT __stdcall wrapper_IDirectDraw_Initialize(
        IDirectDraw *self, GUID *guid)
{
    return REAL->Initialize(guid);
}

static HRESULT __stdcall wrapper_IDirectDraw_RestoreDisplayMode(IDirectDraw *self)
{
    return REAL->RestoreDisplayMode();
}

static HRESULT __stdcall wrapper_IDirectDraw_SetCooperativeLevel(
        IDirectDraw *self, HWND hwnd, DWORD flags)
{
    return REAL->SetCooperativeLevel(hwnd, flags);
}

static HRESULT __stdcall wrapper_IDirectDraw_SetDisplayMode(
        IDirectDraw *self, DWORD w, DWORD h, DWORD bpp)
{
    return REAL->SetDisplayMode(w, h, bpp);
}

static HRESULT __stdcall wrapper_IDirectDraw_WaitForVerticalBlank(
        IDirectDraw *self, DWORD flags, HANDLE ev)
{
    return REAL->WaitForVerticalBlank(flags, ev);
}

#undef REAL

static void *idirectdraw_vtable[] = {
    (void *)wrapper_IDirectDraw_QueryInterface,
    (void *)wrapper_IDirectDraw_AddRef,
    (void *)wrapper_IDirectDraw_Release,
    (void *)wrapper_IDirectDraw_Compact,
    (void *)wrapper_IDirectDraw_CreateClipper,
    (void *)wrapper_IDirectDraw_CreatePalette,
    (void *)wrapper_IDirectDraw_CreateSurface,
    (void *)wrapper_IDirectDraw_DuplicateSurface,
    (void *)wrapper_IDirectDraw_EnumDisplayModes,
    (void *)wrapper_IDirectDraw_EnumSurfaces,
    (void *)wrapper_IDirectDraw_FlipToGDISurface,
    (void *)wrapper_IDirectDraw_GetCaps,
    (void *)wrapper_IDirectDraw_GetDisplayMode,
    (void *)wrapper_IDirectDraw_GetFourCCCodes,
    (void *)wrapper_IDirectDraw_GetGDISurface,
    (void *)wrapper_IDirectDraw_GetMonitorFrequency,
    (void *)wrapper_IDirectDraw_GetScanLine,
    (void *)wrapper_IDirectDraw_GetVerticalBlankStatus,
    (void *)wrapper_IDirectDraw_Initialize,
    (void *)wrapper_IDirectDraw_RestoreDisplayMode,
    (void *)wrapper_IDirectDraw_SetCooperativeLevel,
    (void *)wrapper_IDirectDraw_SetDisplayMode,
    (void *)wrapper_IDirectDraw_WaitForVerticalBlank,
};

/* ── entry hook ───────────────────────────────────────────────────────────── */

typedef HRESULT (WINAPI *DirectDrawCreate_t)(GUID *, LPDIRECTDRAW *, IUnknown *);

extern "C" __declspec(dllexport) HRESULT WINAPI hooks_DirectDrawCreate(
        GUID *lpGUID, LPDIRECTDRAW *lplpDD, IUnknown *pUnkOuter)
{
    DirectDrawCreate_t real_fn = (DirectDrawCreate_t)
        GetProcAddress(GetModuleHandleA("ddraw.dll"), "DirectDrawCreate");
    if (!real_fn)
        return DDERR_GENERIC;

    LPDIRECTDRAW real_dd = NULL;
    HRESULT hr = real_fn(lpGUID, &real_dd, pUnkOuter);
    if (FAILED(hr) || !real_dd)
        return hr;

    ComProxy *proxy = make_proxy(idirectdraw_vtable, (IUnknown *)real_dd);
    if (!proxy) {
        real_dd->Release();
        return E_OUTOFMEMORY;
    }
    *lplpDD = (LPDIRECTDRAW)proxy;
    return hr;
}
