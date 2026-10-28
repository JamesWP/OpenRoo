/* What the drawing needs that outlives the tick it happened in, kept in the
 * World's state: the one-shot effects the rules have started, and the
 * player's recent path for what trails behind it.  World::recordEffects adds
 * to both each tick and drops what can no longer be seen. */
#pragma once

#include <deque>
#include <vector>

/* A one-shot effect: a burst that plays at a point for EFFECT_MS. */
enum class EffectKind : unsigned char {
    CrystalPickup,  // a crystal picked up, by the player or a foe
    ItemPickup,     // any other pickup, by the player
    FallingDust,    // a falling tile falling
};

const double EFFECT_MS = 1000.0;

struct EffectStart {
    EffectKind kind;
    float      u, y, v;   // where, in the entities' (u, height, v)
    double     startMs;   // the World's clock when it started
    unsigned   seed;      // tells it from every other
};

/* Where the player was: one sample every PATH_STEP_MS, for the last
 * PATH_MS. */
const double PATH_STEP_MS = 1000.0 / 60.0;
const double PATH_MS = 2000.0;

struct PathSample {
    double        timeMs;
    float         u, y, v;
    unsigned char facing;
    bool          trail;  // sped up and moving along its facing: the speed trail emits
};
