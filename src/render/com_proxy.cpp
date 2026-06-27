#include "com_proxy.h"

typedef HRESULT (WINAPI *DirectDrawCreate_t)(GUID *, LPDIRECTDRAW *, IUnknown *);

#define NOINLINE __attribute__((noinline))

/* IID_IDirectDraw4 = {9c59509a-39bd-11d1-8c4a-00c04fd930c5} */
static const GUID IID_IDirectDraw4_g = {
    0x9c59509a, 0x39bd, 0x11d1,
    {0x8c, 0x4a, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5}
};

/* IID_IDirect3D3 = {bb223240-e72b-11d0-a9b4-00aa00c0993e} */
static const GUID IID_IDirect3D3_g = {
    0xbb223240, 0xe72b, 0x11d0,
    {0xa9, 0xb4, 0x00, 0xaa, 0x00, 0xc0, 0x99, 0x3e}
};

/* Extract the real IDirect3DDevice3 pointer from a proxy. */
static inline IDirect3DDevice3 *real_dev3(IDirect3DDevice3 *self)
{
    return (IDirect3DDevice3 *)((ComProxy *)self)->real;
}

/* --- IDirect3DDevice3 wrapper functions (slots 0-41) --- */

static HRESULT WINAPI NOINLINE wd3_QueryInterface(IDirect3DDevice3 *s, REFIID r, void **p)
    { return real_dev3(s)->QueryInterface(r, p); }
static ULONG   WINAPI NOINLINE wd3_AddRef(IDirect3DDevice3 *s)
    { return real_dev3(s)->AddRef(); }
static ULONG   WINAPI NOINLINE wd3_Release(IDirect3DDevice3 *s)
    { return real_dev3(s)->Release(); }
static HRESULT WINAPI NOINLINE wd3_GetCaps(IDirect3DDevice3 *s, D3DDEVICEDESC *hal, D3DDEVICEDESC *hel)
    { return real_dev3(s)->GetCaps(hal, hel); }
static HRESULT WINAPI NOINLINE wd3_GetStats(IDirect3DDevice3 *s, D3DSTATS *stats)
    { return real_dev3(s)->GetStats(stats); }
static HRESULT WINAPI NOINLINE wd3_AddViewport(IDirect3DDevice3 *s, IDirect3DViewport3 *vp)
    { return real_dev3(s)->AddViewport(vp); }
static HRESULT WINAPI NOINLINE wd3_DeleteViewport(IDirect3DDevice3 *s, IDirect3DViewport3 *vp)
    { return real_dev3(s)->DeleteViewport(vp); }
static HRESULT WINAPI NOINLINE wd3_NextViewport(IDirect3DDevice3 *s, IDirect3DViewport3 *ref, IDirect3DViewport3 **next, DWORD flags)
    { return real_dev3(s)->NextViewport(ref, next, flags); }
static HRESULT WINAPI NOINLINE wd3_EnumTextureFormats(IDirect3DDevice3 *s, LPD3DENUMPIXELFORMATSCALLBACK cb, void *ctx)
    { return real_dev3(s)->EnumTextureFormats(cb, ctx); }
static HRESULT WINAPI NOINLINE wd3_BeginScene(IDirect3DDevice3 *s)
    { return real_dev3(s)->BeginScene(); }
static HRESULT WINAPI NOINLINE wd3_EndScene(IDirect3DDevice3 *s)
    { return real_dev3(s)->EndScene(); }
static HRESULT WINAPI NOINLINE wd3_GetDirect3D(IDirect3DDevice3 *s, IDirect3D3 **d3d)
    { return real_dev3(s)->GetDirect3D(d3d); }
static HRESULT WINAPI NOINLINE wd3_SetCurrentViewport(IDirect3DDevice3 *s, IDirect3DViewport3 *vp)
    { return real_dev3(s)->SetCurrentViewport(vp); }
static HRESULT WINAPI NOINLINE wd3_GetCurrentViewport(IDirect3DDevice3 *s, IDirect3DViewport3 **vp)
    { return real_dev3(s)->GetCurrentViewport(vp); }
static HRESULT WINAPI NOINLINE wd3_SetRenderTarget(IDirect3DDevice3 *s, IDirectDrawSurface4 *surf, DWORD flags)
    { return real_dev3(s)->SetRenderTarget(surf, flags); }
static HRESULT WINAPI NOINLINE wd3_GetRenderTarget(IDirect3DDevice3 *s, IDirectDrawSurface4 **surf)
    { return real_dev3(s)->GetRenderTarget(surf); }
static HRESULT WINAPI NOINLINE wd3_Begin(IDirect3DDevice3 *s, D3DPRIMITIVETYPE pt, DWORD fvf, DWORD flags)
    { return real_dev3(s)->Begin(pt, fvf, flags); }
