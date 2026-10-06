/* The level's placement lists: for each kind of cell the renderer draws
 * (lifts, platforms, falling tiles, stairs...), where each one stands and how it is
 * turned, plus the tile-top template and the wall strips at height steps.
 * Built on level entry, read by the frame renderer every frame.  Every list is
 * a count and one or two heap arrays, freed only by LevelPlacements::release.
 * Positions are (u, height, -v); rotations are (0, yaw, 0). */
#pragma once

#include <stdint.h>
#include <memory>
 

class Game;
class RenderDevice;
struct VertexBuffer;
struct ThemeAssetBlock;

/* A position and rotation list, one entry per cell of one kind. */
class PlacementList {
public:
     

    int      count() const { return count_; }
    float (*pos() const)[3] { return pos_.get(); }
    float (*rot() const)[3] { return rot_.get(); }

private:
    friend class LevelPlacements;  // counts, allocates and fills the lists

    void alloc(unsigned entries);
    void release();
    void put(unsigned *next, float x, float y, float z, float yaw);

    int     count_;
    std::unique_ptr<float[][3]> pos_;  // zeroed on alloc
    std::unique_ptr<float[][3]> rot_;
     
};

/* Not a decoded type: 0x20-byte vertices in two formats that share the size.
 * The tile-top template is x, y, z, diffuse 0xffffffff, (u0, v0), (u1, v1);
 * everything else is a BbVertex (d3dmath.h): x, y, z, 0, diffuse 0x00ffffff,
 * specular 0, u, v. */
struct PlacementVertex { uint32_t d[8]; };

class LevelPlacements {
public:
     

    /* Frees every array and zeroes every count.  Also called at shutdown. */
    void release();

    /* Releases, counts, allocates and fills the lists, then the wall strips.
     * Called on level entry. */
    void build(const Game *g, const ThemeAssetBlock *theme);

    void drawLifts(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                   double now);
    void drawPlatforms(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                    double now);

    /* Every dword of the tile quad template zero but the diffuse, 0xffffffff.
     * The builder overwrites all four later. */
    LevelPlacements();

 
 
    const float           *exitPos() const      { return exitPos_; }
    const float           *exitRot() const      { return exitRot_; }
 
    const PlacementList   &lifts() const        { return lifts_; }
    const PlacementList   &platforms() const       { return platforms_; }
    const PlacementList   &falling() const   { return falling_; }
    const PlacementList   &jumpPads() const     { return jumpPads_; }
    const PlacementList   &teleporters() const  { return teleporters_; }
    const PlacementList   &sticky() const         { return sticky_; }
    const PlacementList   &switches() const     { return switches_; }
    const PlacementList   &stairs() const       { return stairs_; }
    const PlacementList   &slides() const       { return slides_; }
    const PlacementList   &ice() const    { return ice_; }
    const PlacementList   &bombables() const { return bombables_; }

    int kind01Count() const { return kind01Count_; }
    PlacementVertex* kind01Verts() const { return kind01Verts_.get(); }

    int wallStripCount() const {return wallStripCount_;}
    PlacementVertex * wallStripVerts() const { return wallVerts_.get(); }

    /* The flat quads and the wall strips on the device, six vertices apiece:
     * made on first use, dropped by release.  NULL when there are none. */
    const VertexBuffer *kind01Buffer(RenderDevice *dev) const;
    const VertexBuffer *wallStripBuffer(RenderDevice *dev) const;

private:
    void buildWalls(const Game *g, float depth);

    PlacementVertex tileQuad_[4];     // unit quad at y 0, +-0.5
    int             kind01Count_;     // TILE_KIND_01 cells
    std::unique_ptr<PlacementVertex[]> kind01Verts_;  // 6 per cell, two triangles
    float           exitPos_[3];      // the TILE_EXIT cell; the last one wins
    float           exitRot_[3];      // always 0
    PlacementList   lifts_;           // TILE_LIFT; pos and rot all 0
    PlacementList   platforms_;  // count is the Game's platform count, not a cell count; rot zeroed
    PlacementList   falling_;      // TILE_FALLING
    PlacementList   jumpPads_;        // TILE_JUMP_PAD
    PlacementList   teleporters_;     // TILE_TELEPORTER
    PlacementList   sticky_;            // TILE_STICKY
    PlacementList   switches_;        // TILE_SWITCH
    PlacementList   stairs_;           // TILE_STAIRS_1..4, yaw by kind
    PlacementList   slides_;          // TILE_SLIDE, yaw by slide direction
    PlacementList   ice_;       // TILE_ICE
    PlacementList   bombables_;   // TILE_BOMBABLE
    int             wallStripCount_;  // strips, 6 vertices each
    std::unique_ptr<PlacementVertex[]> wallVerts_;    // the wall strips
    mutable VertexBuffer *kind01Vb_ = nullptr;  // made by the first draw, so const
    mutable VertexBuffer *wallVb_   = nullptr;
     
};

 
extern LevelPlacements g_levelPlacements;

class Game;
class ThemeAssetBlock;


/* Copies every live lift's (or platform's) position into its list, v negated to
 * z, then draws the list with the theme's ELEVATOR (PLATFORM) records.  A
 * platform along u gets a quarter-turn yaw; platforms animate with fmod(now * 0.002,
 * 1). */
class ThemeAssetBlock;
class RenderDevice;
