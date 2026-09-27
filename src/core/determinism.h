/* The determinism hash (determinism.cpp).  With KAROO_HASH_LOG set, each frame
 * folds the live simulation state into an FNV-1a hash and writes one line to
 * that file.  Two runs given the same input must write identical files; where
 * they differ, real time is leaking into the simulation. */

#pragma once
#include <windows.h>

struct ParticleSystem;

/* Whether KAROO_HASH_LOG is set. */
bool dethash_enabled(void);

/* Folds one particle system's live ring into this frame's hash. */
void dethash_particles(ParticleSystem *ps);

/* Folds in the Game fields, writes the frame's line and starts the next hash.
 */
void dethash_frame_end(double virtual_seconds);
