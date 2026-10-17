/* ExplodeDebris (explodedebris.h).
 *
 * The game moved every vertex on the CPU each frame and sent them all to the
 * device again; it had every shard fly straight, from a table of thirty
 * speeds, and dropped them from the end of the mesh.  Here nothing is moved:
 * the device works out each shard from the burst and its age.  Its shards
 * also tumble and shrink away, and go in an order of their own. */

#include "explodedebris.h"
#include "animatedmesh.h"
#include "renderdevice.h"

namespace {

const float kSpin = 6.0f;     // the fastest a shard turns, radians a second
const float kShrink = 0.15f;  // seconds a shard takes to shrink away
/* The themes' speeds scaled: at theirs the shards end about three times the
 * model's size, as they did in the game, which reads as far too big. */
const float kSpread = 0.8f;

}  // namespace

ExplodeDebris::~ExplodeDebris()
{
    release();
}

void ExplodeDebris::release()
{
    RenderDevice::DestroyShardBuffer(buffer_);
    buffer_ = nullptr;
    mesh_ = nullptr;
    vertexCount_ = 0;
    active_ = false;
}

void ExplodeDebris::setup(AnimatedMesh *mesh, float speedMean, float speedVariance,
                          float dropScale)
{
    release();
    mesh_ = mesh;
    vertexCount_ = (int)mesh->vertexCount();
    speedMean_ = speedMean * kSpread;
    speedVariance_ = speedVariance * kSpread * kSpread;
    dropRate_ = (float)vertexCount_ * dropScale / 300.0f;
}

void ExplodeDebris::begin(const float *origin)
{
    if (mesh_ == nullptr)
        return;
    for (int k = 0; k < 3; k++)
        origin_[k] = origin[k];
    blasts_++;
    age_ = 0.0f;
    active_ = true;
}

void ExplodeDebris::advance(float dt)
{
    if (!active_)
        return;
    age_ += dt;
    // The last shard's time is the triangles over the rate.
    if (dropRate_ > 0.0f && age_ * dropRate_ >= (float)(vertexCount_ / 3))
        active_ = false;
}

void ExplodeDebris::draw(RenderDevice *dev)
{
    if (!active_)
        return;
    if (buffer_ == nullptr)
        buffer_ = dev->CreateShardBuffer(VertexFormat::Normal2, mesh_->vertexData(),
                                         (uint32_t)vertexCount_);
    if (buffer_ == nullptr)
        return;

    ShardBurst burst;
    burst.origin = { origin_[0], origin_[1], origin_[2] };
    burst.speedMean = speedMean_;
    burst.speedVariance = speedVariance_;
    burst.spin = kSpin;
    burst.dropRate = dropRate_;
    burst.shrink = kShrink;
    burst.seed = blasts_ * 0x9E3779B9u;
    burst.time = age_;

    const BlendState saved = dev->blend();
    BlendState pass = saved;
    pass.src = BlendFactor::SrcAlpha;
    dev->SetBlend(pass);
    dev->DrawShards(buffer_, burst);
    pass.src = BlendFactor::DestAlpha;
    dev->SetBlend(pass);
    dev->DrawShards(buffer_, burst);
    dev->SetBlend(saved);
}
