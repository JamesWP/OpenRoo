/* RenderDevice: everything but creation (createdevice.cpp), the shaders
 * (shaders.cpp) and textures (devicetexture.cpp).
 *
 * The setters only record the game's state in PipelineState and mark a group
 * of it dirty (glnative.h); a draw then brings OpenGL up to date with what is
 * dirty, uploads the transforms and the lighting if they changed, and issues
 * the draw.  Direct3D 6 had a viewport whose clip volume put the y axis at
 * +-aspect, which the projection compensates for when it is uploaded.
 *
 * Every method that reaches OpenGL checks `active` first: a headless
 * RenderDevice has no context (glnative.h).
 *
 * KAROO_RENDER_FX=nodraw: every draw returns success without reaching
 * OpenGL.  The replay suite's fast mode sets it: draws are pure output that
 * nothing in the simulation reads back. */

#include "glnative.h"
#include "sysdev.h"
#include <stdio.h>
#include "image.h"
#include "logger.h"
#include <math.h>
#include <string.h>
#include <strings.h>

RenderDevice *g_renderDevice;

static windev::Window *g_window;

void gl_set_window(windev::Window *window)
{
    g_window = window;
}

bool gl_usable()
{
    return g_window != NULL && g_window->hasGLContext();
}

static bool fx_nodraw(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = sysdev::getEnv("KAROO_RENDER_FX", buf, sizeof(buf))
                 && strcasecmp(buf, "nodraw") == 0;
        if (cached)
            g_logger.write("renderdevice: draw FX mode = nodraw\n");
    }
    return cached != 0;
}

// ── The backend's names for the game's vocabulary ──

static GLenum gl_prim(Prim p)
{
    switch (p) {
    case Prim::PointList:     return GL_POINTS;
    case Prim::LineList:      return GL_LINES;
    case Prim::LineStrip:     return GL_LINE_STRIP;
    case Prim::TriangleList:  return GL_TRIANGLES;
    case Prim::TriangleStrip: return GL_TRIANGLE_STRIP;
    case Prim::TriangleFan:   return GL_TRIANGLE_FAN;
    }
    return GL_TRIANGLES;
}

/* The vertices that make up at least one primitive of `p`. */
static bool prim_complete(Prim p, uint32_t count)
{
    switch (p) {
    case Prim::PointList:     return count >= 1;
    case Prim::LineList:      return count >= 2;
    case Prim::LineStrip:     return count >= 2;
    case Prim::TriangleList:  return count >= 3;
    case Prim::TriangleStrip:
    case Prim::TriangleFan:   return count >= 3;
    }
    return false;
}

/* Where each format keeps its attributes.  A offset of -1 is "absent": the
 * attribute then takes the default the draw sets for it. */
struct VertexLayout {
    uint32_t stride;
    int posComponents;  // 4 for Screen's x, y, z, 1/w
    int normal, color, specular, uv;
};

static const VertexLayout &vertex_layout(VertexFormat f)
{
    static const VertexLayout screen   = { 32, 4, -1, 16, 20, 24 };
    static const VertexLayout lit      = { 32, 3, -1, 16, 20, 24 };  // skips the reserved word
    static const VertexLayout normal2  = { 40, 3, 12, -1, -1, 24 };
    static const VertexLayout diffuse1 = { 24, 3, -1, 12, -1, 16 };
    static const VertexLayout diffuse2 = { 32, 3, -1, 12, -1, 16 };
    switch (f) {
    case VertexFormat::Screen:   return screen;
    case VertexFormat::Lit:      return lit;
    case VertexFormat::Normal2:  return normal2;
    case VertexFormat::Diffuse1: return diffuse1;
    case VertexFormat::Diffuse2: return diffuse2;
    }
    return screen;
}

static_assert(sizeof(ScreenVertex) == 32 && sizeof(LitVertex) == 32 &&
              sizeof(Diffuse1Vertex) == 24, "the layouts above are the game's");

// ── State ──

