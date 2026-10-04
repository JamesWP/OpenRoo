/* ExplodeDebris: the theme "explode" effect.  A model is shattered into its
 * triangles, which fly outward and thin out.  RenderGameFrame begins it when
 * an object explodes; RenderSceneObjects advances and draws it, and
 * DrawObjectShadows draws its shadow, the debris flattened like any model.
 *
 * The object is embedded in ThemeLevelObject. */

#pragma once

#include <stdint.h>
#include <array>
#include <vector>
#include "animatedmesh.h"
 
class RenderDevice;
struct VertexBuffer;

class ExplodeDebris {
public:
    ExplodeDebris();
    /* Releases the scratch buffers. */
    ~ExplodeDebris();
    ExplodeDebris(const ExplodeDebris &) = delete;
    ExplodeDebris &operator=(const ExplodeDebris &) = delete;

    /* Frees the two scratch buffers. */
    void release();

    /* The theme loader's "explode": size the buffers, and store the drop rate
     * (the keyword's third argument). */
    void allocateExplodeBuffers(struct AnimatedMesh *mesh);

    void storeExplodeScaledCount(float scale);

    /* Copies one mesh frame and gives every triangle a velocity.  Returns 1, or
     * 0 if the frame, the vertex count or the buffer is wrong. */
    int begin(struct AnimatedMesh *mesh, unsigned short frame,
              const float *origin);

    /* Advances by dt: moves the live vertices, then drops whole triangles. */
    void advance(float dt);

    /* Draws the live triangles twice (SRCALPHA, then DESTALPHA); 0x800401f0 if
     * inactive. */
    long draw(RenderDevice *dev);

    int vertexCount() const { return nVertexCount_; }
    uint32_t active() const { return bActive_; }
 
 
    float *samples() { return samples_; }
 
    uint32_t cursor() const { return cursor_; }
    void  setCursor(uint32_t c) { cursor_ = c; }
    float explodeScaledCount() const { return flExplodeScaledCount_; }

private:
    float *debrisVertex(int i);
    float *debrisVelocity(int tri);

    VertexBuffer *vb_ = nullptr;   // vertexCopy_ on the device, made by the first draw
    std::vector<MeshVertex>              vertexCopy_;   // nVertexCount of them (FVF 0x212)
    std::vector<std::array<float, 3>>    faceRecords_;  // a velocity per triangle, nVertexCount / 3
    int           nVertexCount_;          // as it was when the buffers were built
    uint32_t         bActive_;               // set by begin; cleared by the ctor and release
    float         samples_[30];           // Gaussian speeds, mu 2.0, sigma 1.0; refilled whole
    uint32_t         cursor_;                // reset by the same refill
    int           nLiveVertices_;         // begin sets it; advance drops it by threes
    float         flDropAccum_;           // fractional triangles owed
    float         flExplodeScaledCount_;  // the drop rate: nVertexCount * arg / 300
     
};