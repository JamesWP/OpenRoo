/* RenderDevice: everything but creation (createdevice.cpp) and textures
 * (devicetexture.cpp).
 *
 * The game's render states are Direct3D 6's numbers, and most of them are
 * Direct3D 9's too; the few that moved are mapped in apply_render_state.
 * Direct3D 6 also had a viewport whose clip volume put the y axis at
 * +-aspect, which SetTransform compensates for in the projection matrix.
 *
 * Every method that reaches the device checks for NULL first: a headless
 * RenderDevice has none (d3dnative.h).
 *
 * KAROO_D3D_FX=nodraw: every draw returns success without reaching the
 * device.  The replay suite's fast mode sets it: draws are pure output that
 * nothing in the simulation reads back. */

#include "d3dnative.h"
#include "sysdev.h"
#include <stdio.h>
#include "image.h"
#include "logger.h"
#include <float.h>
#include <math.h>
#include <string.h>

RenderDevice *g_renderDevice;

static bool fx_nodraw(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = sysdev::getEnv("KAROO_D3D_FX", buf, sizeof(buf))
                 && lstrcmpiA(buf, "nodraw") == 0;
        if (cached)
            g_logger.write("renderdevice: draw FX mode = nodraw\n");
    }
    return cached != 0;
}

// ── The backend's names for the game's vocabulary ──

static D3DPRIMITIVETYPE d3d_prim(Prim p)
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

/* The triangles, lines or points `count` vertices make. */
static int prim_count(Prim p, uint32_t count)
{
    switch (p) {
    case Prim::PointList:     return (int)count;
    case Prim::LineList:      return (int)count / 2;
    case Prim::LineStrip:     return (int)count - 1;
    case Prim::TriangleList:  return (int)count / 3;
    case Prim::TriangleStrip:
    case Prim::TriangleFan:   return (int)count - 2;
    }
    return 0;
}

/* The vertex declarations.  Lit is the game's LitVertex without its reserved
 * word; the others are as the game lays them out. */
static DWORD d3d_fvf(VertexFormat f)
{
    switch (f) {
    case VertexFormat::Screen:   return D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1;
    case VertexFormat::Lit:      return D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1;
    case VertexFormat::Normal2:  return D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2;
    case VertexFormat::Diffuse1: return D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    case VertexFormat::Diffuse2: return D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2;
    }
    return 0;
}

static unsigned fvf_tex_count(DWORD fvf)
{
    return (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
}

static unsigned fvf_stride(DWORD fvf)
{
    unsigned s = (fvf & D3DFVF_XYZRHW) ? 16 : 12;
    if (fvf & D3DFVF_NORMAL)   s += 12;
    if (fvf & D3DFVF_DIFFUSE)  s += 4;
    if (fvf & D3DFVF_SPECULAR) s += 4;
    return s + 8 * fvf_tex_count(fvf);
}

static D3DTRANSFORMSTATETYPE d3d_transform(Transform t)
{
    switch (t) {
    case Transform::World:      return D3DTS_WORLD;
    case Transform::View:       return D3DTS_VIEW;
    case Transform::Projection: return D3DTS_PROJECTION;
    }
    return D3DTS_WORLD;
}

// ── State, on the device ──

/* Texture filters are Direct3D 6's: 1 is nearest, anything else linear. */
static DWORD d3d_filter(uint32_t v)
{
    return v == 1 ? D3DTEXF_POINT : D3DTEXF_LINEAR;
}

/* The two texture stages the game's vertex formats can address. */
enum { kStages = 2 };

static void apply_render_state(RenderDevice::Native *n, uint32_t s, uint32_t v)
{
    IDirect3DDevice9 *dev = n->device;
    switch (s) {
    case (uint32_t)RS::TextureMag:
        for (DWORD i = 0; i < kStages; i++) dev->SetSamplerState(i, D3DSAMP_MAGFILTER, d3d_filter(v));
        break;
    case (uint32_t)RS::TextureMin:
        for (DWORD i = 0; i < kStages; i++) dev->SetSamplerState(i, D3DSAMP_MINFILTER, d3d_filter(v));
        break;
    case (uint32_t)RS::TextureAddressU:
        for (DWORD i = 0; i < kStages; i++) dev->SetSamplerState(i, D3DSAMP_ADDRESSU, v);
        break;
    case (uint32_t)RS::TextureAddressV:
        for (DWORD i = 0; i < kStages; i++) dev->SetSamplerState(i, D3DSAMP_ADDRESSV, v);
        break;
    case (uint32_t)RS::TextureMapBlend:   // see apply_texture
    case (uint32_t)RS::ColorKeyEnable:    // gone in Direct3D 9; the game never sets it
        break;
    default:
        dev->SetRenderState((D3DRENDERSTATETYPE)s, v);
        break;
    }
}

/* Direct3D 6's legacy modulate, which is the only texture blend the game
 * uses: the texture modulates the vertex colour; alpha comes from the
 * texture if it has any, else from the vertex.  A stage with no texture
 * passes the vertex colour through -- Direct3D 9 would sample black. */
static void apply_texture(RenderDevice::Native *n, int stage)
{
    IDirect3DDevice9 *dev = n->device;
    const DeviceTexture *t = n->bound[stage];
    dev->SetTexture(stage, d3d_texture_object(t));
    if (stage != 0)
        return;
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, t ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP,
                              t && d3d_texture_has_alpha(t) ? D3DTOP_SELECTARG1
                                                            : D3DTOP_SELECTARG2);
}

