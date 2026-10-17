/* The vocabulary of the device's shattered meshes (renderdevice.h): a
 * ShardBuffer holds one frame of a mesh's triangles on the device, and
 * DrawShards draws them flying apart, as they are a given time after the
 * blast.
 *
 * Nothing is simulated or kept between draws: every shard's path is a
 * function of its triangle, the burst and the time, worked out per vertex on
 * the device.  The CPU's share is the time. */

#pragma once
#include <stdint.h>
#include "vectypes.h"

/* A shattered mesh on the device: opaque outside the backend.  Made by
 * CreateShardBuffer, freed by DestroyShardBuffer. */
struct ShardBuffer {
    virtual ~ShardBuffer() {}
};

/* One blast, `time` seconds after it, in the mesh's own coordinates.  Each
 * triangle is a shard which
 *   flies     straight out from `origin`, through its centroid, at
 *             gauss(speedMean, speedVariance) units a second (a variance that
 *             is not positive gives the mean; the cut-off is about 2.45
 *             standard deviations, as for particles);
 *   tumbles   about its centroid, round an axis of its own, at up to `spin`
 *             radians a second;
 *   vanishes  at a time of its own, uniform in [0, triangles / dropRate)
 *             seconds, so that dropRate triangles a second go on average;
 *             it shrinks to its centroid over the `shrink` seconds before
 *             (0: it simply goes).
 * Every draw is taken afresh from a hash of the triangle and `seed`: draws of
 * the same burst at different times are one continuous explosion. */
struct ShardBurst {
    Vec3     origin = { 0, 0, 0 };
    float    speedMean = 2.0f, speedVariance = 1.0f;
    float    spin = 0.0f;
    float    dropRate = 0.0f;  // triangles a second; 0: none vanish
    float    shrink = 0.0f;
    uint32_t seed = 0;
    float    time = 0.0f;      // seconds since the blast
};