static HRESULT WINAPI NOINLINE wd3_BeginIndexed(IDirect3DDevice3 *s, D3DPRIMITIVETYPE pt, DWORD fvf, void *verts, DWORD vert_count, DWORD flags)
    { return real_dev3(s)->BeginIndexed(pt, fvf, verts, vert_count, flags); }
static HRESULT WINAPI NOINLINE wd3_Vertex(IDirect3DDevice3 *s, void *v)
    { return real_dev3(s)->Vertex(v); }
static HRESULT WINAPI NOINLINE wd3_Index(IDirect3DDevice3 *s, WORD w)
    { return real_dev3(s)->Index(w); }
static HRESULT WINAPI NOINLINE wd3_End(IDirect3DDevice3 *s, DWORD flags)
    { return real_dev3(s)->End(flags); }
static HRESULT WINAPI NOINLINE wd3_GetRenderState(IDirect3DDevice3 *s, D3DRENDERSTATETYPE rst, LPDWORD val)
    { return real_dev3(s)->GetRenderState(rst, val); }
static HRESULT WINAPI NOINLINE wd3_SetRenderState(IDirect3DDevice3 *s, D3DRENDERSTATETYPE rst, DWORD val)
    { return real_dev3(s)->SetRenderState(rst, val); }
static HRESULT WINAPI NOINLINE wd3_GetLightState(IDirect3DDevice3 *s, D3DLIGHTSTATETYPE lst, LPDWORD val)
    { return real_dev3(s)->GetLightState(lst, val); }
static HRESULT WINAPI NOINLINE wd3_SetLightState(IDirect3DDevice3 *s, D3DLIGHTSTATETYPE lst, DWORD val)
    { return real_dev3(s)->SetLightState(lst, val); }
static HRESULT WINAPI NOINLINE wd3_SetTransform(IDirect3DDevice3 *s, D3DTRANSFORMSTATETYPE tst, D3DMATRIX *mat)
    { return real_dev3(s)->SetTransform(tst, mat); }
static HRESULT WINAPI NOINLINE wd3_GetTransform(IDirect3DDevice3 *s, D3DTRANSFORMSTATETYPE tst, D3DMATRIX *mat)
    { return real_dev3(s)->GetTransform(tst, mat); }
static HRESULT WINAPI NOINLINE wd3_MultiplyTransform(IDirect3DDevice3 *s, D3DTRANSFORMSTATETYPE tst, D3DMATRIX *mat)
    { return real_dev3(s)->MultiplyTransform(tst, mat); }
static HRESULT WINAPI NOINLINE wd3_DrawPrimitive(IDirect3DDevice3 *s, D3DPRIMITIVETYPE pt, DWORD fvf, void *verts, DWORD vert_count, DWORD flags)
    { return real_dev3(s)->DrawPrimitive(pt, fvf, verts, vert_count, flags); }
static HRESULT WINAPI NOINLINE wd3_DrawIndexedPrimitive(IDirect3DDevice3 *s, D3DPRIMITIVETYPE pt, DWORD fvf, void *verts, DWORD vert_count, WORD *indices, DWORD idx_count, DWORD flags)
    { return real_dev3(s)->DrawIndexedPrimitive(pt, fvf, verts, vert_count, indices, idx_count, flags); }
static HRESULT WINAPI NOINLINE wd3_SetClipStatus(IDirect3DDevice3 *s, D3DCLIPSTATUS *cs)
    { return real_dev3(s)->SetClipStatus(cs); }
static HRESULT WINAPI NOINLINE wd3_GetClipStatus(IDirect3DDevice3 *s, D3DCLIPSTATUS *cs)
    { return real_dev3(s)->GetClipStatus(cs); }
static HRESULT WINAPI NOINLINE wd3_DrawPrimitiveStrided(IDirect3DDevice3 *s, D3DPRIMITIVETYPE pt, DWORD fvf, D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vert_count, DWORD flags)
    { return real_dev3(s)->DrawPrimitiveStrided(pt, fvf, data, vert_count, flags); }
static HRESULT WINAPI NOINLINE wd3_DrawIndexedPrimitiveStrided(IDirect3DDevice3 *s, D3DPRIMITIVETYPE pt, DWORD fvf, D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vert_count, WORD *indices, DWORD idx_count, DWORD flags)
    { return real_dev3(s)->DrawIndexedPrimitiveStrided(pt, fvf, data, vert_count, indices, idx_count, flags); }
