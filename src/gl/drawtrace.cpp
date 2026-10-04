/* KAROO_DRAW_TRACE=<file>: one line per draw, hashing everything the draw
 * sees -- the primitive, the vertex bytes, and the pipeline state the game
 * has set.  Two builds that draw the same pictures leave identical traces
 * however differently they get there: a change to the RenderDevice API can be
 * checked for pixel-identical output by diffing the traces of a replay before
 * and after.
 *
 * The state hashed is the RenderDevice's own shadow of it, reduced to what
 * the draw can see: the factors of a blend only while blending is on, and so
 * on.  Nothing here depends on the backend, so a trace is comparable across
 * backends as far as the game's state goes.
 *
 * KAROO_DRAW_TRACE_VERBOSE=1 writes the state as text instead of a hash, to
 * find which field differs when two traces do. */

#include "glnative.h"
#include "sysdev.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>

namespace {

struct Hasher {
    uint64_t h = 1469598103934665603ull;
    void add(const void *p, size_t n)
    {
        const uint8_t *b = (const uint8_t *)p;
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    }
    template <typename T> void add(const T &v) { add(&v, sizeof(v)); }
};

struct Trace {
    FILE *f = NULL;
    bool  verbose = false;
    unsigned long seq = 0;
};

Trace &trace()
{
    static Trace t;
    static bool init = false;
    if (!init) {
        init = true;
        char path[512];
        if (sysdev::getEnv("KAROO_DRAW_TRACE", path, sizeof(path)) && path[0]) {
            t.f = fopen(path, "w");
            char v[8];
            t.verbose = sysdev::getEnv("KAROO_DRAW_TRACE_VERBOSE", v, sizeof(v)) && v[0] == '1';
        }
    }
    return t;
}

}  // namespace

bool gl_trace_enabled()
{
    return trace().f != NULL;
}

void gl_trace_draw(const PipelineState &st, Prim prim, VertexFormat format,
                   const void *verts, uint32_t count, uint32_t flags)
{
    Trace &t = trace();
    if (!t.f)
        return;
    Hasher h;
    char text[2048];
    size_t len = 0;
    auto put = [&](const char *name, unsigned long v) {
        h.add(v);
        if (t.verbose && len < sizeof(text) - 64)
            len += snprintf(text + len, sizeof(text) - len, "%s=%lx ", name, v);
    };

    put("blend", st.blend.enable);
    if (st.blend.enable) {
        put("src", (unsigned long)st.blend.src);
        put("dst", (unsigned long)st.blend.dst);
    }
    put("ztest", st.depth.test);
    put("zwrite", st.depth.write);
    put("stencil", st.stencil.enable);
    if (st.stencil.enable) {
        put("sfunc", (unsigned long)st.stencil.func);
        put("sref", st.stencil.ref);
        put("sfail", (unsigned long)st.stencil.fail);
        put("szfail", (unsigned long)st.stencil.zfail);
        put("spass", (unsigned long)st.stencil.pass);
    }
    put("cull", (unsigned long)st.raster.cull);
    put("fog", st.fog.enable);
    if (st.fog.enable) {
        put("fogmode", (unsigned long)st.fog.mode);
        put("fogcolor", st.fog.color);
        h.add(st.fog.start); h.add(st.fog.end); h.add(st.fog.density);
    }
    put("specular", st.specular);
    put("ambient", st.ambient);
    put("lit", format == VertexFormat::Normal2 && !(flags & DrawFlag::NoLight));

    // The game only ever samples stage 0.
    const SamplerState &s = st.samplers[0];
    put("mag", (unsigned long)s.mag);
    put("min", (unsigned long)s.min);
    put("u", (unsigned long)s.u);
    put("v", (unsigned long)s.v);
    if (const DeviceTexture *tex = st.bound[0]) {
        put("texw", (unsigned long)tex->width);
        put("texh", (unsigned long)tex->height);
        put("texbits", tex->format.bits);
        put("texalpha", tex->alpha);
    } else {
        put("tex", 0xffffffffu);
    }

    h.add(st.world); h.add(st.view); h.add(st.projection);
    if (t.verbose)
        for (const Mat4 *m : { &st.world, &st.view, &st.projection })
            for (int i = 0; i < 16; i++)
                len += snprintf(text + len, sizeof(text) - len, "%g ", m->m[i]);
    h.add(st.material);
    h.add(st.lightSet);
    if (st.lightSet)
        h.add(st.light);

    // The vertices as the draw samples them: texture coordinates through the
    // stage-0 transform, and rounded to 1/64, so that two ways of arriving at
    // the same coordinates (a rewritten vertex or a transform) hash alike.
    static const struct { uint32_t stride; int uv; } kLayouts[] = {
        { 32, -1 },  // Screen: the transform does not apply
        { 32, 24 },  // Lit
        { 40, 24 },  // Normal2
        { 24, 16 },  // Diffuse1
        { 32, 16 },  // Diffuse2
    };
    const auto &layout = kLayouts[(int)format];
    std::vector<uint8_t> copy((const uint8_t *)verts,
                              (const uint8_t *)verts + (size_t)count * layout.stride);
    if (layout.uv >= 0) {
        const UVTransform &x = st.uvTransform[0];
        for (uint32_t i = 0; i < count; i++) {
            float uv[2];
            uint8_t *p = copy.data() + (size_t)i * layout.stride + layout.uv;
            memcpy(uv, p, 8);
            uv[0] = uv[0] * x.scaleU + x.offsetU;
            uv[1] = uv[1] * x.scaleV + x.offsetV;
            int32_t q[2] = { (int32_t)floorf(uv[0] * 64.0f + 0.5f),
                             (int32_t)floorf(uv[1] * 64.0f + 0.5f) };
            memcpy(p, q, 8);
        }
    }
    Hasher vh;
    vh.add(copy.data(), copy.size());
    fprintf(t.f, "%lu prim=%d fmt=%d n=%u state=%016llx verts=%016llx\n", t.seq++,
            (int)prim, (int)format, count, (unsigned long long)h.h, (unsigned long long)vh.h);
    if (t.verbose)
        fprintf(t.f, "  %s\n", text);
}