static GLenum gl_blend_factor(BlendFactor f)
{
    switch (f) {
    case BlendFactor::Zero:            return GL_ZERO;
    case BlendFactor::One:             return GL_ONE;
    case BlendFactor::SrcColor:        return GL_SRC_COLOR;
    case BlendFactor::InvSrcColor:     return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:        return GL_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:     return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestAlpha:       return GL_DST_ALPHA;
    case BlendFactor::InvDestAlpha:    return GL_ONE_MINUS_DST_ALPHA;
    case BlendFactor::DestColor:       return GL_DST_COLOR;
    case BlendFactor::InvDestColor:    return GL_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlphaSat:     return GL_SRC_ALPHA_SATURATE;
    case BlendFactor::BothInvSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
    }
    return GL_ONE;
}

static GLenum gl_compare(CompareFunc f)
{
    switch (f) {
    case CompareFunc::Never:        return GL_NEVER;
    case CompareFunc::Less:         return GL_LESS;
    case CompareFunc::Equal:        return GL_EQUAL;
    case CompareFunc::LessEqual:    return GL_LEQUAL;
    case CompareFunc::Greater:      return GL_GREATER;
    case CompareFunc::NotEqual:     return GL_NOTEQUAL;
    case CompareFunc::GreaterEqual: return GL_GEQUAL;
    case CompareFunc::Always:       return GL_ALWAYS;
    }
    return GL_ALWAYS;
}

static GLenum gl_stencil_op(StencilOp o)
{
    switch (o) {
    case StencilOp::Keep:    return GL_KEEP;
    case StencilOp::Zero:    return GL_ZERO;
    case StencilOp::Replace: return GL_REPLACE;
    case StencilOp::IncrSat: return GL_INCR;
    case StencilOp::DecrSat: return GL_DECR;
    case StencilOp::Invert:  return GL_INVERT;
    case StencilOp::Incr:    return GL_INCR_WRAP;
    case StencilOp::Decr:    return GL_DECR_WRAP;
    }
    return GL_KEEP;
}

static GLint gl_filter(Filter f)
{
    return f == Filter::Nearest ? GL_NEAREST : GL_LINEAR;
}

static GLint gl_address(AddressMode a)
{
    switch (a) {
    case AddressMode::Wrap:   return GL_REPEAT;
    case AddressMode::Mirror: return GL_MIRRORED_REPEAT;
    case AddressMode::Clamp:  return GL_CLAMP_TO_EDGE;
    case AddressMode::Border: return GL_CLAMP_TO_BORDER;
    }
    return GL_REPEAT;
}

/* The pair of factors a BlendState means.  BothInvSrcAlpha as the source
 * stands for (InvSrcAlpha, SrcAlpha), and the destination set with it
 * replaces the second; as the destination it sets both. */
static void blend_factors(const BlendState &b, GLenum *src, GLenum *dst)
{
    *src = gl_blend_factor(b.src);
    *dst = gl_blend_factor(b.dst);
    if (b.dst == BlendFactor::BothInvSrcAlpha) {
        *src = GL_ONE_MINUS_SRC_ALPHA;
        *dst = GL_SRC_ALPHA;
    }
}

/* Brings OpenGL's fixed state up to date with what the game has set since
 * the last draw. */
