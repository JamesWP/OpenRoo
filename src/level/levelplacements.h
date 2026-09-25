/* levelplacements.h -- the per-level placement lists at 0x004e0070, built on
 * level entry and read ~100 times by RenderGameFrame (ENDGAME_PLAN.md E4,
 * the level-load chain).  The builders are ours: levelplacements.cpp.
 *
 *   0x00426c50  PrepareLevelAssetsOnEntry   calls the builder, once per level
 *   0x00404dd0  BuildLevelPlacementLists    free, count, allocate, fill;
 *                                           tail-calls the wall builder
 *   0x00406530  BuildLevelWallStrips        the vertical faces at height steps
 *   0x00407fc0  ReleaseLevelPlacementArrays     also WinMain's shutdown 0x42d62f
 *
 * The grid walk is the same in all three: rows v < LevelMap::extentV(),
 * columns u < extentU(), cells through LevelMap::tile() (tile.h's
 * TileKind for the kind byte).
 *
 * Every list is a count followed by one or two arrays from the GAME's
 * operator new, freed only by ReleaseLevelPlacementArrays.  "pos" entries are
 * (u, height, -v); "rot" entries are (0, yaw, 0).
 *
 * The object is a static; the next global (0x4e01a0, zeroed by the level
 * entry) starts 4 bytes after the last field here.
 */
#pragma once

#include <windows.h>
#include "layout.h"

/* A position/rotation pair list, one entry per cell of one kind. */
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

/* PLACEHOLDER, not a decoded type -- to be replaced once the readers in
 * RenderGameFrame say what each vertex array is.  0x20 bytes; what the
 * builders write suggests two formats share the size:
 *   the tile-top template: x,y,z, diffuse 0xffffffff, (u0,v0), (u1,v1)
 *   everything else:       BbVertex (d3dmath.h), FVF 0x1e2 --
 *                          x,y,z, 0, diffuse 0x00ffffff, specular 0, u, v */
struct PlacementVertex { DWORD d[8]; };

struct __attribute__((packed)) LevelPlacements {
    static const int ORIGIN = 0;

    PlacementVertex tileQuad[4];         /* +0x000  unit quad at y=0, +-0.5 */
    int             kind01Count;         /* +0x080  TILE_KIND_01 cells */
    PlacementVertex *kind01Verts;        /* +0x084  6 per cell (two tris) */
    float           exitPos[3];          /* +0x088  the TILE_EXIT cell (the last one wins) */
    float           exitRot[3];          /* +0x094  always 0 */
    PlacementList   lifts;               /* +0x0a0  TILE_LIFT; pos/rot all 0 */
    PlacementList   slides;              /* +0x0ac  count = Game slideCount,
                                                    NOT a cell count; rot zeroed */
    PlacementList   breakables;          /* +0x0b8  TILE_BREAKABLE */
    PlacementList   jumpPads;            /* +0x0c4  TILE_JUMP_PAD */
    PlacementList   teleporters;         /* +0x0d0  TILE_TELEPORTER */
    PlacementList   glue;                /* +0x0dc  TILE_GLUE */
    PlacementList   switches;            /* +0x0e8  TILE_SWITCH */
    PlacementList   ramps;               /* +0x0f4  TILE_RAMP_1..4, yaw by kind */
    PlacementList   climbs;              /* +0x100  TILE_CLIMB, yaw by climbDir (+0x1f2) */
    PlacementList   conveyors;           /* +0x10c  TILE_CONVEYOR */
    PlacementList   destructibles;       /* +0x118  TILE_DESTRUCTIBLE */
    int             wallStripCount;      /* +0x124  strips, 6 verts each */
    PlacementVertex *wallVerts;          /* +0x128  BuildLevelWallStrips */

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

static LevelPlacements *const GG_LEVEL_PLACEMENTS = (LevelPlacements *)0x004e0070;

class Game;
class ThemeAssetBlock;

/* 0x00407fc0 -- free every array and zero every count.  Callers: the
 * builder, and WinMain's shutdown (0x42d62f). */
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_Release(LevelPlacements *p);
/* 0x00404dd0 -- release, count, allocate, fill, then the wall strips
 * (0x00406530).  One caller: PrepareLevelAssetsOnEntry 0x426dc9. */
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_Build(LevelPlacements *p, const Game *g,
                      const ThemeAssetBlock *theme);
/* 0x00425680 -- the static initialiser (C++ init-table entry 7, via the
 * thunk 0x00425670's E9): default-constructs tileQuad[4].  No dtor. */
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_StaticInit(void);

/* 0x408870 / 0x408920 -- copy every live lift's / slide's position into
 * its list (v negated to z), then draw the list with RenderSceneObjects
 * (sceneobjects.cpp) on theme slot ELEVATOR / PLATFORM.  A slide along U
 * (kind 0x0a) gets a quarter-turn yaw; slides animate code 0x14 on
 * fmod(now * 0.002, 1).  __cdecl; RenderGameFrame is the only caller. */
class ThemeAssetBlock;
struct Direct3D;
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_DrawLifts(Game *g, LevelPlacements *p, ThemeAssetBlock *theme,
                          Direct3D *d3d, double now);
extern "C" __declspec(dllexport) void __cdecl
LevelPlacements_DrawSlides(Game *g, LevelPlacements *p, ThemeAssetBlock *theme,
                           Direct3D *d3d, double now);
