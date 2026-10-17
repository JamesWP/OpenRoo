/* The vocabulary of the device's particle simulation (renderdevice.h): a
 * ParticleBuffer holds a fixed number of particle slots on the device, and
 * StepParticles advances every slot at once -- emitting into dead slots,
 * moving live ones through a force field, fading and retiring them -- with
 * nothing read back.  DrawParticles then draws the live ones as quads.
 *
 * Emission samples precomputed tables (ParticleTables) rather than a random
 * source: the emitter builds them once on the CPU, and a step names where in
 * them its first new particle reads; the k-th new particle reads k entries
 * further on (k * velStride for velocity), wrapping at each table's end. */

#pragma once
#include <stdint.h>
#include "vectypes.h"

/* A particle buffer on the device: opaque outside the backend.  Made by
 * CreateParticleBuffer, freed by DestroyParticleBuffer. */
struct ParticleBuffer {
    virtual ~ParticleBuffer() {}
};

/* What new particles sample.  Each table is `count` entries; pos and vel are
 * xyz triples, colour is 0xAARRGGBB. */
struct ParticleTables {
    const float    *pos = nullptr;    uint32_t posCount = 0;
    const float    *vel = nullptr;    uint32_t velCount = 0;
    const float    *life = nullptr;   uint32_t lifeCount = 0;  // seconds
    const uint32_t *colour = nullptr; uint32_t colourCount = 0;
};

/* One step's emission: `count` particles into the slots from `firstSlot`
 * (wrapping), each into its slot only if that slot is dead.  A new particle's
 * position is (table pos * posScale) * posMatrix + posOffset, its velocity
 * table vel + velOffset. */
struct ParticleEmit {
    uint32_t count = 0;
    uint32_t firstSlot = 0;
    uint32_t pos = 0, vel = 0, life = 0, colour = 0;  // table cursors of the first one
    uint32_t velStride = 1;
    float    posScale = 1.0f;
    Mat4     posMatrix = { { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 } };
    Vec3     posOffset = { 0, 0, 0 };
    Vec3     velOffset = { 0, 0, 0 };
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

/* How DrawParticles draws: every live particle as a quad of six corners
 * (two triangles) about its position, in the world transform, lit as the
 * vertices of VertexFormat::Lit are (not at all).  The corners come from
 * `shapes`, shapeCount entries of six xyz offsets; a particle uses entry 0, or
 * with perParticleShape the entry its slot was made with. */
struct ParticleLook {
    const float (*shapes)[6][3] = nullptr;
    uint32_t     shapeCount = 0;
    bool         perParticleShape = false;
    float        uv[6][2] = {};
};
