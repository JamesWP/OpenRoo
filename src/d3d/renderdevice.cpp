/* RenderDevice: everything but creation (createdevice.cpp) and textures
 * (devicetexture.cpp).
 *
 * Each of the game's state types is written to the device by an apply_
 * function, which both the setters and d3d_restore_state call.
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

// ── State, on the device ──

static DWORD d3d_blend(BlendFactor f)
{
    switch (f) {
    case BlendFactor::Zero:            return D3DBLEND_ZERO;
    case BlendFactor::One:             return D3DBLEND_ONE;
    case BlendFactor::SrcColor:        return D3DBLEND_SRCCOLOR;
    case BlendFactor::InvSrcColor:     return D3DBLEND_INVSRCCOLOR;
    case BlendFactor::SrcAlpha:        return D3DBLEND_SRCALPHA;
    case BlendFactor::InvSrcAlpha:     return D3DBLEND_INVSRCALPHA;
    case BlendFactor::DestAlpha:       return D3DBLEND_DESTALPHA;
    case BlendFactor::InvDestAlpha:    return D3DBLEND_INVDESTALPHA;
    case BlendFactor::DestColor:       return D3DBLEND_DESTCOLOR;
    case BlendFactor::InvDestColor:    return D3DBLEND_INVDESTCOLOR;
    case BlendFactor::SrcAlphaSat:     return D3DBLEND_SRCALPHASAT;
    case BlendFactor::BothInvSrcAlpha: return D3DBLEND_BOTHINVSRCALPHA;
    }
    return D3DBLEND_ONE;
}

static DWORD d3d_compare(CompareFunc f)
{
    switch (f) {
    case CompareFunc::Never:        return D3DCMP_NEVER;
    case CompareFunc::Less:         return D3DCMP_LESS;
    case CompareFunc::Equal:        return D3DCMP_EQUAL;
    case CompareFunc::LessEqual:    return D3DCMP_LESSEQUAL;
    case CompareFunc::Greater:      return D3DCMP_GREATER;
    case CompareFunc::NotEqual:     return D3DCMP_NOTEQUAL;
    case CompareFunc::GreaterEqual: return D3DCMP_GREATEREQUAL;
    case CompareFunc::Always:       return D3DCMP_ALWAYS;
    }
    return D3DCMP_ALWAYS;
}

static DWORD d3d_stencil_op(StencilOp o)
{
    switch (o) {
    case StencilOp::Keep:    return D3DSTENCILOP_KEEP;
    case StencilOp::Zero:    return D3DSTENCILOP_ZERO;
    case StencilOp::Replace: return D3DSTENCILOP_REPLACE;
    case StencilOp::IncrSat: return D3DSTENCILOP_INCRSAT;
    case StencilOp::DecrSat: return D3DSTENCILOP_DECRSAT;
    case StencilOp::Invert:  return D3DSTENCILOP_INVERT;
    case StencilOp::Incr:    return D3DSTENCILOP_INCR;
    case StencilOp::Decr:    return D3DSTENCILOP_DECR;
    }
    return D3DSTENCILOP_KEEP;
}

static DWORD d3d_cull(CullMode c)
{
    switch (c) {
    case CullMode::None: return D3DCULL_NONE;
    case CullMode::CW:   return D3DCULL_CW;
    case CullMode::CCW:  return D3DCULL_CCW;
    }
    return D3DCULL_CCW;
}

static DWORD d3d_filter(Filter f)
{
    return f == Filter::Nearest ? D3DTEXF_POINT : D3DTEXF_LINEAR;
}

static DWORD d3d_address(AddressMode a)
{
    switch (a) {
    case AddressMode::Wrap:   return D3DTADDRESS_WRAP;
    case AddressMode::Mirror: return D3DTADDRESS_MIRROR;
    case AddressMode::Clamp:  return D3DTADDRESS_CLAMP;
    case AddressMode::Border: return D3DTADDRESS_BORDER;
    }
    return D3DTADDRESS_WRAP;
}

static DWORD d3d_fog_mode(FogMode m)
{
    switch (m) {
    case FogMode::None:   return D3DFOG_NONE;
    case FogMode::Exp:    return D3DFOG_EXP;
    case FogMode::Exp2:   return D3DFOG_EXP2;
    case FogMode::Linear: return D3DFOG_LINEAR;
    }
    return D3DFOG_NONE;
}

static DWORD float_bits(float f)
{
    DWORD d;
    memcpy(&d, &f, sizeof d);
    return d;
}

/* The two texture stages the game's vertex formats can address. */
enum { kStages = 2 };

