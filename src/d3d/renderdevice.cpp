/* RenderDevice: everything but creation (renderdevice.h).  Device creation
 * is in createdevice.cpp.
 *
 * KAROO_FLIP_FX=noblt makes PresentImage skip the Blt and flip whatever is
 * already on the back buffer: the loading/theme bitmap never appears. */

#include "d3dnative.h"
#include "scenetexture.h"
#include "log.h"
#include <math.h>
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

/* KAROO_D3D_FX=nodraw: every draw returns success without reaching the
 * device.  The replay suite's fast mode sets it: draws are pure output that
 * nothing in the simulation reads back. */
static bool fx_nodraw(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = GetEnvironmentVariableA("KAROO_D3D_FX", buf, sizeof(buf))
                 && lstrcmpiA(buf, "nodraw") == 0;
        if (cached)
            log_write("renderdevice: draw FX mode = nodraw\n");
    }
    return cached != 0;
}

// ── The backend's names for the game's vocabulary ──

D3DPRIMITIVETYPE d3d_prim(Prim p)
{
    switch (p) {
    case Prim::PointList:     return D3DPT_POINTLIST;
    case Prim::LineList:      return D3DPT_LINELIST;
    case Prim::LineStrip:     return D3DPT_LINESTRIP;
    case Prim::TriangleList:  return D3DPT_TRIANGLELIST;
    case Prim::TriangleStrip: return D3DPT_TRIANGLESTRIP;
    case Prim::TriangleFan:   return D3DPT_TRIANGLEFAN;
    }
    return D3DPT_TRIANGLELIST;
}

DWORD d3d_fvf(VertexFormat f)
{
    switch (f) {
    case VertexFormat::Screen:   return D3DFVF_TLVERTEX;
    case VertexFormat::Lit:      return D3DFVF_LVERTEX;
    case VertexFormat::Normal2:  return D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2;
    case VertexFormat::Diffuse1: return D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    case VertexFormat::Diffuse2: return D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2;
    }
    return 0;
}

static D3DTRANSFORMSTATETYPE d3d_transform(Transform t)
{
    switch (t) {
    case Transform::World:      return D3DTRANSFORMSTATE_WORLD;
    case Transform::View:       return D3DTRANSFORMSTATE_VIEW;
    case Transform::Projection: return D3DTRANSFORMSTATE_PROJECTION;
    }
    return D3DTRANSFORMSTATE_WORLD;
}

static DWORD d3d_draw_flags(uint32_t flags)
{
    DWORD out = 0;
    if (flags & DrawFlag::NoLight)         out |= D3DDP_DONOTLIGHT;
    if (flags & DrawFlag::NoUpdateExtents) out |= D3DDP_DONOTUPDATEEXTENTS;
    return out;
}

// ── Lifetime ──

RenderDevice::RenderDevice()
    : native_(new Native()), mode_(NULL), modeFilterFlags_(0)
{
    lastError_[0] = '\0';
}

RenderDevice::~RenderDevice()
{
    Release();
    delete native_;
}

void RenderDevice::Release()
{
    Native *n = native_;
    if (n->light)      { n->light->Release();      n->light      = NULL; }
    if (n->material)   { n->material->Release();   n->material   = NULL; }
    n->hMaterial = 0;
    if (n->viewport)   { n->viewport->Release();   n->viewport   = NULL; }
    if (n->device)     { n->device->Release();     n->device     = NULL; }
    if (n->zBuffer)    { n->zBuffer->Release();    n->zBuffer    = NULL; }
    // The back buffer came from GetAttachedSurface, which AddRef'd it.
    // REVIEW: the original NULLed it without a Release, leaking that
    // reference.
    if (n->backBuffer) { n->backBuffer->Release(); n->backBuffer = NULL; }
    if (n->primary)    { n->primary->Release();    n->primary    = NULL; }
    if (n->d3d)        { n->d3d->Release();        n->d3d        = NULL; }
    if (n->dd)         { n->dd->Release();         n->dd         = NULL; }
    memset(&n->zbufFmt, 0, sizeof(n->zbufFmt));

    modes_.clear();
    mode_            = NULL;
    modeFilterFlags_ = 0;
}

