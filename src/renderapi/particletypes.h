/* The vocabulary of the device's particle simulation (renderdevice.h): a
 * ParticleBuffer holds a fixed number of particle slots on the device, and
 * StepParticles advances every slot at once -- emitting into dead slots,
 * moving live ones through a force field, fading and retiring them -- with
 * nothing read back.  DrawParticles then draws the live ones as quads, at any
 * number of placements in one draw.
 *
 * Nothing about a new particle is decided on the CPU: a step carries the
 * distributions it is drawn from (ParticleEmit), and the device draws from
 * them with random numbers of its own. */

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

/* One step's emission: `count` particles into the slots from `firstSlot`
 * (wrapping), each into its slot only if that slot is dead.  Every value is
 * drawn afresh for each particle, on the device, from `seed`.
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
 *             there are none. */
struct ParticleEmit {
    uint32_t count = 0;
    uint32_t firstSlot = 0;
    uint32_t seed = 0;
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

/* What acts on live particles for one step.  Every live particle ages by the
 * step's dt and dies when its life goes below zero; its rgb then moves toward
 * fadeRGB by up to fadeStep (0..255 units). */
struct ParticleField {
    ParticleFieldKind kind = ParticleFieldKind::None;
    Vec3     accel = { 0, 0, 0 };                     // Gravity, per second^2
    bool     clip[3] = { false, false, false };       // Gravity: kill outside [clipMin, clipMax]
    Vec3     clipMin = { 0, 0, 0 }, clipMax = { 0, 0, 0 };
    Vec3     centre = { 0, 0, 0 };                    // Magnet
    Vec3     force = { 0, 0, 0 };                     // Magnet, per axis
    Vec3     halfExtent = { 0, 0, 0 };                // Magnet: arrival box
    uint32_t fadeRGB = 0;                             // 0x00RRGGBB
    float    fadeStep = 0.0f;
};

struct ParticleStep {
    float         dt = 0.0f;  // seconds
    ParticleEmit  emit;
    ParticleField field;
};

/* A tumbling particle's shape: a quad's six corners, which turn about
 * `axis` (unit length) at `rate` radians a second. */
struct ParticleShape {
    float corners[6][3] = {};
    Vec3  axis = { 0, 1, 0 };
    float rate = 0.0f;
};

/* One placement of a buffer's particles: they are drawn through `world`, and
 * each quad's corners through `corners` first (row vectors, as Mat4 is). */
struct ParticleInstance {
    Mat4 world;
    Mat4 corners;
};

/* How DrawParticles draws: every live particle as a quad of six corners (two
 * triangles) about its position, lit as the vertices of VertexFormat::Lit are
 * (not at all), with the corners' texture coordinates `uv`.
 *
 * A billboard's quad is `size` across each way, square to `facing`.  A
 * tumbling particle's is the ParticleShape its slot was made with
 * (SetParticleShapes), turned for `time` seconds; it is two-sided. */
struct ParticleLook {
    bool  tumbling = false;
    Vec3  facing = { 0, 0, 1 };
    float size = 1.0f;
    float time = 0.0f;
    float uv[6][2] = {};
};
