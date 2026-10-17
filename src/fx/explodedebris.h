/* ExplodeDebris: the theme "explode" effect.  A model is shattered into its
 * triangles, which fly outward, tumbling, and thin out.  draw::explode begins
 * it when an object explodes; RenderSceneObjects advances and draws it, and
 * DrawObjectShadows draws its shadow, the debris flattened like any model.
 *
 * It all happens on the device (shardtypes.h): the mesh's first frame is
 * copied there once, and each draw hands it only the burst and its age.
 *
 * The object is embedded in ThemeLevelObject. */

#pragma once

#include <stdint.h>

class RenderDevice;
class AnimatedMesh;
struct ShardBuffer;

class ExplodeDebris {
public:
    ExplodeDebris() = default;
    ~ExplodeDebris();
    ExplodeDebris(const ExplodeDebris &) = delete;
    ExplodeDebris &operator=(const ExplodeDebris &) = delete;

    /* Forgets the mesh and frees the device's copy. */
    void release();

    /* The theme loader's "explode": `mesh` shatters, its shards flying at
     * gauss(speedMean, speedVariance) units a second, and vertexCount *
     * dropScale / 300 of them going each second. */
    void setup(AnimatedMesh *mesh, float speedMean, float speedVariance, float dropScale);

    /* A new blast, away from `origin` (model coordinates).  Nothing without
     * a mesh. */
    void begin(const float *origin);

    /* Ages the blast by dt seconds. */
    void advance(float dt);

    /* Draws the blast's shards twice (SRCALPHA, then DESTALPHA), if it has
     * any left. */
    void draw(RenderDevice *dev);

    int vertexCount() const { return vertexCount_; }
    float dropRate() const { return dropRate_; }

private:
    AnimatedMesh *mesh_ = nullptr;
    int           vertexCount_ = 0;
    ShardBuffer  *buffer_ = nullptr;  // the mesh's first frame, made by the first draw
    float         speedMean_ = 0.0f, speedVariance_ = 0.0f;
    float         dropRate_ = 0.0f;   // triangles a second
    float         origin_[3] = {};    // this blast's
    float         age_ = 0.0f;        // seconds since it
    bool          active_ = false;    // begun, and not every shard gone yet
    uint32_t      blasts_ = 0;        // blasts begun: seeds each one's shards
};