static HRESULT WINAPI NOINLINE wd3_DrawPrimitiveVB(IDirect3DDevice3 *s, D3DPRIMITIVETYPE pt, IDirect3DVertexBuffer *vb, DWORD start, DWORD count, DWORD flags)
    { return real_dev3(s)->DrawPrimitiveVB(pt, vb, start, count, flags); }
static HRESULT WINAPI NOINLINE wd3_DrawIndexedPrimitiveVB(IDirect3DDevice3 *s, D3DPRIMITIVETYPE pt, IDirect3DVertexBuffer *vb, WORD *indices, DWORD idx_count, DWORD flags)
    { return real_dev3(s)->DrawIndexedPrimitiveVB(pt, vb, indices, idx_count, flags); }
static HRESULT WINAPI NOINLINE wd3_ComputeSphereVisibility(IDirect3DDevice3 *s, D3DVECTOR *centers, D3DVALUE *radii, DWORD count, DWORD flags, DWORD *ret)
    { return real_dev3(s)->ComputeSphereVisibility(centers, radii, count, flags, ret); }
static HRESULT WINAPI NOINLINE wd3_GetTexture(IDirect3DDevice3 *s, DWORD stage, IDirect3DTexture2 **tex)
    { return real_dev3(s)->GetTexture(stage, tex); }
static HRESULT WINAPI NOINLINE wd3_SetTexture(IDirect3DDevice3 *s, DWORD stage, IDirect3DTexture2 *tex)
    { return real_dev3(s)->SetTexture(stage, tex); }
static HRESULT WINAPI NOINLINE wd3_GetTextureStageState(IDirect3DDevice3 *s, DWORD stage, D3DTEXTURESTAGESTATETYPE st, LPDWORD val)
    { return real_dev3(s)->GetTextureStageState(stage, st, val); }
static HRESULT WINAPI NOINLINE wd3_SetTextureStageState(IDirect3DDevice3 *s, DWORD stage, D3DTEXTURESTAGESTATETYPE st, DWORD val)
    { return real_dev3(s)->SetTextureStageState(stage, st, val); }
static HRESULT WINAPI NOINLINE wd3_ValidateDevice(IDirect3DDevice3 *s, LPDWORD passes)
    { return real_dev3(s)->ValidateDevice(passes); }

static void *s_dev3_vtable_data[42] = {
    (void*)wd3_QueryInterface,
    (void*)wd3_AddRef,
    (void*)wd3_Release,
    (void*)wd3_GetCaps,
    (void*)wd3_GetStats,
    (void*)wd3_AddViewport,
    (void*)wd3_DeleteViewport,
    (void*)wd3_NextViewport,
    (void*)wd3_EnumTextureFormats,
    (void*)wd3_BeginScene,
    (void*)wd3_EndScene,
    (void*)wd3_GetDirect3D,
    (void*)wd3_SetCurrentViewport,
    (void*)wd3_GetCurrentViewport,
    (void*)wd3_SetRenderTarget,
    (void*)wd3_GetRenderTarget,
    (void*)wd3_Begin,
    (void*)wd3_BeginIndexed,
    (void*)wd3_Vertex,
    (void*)wd3_Index,
    (void*)wd3_End,
    (void*)wd3_GetRenderState,
    (void*)wd3_SetRenderState,
    (void*)wd3_GetLightState,
    (void*)wd3_SetLightState,
    (void*)wd3_SetTransform,
    (void*)wd3_GetTransform,
    (void*)wd3_MultiplyTransform,
    (void*)wd3_DrawPrimitive,
    (void*)wd3_DrawIndexedPrimitive,
    (void*)wd3_SetClipStatus,
    (void*)wd3_GetClipStatus,
    (void*)wd3_DrawPrimitiveStrided,
    (void*)wd3_DrawIndexedPrimitiveStrided,
    (void*)wd3_DrawPrimitiveVB,
    (void*)wd3_DrawIndexedPrimitiveVB,
    (void*)wd3_ComputeSphereVisibility,
    (void*)wd3_GetTexture,
    (void*)wd3_SetTexture,
    (void*)wd3_GetTextureStageState,
    (void*)wd3_SetTextureStageState,
    (void*)wd3_ValidateDevice,
};

static ComProxy s_dev3_proxy;

/* Extract the real IDirect3D3 pointer from a proxy. */
static inline IDirect3D3 *real_d3d3(IDirect3D3 *self)
{
    return (IDirect3D3 *)((ComProxy *)self)->real;
}

/* --- IDirect3D3 wrapper functions (slots 0-11) --- */

static HRESULT WINAPI NOINLINE w3_QueryInterface(IDirect3D3 *s, REFIID r, void **p)
    { return real_d3d3(s)->QueryInterface(r, p); }
static ULONG   WINAPI NOINLINE w3_AddRef(IDirect3D3 *s)
    { return real_d3d3(s)->AddRef(); }