// ── Display ──

bool RenderDevice::hasStencil() const
{
    return native_->zbufFmt.dwStencilBitDepth != 0;
}

void RenderDevice::RestoreDisplayMode()
{
    if (native_->dd)
        native_->dd->RestoreDisplayMode();
}

void RenderDevice::GetMovieTarget(void **directDraw, void **primarySurface)
{
    *directDraw = NULL;
    *primarySurface = NULL;
    native_->dd->QueryInterface(IID_IDirectDraw, directDraw);
    native_->primary->QueryInterface(IID_IDirectDrawSurface, primarySurface);
}

// ── Frames ──

void RenderDevice::ClearDepth()
{
    D3DRECT rect = { 0, 0, (LONG)width(), (LONG)height() };
    DWORD flags = D3DCLEAR_ZBUFFER;
    if (hasStencil())
        flags |= D3DCLEAR_STENCIL;
    native_->viewport->Clear2(1, &rect, flags, 0, 1.0f, 0);
}

bool RenderDevice::BeginScene()
{
    return SUCCEEDED(native_->device->BeginScene());
}

void RenderDevice::EndScene()
{
    native_->device->EndScene();
}

void RenderDevice::Flip()
{
    native_->primary->Flip(NULL, DDFLIP_WAIT);
}

void RenderDevice::ClearBackBuffer()
{
    DDBLTFX fx;
    memset(&fx, 0, sizeof fx);
    fx.dwSize = sizeof fx;
    native_->backBuffer->Blt(NULL, NULL, NULL, DDBLT_COLORFILL, &fx);
}

void RenderDevice::PresentImage(LoadedImage *img)
{
    Native *n = native_;
    HRESULT hr_blt = S_OK;
    bool skipped = fx_noblt();

    if (!skipped)
        hr_blt = n->backBuffer->Blt(NULL, img->textureSurface(), NULL,
                                    DDBLT_WAIT, NULL);

    HRESULT hr_flip = n->primary->Flip(NULL, DDFLIP_WAIT);

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= FLIP_LOG_FIRST) {
        char blt[16];
        if (skipped)
            lstrcpyA(blt, "skipped");
        else
            wsprintfA(blt, "%08lX", hr_blt);
        log_write("renderdevice: PresentImage img=%p src=%p back=%p primary=%p "
                  "blt=%s flip=%08lX\n",
                  img, img->textureSurface(), n->backBuffer, n->primary,
                  blt, hr_flip);
    }
}

// ── State ──

void RenderDevice::SetRenderState(RS state, uint32_t value)
{
    native_->device->SetRenderState((D3DRENDERSTATETYPE)state, value);
}

uint32_t RenderDevice::GetRenderState(RS state)
{
    DWORD v = 0;
    native_->device->GetRenderState((D3DRENDERSTATETYPE)state, &v);
    return v;
}

void RenderDevice::SetTransform(Transform which, const Mat4 *m)
{
    native_->device->SetTransform(d3d_transform(which), (D3DMATRIX *)m);
}

void RenderDevice::GetTransform(Transform which, Mat4 *m)
{
    native_->device->GetTransform(d3d_transform(which), (D3DMATRIX *)m);
}

void RenderDevice::SetTexture(int stage, const SceneTexture *tex)
{
    native_->device->SetTexture(stage, tex ? tex->texture2() : NULL);
}

void RenderDevice::SetAmbientLight(uint32_t rgb)
{
    native_->device->SetLightState(D3DLIGHTSTATE_AMBIENT, rgb);
}

static D3DCOLORVALUE d3d_color(const ColorF &c)
{
    D3DCOLORVALUE v;
    v.r = c.r; v.g = c.g; v.b = c.b; v.a = c.a;
    return v;
}

