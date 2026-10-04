/* RenderDevice::Native: the Direct3D 9 objects behind the RenderDevice, and
 * the shadow of everything the game has set on it.  For backend files only --
 * game code goes through renderdevice.h.
 *
 * A headless RenderDevice (renderdevice.h) has no Direct3D objects at all:
 * `device` stays NULL, and every RenderDevice method that would reach it
 * does nothing instead.  Everything else -- the mode list, the shadow state,
 * the texture sizes -- works as normal, so the game cannot tell. */

#pragma once
#include <windows.h>
#include <d3d9.h>
#include <vector>
#include "renderdevice.h"
#include "pixelconvert.h"

struct RenderDevice::Native {
    IDirect3D9           *d3d;
    IDirect3DDevice9     *device;       // NULL when headless, or before Create
    D3DPRESENT_PARAMETERS pp;
    UINT                  adapter;
    D3DFORMAT             displayFormat;
    D3DFORMAT             depthFormat;
    bool                  stencil;      // the depth format has stencil bits
    bool                  lost;         // Present said the device is lost

    // What the game has set, so GetRenderState and GetTransform need no
    // device, and a Reset (which puts every state back to its default) can
    // be undone.
    uint32_t              rs[256];      // by RS number
    bool                  rsSet[256];
    Mat4                  transform[3]; // by Transform; the game's own matrices
    bool                  transformSet[3];
    Material              material;
    bool                  materialSet;
    DirectionalLight      light;
    bool                  lightSet;
    const DeviceTexture  *bound[2];     // by stage
    int                   lighting;     // last D3DRS_LIGHTING written, -1 unknown

    // PresentImage's staging: a system-memory surface the image is converted
    // into, and the video-memory one StretchRect scales onto the back buffer.
    IDirect3DSurface9    *imageSys;
    IDirect3DSurface9    *imageGpu;
    int                   imageW, imageH;

    std::vector<uint8_t>  scratch;      // repacked vertices for DrawPrimitiveUP
};

/* The 32- and 16-bit display formats a mode can have, in the order the mode
 * list holds them (src/d3d/createdevice.cpp). */
D3DFORMAT d3d_display_format(unsigned bitDepth);

/* The pixel layout of a texture or display format: bit count and masks. */
PixelFormat d3d_pixel_format(D3DFORMAT f);

/* Hashes a converted surface into the file named by KAROO_TEXTURE_DUMP, if
 * there is one: one line of kind, name, size, layout and an FNV-1a of the
 * pixel bytes.  Diffing two runs' dumps shows whether a change moved a
 * pixel. */
void d3d_dump_pixels(const char *kind, const char *name, int width, int height,
                     const PixelFormat &pf, const uint8_t *bits, long pitch);
bool d3d_dump_enabled();

/* Backend internals shared between its files. */
void d3d_apply_viewport(RenderDevice::Native *n);
void d3d_restore_state(RenderDevice::Native *n);
void d3d_release_image_surfaces(RenderDevice::Native *n);

/* A DeviceTexture's Direct3D texture (NULL when headless or t is NULL), and
 * whether its format has an alpha channel. */
IDirect3DTexture9 *d3d_texture_object(const DeviceTexture *t);
bool               d3d_texture_has_alpha(const DeviceTexture *t);

/* KAROO_DRAW_TRACE (src/d3d/drawtrace.cpp): called by each draw, just before
 * it reaches the device. */
bool d3d_trace_enabled();
void d3d_trace_draw(RenderDevice::Native *n, int prim, unsigned long fvf,
                    const void *verts, uint32_t count, unsigned stride);
