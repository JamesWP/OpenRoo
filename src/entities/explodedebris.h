/* ExplodeDebris: the theme "explode" effect.  A model is shattered into its
 * triangles, which fly outward and thin out.  RenderGameFrame begins it when
 * an object explodes; RenderSceneObjects advances and draws it, and
 * DrawObjectShadows draws its shadow, the debris flattened like any model.
 *
 * The object is embedded in ThemeLevelObject. */

#pragma once

#include <windows.h>
#include "faktmesh.h"
 
class RenderDevice;

class ExplodeDebris {
public:
    ExplodeDebris();
    /* Releases the scratch buffers. */
    virtual ~ExplodeDebris();
    ExplodeDebris(const ExplodeDebris &) = delete;
    ExplodeDebris &operator=(const ExplodeDebris &) = delete;

    /* Frees the two scratch buffers.  See the .cpp for the field it does not
     * clear. */
    void release();

    /* The theme loader's "explode": size the buffers, and store the drop rate
     * (the keyword's third argument). */
    void allocateExplodeBuffers(struct CFaktMesh *mesh);

    void storeExplodeScaledCount(float scale);

    /* Copies one mesh frame and gives every triangle a velocity.  Returns 1, or
     * 0 if the frame, the vertex count or the buffer is wrong. */
    int begin(struct CFaktMesh *mesh, unsigned short frame,
              const float *origin);

    /* Advances by dt: moves the live vertices, then drops whole triangles. */
    void advance(float dt);

    /* Draws the live triangles twice (SRCALPHA, then DESTALPHA); 0x800401f0 if
     * inactive. */
    HRESULT draw(RenderDevice *dev);

    int vertexCount() const { return nVertexCount_; }
    DWORD active() const { return bActive_; }
    /* 4-aligned in practice; the class is packed only for its embedders. */
 
 
    float *samples() { return samples_; }
 
    DWORD cursor() const { return cursor_; }
    void  setCursor(DWORD c) { cursor_ = c; }
    float explodeScaledCount() const { return flExplodeScaledCount_; }

private:
    float *debrisVertex(int i);
    float *debrisVelocity(int tri);

    MeshVertex   *pVertexCopy_;           // nVertexCount * 0x28 (FVF 0x212)
    float       (*pFaceRecords_)[3];      // a velocity per triangle, (nVertexCount / 3) * 0xc
    int           nVertexCount_;          // as it was when the buffers were built
    DWORD         bActive_;               // set by begin; cleared by the ctor and release
    float         samples_[30];           // Gaussian speeds, mu 2.0, sigma 1.0; refilled whole
    DWORD         cursor_;                // reset by the same refill
    int           nLiveVertices_;         // begin sets it; advance drops it by threes
    float         flDropAccum_;           // fractional triangles owed
    float         flExplodeScaledCount_;  // the drop rate: nVertexCount * arg / 300
     
};