static void flush_pipeline(RenderDevice::Native *n, const PipelineState &st)
{
    const uint32_t dirty = n->dirty & ~GLDirty::Scene;
    if (dirty & GLDirty::Blend) {
        if (st.blend.enable) {
            GLenum src, dst;
            blend_factors(st.blend, &src, &dst);
            gl.Enable(GL_BLEND);
            gl.BlendFunc(src, dst);
        } else {
            gl.Disable(GL_BLEND);
        }
    }
    if (dirty & GLDirty::Depth) {
        if (st.depth.test)
            gl.Enable(GL_DEPTH_TEST);
        else
            gl.Disable(GL_DEPTH_TEST);
        gl.DepthMask(st.depth.write ? GL_TRUE : GL_FALSE);
    }
    if (dirty & GLDirty::Stencil) {
        const StencilState &s = st.stencil;
        if (s.enable) {
            gl.Enable(GL_STENCIL_TEST);
            gl.StencilFunc(gl_compare(s.func), s.ref, 0xFF);
            gl.StencilOp(gl_stencil_op(s.fail), gl_stencil_op(s.zfail), gl_stencil_op(s.pass));
        } else {
            gl.Disable(GL_STENCIL_TEST);
        }
    }
    if (dirty & GLDirty::Raster) {
        // Front faces are the counter-clockwise ones, so culling the front
        // culls what Direct3D calls CCW.
        if (st.raster.cull == CullMode::None) {
            gl.Disable(GL_CULL_FACE);
        } else {
            gl.Enable(GL_CULL_FACE);
            gl.CullFace(st.raster.cull == CullMode::CCW ? GL_FRONT : GL_BACK);
        }
    }
    if (dirty & GLDirty::Sampler) {
        const SamplerState &s = st.samplers[0];
        static const float border[4] = { 0, 0, 0, 0 };
        gl.SamplerParameteri(n->sampler, GL_TEXTURE_MAG_FILTER, gl_filter(s.mag));
        gl.SamplerParameteri(n->sampler, GL_TEXTURE_MIN_FILTER, gl_filter(s.min));
        gl.SamplerParameteri(n->sampler, GL_TEXTURE_WRAP_S, gl_address(s.u));
        gl.SamplerParameteri(n->sampler, GL_TEXTURE_WRAP_T, gl_address(s.v));
        gl.SamplerParameterfv(n->sampler, GL_TEXTURE_BORDER_COLOR, border);
    }
    if (dirty & GLDirty::Texture)
        gl.BindTexture(GL_TEXTURE_2D, st.bound[0] ? st.bound[0]->texture : 0);
    if (dirty & GLDirty::Viewport)
        gl.Viewport((GLint)n->vpX, (GLint)n->vpY, (GLsizei)n->vpW, (GLsizei)n->vpH);
    n->dirty &= GLDirty::Scene;
}

// ── Matrices ──

/* The cofactors c[3*row + col] and the determinant of the upper 3x3 of a
 * row-major 4x4. */
static void cofactors(const float *m, double c[9], double *det)
{
    const double a00 = m[0], a01 = m[1], a02 = m[2];
    const double a10 = m[4], a11 = m[5], a12 = m[6];
    const double a20 = m[8], a21 = m[9], a22 = m[10];
    c[0] = a11 * a22 - a12 * a21;
    c[1] = -(a10 * a22 - a12 * a20);
    c[2] = a10 * a21 - a11 * a20;
    c[3] = -(a01 * a22 - a02 * a21);
    c[4] = a00 * a22 - a02 * a20;
    c[5] = -(a00 * a21 - a01 * a20);
    c[6] = a01 * a12 - a02 * a11;
    c[7] = -(a00 * a12 - a02 * a10);
    c[8] = a00 * a11 - a01 * a10;
    *det = a00 * c[0] + a01 * c[1] + a02 * c[2];
}

/* The matrix the world's normals are carried by: the inverse transpose of its
 * upper 3x3, which is the cofactor matrix over the determinant.  The shader
 * normalises, so only its direction matters. */
static Mat4 normal_matrix(const Mat4 &world)
{
    double c[9], det;
    cofactors(world.m, c, &det);
    Mat4 out = PipelineState::identity();
    if (fabs(det) < 1e-12)
        return out;
    for (int r = 0; r < 3; r++)
        for (int col = 0; col < 3; col++)
            out.m[r * 4 + col] = (float)(c[r * 3 + col] / det);
    return out;
}

/* Where the camera is in world space: the point the view matrix sends to the
 * origin. */
static void eye_position(const Mat4 &view, float eye[3])
{
    double c[9], det;
    cofactors(view.m, c, &det);
    eye[0] = eye[1] = eye[2] = 0.0f;
    if (fabs(det) < 1e-12)
        return;
    for (int j = 0; j < 3; j++) {
        double e = 0.0;
        for (int i = 0; i < 3; i++)
            e -= view.m[12 + i] * c[j * 3 + i] / det;
        eye[j] = (float)e;
    }
}

static void copy_color(float out[4], const ColorF &c)
{
    out[0] = c.r; out[1] = c.g; out[2] = c.b; out[3] = c.a;
}

static void unpack_rgb(float out[4], uint32_t rgb)
{
    out[0] = (float)((rgb >> 16) & 255) / 255.0f;
    out[1] = (float)((rgb >> 8) & 255) / 255.0f;
    out[2] = (float)(rgb & 255) / 255.0f;
    out[3] = 1.0f;
}

