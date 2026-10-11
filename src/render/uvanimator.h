/* UVAnimator: the per-mesh texture-coordinate animator, embedded in whatever
 * owns an AnimatedMesh.  It snapshots the mesh's first texture-coordinate set
 * when the mesh is attached.  The scroll and the sine warp are a 2D transform
 * of the texture coordinates (UVTransform), which the mesh hands the device
 * when it draws; the environment map is computed from the vertex normals and
 * writes the mesh's vertices.  Stopping the animation restores both.
 *
 * RenderSceneObjects switches on the scene object's animation mode and calls
 * one method per object per frame: mode 0 flush, 4 sine wave, 5 environment
 * map, 6 scroll.  The three animating modes leave the dirty flag set, so that
 * a later mode 0 restores the mesh. */

#pragma once

#include <stdint.h>
 
#include "animatedmesh.h"
class RenderDevice;

/* One snapshotted UV pair: a MeshVertex's uv0, the only part of the vertex
 * kept. */
struct AnimatedUV {
    float u, v;
};

class UVAnimator {
public:
     

    // Zeroes the three fields.
    UVAnimator();
    // Frees the snapshot.
    ~UVAnimator();
    UVAnimator(const UVAnimator &) = delete;
    UVAnimator &operator=(const UVAnimator &) = delete;

    // Attaches mesh and snapshots every frame's UVs.  A NULL mesh is ignored
    // entirely, including the free of the old snapshot.
    void setMesh(AnimatedMesh *mesh);

    // Frees the snapshot and nulls it, leaving the mesh and the dirty flag:
    // the owner calls this from its own destructor and never touches the
    // object again.
    void releaseSnapshot();

    // Undoes the animation: restores the mesh's UVs from the snapshot if the
    // environment map wrote them, clears the UV transform and the dirty flag;
    // does nothing without a mesh or when not dirty.
    void flush();

    // The sine-wave warp, as a UV transform on the mesh.  ticks is a millisecond count the caller has already
    // truncated.
    void applySineWave(unsigned int ticks, float rate, float amplitude,
                       float skew);

    // Scrolls one texture axis: u when axisU is nonzero, else v.  The offset
    // accumulates, every call, until flush.
    void scrollUVs(unsigned int ticks, int axisU, float speed);

    // Spherical environment map: UVs from each vertex normal, transformed by
    // world * view.
    void updateObjectTransform(RenderDevice *dev, unsigned short frame);

    AnimatedMesh *mesh() const { return pMesh_; }

private:
     

    AnimatedUV *pBaseUV_;  // wFrameCount * dwVertexCount pairs
    AnimatedMesh *pMesh_;    // not owned
    uint8_t       dirty_;    // set by the three animating modes
    uint8_t       vertsDirty_ = 0;  // the environment map has written the mesh's UVs

    void restoreVertices();
};

