/* KAROO_DRAW_TRACE=<file>: one line per draw, hashing everything the draw
 * sees -- the primitive, the vertex bytes, and the Direct3D 9 device's state
 * as the device itself reports it.  Because it reads the device back rather
 * than the RenderDevice's own bookkeeping, two builds that draw the same
 * pictures leave identical traces however differently they get there: a change
 * to the RenderDevice API can be checked for pixel-identical output by diffing
 * the traces of a replay before and after.
 *
 * KAROO_DRAW_TRACE_VERBOSE=1 writes the state as text instead of a hash, to
 * find which field differs when two traces do. */

#include "d3dnative.h"
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

/* Render states that always matter.  Others matter only while the feature
 * they belong to is on, so a draw that sets the factors of a blend it then
 * leaves off does not differ from one that never touched them. */
const D3DRENDERSTATETYPE kRenderStates[] = {
    D3DRS_ZENABLE, D3DRS_FILLMODE, D3DRS_SHADEMODE, D3DRS_ZWRITEENABLE,
    D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_ZFUNC, D3DRS_ALPHAREF,
    D3DRS_ALPHAFUNC, D3DRS_DITHERENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_FOGENABLE,
    D3DRS_SPECULARENABLE, D3DRS_STENCILENABLE, D3DRS_TEXTUREFACTOR,
    D3DRS_LIGHTING, D3DRS_AMBIENT, D3DRS_COLORVERTEX, D3DRS_COLORWRITEENABLE,
    D3DRS_BLENDOP, D3DRS_CLIPPING, D3DRS_FOGVERTEXMODE, D3DRS_LOCALVIEWER,
    D3DRS_NORMALIZENORMALS, D3DRS_DIFFUSEMATERIALSOURCE,
    D3DRS_SPECULARMATERIALSOURCE, D3DRS_AMBIENTMATERIALSOURCE,
    D3DRS_EMISSIVEMATERIALSOURCE,
};
const D3DRENDERSTATETYPE kBlendStates[] = { D3DRS_SRCBLEND, D3DRS_DESTBLEND };
const D3DRENDERSTATETYPE kFogStates[] = {
    D3DRS_FOGCOLOR, D3DRS_FOGTABLEMODE, D3DRS_FOGSTART, D3DRS_FOGEND,
    D3DRS_FOGDENSITY,
};
const D3DRENDERSTATETYPE kStencilStates[] = {
    D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL, D3DRS_STENCILPASS, D3DRS_STENCILFUNC,
    D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK,
};

const D3DSAMPLERSTATETYPE kSamplerStates[] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER,
    D3DSAMP_MIPFILTER,
};

const D3DTEXTURESTAGESTATETYPE kStageStates[] = {
    D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2, D3DTSS_ALPHAOP,
    D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, D3DTSS_TEXCOORDINDEX,
    D3DTSS_TEXTURETRANSFORMFLAGS,
};

}  // namespace

bool d3d_trace_enabled()
{
    return trace().f != NULL;
}