static D3DCOLORVALUE d3d_color(const ColorF &c)
{
    D3DCOLORVALUE v;
    v.r = c.r; v.g = c.g; v.b = c.b; v.a = c.a;
    return v;
}

static void apply_material(RenderDevice::Native *n)
{
    const Material &m = n->material;
    D3DMATERIAL9 dm = {};
    dm.Diffuse  = d3d_color(m.diffuse);
    dm.Ambient  = d3d_color(m.ambient);
    dm.Specular = d3d_color(m.specular);
    dm.Emissive = d3d_color(m.emissive);
    dm.Power    = m.power;
    n->device->SetMaterial(&dm);
}

static void apply_light(RenderDevice::Native *n)
{
    const DirectionalLight &l = n->light;
    const float len = sqrtf(l.direction.x * l.direction.x +
                            l.direction.y * l.direction.y +
                            l.direction.z * l.direction.z);
    D3DLIGHT9 dl = {};
    dl.Type      = D3DLIGHT_DIRECTIONAL;
    dl.Diffuse   = d3d_color(l.color);
    dl.Specular  = dl.Diffuse;
    dl.Direction = { l.direction.x / len, l.direction.y / len, l.direction.z / len };
    dl.Range     = sqrtf(FLT_MAX);
    n->device->SetLight(0, &dl);
    n->device->LightEnable(0, TRUE);
}

/* Direct3D 6's viewport put the clip volume's y axis at +-aspect; Direct3D
 * 9's is +-1.  The projection takes up the difference. */
static Mat4 adjust_projection(const RenderDevice::Native *n, const Mat4 &m)
{
    Mat4 out = m;
    const float yScale = (float)((double)n->pp.BackBufferWidth
                               / (double)n->pp.BackBufferHeight);
    for (int row = 0; row < 4; row++)
        out.m[row * 4 + 1] *= yScale;
    return out;
}

static void apply_transform(RenderDevice::Native *n, Transform which)
{
    const Mat4 m = which == Transform::Projection
        ? adjust_projection(n, n->transform[(int)which])
        : n->transform[(int)which];
    n->device->SetTransform(d3d_transform(which), (const D3DMATRIX *)&m);
}

static void set_lighting(RenderDevice::Native *n, bool on)
{
    if (n->lighting != (int)on) {
        n->device->SetRenderState(D3DRS_LIGHTING, on);
        n->lighting = on;
    }
}

