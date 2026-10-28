/* The level's objects drawn from what they are (entitydraw.cpp): each pass of
 * the frame reads the World and draws every lift, platform, bridge, falling
 * tile, the player, bombs and foes as their state says they look.  The
 * entities themselves know nothing of drawing. */
#pragma once

#include "drawfwd.h"

class World;
class MovableEntity;

/* Draws every object in the level for one pass of the frame, then the bursts
 * they started.  deathsDrawn is World::deaths() as it was at the last frame
 * drawn: newer deaths fire their effects. */
void drawWorld(draw::Frame &f, draw::Pass pass, const World &world, unsigned deathsDrawn);

/* Where a mover draws and how far through its step at clock now: (u, height,
 * -v), turned to its facing and through any turn in progress, playing its
 * animation at the step's fraction (0 when still). */
draw::Instance pose(const MovableEntity &e, double now);
