/* The world-state reader (worldstate.cpp): the view of a level,
 * read from the Game each frame.  Reads only: it never writes game state, so
 * it cannot perturb a recording.
 *
 * The grid is indexed v + u * 100.  U is the axis multiplied by 100 and V the
 * one added; every float position in the game is ordered (U, H, V).  Which is
 * which on screen is not settled, hence U and V rather than X and Y. */

#pragma once

#include "tile.h"  // the tile kinds and contents
#include <stdint.h>

#define WS_GRID_PITCH 100  // fixed; not the level's column count

/* Facing is a direction 1 to 4, from the movement interpolation:
 *   1: V decreasing   2: U increasing   3: V increasing   4: U decreasing
 * Turning right adds 1 and turning left adds 3, wrapping in 1 to 4. */

#define WS_DIR_MIN 1
#define WS_DIR_MAX 4
static const int WS_DIR_DU[5] = { 0,  0, +1,  0, -1 };
static const int WS_DIR_DV[5] = { 0, -1,  0, +1,  0 };
#define WS_MAX_ENT    500  // the capacity of both tables

/* One tile, decoded. */
struct WsTile {
    uint8_t  kind;  // the object marker
    uint8_t  param;
    uint8_t  contents;  // 1 crystal, 7 extra life
    uint8_t  occupant;  // nonzero blocks the cell: an object or a foe
    uint8_t  height;
    uint8_t  spawn;     // the snapshot's contents: the cell as the map gave it
    uint8_t  spawn_a;   // the snapshot's height
    uint8_t  spawn_b;   // the snapshot's param
    float height_f;  // a lift's live height
    uint32_t spent;     // the busy flag: a spent sticky pad, an armed falling tile
};

/* One live foe or bomb. */
struct WsEntity {
    uint8_t  slot;        // its index in the ID table
    uint8_t  kind;        // the foe kind; 0 for bombs
    uint8_t  facing;      // 1..4, see WS_DIR_*
    uint8_t  moving;      // the move in progress; 0 idle
    uint8_t  subtype;     // behaviour: 1 heads for the exit, 2 and 3 chase
    uint32_t frozen;      // held: nonzero does not act this tick
    uint8_t  category;    // foes only: the contents it drops
    uint8_t  gu, gv, gh;  // its cell
    uint8_t  su, sv, sh;  // foes only: its home cell
    float pos[3];      // (U, H, V)
    uint32_t hidden;      // nonzero once it has started dying
};

/* One frame's observation: what the debug UI and dumps read. */
struct Observation {
    bool  valid;
    uint32_t frame;
    unsigned short mode;  // 0: not in a level

    uint8_t  cols, rows;    // U extent, V extent
    const WsTile *grid;  // v + u * WS_GRID_PITCH

    uint8_t  player_facing;    // 1..4
    uint8_t  player_moving;    // 0 idle
    float player_grid[3];   // (U, H, V)
    float player_world[3];  // the camera eye, (U, H, V)
    uint8_t  player_cell[3];   // (U, V, H)
    uint8_t  exit_cell[3];     // (U, V, H), the level exit

    uint32_t freeze_timer;  // nonzero holds every foe
    unsigned n_foes;
    unsigned n_enemies;
    WsEntity foes[WS_MAX_ENT];
    WsEntity enemies[WS_MAX_ENT];

    // The same fields gamestate.cpp reads.
    int gems_collected, gems_required;
    uint8_t foes_killed, lives;
    int level_complete;
    unsigned short crystals_in_level;  // the crystals in the level

    /* Fills obs from the live Game.  False when there is no Game or the grid's
     * extents are out of range (menus, teardown). */
    bool observe();

    /* KAROO_WS_TRACE: logs the frame's player and entity lines. */
    void traceFrame() const;

private:
    void traceEntities(const char *tag, const WsEntity *ents, unsigned n,
                       bool foe) const;
};

/* Called once a frame from clock_seconds(), after gamestate_tick().  Drives
 * KAROO_MAP_DUMP, KAROO_ENTITY_TRACE and KAROO_OBS_DUMP; does nothing when
 * none is set. */
void worldstate_tick(void);

