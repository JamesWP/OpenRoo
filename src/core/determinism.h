#pragma once
#include <windows.h>

struct ParticleSystem;

/* Stage A2 determinism proof — REPLAY_PLAN.md.
 *
 * Accumulates a per-frame FNV-1a hash over live simulation state and writes one
 * line per frame to KAROO_HASH_LOG.  Two runs with identical input must produce
 * byte-identical files; if they do not, a clock source is still leaking real
 * time into the simulation. */
bool dethash_enabled(void);

/* Fold one particle system's live ring into the current frame's hash. */
void dethash_particles(ParticleSystem *ps);

/* Close the frame: fold in the Game fields, emit a line, reset. */
void dethash_frame_end(double virtual_seconds);