/* Blend factors are written only while blending is on: they mean nothing
 * otherwise. */
static void apply_blend(RenderDevice::Native *n, const BlendState &b)
{
    IDirect3DDevice9 *dev = n->device;
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, b.enable);
    if (b.enable) {
        dev->SetRenderState(D3DRS_SRCBLEND, d3d_blend(b.src));
        dev->SetRenderState(D3DRS_DESTBLEND, d3d_blend(b.dst));
    }
}

static void apply_depth(RenderDevice::Native *n, const DepthState &d)
{
    n->device->SetRenderState(D3DRS_ZENABLE, d.test);
    n->device->SetRenderState(D3DRS_ZWRITEENABLE, d.write);
}

/* The stencil reads and writes every bit (the Direct3D default), so the
 * masks are never written. */
static void apply_stencil(RenderDevice::Native *n, const StencilState &s)
{
    IDirect3DDevice9 *dev = n->device;
    dev->SetRenderState(D3DRS_STENCILENABLE, s.enable);
    if (s.enable) {
        dev->SetRenderState(D3DRS_STENCILFUNC, d3d_compare(s.func));
        dev->SetRenderState(D3DRS_STENCILREF, s.ref);
        dev->SetRenderState(D3DRS_STENCILFAIL, d3d_stencil_op(s.fail));
        dev->SetRenderState(D3DRS_STENCILZFAIL, d3d_stencil_op(s.zfail));
        dev->SetRenderState(D3DRS_STENCILPASS, d3d_stencil_op(s.pass));
    }
}

static void apply_raster(RenderDevice::Native *n, const RasterState &r)
{
    n->device->SetRenderState(D3DRS_CULLMODE, d3d_cull(r.cull));
}

/* Fog is Direct3D's per-pixel "table" fog. */
static void apply_fog(RenderDevice::Native *n, const FogState &f)
{
    IDirect3DDevice9 *dev = n->device;
    dev->SetRenderState(D3DRS_FOGENABLE, f.enable);
    if (!f.enable)
        return;
    dev->SetRenderState(D3DRS_FOGTABLEMODE, d3d_fog_mode(f.mode));
    if (f.mode == FogMode::Linear) {
        dev->SetRenderState(D3DRS_FOGSTART, float_bits(f.start));
        dev->SetRenderState(D3DRS_FOGEND, float_bits(f.end));
    } else {
        dev->SetRenderState(D3DRS_FOGDENSITY, float_bits(f.density));
    }
    dev->SetRenderState(D3DRS_FOGCOLOR, f.color);
}

static void apply_sampler(RenderDevice::Native *n, int stage, const SamplerState &s)
{
    IDirect3DDevice9 *dev = n->device;
    dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, d3d_filter(s.mag));
    dev->SetSamplerState(stage, D3DSAMP_MINFILTER, d3d_filter(s.min));
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, d3d_address(s.u));
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, d3d_address(s.v));
}

/* A 2D scale and offset as a texture-stage transform: (u, v, 1) * M. */
static void apply_uv_transform(RenderDevice::Native *n, int stage, const UVTransform &t)
{
    IDirect3DDevice9 *dev = n->device;
    if (t.isIdentity()) {
        dev->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        return;
    }
    D3DMATRIX m = {};
    m._11 = t.scaleU;  m._22 = t.scaleV;
    m._31 = t.offsetU; m._32 = t.offsetV;
    m._33 = 1.0f;      m._44 = 1.0f;
    dev->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + stage), &m);
    dev->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
}

/* Direct3D 6's legacy modulate, which is the only texture blend the game
 * uses: the texture modulates the vertex colour; alpha comes from the
 * texture if it has any, else from the vertex.  A stage with no texture
 * passes the vertex colour through -- Direct3D 9 would sample black. */
