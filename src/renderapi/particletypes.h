/* The vocabulary of the device's particles (renderdevice.h).  Nothing is
 * simulated or kept between draws: a draw names which particles have been
 * emitted and how long ago, and the device works out each one -- where it
 * started, how it has moved, faded and turned, and whether it is still alive
 * -- from the distributions it was emitted with, the field it moves through
 * and its age, with random numbers taken afresh from a hash of its number
 * and the placement's seed.  The same draw at the same time always gives the
 * same particles.
 *
 * A ParticleBuffer holds what does not change between draws: how many
 * particles a draw shows at most, and the shapes they tumble with. */

#pragma once
#include <stdint.h>
#include "vectypes.h"

/* A particle buffer on the device: opaque outside the backend.  Made by
 * CreateParticleBuffer, freed by DestroyParticleBuffer. */
struct ParticleBuffer {
    virtual ~ParticleBuffer() {}
};

/* Where new particles start: per axis from a normal distribution, or
 * uniform in a box, or on the unit circle in the XZ plane. */
enum class ParticleSpawn { Gauss, Box, Circle };

/* Up to this many colours to choose new particles' from. */
constexpr unsigned PARTICLE_COLOURS = 8;

/* Where a moving emitter was: samples newest first, each `age` seconds
 * before the draw, with the way it faced and whether it was emitting then.  A
 * particle starts from the emitter as it was when it was emitted. */
constexpr unsigned PARTICLE_PATH = 64;

struct ParticlePath {
    uint32_t count = 0;
    float    age[PARTICLE_PATH] = {};
    Vec3     pos[PARTICLE_PATH] = {};
    Vec3     dir[PARTICLE_PATH] = {};  // unit; replaces velOffset's direction
    bool     on[PARTICLE_PATH] = {};
};

/* The particles a draw shows and how they were emitted.  They are numbered
 * from 0 in the order they were emitted, `interval` seconds apart; the
 * newest, number `newest`, `newestAge` seconds ago.  The draw shows that one
 * and those before it, up to the buffer's count, while they live.
 *
 * A "gauss(mean, variance)" draw is normal about mean with that variance, cut
 * off at about 2.45 standard deviations; a variance that is not positive
 * gives the mean.  The new particle's
 *   position  the spawn's draw * posScale, through posMatrix, + posOffset
 *             (Gauss: gauss(posA, posB) per axis; Box: uniform between posA
 *             and posB per axis; Circle: a uniform angle round the circle);
 *   velocity  gauss(dirMean, dirVariance) per axis, made unit length, times
 *             gauss(speedMean, speedVariance), + velOffset;
 *   life      gauss(lifeMean, lifeVariance) seconds;
 *   colour    one of `colours`, picked in proportion to its weight; white if
 *             there are none.
 * With a path, the position adds where the emitter was when the particle was
 * emitted, velOffset points the way it faced then, and a particle emitted
 * while the emitter was off is not drawn. */
struct ParticleEmit {
    int32_t  newest = -1;      // none emitted yet
    float    newestAge = 0.0f;
    float    interval = 1.0f;
    const ParticlePath *path = nullptr;  // where a moving emitter was; null: it stands still
    ParticleSpawn spawn = ParticleSpawn::Gauss;
    Vec3     posA = { 0, 0, 0 }, posB = { 0, 0, 0 };
    float    posScale = 1.0f;
    Mat4     posMatrix = { { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 } };
    Vec3     posOffset = { 0, 0, 0 };
    Vec3     dirMean = { 0, 0, 0 }, dirVariance = { 0, 0, 0 };
    float    speedMean = 0.0f, speedVariance = 0.0f;
    Vec3     velOffset = { 0, 0, 0 };
    float    lifeMean = 0.0f, lifeVariance = 0.0f;
    uint32_t colourCount = 0;
    uint32_t colours[PARTICLE_COLOURS] = {};  // 0xAARRGGBB
    float    weights[PARTICLE_COLOURS] = {};
};

enum class ParticleFieldKind {
    None,     // nothing moves, ages or dies
    Gravity,  // a constant acceleration, and per-axis kill planes
    Magnet,   // a pull toward a centre, which kills what reaches it
};

/* What acts on particles once emitted.  A particle dies once its age passes
 * its life, except in no field, where nothing moves or ages; its rgb moves
 * toward fadeRGB at fadeRate units (0..255) a second. */
struct ParticleField {
    ParticleFieldKind kind = ParticleFieldKind::None;
    Vec3     accel = { 0, 0, 0 };                     // Gravity, per second^2
    bool     clip[3] = { false, false, false };       // Gravity: kill outside [clipMin, clipMax]
    Vec3     clipMin = { 0, 0, 0 }, clipMax = { 0, 0, 0 };
    Vec3     centre = { 0, 0, 0 };                    // Magnet
    Vec3     force = { 0, 0, 0 };                     // Magnet, per axis
    Vec3     halfExtent = { 0, 0, 0 };                // Magnet: arrival box
    uint32_t fadeRGB = 0;                             // 0x00RRGGBB
    float    fadeRate = 0.0f;
};

/* A tumbling particle's shape: a quad's six corners, which turn about
 * `axis` (unit length) at `rate` radians a second of the particle's age. */
struct ParticleShape {
    float corners[6][3] = {};
    Vec3  axis = { 0, 1, 0 };
    float rate = 0.0f;
};

/* One placement of a buffer's particles: they are drawn through `world`, and
 * each quad's corners through `corners` first (row vectors, as Mat4 is).
 * Its particles are drawn from `seed`: placements with different seeds show
 * different particles. */
struct ParticleInstance {
    Mat4     world;
    Mat4     corners;
    uint32_t seed = 0;
    uint32_t pad[3] = {};
};

/* How DrawParticles draws: every live particle as a quad of six corners (two
 * triangles) about its position, lit as the vertices of VertexFormat::Lit are
 * (not at all), with the corners' texture coordinates `uv`.
 *
 * A billboard's quad is `size` across each way, square to `facing`.  A
 * tumbling particle's is one of the buffer's ParticleShapes
 * (SetParticleShapes), picked by its hash and turned for its age; it is
 * two-sided. */
struct ParticleLook {
    bool  tumbling = false;
    Vec3  facing = { 0, 0, 1 };
    float size = 1.0f;
    float uv[6][2] = {};
};
