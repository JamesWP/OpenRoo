#include "com_proxy.h"
#include "log.h"

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

/* IID_IDirectDraw = {6c14db80-a733-11ce-a521-0020af0be560} */
static const GUID IID_IDirectDraw_g = {
    0x6c14db80, 0xa733, 0x11ce,
    {0xa5, 0x21, 0x00, 0x20, 0xaf, 0x0b, 0xe5, 0x60}
};

/* Forward declaration — s_dd_proxy is defined after the IDirectDraw4 section. */
static ComProxy s_dd_proxy;

/* --- IDirectDrawSurface4 proxy pool ---
 * Surfaces are multi-instance (primary, backbuffer, z-buffer, textures), so
 * unlike the singleton proxies they live in a small pool.  4a scope: only the
 * render-chain surfaces (primary / backbuffer / z-buffer) are wrapped; texture
 * surfaces stay real until the pool becomes dynamic (4b). */
#define SURF4_POOL_SIZE 8
static ComProxy s_surf4_pool[SURF4_POOL_SIZE];
static void *s_surf4_vtable_data[45];   /* filled in the surface section below */

static inline bool is_surf4_proxy(void *p)
{
    return p >= (void *)&s_surf4_pool[0] && p < (void *)&s_surf4_pool[SURF4_POOL_SIZE];
}

static inline IDirectDrawSurface4 *unwrap_surf4(IDirectDrawSurface4 *surf)
{
    if (is_surf4_proxy(surf))
        return (IDirectDrawSurface4 *)((ComProxy *)surf)->real;
    return surf;
}

/* Wrap a real surface, reusing the pool entry if this real pointer is already
 * wrapped.  Returns the real pointer unchanged if the pool is full (logged). */
