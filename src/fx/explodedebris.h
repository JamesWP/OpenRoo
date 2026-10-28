/* ExplodeDebris: the theme "explode" effect.  A model is shattered into its
 * triangles, which fly outward, tumbling, and thin out.  draw::explode begins
 * draws it, by the blast's seed and age, in place of a dying look's model, and
 * its shadow, the debris flattened like any model.
 *
 * It all happens on the device (shardtypes.h): the mesh's first frame is
 * copied there once, and each draw hands it only the blast and its age.
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

    /* Draws blast `seed`, `age` seconds after it, flying away from `origin`
     * (model coordinates): its shards twice (SRCALPHA, then DESTALPHA), if it
     * has any left.  Nothing without a mesh. */
    void draw(RenderDevice *dev, const float *origin, uint32_t seed, float age);

    int vertexCount() const { return vertexCount_; }
    float dropRate() const { return dropRate_; }

private:
    AnimatedMesh *mesh_ = nullptr;
    int           vertexCount_ = 0;
    ShardBuffer  *buffer_ = nullptr;  // the mesh's first frame, made by the first draw
    float         speedMean_ = 0.0f, speedVariance_ = 0.0f;
    float         dropRate_ = 0.0f;   // triangles a second
};
