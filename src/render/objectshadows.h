/* objectshadows.h -- the planar projected
 * shadows of one theme object type.  See objectshadows.cpp. */
#pragma once

#include <windows.h>

class Game;
class LevelPlacements;
class ThemeObjectTypeSlot;
class RenderDevice;

/*  , twelve dwords (the double is two).  `placements`
 * is never read.  `phase` feeds the animation frame when the record moves
 * with its owner; `debrisMs` scales the explode debris' advance, and every
 * call site passes 0. */
  void  
Shadows_DrawObjectShadows(Game *game, LevelPlacements *placements,
                          const float *pos, const float *rot, unsigned int count,
                          ThemeObjectTypeSlot *slot, RenderDevice *d3d, double t,
                          float phase, unsigned int animKey, unsigned int debrisMs);