/* Puts the device in the state the game's Direct3D 6 device started in,
 * then everything the game has set since.  Called after Create and after a
 * Reset, which sets every state back to its default. */
void d3d_restore_state(RenderDevice::Native *n)
{
    IDirect3DDevice9 *dev = n->device;
    // The vertex colour is not a light's material: the game's materials are.
    dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
    n->lighting = -1;
    d3d_apply_viewport(n);

    for (uint32_t s = 0; s < 256; s++)
        if (n->rsSet[s])
            apply_render_state(n, s, n->rs[s]);
    for (int t = 0; t < 3; t++)
        if (n->transformSet[t])
            apply_transform(n, (Transform)t);
    if (n->materialSet)
        apply_material(n);
    if (n->lightSet)
        apply_light(n);
    for (int stage = 0; stage < kStages; stage++)
        apply_texture(n, stage);
}

// ── Lifetime ──

RenderDevice::RenderDevice()
    : native_(new Native()), mode_(NULL)
{
    lastError_[0] = '\0';
    Release();
}

RenderDevice::~RenderDevice()
{
    Release();
    delete native_;
}

void d3d_release_image_surfaces(RenderDevice::Native *n)
{
    if (n->imageGpu) { n->imageGpu->Release(); n->imageGpu = NULL; }
    if (n->imageSys) { n->imageSys->Release(); n->imageSys = NULL; }
    n->imageW = n->imageH = 0;
}

void RenderDevice::Release()
{
    Native *n = native_;
    d3d_release_image_surfaces(n);
    if (n->device) { n->device->Release(); n->device = NULL; }
    if (n->d3d)    { n->d3d->Release();    n->d3d    = NULL; }
    n->stencil = false;
    n->lost    = false;

    memset(n->rs, 0, sizeof(n->rs));
    memset(n->rsSet, 0, sizeof(n->rsSet));
    memset(n->transform, 0, sizeof(n->transform));
    memset(n->transformSet, 0, sizeof(n->transformSet));
    for (Mat4 &m : n->transform)
        m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    n->materialSet = n->lightSet = false;
    n->bound[0] = n->bound[1] = NULL;
    n->lighting = -1;

    modes_.clear();
    mode_ = NULL;
}

// ── Display ──

bool RenderDevice::hasStencil() const
{
    return native_->stencil;
}

// ── Frames ──

void RenderDevice::ClearDepth()
{
    Native *n = native_;
    if (n->device)
        n->device->Clear(0, NULL,
                         D3DCLEAR_ZBUFFER | (n->stencil ? D3DCLEAR_STENCIL : 0),
                         0, 1.0f, 0);
}

/* After a lost device (another application's full-screen, a mode switch):
 * waits for it to come back, then resets it and puts the state back. */
static bool recover(RenderDevice::Native *n)
{
    HRESULT hr = n->device->TestCooperativeLevel();
    if (hr == D3DERR_DEVICENOTRESET) {
        d3d_release_image_surfaces(n);  // video memory goes before a Reset
        if (FAILED(n->device->Reset(&n->pp)))
            return false;
        d3d_restore_state(n);
        hr = D3D_OK;
    }
    n->lost = FAILED(hr);
    return !n->lost;
}

bool RenderDevice::BeginScene()
{
    Native *n = native_;
    if (!n->device)
        return true;
    if (n->lost && !recover(n))
        return false;
    return SUCCEEDED(n->device->BeginScene());
}

void RenderDevice::EndScene()
{
    if (native_->device)
        native_->device->EndScene();
}

void RenderDevice::Flip()
{
    Native *n = native_;
    if (!n->device)
        return;
    const HRESULT hr = n->device->Present(NULL, NULL, NULL, NULL);
    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET)
        n->lost = true;
}

void RenderDevice::ClearBackBuffer()
{
    if (native_->device)
        native_->device->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
}

// ── State ──

