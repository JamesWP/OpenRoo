/* WrapperObject: the per-mesh texture-coordinate animator, embedded in
 * whatever owns a CFaktMesh.  It
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

/* One snapshotted UV pair: a MeshVertex's uv0, the only part of the vertex
 * kept. */
struct WrapperUV {
    float u, v;
};

class WrapperObject {
public:
     

    // Zeroes the three fields.
    WrapperObject();
    // Frees the snapshot.
    ~WrapperObject();
    WrapperObject(const WrapperObject &) = delete;
    WrapperObject &operator=(const WrapperObject &) = delete;

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
     

    WrapperUV *pBaseUV_;  // wFrameCount * dwVertexCount pairs
    CFaktMesh *pMesh_;    // not owned
    BYTE       dirty_;    // set by the three animating modes
};