static ULONG   WINAPI NOINLINE w3_Release(IDirect3D3 *s)
    { return real_d3d3(s)->Release(); }
static HRESULT WINAPI NOINLINE w3_EnumDevices(IDirect3D3 *s, LPD3DENUMDEVICESCALLBACK cb, void *ctx)
    { return real_d3d3(s)->EnumDevices(cb, ctx); }
static HRESULT WINAPI NOINLINE w3_CreateLight(IDirect3D3 *s, IDirect3DLight **light, IUnknown *outer)
    { return real_d3d3(s)->CreateLight(light, outer); }
static HRESULT WINAPI NOINLINE w3_CreateMaterial(IDirect3D3 *s, IDirect3DMaterial3 **mat, IUnknown *outer)
    { return real_d3d3(s)->CreateMaterial(mat, outer); }
static HRESULT WINAPI NOINLINE w3_CreateViewport(IDirect3D3 *s, IDirect3DViewport3 **vp, IUnknown *outer)
    { return real_d3d3(s)->CreateViewport(vp, outer); }
static HRESULT WINAPI NOINLINE w3_FindDevice(IDirect3D3 *s, D3DFINDDEVICESEARCH *search, D3DFINDDEVICERESULT *result)
    { return real_d3d3(s)->FindDevice(search, result); }
static HRESULT WINAPI NOINLINE w3_CreateDevice(IDirect3D3 *s, REFCLSID rclsid, IDirectDrawSurface4 *surf,
        IDirect3DDevice3 **dev, IUnknown *outer)
    { return real_d3d3(s)->CreateDevice(rclsid, surf, dev, outer); }
/* NOTE: we intentionally do NOT proxy IDirect3DDevice3 here.  Wine's internal
 * unsafe_impl_from_IDirect3DDevice3() hard-asserts that the vtable matches its
 * own d3d_device3_vtbl (device.c:6815).  If the game ever passes our proxy to
 * a non-device COM method (e.g. IDirect3DMaterial3::GetHandle) that call goes
 * through Wine's unsafe_impl_from_IDirect3DDevice3 and hits the assert →
 * abort() → exit code 3.  The GDB D3Dev3CreateDeviceReturnProbe instead reads
 * the real Wine vtable from *dev at return time and installs breakpoints there
 * directly, giving us device-method tracing without replacing the pointer. */
static HRESULT WINAPI NOINLINE w3_CreateVertexBuffer(IDirect3D3 *s, D3DVERTEXBUFFERDESC *desc,
        IDirect3DVertexBuffer **buf, DWORD flags, IUnknown *outer)
    { return real_d3d3(s)->CreateVertexBuffer(desc, buf, flags, outer); }
static HRESULT WINAPI NOINLINE w3_EnumZBufferFormats(IDirect3D3 *s, REFCLSID dev_iid,
        LPD3DENUMPIXELFORMATSCALLBACK cb, void *ctx)
    { return real_d3d3(s)->EnumZBufferFormats(dev_iid, cb, ctx); }
static HRESULT WINAPI NOINLINE w3_EvictManagedTextures(IDirect3D3 *s)
    { return real_d3d3(s)->EvictManagedTextures(); }

static void *s_d3d3_vtable_data[12] = {
    (void*)w3_QueryInterface,
    (void*)w3_AddRef,
    (void*)w3_Release,
    (void*)w3_EnumDevices,
    (void*)w3_CreateLight,
    (void*)w3_CreateMaterial,
    (void*)w3_CreateViewport,
    (void*)w3_FindDevice,
    (void*)w3_CreateDevice,
    (void*)w3_CreateVertexBuffer,
    (void*)w3_EnumZBufferFormats,
    (void*)w3_EvictManagedTextures,
};

static ComProxy s_d3d3_proxy;

/* Extract the real IDirectDraw4 pointer from a proxy. */
static inline IDirectDraw4 *real_dd4(IDirectDraw4 *self)
{
    return (IDirectDraw4 *)((ComProxy *)self)->real;
}

/* --- IDirectDraw4 wrapper functions (slots 0-27) --- */

static HRESULT WINAPI NOINLINE w4_QueryInterface(IDirectDraw4 *s, REFIID riid, void **ppv)
{
    HRESULT hr = real_dd4(s)->QueryInterface(riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv &&
        memcmp(&riid, &IID_IDirect3D3_g, sizeof(GUID)) == 0) {
        s_d3d3_proxy.vtable = s_d3d3_vtable_data;
        s_d3d3_proxy.real   = (IUnknown *)*ppv;
        *ppv = &s_d3d3_proxy;
    }
    return hr;
}
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
