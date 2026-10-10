/* scenepasses.h -- the frame's three scene passes (scenepasses.cpp), called by
 * RenderGameFrame in order: opaque, effects and shadows, translucent. */
#pragma once

class World;
class CameraPose;
class Config;

/* The opaque placement lists, the bombable blocks, the player, the items, the
 * bombs and the foes.  Time is the frame's now (ms) and elapsed ms. */
void Scene_OpaquePasses(World *world, CameraPose *cam, Config *config, double now, double elapsed);

/* The scene objects, the bridges, the player's effect models and the stencil
 * shadows (when the video option is on).  dt is the frame's tick in ms. */
void Scene_EffectsAndShadows(World *world, CameraPose *cam, Config *config, double now, double dt);

/* The particle effects and the speed trail (when the particles option is on),
 * then the particle systems and the player's effect particles. */
void Scene_TranslucentPasses(World *world, CameraPose *cam, Config *config, double now, double elapsed,
                             double dt);
