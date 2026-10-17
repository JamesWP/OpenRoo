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
 * or from the debug UI's "Particle gallery" section, over a running game.
 * Left/Right change theme, Up/Down zoom, Space pauses the orbit, Escape
 * goes back to the game.  The level's own theme stays loaded: the gallery
 * has its own. */
#pragma once

struct Renderer;

/* Opens the gallery if KAROO_PARTICLE_GALLERY asks for it; Escape then quits
 * the game rather than going back to it.  False if not asked for (or
 * headless). */
bool ParticleGallery_StartFromEnv(Renderer *renderer);

/* Inside the debug UI's frame, before the game's: the gallery's section of
 * the debug UI, whose button opens it over the game, and then, while it is
 * open, its frame in place of the game's.  False when the game should run
 * its frame. */
bool ParticleGallery_Frame(Renderer *renderer);

/* Before the device goes. */
void ParticleGallery_Shutdown();