void RenderDevice::SetMaterial(const Material &m)
{
    Native *n = native_;
    if (n->material == NULL) {
        HRESULT hr = n->d3d->CreateMaterial(&n->material, NULL);
        if (SUCCEEDED(hr))
            hr = n->material->GetHandle(n->device, &n->hMaterial);
        log_write("renderdevice: CreateMaterial mat=%p handle=%08lX -> hr=%08lX\n",
                  (void *)n->material, (unsigned long)n->hMaterial, hr);
        if (FAILED(hr))
            return;
    }

    D3DMATERIAL dm;
    memset(&dm, 0, sizeof dm);
    dm.dwSize     = sizeof dm;
    dm.diffuse    = d3d_color(m.diffuse);
    dm.ambient    = d3d_color(m.ambient);
    dm.specular   = d3d_color(m.specular);
    dm.emissive   = d3d_color(m.emissive);
    dm.power      = m.power;
    dm.dwRampSize = 1;
    n->material->SetMaterial(&dm);
    n->device->SetLightState(D3DLIGHTSTATE_MATERIAL, n->hMaterial);
}

void RenderDevice::SetDirectionalLight(const DirectionalLight &l)
{
    Native *n = native_;
    if (n->light == NULL) {
        HRESULT hr = n->d3d->CreateLight(&n->light, NULL);
        log_write("renderdevice: CreateLight light=%p -> hr=%08lX\n",
                  (void *)n->light, hr);
        if (FAILED(hr))
            return;
        n->viewport->AddLight(n->light);
    }

    D3DLIGHT2 dl;
    memset(&dl, 0, sizeof dl);
    dl.dwSize         = sizeof dl;
    dl.dltType        = D3DLIGHT_DIRECTIONAL;
    dl.dcvColor       = d3d_color(l.color);
    dl.dvDirection.x  = l.direction.x;
    dl.dvDirection.y  = l.direction.y;
    dl.dvDirection.z  = l.direction.z;
    dl.dvRange        = D3DLIGHT_RANGE_MAX;
    dl.dvFalloff      = 1.0f;
    dl.dvAttenuation0 = 1.0f;
    dl.dwFlags        = D3DLIGHT_ACTIVE;
    n->light->SetLight((D3DLIGHT *)&dl);
}

// ── Drawing ──

bool RenderDevice::Draw(Prim prim, VertexFormat format, const void *verts,
                        uint32_t count, uint32_t flags)
{
    if (fx_nodraw())
        return true;
    return SUCCEEDED(native_->device->DrawPrimitive(
        d3d_prim(prim), d3d_fvf(format), (void *)verts, count,
        d3d_draw_flags(flags)));
}

/* A driver is entitled to read every texture-coordinate set the vertex
 * format declares, so a declared set left unfilled would be a wild read
 * (CRASH.md): the draw is refused instead. */
bool RenderDevice::DrawStrided(Prim prim, VertexFormat format,
                               StridedVertices *v, uint32_t count,
                               uint32_t flags)
{
    if (fx_nodraw())
        return true;
    DWORD fvf  = d3d_fvf(format);
    DWORD ntex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (ntex > D3DDP_MAXTEXCOORD)
        ntex = D3DDP_MAXTEXCOORD;
    for (DWORD i = 0; i < ntex; i++) {
        if (v->texCoords[i].data == NULL) {
            log_write("renderdevice: DrawStrided refused: format %d declares "
                      "texture set %lu but it is unfilled\n", (int)format, i);
            return false;
        }
    }

    D3DDRAWPRIMITIVESTRIDEDDATA sd;
    memset(&sd, 0, sizeof(sd));
    sd.position.lpvData = (void *)v->position.data;
    sd.position.dwStride = v->position.stride;
    sd.normal.lpvData = (void *)v->normal.data;
    sd.normal.dwStride = v->normal.stride;
    sd.diffuse.lpvData = (void *)v->diffuse.data;
    sd.diffuse.dwStride = v->diffuse.stride;
    sd.specular.lpvData = (void *)v->specular.data;
    sd.specular.dwStride = v->specular.stride;
    for (DWORD i = 0; i < D3DDP_MAXTEXCOORD; i++) {
        sd.textureCoords[i].lpvData  = (void *)v->texCoords[i].data;
        sd.textureCoords[i].dwStride = v->texCoords[i].stride;
    }
    return SUCCEEDED(native_->device->DrawPrimitiveStrided(
        d3d_prim(prim), fvf, &sd, count, d3d_draw_flags(flags)));
}