/* Direct3D 6's viewport put the clip volume's y axis at +-aspect; the
 * projection takes up the difference by scaling its y column.  `aspect` is
 * the display's width over its height. */
static void upload_scene(RenderDevice::Native *n, const PipelineState &st, float aspect)
{
    SceneBlock b = {};
    memcpy(b.view, st.view.m, sizeof(b.view));
    memcpy(b.proj, st.projection.m, sizeof(b.proj));
    for (int row = 0; row < 4; row++)
        b.proj[row * 4 + 1] *= aspect;
    b.eye[0] = n->eye[0]; b.eye[1] = n->eye[1]; b.eye[2] = n->eye[2];

    if (st.lightSet) {
        const Vec3 &d = st.light.direction;
        const float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len > 0.0f) {
            b.lightDir[0] = d.x / len; b.lightDir[1] = d.y / len; b.lightDir[2] = d.z / len;
        }
        copy_color(b.lightDiffuse, st.light.color);
        copy_color(b.lightSpecular, st.light.color);
    }
    unpack_rgb(b.ambient, st.ambient);

    unpack_rgb(b.fogColor, st.fog.color);
    b.fog[0] = st.fog.start;
    b.fog[1] = st.fog.end;
    b.fog[2] = st.fog.density;
    b.fog[3] = st.fog.enable ? (float)st.fog.mode : 0.0f;  // None is 0

    b.target[0] = 1.0f / (float)n->vpW;
    b.target[1] = -1.0f / (float)n->vpH;
    b.target[2] = (float)n->vpW;
    b.target[3] = (float)n->vpH;

    gl.BindBuffer(GL_UNIFORM_BUFFER, n->sceneUbo);
    gl.BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(b), &b);
    n->dirty &= ~GLDirty::Scene;
}

/* What this draw needs that the program's uniforms hold: the world matrix,
 * the material and which of the program's paths to take. */
static void upload_draw(RenderDevice::Native *n, const PipelineState &st,
                        VertexFormat format, uint32_t flags)
{
    DrawBlock b = {};
    const bool screen = format == VertexFormat::Screen;
    // What has normals is lit, and what has not is not: NoLight turns it off
    // for a mesh the game lit itself.
    const bool lit = format == VertexFormat::Normal2 && !(flags & DrawFlag::NoLight);

    memcpy(b.world, st.world.m, sizeof(b.world));
    memcpy(b.normal, n->normalMatrix.m, sizeof(b.normal));
    copy_color(b.matDiffuse, st.material.diffuse);
    copy_color(b.matAmbient, st.material.ambient);
    copy_color(b.matSpecular, st.material.specular);
    b.matSpecular[3] = st.material.power;
    copy_color(b.matEmissive, st.material.emissive);

    const UVTransform id;
    const UVTransform &uv = screen ? id : st.uvTransform[0];
    b.uv[0] = uv.scaleU; b.uv[1] = uv.scaleV; b.uv[2] = uv.offsetU; b.uv[3] = uv.offsetV;

    b.flags[0] = screen ? 1.0f : 0.0f;
    b.flags[1] = lit ? 1.0f : 0.0f;
    b.flags[2] = st.specular ? 1.0f : 0.0f;
    // The texture modulates the vertex colour; its alpha replaces the
    // vertex's if it has one.  With no texture the vertex colour is the
    // colour.
    const DeviceTexture *tex = st.bound[0];
    b.flags[3] = tex ? (tex->alpha ? 2.0f : 1.0f) : 0.0f;

    if (n->lastDrawValid && memcmp(&b, &n->lastDraw, sizeof(b)) == 0)
        return;
    n->lastDraw = b;
    n->lastDrawValid = true;
    gl.BindBuffer(GL_UNIFORM_BUFFER, n->drawUbo);
    gl.BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(b), &b);
}

/* Points the program's attributes at `count` vertices of `format` in
 * `buffer`, starting `offset` bytes in. */
