/* The level's fixed picture: the floor and the walls, and every cell that is
 * drawn as a theme object -- the pads, stairs, switches, the exit, the
 * bombable blocks -- and the items lying about.  Built on level entry from the
 * map; drawn every frame from the cells' live state (busy, blasting, what lies
 * there).  The level's moving objects draw themselves (World::draw).
 *
 * Positions are (u, height, -v). */
#pragma once

#include <stdint.h>
#include <memory>
#include <vector>
#include "themedraw.h"

class RenderDevice;
class LevelMap;
struct VertexBuffer;

/* A 0x20-byte VertexFormat::Lit vertex, as the floor and wall buffers hold
 * them: x, y, z, 0, diffuse, specular 0, u, v. */
struct PlacementVertex { uint32_t d[8]; };

class LevelPlacements {
public:
    /* Frees everything; also called at shutdown. */
    void release();

    /* Releases, then collects the cells and builds the floor and the walls
     * (sideHeight deep, from the theme).  Called on level entry. */
    void build(const LevelMap *map, float sideHeight);

    /* Draws the level's fixed picture for one pass of the frame.  Fires and
     * clears a blasted block's pending debris. */
    void draw(draw::Frame &f, draw::Pass pass, LevelMap *map);

    LevelPlacements() = default;
    ~LevelPlacements() { release(); }
    LevelPlacements(const LevelPlacements &) = delete;
    LevelPlacements &operator=(const LevelPlacements &) = delete;

private:
    /* The kinds of cell drawn as a theme object, each with its look. */
    enum Group {
        GROUP_SWITCH, GROUP_ICE, GROUP_STICKY, GROUP_FALLING, GROUP_JUMP_PAD,
        GROUP_TELEPORTER, GROUP_STAIRS, GROUP_SLIDE, GROUP_EXIT, GROUP_BOMBABLE,
        GROUP_COUNT
    };
    struct Cell {
        draw::Instance in;
        unsigned char  u, v;
    };

    void buildWalls(const LevelMap *map, float depth);
    /* Group g's instances, their conditions and phases read from the cells. */
    const std::vector<draw::Instance> &instances(Group g, const LevelMap *map);
    void group(draw::Frame &f, Group g, ThemeObjectType look, const LevelMap *map,
               bool particles);
    void bombables(draw::Frame &f, LevelMap *map);
    /* The items lying about, one instance each (looks[i] is items_[i]'s). */
    void collectItems(const LevelMap *map);

    const VertexBuffer *floorBuffer(RenderDevice *dev);
    const VertexBuffer *wallBuffer(RenderDevice *dev);

    std::vector<Cell>            cells_[GROUP_COUNT];
    std::vector<draw::Instance>  scratch_;
    std::vector<draw::Instance>  items_;
    std::vector<ThemeObjectType> itemLooks_;
    std::vector<PlacementVertex> floor_;   // TILE_KIND_01 cells, six vertices each
    std::vector<PlacementVertex> walls_;   // the wall strips, six vertices each
    VertexBuffer *floorVb_ = nullptr;      // made by the first draw
    VertexBuffer *wallVb_  = nullptr;
};