void RenderDevice::LogState(const char *tag)
{
    IDirect3DDevice3 *dev = native_->device;

    static const struct { D3DRENDERSTATETYPE rs; const char *name; } rstates[] = {
        { D3DRENDERSTATE_SHADEMODE,       "SHADEMODE"       },
        { D3DRENDERSTATE_SRCBLEND,        "SRCBLEND"        },
        { D3DRENDERSTATE_DESTBLEND,       "DESTBLEND"       },
        { D3DRENDERSTATE_TEXTUREMAPBLEND, "TEXTUREMAPBLEND" },
        { D3DRENDERSTATE_CULLMODE,        "CULLMODE"        },
        { D3DRENDERSTATE_ALPHABLENDENABLE,"ALPHABLENDENABLE"},
        { D3DRENDERSTATE_FOGENABLE,       "FOGENABLE"       },
        { D3DRENDERSTATE_FOGCOLOR,        "FOGCOLOR"        },
        { D3DRENDERSTATE_SPECULARENABLE,  "SPECULARENABLE"  },
        { D3DRENDERSTATE_COLORKEYENABLE,  "COLORKEYENABLE"  },
        { D3DRENDERSTATE_TEXTUREFACTOR,   "TEXTUREFACTOR"   },
        { D3DRENDERSTATE_AMBIENT,         "AMBIENT"         },
    };
    for (unsigned i = 0; i < sizeof rstates / sizeof rstates[0]; i++) {
        DWORD v = 0xdeadbeef;
        HRESULT hr = dev->GetRenderState(rstates[i].rs, &v);
        log_write("%s: rs %-17s = %08lX (hr=%08lX)\n", tag, rstates[i].name, v, hr);
    }

    static const struct { D3DTEXTURESTAGESTATETYPE ts; const char *name; } tstates[] = {
        { D3DTSS_COLOROP,   "COLOROP"   },
        { D3DTSS_COLORARG1, "COLORARG1" },
        { D3DTSS_COLORARG2, "COLORARG2" },
        { D3DTSS_ALPHAOP,   "ALPHAOP"   },
        { D3DTSS_TEXCOORDINDEX, "TEXCOORDINDEX" },
    };
    for (unsigned i = 0; i < sizeof tstates / sizeof tstates[0]; i++) {
        DWORD v = 0xdeadbeef;
        HRESULT hr = dev->GetTextureStageState(0, tstates[i].ts, &v);
        log_write("%s: ts0 %-14s = %08lX (hr=%08lX)\n", tag, tstates[i].name, v, hr);
    }

    DWORD lmat = 0xdeadbeef, lamb = 0xdeadbeef;
    HRESULT hr1 = dev->GetLightState(D3DLIGHTSTATE_MATERIAL, &lmat);
    HRESULT hr2 = dev->GetLightState(D3DLIGHTSTATE_AMBIENT,  &lamb);
    IDirect3DTexture2 *tex = NULL;
    HRESULT hr3 = dev->GetTexture(0, &tex);
    log_write("%s: lightstate MATERIAL=%08lX (hr=%08lX) AMBIENT=%08lX (hr=%08lX) "
              "tex0=%p (hr=%08lX)\n",
              tag, lmat, hr1, lamb, hr2, (void *)tex, hr3);
    if (tex)
        tex->Release();
}