void d3d_trace_draw(RenderDevice::Native *n, int prim, unsigned long fvf,
                    const void *verts, uint32_t count, unsigned stride)
{
    Trace &t = trace();
    if (!t.f)
        return;
    IDirect3DDevice9 *dev = n->device;
    Hasher st;
    char text[8192];
    size_t len = 0;
    auto put = [&](const char *fmt, unsigned long a, unsigned long b = 0) {
        if (t.verbose && len < sizeof(text) - 64)
            len += snprintf(text + len, sizeof(text) - len, fmt, a, b);
    };

    auto rsAdd = [&](D3DRENDERSTATETYPE rs) {
        DWORD v = 0; dev->GetRenderState(rs, &v);
        st.add(v); put("rs%lu=%lx ", rs, v);
        return v;
    };
    for (D3DRENDERSTATETYPE rs : kRenderStates) {
        const DWORD v = rsAdd(rs);
        if (rs == D3DRS_ALPHABLENDENABLE && v)
            for (D3DRENDERSTATETYPE r : kBlendStates) rsAdd(r);
        if (rs == D3DRS_FOGENABLE && v)
            for (D3DRENDERSTATETYPE r : kFogStates) rsAdd(r);
        if (rs == D3DRS_STENCILENABLE && v)
            for (D3DRENDERSTATETYPE r : kStencilStates) rsAdd(r);
    }
    for (DWORD s = 0; s < 2; s++) {
        // The game only ever samples stage 0; stage 1 has no texture.
        if (s == 0)
            for (D3DSAMPLERSTATETYPE ss : kSamplerStates) {
                DWORD v = 0; dev->GetSamplerState(s, ss, &v);
                st.add(v); put("s%lu.%lu ", s * 100 + ss, v);
            }
        IDirect3DBaseTexture9 *base = NULL;
        dev->GetTexture(s, &base);
        if (base) {
            IDirect3DTexture9 *tex = NULL;
            if (SUCCEEDED(base->QueryInterface(__uuidof(IDirect3DTexture9), (void **)&tex))) {
                D3DSURFACE_DESC d; tex->GetLevelDesc(0, &d);
                st.add(d.Width); st.add(d.Height); st.add(d.Format);
                put("tex%lu=%lx ", s, d.Format);
                tex->Release();
            }
            base->Release();
        } else {
            st.add(0xffffffffu);
        }
    }
    for (D3DTEXTURESTAGESTATETYPE ts : kStageStates) {
        for (DWORD s = 0; s < 2; s++) {
            DWORD v = 0; dev->GetTextureStageState(s, ts, &v);
            st.add(v); put("t%lu=%lx ", s * 100 + ts, v);
        }
    }
    for (D3DTRANSFORMSTATETYPE ts : { D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION }) {
        D3DMATRIX m; dev->GetTransform(ts, &m);
        st.add(m);
        if (t.verbose)
            for (int i = 0; i < 16; i++)
                len += snprintf(text + len, sizeof(text) - len, "%g ", ((float *)&m)[i]);
    }
    D3DMATERIAL9 mat; dev->GetMaterial(&mat); st.add(mat);
    D3DLIGHT9 light; BOOL on = FALSE;
    if (SUCCEEDED(dev->GetLight(0, &light)) && SUCCEEDED(dev->GetLightEnable(0, &on))) {
        st.add(light); st.add(on);
    }

    // The vertices as the draw samples them: texture coordinates through the
    // stage-0 transform, and rounded to 1/64, so that two ways of arriving at
    // the same coordinates (a rewritten vertex or a transform) hash alike.
    Hasher vh;
    std::vector<uint8_t> copy((const uint8_t *)verts, (const uint8_t *)verts + (size_t)count * stride);
    const unsigned ntex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (ntex > 0 && !(fvf & D3DFVF_XYZRHW)) {
        unsigned uvOff = 12 + ((fvf & D3DFVF_NORMAL) ? 12 : 0) + ((fvf & D3DFVF_DIFFUSE) ? 4 : 0) +
                         ((fvf & D3DFVF_SPECULAR) ? 4 : 0);
        DWORD ttf = 0;
        dev->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &ttf);
        D3DMATRIX tm; dev->GetTransform(D3DTS_TEXTURE0, &tm);
        for (uint32_t i = 0; i < count; i++) {
            float uv[2];
            uint8_t *p = copy.data() + (size_t)i * stride + uvOff;
            memcpy(uv, p, 8);
            if (ttf & D3DTTFF_COUNT2) {
                const float u = uv[0] * tm._11 + uv[1] * tm._21 + tm._31;
                const float v = uv[0] * tm._12 + uv[1] * tm._22 + tm._32;
                uv[0] = u; uv[1] = v;
            }
            int32_t q[2] = { (int32_t)floorf(uv[0] * 64.0f + 0.5f), (int32_t)floorf(uv[1] * 64.0f + 0.5f) };
            memcpy(p, q, 8);
        }
    }
    vh.add(copy.data(), copy.size());
    fprintf(t.f, "%lu prim=%d fvf=%lx n=%u state=%016llx verts=%016llx\n", t.seq++,
            prim, fvf, count, (unsigned long long)st.h, (unsigned long long)vh.h);
    if (t.verbose)
        fprintf(t.f, "  %s\n", text);
}
