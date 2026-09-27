/* The level's placement lists: for each kind of cell the renderer draws
 * (lifts, slides, breakables, ramps...), where each one stands and how it is
 * turned, plus the tile-top template and the wall strips at height steps.
 * Built on level entry, read by the frame renderer every frame.  Every list is
 * a count and one or two heap arrays, freed only by LevelPlacements_Release.
 * Positions are (u, height, -v); rotations are (0, yaw, 0). */
#pragma once

#include <windows.h>

/* A position and rotation list, one entry per cell of one kind. */
struct PlacementList {
    int     count;
    float (*pos)[3];
    float (*rot)[3];
};

/* Not a decoded type: 0x20-byte vertices in two formats that share the size.
 * The tile-top template is x, y, z, diffuse 0xffffffff, (u0, v0), (u1, v1);
 * everything else is a BbVertex (d3dmath.h): x, y, z, 0, diffuse 0x00ffffff,
 * specular 0, u, v. */
struct PlacementVertex { DWORD d[8]; };

struct LevelPlacements {

    PlacementVertex tileQuad[4];     // unit quad at y 0, +-0.5
    int             kind01Count;     // TILE_KIND_01 cells
    PlacementVertex *kind01Verts;    // 6 per cell, two triangles
    float           exitPos[3];      // the TILE_EXIT cell; the last one wins
    float           exitRot[3];      // always 0
    PlacementList   lifts;           // TILE_LIFT; pos and rot all 0
    PlacementList   slides;  // count is the Game's slide count, not a cell count; rot zeroed
    PlacementList   breakables;      // TILE_BREAKABLE
    PlacementList   jumpPads;        // TILE_JUMP_PAD
    PlacementList   teleporters;     // TILE_TELEPORTER
    PlacementList   glue;            // TILE_GLUE
    PlacementList   switches;        // TILE_SWITCH
    PlacementList   ramps;           // TILE_RAMP_1..4, yaw by kind
    PlacementList   climbs;          // TILE_CLIMB, yaw by climb direction
    PlacementList   conveyors;       // TILE_CONVEYOR
    PlacementList   destructibles;   // TILE_DESTRUCTIBLE
    int             wallStripCount;  // strips, 6 vertices each
    PlacementVertex *wallVerts;      // the wall strips

};

extern LevelPlacements g_levelPlacements;

class Game;
class ThemeAssetBlock;

/* Frees every array and zeroes every count.  Also called at shutdown. */
void LevelPlacements_Release(LevelPlacements *p);

/* Releases, counts, allocates and fills the lists, then the wall strips.
 * Called on level entry. */
void LevelPlacements_Build(LevelPlacements *p, const Game *g,
                           const ThemeAssetBlock *theme);

/* Default-constructs the tile-top template, before WinMain. */
void LevelPlacements_StaticInit(void);

/* Copies every live lift's (or slide's) position into its list, v negated to
 * z, then draws the list with the theme's ELEVATOR (PLATFORM) records.  A
 * slide along u gets a quarter-turn yaw; slides animate with fmod(now * 0.002,
 * 1). */
class ThemeAssetBlock;
class RenderDevice;
void
LevelPlacements_DrawLifts(Game *g, LevelPlacements *p, ThemeAssetBlock *theme,
                          RenderDevice *d3d, double now);
void
LevelPlacements_DrawSlides(Game *g, LevelPlacements *p, ThemeAssetBlock *theme,
                           RenderDevice *d3d, double now);