static void apply_texture(RenderDevice::Native *n, int stage, const DeviceTexture *t)
{
    IDirect3DDevice9 *dev = n->device;
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

static void apply_material(RenderDevice::Native *n, const Material &m)
{
    D3DMATERIAL9 dm = {};
    dm.Diffuse  = d3d_color(m.diffuse);
    dm.Ambient  = d3d_color(m.ambient);
    dm.Specular = d3d_color(m.specular);
    dm.Emissive = d3d_color(m.emissive);
    dm.Power    = m.power;
    n->device->SetMaterial(&dm);
}

static void apply_light(RenderDevice::Native *n, const DirectionalLight &l)
{
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

static void apply_transform(RenderDevice::Native *n, D3DTRANSFORMSTATETYPE which, const Mat4 &m)
{
    n->device->SetTransform(which, (const D3DMATRIX *)&m);
}

static void apply_projection(RenderDevice::Native *n, const Mat4 &m)
{
    apply_transform(n, D3DTS_PROJECTION, adjust_projection(n, m));
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
void d3d_restore_state(RenderDevice::Native *n, const PipelineState &st)
{
    IDirect3DDevice9 *dev = n->device;
    // The vertex colour is not a light's material: the game's materials are.
    dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
    n->lighting = -1;
    d3d_apply_viewport(n);

    apply_blend(n, st.blend);
    apply_depth(n, st.depth);
    apply_stencil(n, st.stencil);
    apply_raster(n, st.raster);
    apply_fog(n, st.fog);
    dev->SetRenderState(D3DRS_SPECULARENABLE, st.specular);
    dev->SetRenderState(D3DRS_AMBIENT, st.ambient);
    for (int stage = 0; stage < kStages; stage++) {
        apply_sampler(n, stage, st.samplers[stage]);
        apply_uv_transform(n, stage, st.uvTransform[stage]);
        apply_texture(n, stage, st.bound[stage]);
    }
    apply_transform(n, D3DTS_WORLD, st.world);
    apply_transform(n, D3DTS_VIEW, st.view);
    apply_projection(n, st.projection);
    if (st.materialSet)
        apply_material(n, st.material);
    if (st.lightSet)
        apply_light(n, st.light);
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
    n->lighting = -1;

    state_ = PipelineState();

    modes_.clear();
    mode_ = NULL;
}

// ── Display ──

bool RenderDevice::hasStencil() const
{
    return native_->stencil;
}

// ── Frames ──

void RenderDevice::Clear(uint32_t flags)
{
    Native *n = native_;
    if (!n->device)
        return;
    DWORD d3dFlags = 0;
    if (flags & ClearFlag::Color)
        d3dFlags |= D3DCLEAR_TARGET;
    if (flags & ClearFlag::Depth)
        d3dFlags |= D3DCLEAR_ZBUFFER | (n->stencil ? D3DCLEAR_STENCIL : 0);
    if (d3dFlags)
        n->device->Clear(0, NULL, d3dFlags, 0, 1.0f, 0);
}

/* After a lost device (another application's full-screen, a mode switch):
 * waits for it to come back, then resets it and puts the state back. */
static bool recover(RenderDevice::Native *n, const PipelineState &st)
{
    HRESULT hr = n->device->TestCooperativeLevel();
    if (hr == D3DERR_DEVICENOTRESET) {
        d3d_release_image_surfaces(n);  // video memory goes before a Reset
        if (FAILED(n->device->Reset(&n->pp)))
            return false;
        d3d_restore_state(n, st);
        hr = D3D_OK;
    }
    n->lost = FAILED(hr);
    return !n->lost;
}

bool RenderDevice::BeginFrame()
{
    Native *n = native_;
    if (!n->device)
        return true;
    if (n->lost && !recover(n, state_))
        return false;
    return SUCCEEDED(n->device->BeginScene());
}

void RenderDevice::EndFrame()
{
    if (native_->device)
        native_->device->EndScene();
}

void RenderDevice::Present()
{
    Native *n = native_;
    if (!n->device)
        return;
    const HRESULT hr = n->device->Present(NULL, NULL, NULL, NULL);
    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET)
        n->lost = true;
}

// ── State ──

void RenderDevice::SetBlend(const BlendState &s)
{
    state_.blend = s;
    if (native_->device)
        apply_blend(native_, s);
}

void RenderDevice::SetDepth(const DepthState &s)
{
    state_.depth = s;
    if (native_->device)
        apply_depth(native_, s);
}

void RenderDevice::SetStencil(const StencilState &s)
{
    state_.stencil = s;
    if (native_->device)
        apply_stencil(native_, s);
}

void RenderDevice::SetRaster(const RasterState &s)
{
    state_.raster = s;
    if (native_->device)
        apply_raster(native_, s);
}

void RenderDevice::SetFog(const FogState &s)
{
    state_.fog = s;
    if (native_->device)
        apply_fog(native_, s);
}

void RenderDevice::SetSampler(int stage, const SamplerState &s)
{
    if (stage < 0 || stage >= kStages)
        return;
    state_.samplers[stage] = s;
    if (native_->device)
        apply_sampler(native_, stage, s);
}

void RenderDevice::SetSamplerAddress(int stage, AddressMode u, AddressMode v)
{
    if (stage < 0 || stage >= kStages)
        return;
    SamplerState s = state_.samplers[stage];
    s.u = u;
    s.v = v;
    SetSampler(stage, s);
}

void RenderDevice::SetUVTransform(int stage, const UVTransform &t)
{
    if (stage < 0 || stage >= kStages)
        return;
    state_.uvTransform[stage] = t;
    if (native_->device)
        apply_uv_transform(native_, stage, t);
}

void RenderDevice::SetTexture(int stage, const DeviceTexture *tex)
{
    if (stage < 0 || stage >= kStages)
        return;
    state_.bound[stage] = tex;
    if (native_->device)
        apply_texture(native_, stage, tex);
}

void RenderDevice::SetSpecular(bool on)
{
    state_.specular = on;
    if (native_->device)
        native_->device->SetRenderState(D3DRS_SPECULARENABLE, on);
}

void RenderDevice::SetAmbientLight(uint32_t rgb)
{
    state_.ambient = rgb;
    if (native_->device)
        native_->device->SetRenderState(D3DRS_AMBIENT, rgb);
}

void RenderDevice::SetMaterial(const Material &m)
{
    state_.material = m;
    state_.materialSet = true;
    if (native_->device)
        apply_material(native_, m);
}

void RenderDevice::SetDirectionalLight(const DirectionalLight &l)
{
    state_.light = l;
    state_.lightSet = true;
    if (native_->device)
        apply_light(native_, l);
}

void RenderDevice::SetWorld(const Mat4 &m)
{
    state_.world = m;
    if (native_->device)
        apply_transform(native_, D3DTS_WORLD, m);
}

void RenderDevice::SetView(const Mat4 &m)
{
    state_.view = m;
    if (native_->device)
        apply_transform(native_, D3DTS_VIEW, m);
}

void RenderDevice::SetProjection(const Mat4 &m)
{
    state_.projection = m;
    if (native_->device)
        apply_projection(native_, m);
}

// ── Vertex buffers ──

/* Copies `count` vertices of `format` into `out` in the Direct3D layout.
 * They are the game's own layouts but for Lit, whose reserved word after the
 * position is dropped. */
static void pack_vertices(VertexFormat format, const void *in, uint32_t count,
                          unsigned stride, uint8_t *out)
{
    if (format != VertexFormat::Lit) {
        memcpy(out, in, (size_t)count * stride);
        return;
    }
    const LitVertex *v = (const LitVertex *)in;
    for (uint32_t i = 0; i < count; i++, out += stride) {
        memcpy(out, &v[i].x, 12);
        memcpy(out + 12, &v[i].color, 16);  // color, specular, tu, tv
    }
}

/* Writes the CPU copy's vertices [first, first + count) to the buffer. */
static bool upload_vertices(VertexBuffer *vb, uint32_t first, uint32_t count)
{
    if (!vb->vb || count == 0)
        return true;
    void *p = NULL;
    if (FAILED(vb->vb->Lock(first * vb->stride, count * vb->stride, &p, 0)))
        return false;
    memcpy(p, vb->shadow.data() + (size_t)first * vb->stride, (size_t)count * vb->stride);
    vb->vb->Unlock();
    return true;
}

VertexBuffer *RenderDevice::CreateVertexBuffer(VertexFormat format, uint32_t count,
                                               BufferUsage usage, const void *data)
{
    Native *n = native_;
    if (count == 0)
        return NULL;
    VertexBuffer *vb = new VertexBuffer();
    vb->format = format;
    vb->usage  = usage;
    vb->count  = count;
    vb->fvf    = d3d_fvf(format);
    vb->stride = fvf_stride(vb->fvf);
    vb->vb     = NULL;
    vb->shadow.assign((size_t)count * vb->stride, 0);
    if (data)
        pack_vertices(format, data, count, vb->stride, vb->shadow.data());

    // Managed, so the buffer outlives a device Reset.
    if (n->device &&
        FAILED(n->device->CreateVertexBuffer(count * vb->stride,
                                             usage == BufferUsage::Dynamic ? D3DUSAGE_DYNAMIC : 0,
                                             vb->fvf, D3DPOOL_MANAGED, &vb->vb, NULL))) {
        delete vb;
        return NULL;
    }
    if (!upload_vertices(vb, 0, count)) {
        DestroyVertexBuffer(vb);
        return NULL;
    }
    return vb;
}

bool RenderDevice::UpdateVertexBuffer(VertexBuffer *vb, uint32_t first,
                                      const void *verts, uint32_t count)
{
    if (!vb || vb->usage != BufferUsage::Dynamic || first > vb->count ||
        count > vb->count - first)
        return false;
    pack_vertices(vb->format, verts, count, vb->stride,
                  vb->shadow.data() + (size_t)first * vb->stride);
    return upload_vertices(vb, first, count);
}

void RenderDevice::DestroyVertexBuffer(VertexBuffer *vb)
{
    if (!vb)
        return;
    if (vb->vb)
        vb->vb->Release();
    delete vb;
}

// ── Drawing ──

/* Direct3D 9 lights what has normals, so the game's lit meshes (with them)
 * are lit and its vertex-coloured quads (without) are not.  NoLight turns
 * the lighting off for a mesh the game lit itself. */
static void prepare_draw(RenderDevice::Native *n, unsigned long fvf, uint32_t flags)
{
    set_lighting(n, (fvf & D3DFVF_NORMAL) && !(flags & DrawFlag::NoLight));
    n->device->SetFVF(fvf);
}

bool RenderDevice::Draw(Prim prim, VertexFormat format, const void *verts,
                        uint32_t count, uint32_t flags)
{
    Native *n = native_;
    if (!n->device || fx_nodraw())
        return true;
    const int primitives = prim_count(prim, count);
    if (primitives <= 0)
        return false;
    const DWORD fvf = d3d_fvf(format);
    const unsigned stride = fvf_stride(fvf);

    if (format == VertexFormat::Lit) {
        n->scratch.resize((size_t)count * stride);
        pack_vertices(format, verts, count, stride, n->scratch.data());
        verts = n->scratch.data();
    }
    prepare_draw(n, fvf, flags);
    if (d3d_trace_enabled())
        d3d_trace_draw(n, (int)prim, fvf, verts, count, stride);
    return SUCCEEDED(n->device->DrawPrimitiveUP(d3d_prim(prim), primitives, verts, stride));
}

bool RenderDevice::DrawBuffer(Prim prim, const VertexBuffer *vb, uint32_t first,
                              uint32_t count, uint32_t flags)
{
    Native *n = native_;
    if (!n->device || fx_nodraw())
        return true;
    const int primitives = prim_count(prim, count);
    if (!vb || primitives <= 0 || first > vb->count || count > vb->count - first)
        return false;
    prepare_draw(n, vb->fvf, flags);
    n->device->SetStreamSource(0, vb->vb, 0, vb->stride);
    if (d3d_trace_enabled()) {
        // What the device holds, not the CPU copy, so a bad upload shows.
        void *p = NULL;
        if (SUCCEEDED(vb->vb->Lock(first * vb->stride, count * vb->stride, &p, D3DLOCK_READONLY))) {
            d3d_trace_draw(n, (int)prim, vb->fvf, p, count, vb->stride);
            vb->vb->Unlock();
        }
    }
    return SUCCEEDED(n->device->DrawPrimitive(d3d_prim(prim), first, primitives));
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