static void bind_vertices(RenderDevice::Native *n, VertexFormat format,
                          GLuint buffer, size_t offset)
{
    const VertexLayout &l = vertex_layout(format);
    gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
    const GLsizei stride = (GLsizei)l.stride;
    auto at = [&](int off) { return (const void *)(offset + (size_t)off); };

    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, l.posComponents, GL_FLOAT, GL_FALSE, stride, at(0));

    // An attribute a format lacks keeps the value it would have had under
    // fixed-function: a white diffuse, and no specular.
    if (l.normal >= 0) {
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, at(l.normal));
    } else {
        gl.DisableVertexAttribArray(1);
        gl.VertexAttrib4f(1, 0.0f, 0.0f, 1.0f, 0.0f);
    }
    // D3DCOLORs are 0xAARRGGBB: bytes B, G, R, A.
    if (l.color >= 0) {
        gl.EnableVertexAttribArray(2);
        gl.VertexAttribPointer(2, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, stride, at(l.color));
    } else {
        gl.DisableVertexAttribArray(2);
        gl.VertexAttrib4f(2, 1.0f, 1.0f, 1.0f, 1.0f);
    }
    if (l.specular >= 0) {
        gl.EnableVertexAttribArray(3);
        gl.VertexAttribPointer(3, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, stride, at(l.specular));
    } else {
        gl.DisableVertexAttribArray(3);
        gl.VertexAttrib4f(3, 0.0f, 0.0f, 0.0f, 0.0f);
    }
    gl.EnableVertexAttribArray(4);
    gl.VertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, at(l.uv));
    (void)n;
}

// ── Lifetime ──

RenderDevice::RenderDevice()
    : native_(new Native()), mode_(NULL)
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
    if (n->image)
        DestroyTexture(n->image);
    if (n->active && gl_usable()) {
        gl.DeleteBuffers(1, &n->stream);
        gl.DeleteBuffers(1, &n->sceneUbo);
        gl.DeleteBuffers(1, &n->drawUbo);
        gl.DeleteSamplers(1, &n->sampler);
        gl.DeleteVertexArrays(1, &n->vao);
        gl.DeleteProgram(n->program);
        n->window->destroyGLContext();
    }
    *n = Native();
    gl_set_window(NULL);

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
    if (!n->active)
        return;
    GLbitfield bits = 0;
    if (flags & ClearFlag::Color)
        bits |= GL_COLOR_BUFFER_BIT;
    if (flags & ClearFlag::Depth) {
        bits |= GL_DEPTH_BUFFER_BIT | (n->stencil ? GL_STENCIL_BUFFER_BIT : 0);
        gl.DepthMask(GL_TRUE);  // a clear ignores the game's depth write
        n->dirty |= GLDirty::Depth;
    }
    if (bits)
        gl.Clear(bits);
}

bool RenderDevice::BeginFrame()
{
    return true;
}

void RenderDevice::EndFrame()
{
}

/* KAROO_SCREENSHOT=<prefix> with KAROO_SCREENSHOT_FRAMES=<n>[,<n>...]: writes
 * the back buffer of the n-th presented frames, just before they are shown,
 * to <prefix>-<n>.ppm (a diagnostic: to see what the backend drew without a
 * screen grab, which the display server may not give). */
static void capture_frame(RenderDevice::Native *n, unsigned frame)
{
    static char prefix[400], frames[200];
    static int enabled = -1;
    if (enabled < 0) {
        enabled = sysdev::getEnv("KAROO_SCREENSHOT", prefix, sizeof(prefix))
                  && sysdev::getEnv("KAROO_SCREENSHOT_FRAMES", frames, sizeof(frames));
    }
    if (!enabled)
        return;
    // `frames` is a comma-separated list: frame is wanted if it is in it.
    bool wanted = false;
    for (const char *p = frames; *p;) {
        char *end;
        const unsigned long n = strtoul(p, &end, 10);
        if (end == p)
            break;
        wanted = wanted || n == frame;
        p = *end == ',' ? end + 1 : end;
    }
    if (!wanted)
        return;

    const unsigned w = n->vpX + n->vpW, h = n->vpY + n->vpH;
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    char path[460];
    snprintf(path, sizeof(path), "%s-%u.ppm", prefix, frame);
    if (FILE *f = fopen(path, "wb")) {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (unsigned y = h; y-- > 0;)  // OpenGL's first row is the bottom one
            fwrite(&rgb[(size_t)y * w * 3], 1, (size_t)w * 3, f);
        fclose(f);
        g_logger.write("renderdevice: wrote %s\n", path);
    }
}

