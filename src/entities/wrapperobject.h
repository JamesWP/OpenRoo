/* WrapperObject: the per-mesh texture-coordinate animator, 13 bytes, embedded
 * in whatever owns a CFaktMesh at +8 (the next member is at +0x15).  It
 * snapshots the mesh's first texture-coordinate set when the mesh is attached,
 * writes animated UVs straight into the mesh's vertices, and restores them
 * from the snapshot when the animation stops.  The name says where it sits
 * rather than what it does.
 *
 * RenderSceneObjects switches on the scene object's animation mode and calls
 * one method per object per frame: mode 0 flush, 4 sine wave, 5 environment
 * map, 6 scroll.  The three animating modes leave the dirty flag set, so that
 * a later mode 0 restores the mesh. */

#pragma once

#include <windows.h>
#include "faktmesh.h"
class RenderDevice;

/* One snapshotted UV pair: the 8 bytes at +0x18 of an FVF 0x212 vertex, the
 * only part of the vertex kept. */
struct WrapperUV {
    float u, v;
};
static_assert(sizeof(WrapperUV) == 8, "WrapperUV stride mismatch");

class __attribute__((packed)) WrapperObject {
public:

    // Installs the one-slot vtable and zeroes the three fields.
    void construct();

    // Re-installs the vtable and frees the snapshot.
    void dtorBody();

    // Attaches mesh and snapshots every frame's UVs.  A NULL mesh is ignored
    // entirely, including the free of the old snapshot.
    void setMesh(CFaktMesh *mesh);

    // Frees the snapshot and nulls it, leaving the mesh and the dirty flag:
    // the owner calls this from its own destructor and never touches the
    // object again.
    void releaseSnapshot();

    // Restores the mesh's UVs from the snapshot and clears the dirty flag;
    // does nothing without a mesh or when not dirty.
    void flush();

    // The sine-wave warp.  ticks is a millisecond count the caller has already
    // truncated.
    void applySineWave(unsigned int ticks, float rate, float amplitude,
                       float skew);

    // Scrolls one texture axis: u when axisU is nonzero, else v.
    void scrollUVs(unsigned int ticks, int axisU, float speed);

    // Spherical environment map: UVs from each vertex normal, transformed by
    // world * view.
    void updateObjectTransform(RenderDevice *dev, unsigned short frame);

    CFaktMesh *mesh() const { return pMesh_; }

private:

    void      *vtable_;   // +0x00  the one-slot vtable
    WrapperUV *pBaseUV_;  // +0x04  wFrameCount * dwVertexCount pairs
    CFaktMesh *pMesh_;    // +0x08  not owned
    BYTE       dirty_;    // +0x0c  set by the three animating modes
};

/* The vtable, installed by the ctor and the dtor body. */
void *Wrapper_Vtable(void);

WrapperObject *Wrapper_Construct(WrapperObject *self);

/* Vtable slot 0.  Returns self; bit 0 of flags frees. */
void *Wrapper_ScalarDtor(WrapperObject *self, unsigned int flags);

void Wrapper_DtorBody(WrapperObject *self);

void Wrapper_SetMesh(WrapperObject *self, CFaktMesh *mesh);

void Wrapper_ReleaseSnapshot(WrapperObject *self);

void Wrapper_Flush(WrapperObject *self);

void Wrapper_ApplySineWave(WrapperObject *self, unsigned int ticks, float rate,
                           float amplitude, float skew);

void Wrapper_ScrollUVs(WrapperObject *self, unsigned int ticks, int axisU,
                       float speed);

void Wrapper_UpdateObjectTransform(WrapperObject *self, RenderDevice *dev,
                                   unsigned short frame);