void RenderDevice::SetRenderState(RS state, uint32_t value)
{
    Native *n = native_;
    const uint32_t s = (uint32_t)state;
    if (s >= 256)
        return;
    n->rs[s] = value;
    n->rsSet[s] = true;
    if (n->device)
        apply_render_state(n, s, value);
}

uint32_t RenderDevice::GetRenderState(RS state)
{
    Native *n = native_;
    const uint32_t s = (uint32_t)state;
    if (s >= 256)
        return 0;
    // One the game never set is the device's default.
    if (!n->rsSet[s] && n->device) {
        DWORD v = 0;
        n->device->GetRenderState((D3DRENDERSTATETYPE)s, &v);
        return v;
    }
    return n->rs[s];
}

void RenderDevice::SetTransform(Transform which, const Mat4 *m)
{
    Native *n = native_;
    n->transform[(int)which] = *m;
    n->transformSet[(int)which] = true;
    if (n->device)
        apply_transform(n, which);
}

void RenderDevice::GetTransform(Transform which, Mat4 *m)
{
    *m = native_->transform[(int)which];
}

void RenderDevice::SetAmbientLight(uint32_t rgb)
{
    SetRenderState(RS::Ambient, rgb);
}

void RenderDevice::SetMaterial(const Material &m)
{
    Native *n = native_;
    n->material = m;
    n->materialSet = true;
    if (n->device)
        apply_material(n);
}

void RenderDevice::SetDirectionalLight(const DirectionalLight &l)
{
    Native *n = native_;
    n->light = l;
    n->lightSet = true;
    if (n->device)
        apply_light(n);
}

void RenderDevice::SetTexture(int stage, const DeviceTexture *tex)
{
    Native *n = native_;
    if (stage < 0 || stage >= kStages)
        return;
    n->bound[stage] = tex;
    if (n->device)
        apply_texture(n, stage);
}

// ── Drawing ──

/* Direct3D 9 lights what has normals, so the game's lit meshes (with them)
 * are lit and its vertex-coloured quads (without) are not.  NoLight turns
 * the lighting off for a mesh the game lit itself. */
static bool draw_vertices(RenderDevice::Native *n, Prim prim, DWORD fvf,
                          const void *verts, uint32_t count, uint32_t flags)
{
    const int primitives = prim_count(prim, count);
    if (primitives <= 0)
        return false;
    set_lighting(n, (fvf & D3DFVF_NORMAL) && !(flags & DrawFlag::NoLight));
    n->device->SetFVF(fvf);
    return SUCCEEDED(n->device->DrawPrimitiveUP(d3d_prim(prim), primitives, verts,
                                                fvf_stride(fvf)));
}

bool RenderDevice::Draw(Prim prim, VertexFormat format, const void *verts,
                        uint32_t count, uint32_t flags)
{
    Native *n = native_;
    if (!n->device || fx_nodraw())
        return true;
    const DWORD fvf = d3d_fvf(format);

    if (format == VertexFormat::Lit) {
        // LitVertex has a reserved word after the position; drop it.
        const LitVertex *in = (const LitVertex *)verts;
        n->scratch.resize((size_t)count * fvf_stride(fvf));
        uint8_t *out = n->scratch.data();
        for (uint32_t i = 0; i < count; i++, out += fvf_stride(fvf)) {
            memcpy(out, &in[i].x, 12);
            memcpy(out + 12, &in[i].color, 16);  // color, specular, tu, tv
        }
        verts = n->scratch.data();
    }
    return draw_vertices(n, prim, fvf, verts, count, flags);
}

/* Direct3D 9 has no strided draw, so the streams are interleaved first.  A
 * driver is entitled to read every texture-coordinate set the vertex format
 * declares, so a declared set left unfilled would be a wild read
 * (CRASH.md): the draw is refused instead. */