void RenderDevice::Present()
{
    Native *n = native_;
    if (!n->active)
        return;
    capture_frame(n, ++n->presented);
    n->window->swapBuffers();
}

// ── State ──

void RenderDevice::SetBlend(const BlendState &s)
{
    state_.blend = s;
    native_->dirty |= GLDirty::Blend;
}

void RenderDevice::SetDepth(const DepthState &s)
{
    state_.depth = s;
    native_->dirty |= GLDirty::Depth;
}

void RenderDevice::SetStencil(const StencilState &s)
{
    state_.stencil = s;
    native_->dirty |= GLDirty::Stencil;
}

void RenderDevice::SetRaster(const RasterState &s)
{
    state_.raster = s;
    native_->dirty |= GLDirty::Raster;
}

void RenderDevice::SetFog(const FogState &s)
{
    state_.fog = s;
    native_->dirty |= GLDirty::Scene;
}

void RenderDevice::SetSampler(int stage, const SamplerState &s)
{
    if (stage < 0 || stage >= 2)
        return;
    state_.samplers[stage] = s;
    if (stage == 0)
        native_->dirty |= GLDirty::Sampler;
}

void RenderDevice::SetSamplerAddress(int stage, AddressMode u, AddressMode v)
{
    if (stage < 0 || stage >= 2)
        return;
    SamplerState s = state_.samplers[stage];
    s.u = u;
    s.v = v;
    SetSampler(stage, s);
}

void RenderDevice::SetUVTransform(int stage, const UVTransform &t)
{
    if (stage < 0 || stage >= 2)
        return;
    state_.uvTransform[stage] = t;
}

void RenderDevice::SetTexture(int stage, const DeviceTexture *tex)
{
    if (stage < 0 || stage >= 2)
        return;
    state_.bound[stage] = tex;
    if (stage == 0)
        native_->dirty |= GLDirty::Texture;
}

void RenderDevice::SetSpecular(bool on)
{
    state_.specular = on;
}

void RenderDevice::SetAmbientLight(uint32_t rgb)
{
    state_.ambient = rgb;
    native_->dirty |= GLDirty::Scene;
}

void RenderDevice::SetMaterial(const Material &m)
{
    state_.material = m;
    state_.materialSet = true;
}

void RenderDevice::SetDirectionalLight(const DirectionalLight &l)
{
    state_.light = l;
    state_.lightSet = true;
    native_->dirty |= GLDirty::Scene;
}

void RenderDevice::SetWorld(const Mat4 &m)
{
    state_.world = m;
    native_->normalMatrix = normal_matrix(m);
}

void RenderDevice::SetView(const Mat4 &m)
{
    state_.view = m;
    eye_position(m, native_->eye);
    native_->dirty |= GLDirty::Scene;
}

void RenderDevice::SetProjection(const Mat4 &m)
{
    state_.projection = m;
    native_->dirty |= GLDirty::Scene;
}

// ── Vertex buffers ──

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
    vb->stride = vertex_layout(format).stride;
    vb->buffer = 0;
    if (n->active) {
        gl.GenBuffers(1, &vb->buffer);
        gl.BindBuffer(GL_ARRAY_BUFFER, vb->buffer);
        gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * vb->stride, data,
                      usage == BufferUsage::Dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    }
    return vb;
}

bool RenderDevice::UpdateVertexBuffer(VertexBuffer *vb, uint32_t first,
                                      const void *verts, uint32_t count)
{
    if (!vb || vb->usage != BufferUsage::Dynamic || first > vb->count ||
        count > vb->count - first)
        return false;
    if (vb->buffer && count > 0) {
        gl.BindBuffer(GL_ARRAY_BUFFER, vb->buffer);
        gl.BufferSubData(GL_ARRAY_BUFFER, (GLintptr)first * vb->stride,
                         (GLsizeiptr)count * vb->stride, verts);
    }
    return true;
}

