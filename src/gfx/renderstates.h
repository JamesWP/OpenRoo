/* Single-field setters for the RenderDevice's state.  Each reads the current
 * state, changes one field and writes the whole state back, so the other
 * fields keep their values. */
#pragma once
#include "renderdevice.h"

static inline void set_stencil_enable(RenderDevice *d3d, bool on)
{
    StencilState s = d3d->stencil();
    s.enable = on;
    d3d->SetStencil(s);
}

static inline void set_depth(RenderDevice *d3d, bool test, bool write)
{
    d3d->SetDepth(DepthState{ test, write });
}

static inline void set_depth_test(RenderDevice *d3d, bool on)
{
    DepthState d = d3d->depth();
    d.test = on;
    d3d->SetDepth(d);
}

static inline void set_depth_write(RenderDevice *d3d, bool on)
{
    DepthState d = d3d->depth();
    d.write = on;
    d3d->SetDepth(d);
}

static inline void set_fog_enable(RenderDevice *d3d, bool on)
{
    FogState f = d3d->fog();
    f.enable = on;
    d3d->SetFog(f);
}
