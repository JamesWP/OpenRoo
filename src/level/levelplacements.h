/* The level's placement lists: for each kind of cell the renderer draws
 * (lifts, slides, breakables, ramps...), where each one stands and how it is
 * turned, plus the tile-top template and the wall strips at height steps.
 * Built on level entry, read by the frame renderer every frame.  Every list is
 * a count and one or two heap arrays, freed only by LevelPlacements::release.
 * Positions are (u, height, -v); rotations are (0, yaw, 0). */
#pragma once

#include <windows.h>
 

class Game;
class RenderDevice;
struct ThemeAssetBlock;

/* A position and rotation list, one entry per cell of one kind. */
class __attribute__((packed)) PlacementList {
public:
     

    int      count() const { return count_; }
    float (*pos() const)[3] { return pos_; }
    float (*rot() const)[3] { return rot_; }

private:
    friend class LevelPlacements;  // counts, allocates and fills the lists

    void alloc(unsigned entries);
    void release();
    void put(unsigned *next, float x, float y, float z, float yaw);

    int     count_;
    float (*pos_)[3];
    float (*rot_)[3];
     
};

/* Not a decoded type: 0x20-byte vertices in two formats that share the size.
 * The tile-top template is x, y, z, diffuse 0xffffffff, (u0, v0), (u1, v1);
 * everything else is a BbVertex (d3dmath.h): x, y, z, 0, diffuse 0x00ffffff,
 * specular 0, u, v. */
struct PlacementVertex { DWORD d[8]; };

class __attribute__((packed)) LevelPlacements {
public:
     

    /* Frees every array and zeroes every count.  Also called at shutdown. */
    void release();

    /* Releases, counts, allocates and fills the lists, then the wall strips.
     * Called on level entry. */
    void build(const Game *g, const ThemeAssetBlock *theme);

    void drawLifts(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                   double now);
    void drawSlides(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                    double now);

    /* Before WinMain: every dword of the tile quad template zero but the
     * diffuse, 0xffffffff.  The builder overwrites all four later. */
    void initTileQuad();

    /* The block is packed but 4-aligned in memory. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    const float           *exitPos() const      { return exitPos_; }
    const float           *exitRot() const      { return exitRot_; }
#pragma GCC diagnostic pop
    const PlacementList   &lifts() const        { return lifts_; }
    const PlacementList   &slides() const       { return slides_; }
    const PlacementList   &breakables() const   { return breakables_; }
    const PlacementList   &jumpPads() const     { return jumpPads_; }
    const PlacementList   &teleporters() const  { return teleporters_; }
    const PlacementList   &glue() const         { return glue_; }
    const PlacementList   &switches() const     { return switches_; }
    const PlacementList   &ramps() const        { return ramps_; }
    const PlacementList   &climbs() const       { return climbs_; }
    const PlacementList   &conveyors() const    { return conveyors_; }
    const PlacementList   &destructibles() const { return destructibles_; }

private:
    void buildWalls(const Game *g, float depth);

    PlacementVertex tileQuad_[4];     // unit quad at y 0, +-0.5
    int             kind01Count_;     // TILE_KIND_01 cells
    PlacementVertex *kind01Verts_;    // 6 per cell, two triangles
    float           exitPos_[3];      // the TILE_EXIT cell; the last one wins
    float           exitRot_[3];      // always 0
    PlacementList   lifts_;           // TILE_LIFT; pos and rot all 0
    PlacementList   slides_;  // count is the Game's slide count, not a cell count; rot zeroed
    PlacementList   breakables_;      // TILE_BREAKABLE
    PlacementList   jumpPads_;        // TILE_JUMP_PAD
    PlacementList   teleporters_;     // TILE_TELEPORTER
    PlacementList   glue_;            // TILE_GLUE
    PlacementList   switches_;        // TILE_SWITCH
    PlacementList   ramps_;           // TILE_RAMP_1..4, yaw by kind
    PlacementList   climbs_;          // TILE_CLIMB, yaw by climb direction
    PlacementList   conveyors_;       // TILE_CONVEYOR
    PlacementList   destructibles_;   // TILE_DESTRUCTIBLE
    int             wallStripCount_;  // strips, 6 vertices each
    PlacementVertex *wallVerts_;      // the wall strips
     
};

 
extern LevelPlacements g_levelPlacements;

class Game;
class ThemeAssetBlock;


/* Copies every live lift's (or slide's) position into its list, v negated to
 * z, then draws the list with the theme's ELEVATOR (PLATFORM) records.  A
 * slide along u gets a quarter-turn yaw; slides animate with fmod(now * 0.002,
 * 1). */
class ThemeAssetBlock;
class RenderDevice;