void RenderDevice::DestroyVertexBuffer(VertexBuffer *vb)
{
    if (!vb)
        return;
    if (vb->buffer && gl_usable())
        gl.DeleteBuffers(1, &vb->buffer);
    delete vb;
}

// ── Drawing ──

/* Issues the draw of `count` vertices in `buffer` from `offset` bytes. */
static void submit(RenderDevice::Native *n, const PipelineState &st, float aspect,
                   Prim prim, VertexFormat format, GLuint buffer, size_t offset,
                   uint32_t count, uint32_t flags)
{
    flush_pipeline(n, st);
    if (n->dirty & GLDirty::Scene)
        upload_scene(n, st, aspect);
    upload_draw(n, st, format, flags);
    bind_vertices(n, format, buffer, offset);
    gl.DrawArrays(gl_prim(prim), 0, (GLsizei)count);
}

/* Writes `bytes` to the stream buffer, which is orphaned and started again
 * when it is full; `offset` is where they landed. */
static bool stream_write(RenderDevice::Native *n, const void *data, size_t bytes,
                         size_t *offset)
{
    if (bytes > n->streamSize)
        return false;
    size_t at = (n->streamUsed + 31) & ~(size_t)31;
    gl.BindBuffer(GL_ARRAY_BUFFER, n->stream);
    if (at + bytes > n->streamSize) {
        gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n->streamSize, NULL, GL_STREAM_DRAW);
        at = 0;
    }
    gl.BufferSubData(GL_ARRAY_BUFFER, (GLintptr)at, (GLsizeiptr)bytes, data);
    n->streamUsed = at + bytes;
    *offset = at;
    return true;
}

bool RenderDevice::Draw(Prim prim, VertexFormat format, const void *verts,
                        uint32_t count, uint32_t flags)
{
    Native *n = native_;
    if (!n->active || fx_nodraw())
        return true;
    if (!prim_complete(prim, count))
        return false;
    if (gl_trace_enabled())
        gl_trace_draw(state_, prim, format, verts, count, flags);
    size_t offset;
    if (!stream_write(n, verts, (size_t)count * vertex_layout(format).stride, &offset))
        return false;
    submit(n, state_, (float)((double)mode_->dwWidth / (double)mode_->dwHeight),
           prim, format, n->stream, offset, count, flags);
    return true;
}

bool RenderDevice::DrawBuffer(Prim prim, const VertexBuffer *vb, uint32_t first,
                              uint32_t count, uint32_t flags)
{
    Native *n = native_;
    if (!n->active || fx_nodraw())
        return true;
    if (!vb || !prim_complete(prim, count) || first > vb->count || count > vb->count - first)
        return false;
    if (gl_trace_enabled()) {
        // What the buffer holds, not what the game meant to put in it.
        std::vector<uint8_t> bytes((size_t)count * vb->stride);
        gl.BindBuffer(GL_ARRAY_BUFFER, vb->buffer);
        gl.GetBufferSubData(GL_ARRAY_BUFFER, (GLintptr)first * vb->stride,
                            (GLsizeiptr)bytes.size(), bytes.data());
        gl_trace_draw(state_, prim, vb->format, bytes.data(), count, flags);
    }
    submit(n, state_, (float)((double)mode_->dwWidth / (double)mode_->dwHeight),
           prim, vb->format, vb->buffer, (size_t)first * vb->stride, count, flags);
    return true;
}

void RenderDevice::LogState(const char *tag)
{
    if (!native_->active) {
        g_logger.write("%s: headless, no device state\n", tag);
        return;
    }
    const PipelineState &s = state_;
    g_logger.write("%s: blend %d src %d dst %d, depth test %d write %d, stencil %d, "
                   "cull %d\n", tag, s.blend.enable, (int)s.blend.src, (int)s.blend.dst,
                   s.depth.test, s.depth.write, s.stencil.enable, (int)s.raster.cull);
    g_logger.write("%s: fog %d mode %d, specular %d, ambient %06X, texture %p\n", tag,
                   s.fog.enable, (int)s.fog.mode, s.specular, (unsigned)s.ambient,
                   (const void *)s.bound[0]);
    g_logger.write("%s: GL error %04X\n", tag, (unsigned)gl.GetError());
}
