/* themedraw.{h,cpp} -- the theme's
 * particle-system effects, drawn at a list of instance positions.
 *
 *   cdecl(Game*, unused, pos[], rot[], count, ThemeObjectTypeSlot*,
 *         RenderDevice*, double t, double dt, DWORD system)
 *
 * 15 call sites, all in RenderGameFrame; each passes one theme slot (the
 * player's, a foe's, an item type's, ...), its instances' positions and
 * rotations as float triples, and which of the record's 16 particle
 * systems to use.
 */
#pragma once

#include <windows.h>

class Game;
class ThemeObjectTypeSlot;
class RenderDevice;

extern "C" __declspec(dllexport) void __cdecl
Theme_DrawParticleObjects(Game *g, void *unused, const float (*pos)[3],
                          const float (*rot)[3], DWORD count,
                          ThemeObjectTypeSlot *slot, RenderDevice *d3d,
                          double t, double dt, DWORD system);