bool RenderDevice::DrawStrided(Prim prim, VertexFormat format,
                               StridedVertices *v, uint32_t count,
                               uint32_t flags)
{
    Native *n = native_;
    if (fx_nodraw())
        return true;
    const DWORD fvf = d3d_fvf(format);
    const unsigned ntex = fvf_tex_count(fvf);
    for (unsigned i = 0; i < ntex; i++) {
        if (v->texCoords[i].data == NULL) {
            g_logger.write("renderdevice: DrawStrided refused: format %d declares "
                      "texture set %u but it is unfilled\n", (int)format, i);
            return false;
        }
    }
    if (v->position.data == NULL ||
        ((fvf & D3DFVF_NORMAL) && v->normal.data == NULL))
        return false;

    const unsigned stride = fvf_stride(fvf);
    n->scratch.resize((size_t)count * stride);
    uint8_t *out = n->scratch.data();
    auto at = [](const VertexStream &s, uint32_t i) {
        return (const uint8_t *)s.data + (size_t)i * s.stride;
    };
    for (uint32_t i = 0; i < count; i++) {
        memcpy(out, at(v->position, i), 12);
        out += 12;
        if (fvf & D3DFVF_NORMAL) {
            memcpy(out, at(v->normal, i), 12);
            out += 12;
        }
        if (fvf & D3DFVF_DIFFUSE) {
            const uint32_t white = 0xffffffff;
            memcpy(out, v->diffuse.data ? at(v->diffuse, i) : (const uint8_t *)&white, 4);
            out += 4;
        }
        for (unsigned t = 0; t < ntex; t++) {
            memcpy(out, at(v->texCoords[t], i), 8);
            out += 8;
        }
    }
    // A headless device gets this far on purpose: reading every stream is the
    // guard bombstart-crash exists for.
    if (!n->device)
        return true;
    return draw_vertices(n, prim, fvf, n->scratch.data(), count, flags);
}

void RenderDevice::LogState(const char *tag)
{
    IDirect3DDevice9 *dev = native_->device;
    if (!dev) {
        g_logger.write("%s: headless, no device state\n", tag);
        return;
    }

    static const struct { D3DRENDERSTATETYPE rs; const char *name; } rstates[] = {
        { D3DRS_SHADEMODE,        "SHADEMODE"        },
        { D3DRS_SRCBLEND,         "SRCBLEND"         },
        { D3DRS_DESTBLEND,        "DESTBLEND"        },
        { D3DRS_CULLMODE,         "CULLMODE"         },
        { D3DRS_ALPHABLENDENABLE, "ALPHABLENDENABLE" },
        { D3DRS_FOGENABLE,        "FOGENABLE"        },
        { D3DRS_FOGCOLOR,         "FOGCOLOR"         },
        { D3DRS_SPECULARENABLE,   "SPECULARENABLE"   },
        { D3DRS_TEXTUREFACTOR,    "TEXTUREFACTOR"    },
        { D3DRS_AMBIENT,          "AMBIENT"          },
        { D3DRS_LIGHTING,         "LIGHTING"         },
    };
    for (const auto &r : rstates) {
        DWORD v = 0xdeadbeef;
        HRESULT hr = dev->GetRenderState(r.rs, &v);
        g_logger.write("%s: rs %-17s = %08lX (hr=%08lX)\n", tag, r.name, v, hr);
    }

    static const struct { D3DTEXTURESTAGESTATETYPE ts; const char *name; } tstates[] = {
        { D3DTSS_COLOROP,       "COLOROP"       },
        { D3DTSS_COLORARG1,     "COLORARG1"     },
        { D3DTSS_COLORARG2,     "COLORARG2"     },
        { D3DTSS_ALPHAOP,       "ALPHAOP"       },
        { D3DTSS_TEXCOORDINDEX, "TEXCOORDINDEX" },
    };
    for (const auto &t : tstates) {
        DWORD v = 0xdeadbeef;
        HRESULT hr = dev->GetTextureStageState(0, t.ts, &v);
        g_logger.write("%s: ts0 %-14s = %08lX (hr=%08lX)\n", tag, t.name, v, hr);
    }
}
