/* The particle gallery: a test scene, not part of the game.  With
 * KAROO_PARTICLE_GALLERY set, the game runs no level; instead it shows one
 * theme at a time with every look that has a particle system in a row, each
 * on a floor tile with its models, drawn through the same theme draws a level
 * uses.  Bursts and dying effects fire again every few seconds, and the
 * speed trail runs in a circle.
 *
 *   KAROO_PARTICLE_GALLERY=1        start at the first theme
 *   KAROO_PARTICLE_GALLERY=Space    start at that theme
 *
 * Left/Right change theme, Up/Down zoom, Space pauses the orbit, Escape
 * quits. */
#pragma once

struct Renderer;

/* Whether the gallery was asked for. */
bool ParticleGallery_Requested();

/* Runs the gallery until it is quit; the exit code. */
int ParticleGallery_Run(Renderer *renderer);