static IDirectDrawSurface4 *wrap_surf4(IDirectDrawSurface4 *real, const char *what)
{
    int free_slot = -1;
    for (int i = 0; i < SURF4_POOL_SIZE; i++) {
        if (s_surf4_pool[i].real == (IUnknown *)real)
            return (IDirectDrawSurface4 *)&s_surf4_pool[i];
        if (!s_surf4_pool[i].real && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0) {
        log_write("com_proxy: surf4 pool FULL — %s left unproxied (real=%p)\n", what, real);
        return real;
    }
    s_surf4_pool[free_slot].vtable = s_surf4_vtable_data;
    s_surf4_pool[free_slot].real   = (IUnknown *)real;
    log_write("com_proxy: surface proxy[%d] installed for %s (real=%p)\n",
              free_slot, what, real);
    return (IDirectDrawSurface4 *)&s_surf4_pool[free_slot];
}

/* A/B switch: KAROO_D3D_PROXY=0 disables the IDirect3DDevice3 + material
 * proxies (the pre-existing DD/DD4/D3D3 proxies stay active either way). */
static bool dev_proxy_enabled(void)
{
    char buf[8];
    if (GetEnvironmentVariableA("KAROO_D3D_PROXY", buf, sizeof(buf)))
        return buf[0] != '0';
    return true;
}

/* Visual-proof effects: KAROO_D3D_FX selects a deliberately visible render
 * alteration, demonstrating that the corresponding proxy carries live
 * traffic.  Off when unset. */
enum FxMode { FX_OFF = 0, FX_WIRE, FX_TINT, FX_LIGHT };

static FxMode fx_mode(void)
{
    static FxMode mode = (FxMode)-1;
    if (mode == (FxMode)-1) {
        char buf[16];
        mode = FX_OFF;
        if (GetEnvironmentVariableA("KAROO_D3D_FX", buf, sizeof(buf))) {
            if      (lstrcmpiA(buf, "wire")  == 0) mode = FX_WIRE;
            else if (lstrcmpiA(buf, "tint")  == 0) mode = FX_TINT;
            else if (lstrcmpiA(buf, "light") == 0) mode = FX_LIGHT;
        }
        if (mode != FX_OFF)
            log_write("com_proxy: FX mode %d active\n", (int)mode);
    }
    return mode;
}

/* --- IDirectDrawSurface4 wrapper functions (45 slots) --- */

static inline IDirectDrawSurface4 *real_s4(IDirectDrawSurface4 *self)
{
    return (IDirectDrawSurface4 *)((ComProxy *)self)->real;
}

static HRESULT WINAPI NOINLINE ws4_QueryInterface(IDirectDrawSurface4 *s, REFIID r, void **p)
    { return real_s4(s)->QueryInterface(r, p); }
static ULONG   WINAPI NOINLINE ws4_AddRef(IDirectDrawSurface4 *s)
    { return real_s4(s)->AddRef(); }
static ULONG   WINAPI NOINLINE ws4_Release(IDirectDrawSurface4 *s)
    { return real_s4(s)->Release(); }
static HRESULT WINAPI NOINLINE ws4_AddAttachedSurface(IDirectDrawSurface4 *s, IDirectDrawSurface4 *att)
    { return real_s4(s)->AddAttachedSurface(unwrap_surf4(att)); }
static HRESULT WINAPI NOINLINE ws4_AddOverlayDirtyRect(IDirectDrawSurface4 *s, LPRECT r)
    { return real_s4(s)->AddOverlayDirtyRect(r); }
static HRESULT WINAPI NOINLINE ws4_Blt(IDirectDrawSurface4 *s, LPRECT dr, IDirectDrawSurface4 *src, LPRECT sr, DWORD flags, LPDDBLTFX fx)
    { return real_s4(s)->Blt(dr, unwrap_surf4(src), sr, flags, fx); }
static HRESULT WINAPI NOINLINE ws4_BltBatch(IDirectDrawSurface4 *s, LPDDBLTBATCH b, DWORD n, DWORD flags)
    { return real_s4(s)->BltBatch(b, n, flags); }
static HRESULT WINAPI NOINLINE ws4_BltFast(IDirectDrawSurface4 *s, DWORD x, DWORD y, IDirectDrawSurface4 *src, LPRECT sr, DWORD trans)
    { return real_s4(s)->BltFast(x, y, unwrap_surf4(src), sr, trans); }
static HRESULT WINAPI NOINLINE ws4_DeleteAttachedSurface(IDirectDrawSurface4 *s, DWORD flags, IDirectDrawSurface4 *att)
    { return real_s4(s)->DeleteAttachedSurface(flags, unwrap_surf4(att)); }
static HRESULT WINAPI NOINLINE ws4_EnumAttachedSurfaces(IDirectDrawSurface4 *s, LPVOID ctx, LPDDENUMSURFACESCALLBACK2 cb)
    { return real_s4(s)->EnumAttachedSurfaces(ctx, cb); }
static HRESULT WINAPI NOINLINE ws4_EnumOverlayZOrders(IDirectDrawSurface4 *s, DWORD flags, LPVOID ctx, LPDDENUMSURFACESCALLBACK2 cb)
    { return real_s4(s)->EnumOverlayZOrders(flags, ctx, cb); }
static HRESULT WINAPI NOINLINE ws4_Flip(IDirectDrawSurface4 *s, IDirectDrawSurface4 *over, DWORD flags)
    { return real_s4(s)->Flip(unwrap_surf4(over), flags); }
static HRESULT WINAPI NOINLINE ws4_GetAttachedSurface(IDirectDrawSurface4 *s, LPDDSCAPS2 caps, IDirectDrawSurface4 **att)
{
    HRESULT hr = real_s4(s)->GetAttachedSurface(caps, att);
    /* The backbuffer is obtained this way off the (proxied) primary — wrap it
     * so the game's render-target pointer is also ours. */
    if (SUCCEEDED(hr) && att && *att)
        *att = wrap_surf4(*att, "attached surface");
    return hr;
}
static HRESULT WINAPI NOINLINE ws4_GetBltStatus(IDirectDrawSurface4 *s, DWORD flags)
    { return real_s4(s)->GetBltStatus(flags); }
static HRESULT WINAPI NOINLINE ws4_GetCaps(IDirectDrawSurface4 *s, LPDDSCAPS2 caps)
    { return real_s4(s)->GetCaps(caps); }
static HRESULT WINAPI NOINLINE ws4_GetClipper(IDirectDrawSurface4 *s, LPDIRECTDRAWCLIPPER *pp)
    { return real_s4(s)->GetClipper(pp); }
static HRESULT WINAPI NOINLINE ws4_GetColorKey(IDirectDrawSurface4 *s, DWORD flags, LPDDCOLORKEY key)
    { return real_s4(s)->GetColorKey(flags, key); }
static HRESULT WINAPI NOINLINE ws4_GetDC(IDirectDrawSurface4 *s, HDC *hdc)
    { return real_s4(s)->GetDC(hdc); }
static HRESULT WINAPI NOINLINE ws4_GetFlipStatus(IDirectDrawSurface4 *s, DWORD flags)
    { return real_s4(s)->GetFlipStatus(flags); }
static HRESULT WINAPI NOINLINE ws4_GetOverlayPosition(IDirectDrawSurface4 *s, LPLONG x, LPLONG y)
    { return real_s4(s)->GetOverlayPosition(x, y); }
static HRESULT WINAPI NOINLINE ws4_GetPalette(IDirectDrawSurface4 *s, LPDIRECTDRAWPALETTE *pp)
    { return real_s4(s)->GetPalette(pp); }
static HRESULT WINAPI NOINLINE ws4_GetPixelFormat(IDirectDrawSurface4 *s, LPDDPIXELFORMAT pf)
    { return real_s4(s)->GetPixelFormat(pf); }
static HRESULT WINAPI NOINLINE ws4_GetSurfaceDesc(IDirectDrawSurface4 *s, LPDDSURFACEDESC2 d)
    { return real_s4(s)->GetSurfaceDesc(d); }
static HRESULT WINAPI NOINLINE ws4_Initialize(IDirectDrawSurface4 *s, LPDIRECTDRAW dd, LPDDSURFACEDESC2 d)
    { return real_s4(s)->Initialize(dd, d); }
static HRESULT WINAPI NOINLINE ws4_IsLost(IDirectDrawSurface4 *s)
    { return real_s4(s)->IsLost(); }
static HRESULT WINAPI NOINLINE ws4_Lock(IDirectDrawSurface4 *s, LPRECT r, LPDDSURFACEDESC2 d, DWORD flags, HANDLE ev)
    { return real_s4(s)->Lock(r, d, flags, ev); }
static HRESULT WINAPI NOINLINE ws4_ReleaseDC(IDirectDrawSurface4 *s, HDC hdc)
    { return real_s4(s)->ReleaseDC(hdc); }
static HRESULT WINAPI NOINLINE ws4_Restore(IDirectDrawSurface4 *s)
    { return real_s4(s)->Restore(); }
static HRESULT WINAPI NOINLINE ws4_SetClipper(IDirectDrawSurface4 *s, LPDIRECTDRAWCLIPPER cl)
    { return real_s4(s)->SetClipper(cl); }
static HRESULT WINAPI NOINLINE ws4_SetColorKey(IDirectDrawSurface4 *s, DWORD flags, LPDDCOLORKEY key)
    { return real_s4(s)->SetColorKey(flags, key); }
static HRESULT WINAPI NOINLINE ws4_SetOverlayPosition(IDirectDrawSurface4 *s, LONG x, LONG y)
    { return real_s4(s)->SetOverlayPosition(x, y); }
static HRESULT WINAPI NOINLINE ws4_SetPalette(IDirectDrawSurface4 *s, LPDIRECTDRAWPALETTE pal)
    { return real_s4(s)->SetPalette(pal); }
static HRESULT WINAPI NOINLINE ws4_Unlock(IDirectDrawSurface4 *s, LPRECT r)
    { return real_s4(s)->Unlock(r); }
static HRESULT WINAPI NOINLINE ws4_UpdateOverlay(IDirectDrawSurface4 *s, LPRECT sr, IDirectDrawSurface4 *dst, LPRECT dr, DWORD flags, LPDDOVERLAYFX fx)
    { return real_s4(s)->UpdateOverlay(sr, unwrap_surf4(dst), dr, flags, fx); }
static HRESULT WINAPI NOINLINE ws4_UpdateOverlayDisplay(IDirectDrawSurface4 *s, DWORD flags)
    { return real_s4(s)->UpdateOverlayDisplay(flags); }
static HRESULT WINAPI NOINLINE ws4_UpdateOverlayZOrder(IDirectDrawSurface4 *s, DWORD flags, IDirectDrawSurface4 *ref)
    { return real_s4(s)->UpdateOverlayZOrder(flags, unwrap_surf4(ref)); }
static HRESULT WINAPI NOINLINE ws4_GetDDInterface(IDirectDrawSurface4 *s, LPVOID *pp)
    { return real_s4(s)->GetDDInterface(pp); }
static HRESULT WINAPI NOINLINE ws4_PageLock(IDirectDrawSurface4 *s, DWORD flags)
    { return real_s4(s)->PageLock(flags); }
static HRESULT WINAPI NOINLINE ws4_PageUnlock(IDirectDrawSurface4 *s, DWORD flags)
    { return real_s4(s)->PageUnlock(flags); }
static HRESULT WINAPI NOINLINE ws4_SetSurfaceDesc(IDirectDrawSurface4 *s, LPDDSURFACEDESC2 d, DWORD flags)
    { return real_s4(s)->SetSurfaceDesc(d, flags); }
static HRESULT WINAPI NOINLINE ws4_SetPrivateData(IDirectDrawSurface4 *s, REFGUID g, LPVOID data, DWORD size, DWORD flags)
    { return real_s4(s)->SetPrivateData(g, data, size, flags); }
static HRESULT WINAPI NOINLINE ws4_GetPrivateData(IDirectDrawSurface4 *s, REFGUID g, LPVOID data, LPDWORD size)
    { return real_s4(s)->GetPrivateData(g, data, size); }
static HRESULT WINAPI NOINLINE ws4_FreePrivateData(IDirectDrawSurface4 *s, REFGUID g)
    { return real_s4(s)->FreePrivateData(g); }
static HRESULT WINAPI NOINLINE ws4_GetUniquenessValue(IDirectDrawSurface4 *s, LPDWORD v)
    { return real_s4(s)->GetUniquenessValue(v); }
static HRESULT WINAPI NOINLINE ws4_ChangeUniquenessValue(IDirectDrawSurface4 *s)
    { return real_s4(s)->ChangeUniquenessValue(); }

/* Populate s_surf4_vtable_data (declared with the pool above) at load time. */
static void *const s_surf4_vtable_init[45] = {
    (void*)ws4_QueryInterface,
    (void*)ws4_AddRef,
    (void*)ws4_Release,
    (void*)ws4_AddAttachedSurface,
    (void*)ws4_AddOverlayDirtyRect,
    (void*)ws4_Blt,
    (void*)ws4_BltBatch,
    (void*)ws4_BltFast,
    (void*)ws4_DeleteAttachedSurface,
    (void*)ws4_EnumAttachedSurfaces,
    (void*)ws4_EnumOverlayZOrders,
    (void*)ws4_Flip,
    (void*)ws4_GetAttachedSurface,
    (void*)ws4_GetBltStatus,
    (void*)ws4_GetCaps,
    (void*)ws4_GetClipper,
    (void*)ws4_GetColorKey,
    (void*)ws4_GetDC,
    (void*)ws4_GetFlipStatus,
    (void*)ws4_GetOverlayPosition,
    (void*)ws4_GetPalette,
    (void*)ws4_GetPixelFormat,
    (void*)ws4_GetSurfaceDesc,
    (void*)ws4_Initialize,
    (void*)ws4_IsLost,
    (void*)ws4_Lock,
    (void*)ws4_ReleaseDC,
    (void*)ws4_Restore,
    (void*)ws4_SetClipper,
    (void*)ws4_SetColorKey,
    (void*)ws4_SetOverlayPosition,
    (void*)ws4_SetPalette,
    (void*)ws4_Unlock,
    (void*)ws4_UpdateOverlay,
    (void*)ws4_UpdateOverlayDisplay,
    (void*)ws4_UpdateOverlayZOrder,
    (void*)ws4_GetDDInterface,
    (void*)ws4_PageLock,
    (void*)ws4_PageUnlock,
    (void*)ws4_SetSurfaceDesc,
    (void*)ws4_SetPrivateData,
    (void*)ws4_GetPrivateData,
    (void*)ws4_FreePrivateData,
    (void*)ws4_GetUniquenessValue,
    (void*)ws4_ChangeUniquenessValue,
};

struct Surf4VtableInit {
    Surf4VtableInit() {
        for (int i = 0; i < 45; i++)
            s_surf4_vtable_data[i] = s_surf4_vtable_init[i];
    }
};
static Surf4VtableInit s_surf4_vtable_initializer;

/* Extract the real IDirect3DDevice3 pointer from a proxy. */
static inline IDirect3DDevice3 *real_dev3(IDirect3DDevice3 *self)
{
    return (IDirect3DDevice3 *)((ComProxy *)self)->real;
}

/* Viewport proxy storage — defined early so the device wrappers below can
 * unwrap viewport arguments before forwarding to the real Wine device
 * (unsafe_impl_from_IDirect3DViewport3 asserts on a foreign vtable). */
static ComProxy s_vp3_proxy;

static inline IDirect3DViewport3 *unwrap_vp3(IDirect3DViewport3 *vp)
{
    if ((void *)vp == (void *)&s_vp3_proxy)
        return (IDirect3DViewport3 *)s_vp3_proxy.real;
    return vp;
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
    { return real_dev3(s)->AddViewport(unwrap_vp3(vp)); }
static HRESULT WINAPI NOINLINE wd3_DeleteViewport(IDirect3DDevice3 *s, IDirect3DViewport3 *vp)
    { return real_dev3(s)->DeleteViewport(unwrap_vp3(vp)); }
static HRESULT WINAPI NOINLINE wd3_NextViewport(IDirect3DDevice3 *s, IDirect3DViewport3 *ref, IDirect3DViewport3 **next, DWORD flags)
    { return real_dev3(s)->NextViewport(unwrap_vp3(ref), next, flags); }
static HRESULT WINAPI NOINLINE wd3_EnumTextureFormats(IDirect3DDevice3 *s, LPD3DENUMPIXELFORMATSCALLBACK cb, void *ctx)
    { return real_dev3(s)->EnumTextureFormats(cb, ctx); }
static HRESULT WINAPI NOINLINE wd3_BeginScene(IDirect3DDevice3 *s)
{
    HRESULT hr = real_dev3(s)->BeginScene();
    /* FX_WIRE: force wireframe each frame — proves the device proxy sits in
     * the per-frame render path (game never touches FILLMODE, so no restore
     * is needed). */
    if (SUCCEEDED(hr) && fx_mode() == FX_WIRE)
        real_dev3(s)->SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_WIREFRAME);
    return hr;
}
static HRESULT WINAPI NOINLINE wd3_EndScene(IDirect3DDevice3 *s)
    { return real_dev3(s)->EndScene(); }
static HRESULT WINAPI NOINLINE wd3_GetDirect3D(IDirect3DDevice3 *s, IDirect3D3 **d3d)
    { return real_dev3(s)->GetDirect3D(d3d); }
static HRESULT WINAPI NOINLINE wd3_SetCurrentViewport(IDirect3DDevice3 *s, IDirect3DViewport3 *vp)
    { return real_dev3(s)->SetCurrentViewport(unwrap_vp3(vp)); }
static HRESULT WINAPI NOINLINE wd3_GetCurrentViewport(IDirect3DDevice3 *s, IDirect3DViewport3 **vp)
    { return real_dev3(s)->GetCurrentViewport(vp); }
static HRESULT WINAPI NOINLINE wd3_SetRenderTarget(IDirect3DDevice3 *s, IDirectDrawSurface4 *surf, DWORD flags)
    { return real_dev3(s)->SetRenderTarget(unwrap_surf4(surf), flags); }
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

/* Unwrap our device proxy back to the real Wine IDirect3DDevice3.  Needed
 * wherever the game passes the device pointer as an ARGUMENT to a non-device
 * COM method (e.g. IDirect3DMaterial3::GetHandle in CreateSceneMaterial) —
 * Wine's unsafe_impl_from_IDirect3DDevice3() hard-asserts on a foreign vtable
 * (device.c:6815), so the real pointer must be substituted before forwarding. */
static inline IDirect3DDevice3 *unwrap_dev3(IDirect3DDevice3 *dev)
{
    if ((void *)dev == (void *)&s_dev3_proxy)
        return (IDirect3DDevice3 *)s_dev3_proxy.real;
    return dev;
}

/* --- IDirect3DLight proxy (6 slots) --- */

static ComProxy s_light_proxy;

static inline IDirect3DLight *unwrap_light(IDirect3DLight *light)
{
    if ((void *)light == (void *)&s_light_proxy)
        return (IDirect3DLight *)s_light_proxy.real;
    return light;
}

static inline IDirect3DLight *real_light(IDirect3DLight *self)
{
    return (IDirect3DLight *)((ComProxy *)self)->real;
}

static HRESULT WINAPI NOINLINE wl_QueryInterface(IDirect3DLight *s, REFIID r, void **p)
    { return real_light(s)->QueryInterface(r, p); }
static ULONG   WINAPI NOINLINE wl_AddRef(IDirect3DLight *s)
    { return real_light(s)->AddRef(); }
static ULONG   WINAPI NOINLINE wl_Release(IDirect3DLight *s)
    { return real_light(s)->Release(); }
static HRESULT WINAPI NOINLINE wl_Initialize(IDirect3DLight *s, IDirect3D *d3d)
    { return real_light(s)->Initialize(d3d); }
static HRESULT WINAPI NOINLINE wl_SetLight(IDirect3DLight *s, D3DLIGHT *light)
{
    /* FX_LIGHT: strip green+blue from the scene light — proves the light
     * proxy is live (lit geometry turns red). */
    if (light && fx_mode() == FX_LIGHT) {
        light->dcvColor.g = 0.0f;
        light->dcvColor.b = 0.0f;
    }
    return real_light(s)->SetLight(light);
}
static HRESULT WINAPI NOINLINE wl_GetLight(IDirect3DLight *s, D3DLIGHT *light)
    { return real_light(s)->GetLight(light); }

static void *s_light_vtable_data[6] = {
    (void*)wl_QueryInterface,
    (void*)wl_AddRef,
    (void*)wl_Release,
    (void*)wl_Initialize,
    (void*)wl_SetLight,
    (void*)wl_GetLight,
};

/* --- IDirect3DViewport3 proxy (21 slots) --- */

static inline IDirect3DViewport3 *real_vp3(IDirect3DViewport3 *self)
{
    return (IDirect3DViewport3 *)((ComProxy *)self)->real;
}

static HRESULT WINAPI NOINLINE wvp_QueryInterface(IDirect3DViewport3 *s, REFIID r, void **p)
    { return real_vp3(s)->QueryInterface(r, p); }
static ULONG   WINAPI NOINLINE wvp_AddRef(IDirect3DViewport3 *s)
    { return real_vp3(s)->AddRef(); }
static ULONG   WINAPI NOINLINE wvp_Release(IDirect3DViewport3 *s)
    { return real_vp3(s)->Release(); }
static HRESULT WINAPI NOINLINE wvp_Initialize(IDirect3DViewport3 *s, IDirect3D *d3d)
    { return real_vp3(s)->Initialize(d3d); }
static HRESULT WINAPI NOINLINE wvp_GetViewport(IDirect3DViewport3 *s, D3DVIEWPORT *vp)
    { return real_vp3(s)->GetViewport(vp); }
static HRESULT WINAPI NOINLINE wvp_SetViewport(IDirect3DViewport3 *s, D3DVIEWPORT *vp)
    { return real_vp3(s)->SetViewport(vp); }
static HRESULT WINAPI NOINLINE wvp_TransformVertices(IDirect3DViewport3 *s, DWORD n, D3DTRANSFORMDATA *d, DWORD flags, DWORD *off)
    { return real_vp3(s)->TransformVertices(n, d, flags, off); }
static HRESULT WINAPI NOINLINE wvp_LightElements(IDirect3DViewport3 *s, DWORD n, D3DLIGHTDATA *d)
    { return real_vp3(s)->LightElements(n, d); }
static HRESULT WINAPI NOINLINE wvp_SetBackground(IDirect3DViewport3 *s, D3DMATERIALHANDLE h)
    { return real_vp3(s)->SetBackground(h); }
static HRESULT WINAPI NOINLINE wvp_GetBackground(IDirect3DViewport3 *s, D3DMATERIALHANDLE *h, BOOL *valid)
    { return real_vp3(s)->GetBackground(h, valid); }
static HRESULT WINAPI NOINLINE wvp_SetBackgroundDepth(IDirect3DViewport3 *s, IDirectDrawSurface *surf)
    { return real_vp3(s)->SetBackgroundDepth(surf); }
static HRESULT WINAPI NOINLINE wvp_GetBackgroundDepth(IDirect3DViewport3 *s, IDirectDrawSurface **surf, BOOL *valid)
    { return real_vp3(s)->GetBackgroundDepth(surf, valid); }
static HRESULT WINAPI NOINLINE wvp_Clear(IDirect3DViewport3 *s, DWORD n, D3DRECT *rects, DWORD flags)
    { return real_vp3(s)->Clear(n, rects, flags); }
static HRESULT WINAPI NOINLINE wvp_AddLight(IDirect3DViewport3 *s, IDirect3DLight *light)
    { return real_vp3(s)->AddLight(unwrap_light(light)); }
static HRESULT WINAPI NOINLINE wvp_DeleteLight(IDirect3DViewport3 *s, IDirect3DLight *light)
    { return real_vp3(s)->DeleteLight(unwrap_light(light)); }
static HRESULT WINAPI NOINLINE wvp_NextLight(IDirect3DViewport3 *s, IDirect3DLight *ref, IDirect3DLight **next, DWORD flags)
    { return real_vp3(s)->NextLight(unwrap_light(ref), next, flags); }
static HRESULT WINAPI NOINLINE wvp_GetViewport2(IDirect3DViewport3 *s, D3DVIEWPORT2 *vp)
    { return real_vp3(s)->GetViewport2(vp); }
static HRESULT WINAPI NOINLINE wvp_SetViewport2(IDirect3DViewport3 *s, D3DVIEWPORT2 *vp)
    { return real_vp3(s)->SetViewport2(vp); }
static HRESULT WINAPI NOINLINE wvp_SetBackgroundDepth2(IDirect3DViewport3 *s, IDirectDrawSurface4 *surf)
    { return real_vp3(s)->SetBackgroundDepth2(unwrap_surf4(surf)); }
static HRESULT WINAPI NOINLINE wvp_GetBackgroundDepth2(IDirect3DViewport3 *s, IDirectDrawSurface4 **surf, BOOL *valid)
    { return real_vp3(s)->GetBackgroundDepth2(surf, valid); }
static HRESULT WINAPI NOINLINE wvp_Clear2(IDirect3DViewport3 *s, DWORD n, D3DRECT *rects, DWORD flags, D3DCOLOR color, D3DVALUE z, DWORD stencil)
{
    /* FX_TINT: magenta clear — proves the viewport proxy carries the
     * per-frame Clear2.  The game clears depth+stencil only, so the color
     * arg is normally unused; force D3DCLEAR_TARGET so the tint shows
     * (geometry drawn afterwards will still cover most of it). */
    if (fx_mode() == FX_TINT) {
        flags |= D3DCLEAR_TARGET;
        color = 0x00FF00FF;
    }
    HRESULT hr = real_vp3(s)->Clear2(n, rects, flags, color, z, stencil);
    static LONG clear2_logged = 0;
    if (InterlockedIncrement(&clear2_logged) <= 3)
        log_write("com_proxy: Clear2 #%ld n=%lu flags=%08lX color=%08lX -> hr=%08lX%s\n",
            clear2_logged, n, flags, color, (DWORD)hr,
            (n && rects) ? " (has rects)" : "");
    return hr;
}

static void *s_vp3_vtable_data[21] = {
    (void*)wvp_QueryInterface,
    (void*)wvp_AddRef,
    (void*)wvp_Release,
    (void*)wvp_Initialize,
    (void*)wvp_GetViewport,
    (void*)wvp_SetViewport,
    (void*)wvp_TransformVertices,
    (void*)wvp_LightElements,
    (void*)wvp_SetBackground,
    (void*)wvp_GetBackground,
    (void*)wvp_SetBackgroundDepth,
    (void*)wvp_GetBackgroundDepth,
    (void*)wvp_Clear,
    (void*)wvp_AddLight,
    (void*)wvp_DeleteLight,
    (void*)wvp_NextLight,
    (void*)wvp_GetViewport2,
    (void*)wvp_SetViewport2,
    (void*)wvp_SetBackgroundDepth2,
    (void*)wvp_GetBackgroundDepth2,
    (void*)wvp_Clear2,
};

/* --- IDirect3DMaterial3 proxy ---
 * Proxied solely so GetHandle can unwrap the device argument; all other
 * slots are plain passthroughs. */

static inline IDirect3DMaterial3 *real_mat3(IDirect3DMaterial3 *self)
{
    return (IDirect3DMaterial3 *)((ComProxy *)self)->real;
}

static HRESULT WINAPI NOINLINE wm3_QueryInterface(IDirect3DMaterial3 *s, REFIID r, void **p)
    { return real_mat3(s)->QueryInterface(r, p); }
static ULONG   WINAPI NOINLINE wm3_AddRef(IDirect3DMaterial3 *s)
    { return real_mat3(s)->AddRef(); }
static ULONG   WINAPI NOINLINE wm3_Release(IDirect3DMaterial3 *s)
    { return real_mat3(s)->Release(); }
static HRESULT WINAPI NOINLINE wm3_SetMaterial(IDirect3DMaterial3 *s, D3DMATERIAL *mat)
    { return real_mat3(s)->SetMaterial(mat); }
static HRESULT WINAPI NOINLINE wm3_GetMaterial(IDirect3DMaterial3 *s, D3DMATERIAL *mat)
    { return real_mat3(s)->GetMaterial(mat); }
static HRESULT WINAPI NOINLINE wm3_GetHandle(IDirect3DMaterial3 *s, IDirect3DDevice3 *dev, D3DMATERIALHANDLE *handle)
    { return real_mat3(s)->GetHandle(unwrap_dev3(dev), handle); }

static void *s_mat3_vtable_data[6] = {
    (void*)wm3_QueryInterface,
    (void*)wm3_AddRef,
    (void*)wm3_Release,
    (void*)wm3_SetMaterial,
    (void*)wm3_GetMaterial,
    (void*)wm3_GetHandle,
};

static ComProxy s_mat3_proxy;

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
{
    HRESULT hr = real_d3d3(s)->CreateLight(light, outer);
    if (SUCCEEDED(hr) && light && *light && dev_proxy_enabled()) {
        s_light_proxy.vtable = s_light_vtable_data;
        s_light_proxy.real   = (IUnknown *)*light;
        *light = (IDirect3DLight *)&s_light_proxy;
        log_write("com_proxy: light proxy installed (real=%p)\n", s_light_proxy.real);
    }
    return hr;
}
static HRESULT WINAPI NOINLINE w3_CreateMaterial(IDirect3D3 *s, IDirect3DMaterial3 **mat, IUnknown *outer)
{
    HRESULT hr = real_d3d3(s)->CreateMaterial(mat, outer);
    if (SUCCEEDED(hr) && mat && *mat && dev_proxy_enabled()) {
        s_mat3_proxy.vtable = s_mat3_vtable_data;
        s_mat3_proxy.real   = (IUnknown *)*mat;
        *mat = (IDirect3DMaterial3 *)&s_mat3_proxy;
        log_write("com_proxy: material proxy installed (real=%p)\n", s_mat3_proxy.real);
    }
    return hr;
}
static HRESULT WINAPI NOINLINE w3_CreateViewport(IDirect3D3 *s, IDirect3DViewport3 **vp, IUnknown *outer)
{
    HRESULT hr = real_d3d3(s)->CreateViewport(vp, outer);
    if (SUCCEEDED(hr) && vp && *vp && dev_proxy_enabled()) {
        s_vp3_proxy.vtable = s_vp3_vtable_data;
        s_vp3_proxy.real   = (IUnknown *)*vp;
        *vp = (IDirect3DViewport3 *)&s_vp3_proxy;
        log_write("com_proxy: viewport proxy installed (real=%p)\n", s_vp3_proxy.real);
    }
    return hr;
}
static HRESULT WINAPI NOINLINE w3_FindDevice(IDirect3D3 *s, D3DFINDDEVICESEARCH *search, D3DFINDDEVICERESULT *result)
    { return real_d3d3(s)->FindDevice(search, result); }
static HRESULT WINAPI NOINLINE w3_CreateDevice(IDirect3D3 *s, REFCLSID rclsid, IDirectDrawSurface4 *surf,
        IDirect3DDevice3 **dev, IUnknown *outer)
{
    HRESULT hr = real_d3d3(s)->CreateDevice(rclsid, unwrap_surf4(surf), dev, outer);
    if (SUCCEEDED(hr) && dev && *dev && dev_proxy_enabled()) {
        s_dev3_proxy.vtable = s_dev3_vtable_data;
        s_dev3_proxy.real   = (IUnknown *)*dev;
        *dev = (IDirect3DDevice3 *)&s_dev3_proxy;
        log_write("com_proxy: device proxy installed (real=%p)\n", s_dev3_proxy.real);
    }
    return hr;
}
/* The device proxy trips Wine's unsafe_impl_from_IDirect3DDevice3() assert
 * (device.c:6815) if the game ever passes it as an argument to a non-device
 * COM method on a REAL (unproxied) Wine object.  Every such method must be
 * reached through a proxy that unwraps the device via unwrap_dev3() — see
 * the IDirect3DMaterial3 proxy above (GetHandle).  A missed site aborts with
 * exit code 3 and the Wine assert message in the Proton log: that is the
 * detection signal, not a silent failure. */
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
    if (SUCCEEDED(hr) && ppv && *ppv) {
        if (memcmp(&riid, &IID_IDirect3D3_g, sizeof(GUID)) == 0) {
            s_d3d3_proxy.vtable = s_d3d3_vtable_data;
            s_d3d3_proxy.real   = (IUnknown *)*ppv;
            *ppv = &s_d3d3_proxy;
        } else if (memcmp(&riid, &IID_IDirectDraw_g, sizeof(GUID)) == 0) {
            /* QI AddRef'd the real object; our proxy's Release forwards to the
               same real IDirectDraw, so refcounting is correct. */
            *ppv = &s_dd_proxy;
        }
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
{
    HRESULT hr = real_dd4(s)->CreateSurface(d, pp, u);
    /* 4a scope: wrap only render-chain surfaces (primary flip chain and
     * z-buffer).  Texture surfaces stay real until the pool is dynamic. */
    if (SUCCEEDED(hr) && pp && *pp && d && dev_proxy_enabled()) {
        DWORD caps = d->ddsCaps.dwCaps;
        if (caps & DDSCAPS_PRIMARYSURFACE)
            *pp = wrap_surf4(*pp, "primary");
        else if (caps & DDSCAPS_ZBUFFER)
            *pp = wrap_surf4(*pp, "zbuffer");
    }
    return hr;
}
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
