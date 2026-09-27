/* The level's placement lists: for each kind of cell the renderer draws
 * (lifts, slides, breakables, ramps...), where each one stands and how it is
 * turned, plus the tile-top template and the wall strips at height steps.
 * Built on level entry, read by the frame renderer every frame.  Every list is
 * a count and one or two heap arrays, freed only by LevelPlacements_Release.
 * Positions are (u, height, -v); rotations are (0, yaw, 0). */
#pragma once

#include <windows.h>
#include "layout.h"

/* A position and rotation list, one entry per cell of one kind. */
struct __attribute__((packed)) PlacementList {
    static const int ORIGIN = 0;
    int     count;
    float (*pos)[3];
    float (*rot)[3];
    KAROO_LAYOUT_REGISTER(PlacementList);
};

KAROO_LAYOUT_CHECKS(PlacementList)
{
    KAROO_LAYOUT_AT(pos, 0x04);
    KAROO_LAYOUT_AT(rot, 0x08);
    KAROO_LAYOUT_SIZE(0x0c);
}

/* Not a decoded type: 0x20-byte vertices in two formats that share the size.
 * The tile-top template is x, y, z, diffuse 0xffffffff, (u0, v0), (u1, v1);
 * everything else is a BbVertex (d3dmath.h): x, y, z, 0, diffuse 0x00ffffff,
 * specular 0, u, v. */
struct PlacementVertex { DWORD d[8]; };

struct __attribute__((packed)) LevelPlacements {
    static const int ORIGIN = 0;

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

    KAROO_LAYOUT_REGISTER(LevelPlacements);
};

KAROO_LAYOUT_CHECKS(LevelPlacements)
{
    KAROO_LAYOUT_AT(kind01Count,    0x080);
    KAROO_LAYOUT_AT(kind01Verts,    0x084);
    KAROO_LAYOUT_AT(exitPos,        0x088);
    KAROO_LAYOUT_AT(exitRot,        0x094);
    KAROO_LAYOUT_AT(lifts,          0x0a0);
    KAROO_LAYOUT_AT(slides,         0x0ac);
    KAROO_LAYOUT_AT(breakables,     0x0b8);
    KAROO_LAYOUT_AT(jumpPads,       0x0c4);
    KAROO_LAYOUT_AT(teleporters,    0x0d0);
    KAROO_LAYOUT_AT(glue,           0x0dc);
    KAROO_LAYOUT_AT(switches,       0x0e8);
    KAROO_LAYOUT_AT(ramps,          0x0f4);
    KAROO_LAYOUT_AT(climbs,         0x100);
    KAROO_LAYOUT_AT(conveyors,      0x10c);
    KAROO_LAYOUT_AT(destructibles,  0x118);
    KAROO_LAYOUT_AT(wallStripCount, 0x124);
    KAROO_LAYOUT_AT(wallVerts,      0x128);
    KAROO_LAYOUT_SIZE(0x12c);
}

extern LevelPlacements g_levelPlacements;

class Game;
class ThemeAssetBlock;

/* Frees every array and zeroes every count.  Also called at shutdown. */
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_Release(LevelPlacements *p);

/* Releases, counts, allocates and fills the lists, then the wall strips.
 * Called on level entry. */
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_Build(LevelPlacements *p, const Game *g,
                      const ThemeAssetBlock *theme);

/* Default-constructs the tile-top template, before WinMain. */
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_StaticInit(void);

/* Copies every live lift's (or slide's) position into its list, v negated to
 * z, then draws the list with the theme's ELEVATOR (PLATFORM) records.  A
 * slide along u gets a quarter-turn yaw; slides animate with fmod(now * 0.002,
 * 1). */
class ThemeAssetBlock;
struct RenderDevice;
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_DrawLifts(Game *g, LevelPlacements *p, ThemeAssetBlock *theme,
                          RenderDevice *d3d, double now);
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_DrawSlides(Game *g, LevelPlacements *p, ThemeAssetBlock *theme,
                           RenderDevice *d3d, double now);
