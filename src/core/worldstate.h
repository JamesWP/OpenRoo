/* The world-state reader (worldstate.cpp): the autoplayer's view of a level,
 * read from the Game each frame.  Reads only: it never writes game state, so
 * it cannot perturb a recording.
 *
 * The grid is indexed v + u * 100.  U is the axis multiplied by 100 and V the
 * one added; every float position in the game is ordered (U, H, V).  Which is
 * which on screen is not settled, hence U and V rather than X and Y. */

#pragma once

#include "tile.h"  // the tile kinds and contents
#include <windows.h>

#define WS_GRID_PITCH 100  // fixed; not the level's column count

/* Facing is a direction 1 to 4, from the movement interpolation:
 *   1: V decreasing   2: U increasing   3: V increasing   4: U decreasing
 * Turning right adds 1 and turning left adds 3, wrapping in 1 to 4. */

/* A drop of one or two height steps is survivable and three or more is fatal,
 * unless the landing tile is a jump pad; landing at height 1 or lower is
 * fatal too. */
#define WS_MAX_SAFE_DROP 2

/* Every nonzero contents byte is a pickup except CONTENTS_TRANSFORM, which
 * turns the player into something else and is left alone. */

/* A glue pad (TILE_GLUE).  Standing on one whose busy flag is clear cancels
 * the queued move for a fixed time; then the flag is set and the pad is inert.
 * Foes run the same movement code, so a foe on a pad is held too. */
#define WS_TILE_GLUE TILE_GLUE

/* The breakable tile.  Standing on one drops it away a few ticks later, and
 * the player with it; crossing without stopping is safe.  They are laid out as
 * corridors, so the planner avoids them where it can and crosses them where it
 * must.  One whose param is 0 comes back after it falls; any other param falls
 * once.  A severed route is often temporary, so plans are allowed to recover
 * (plan.cpp). */
#define WS_TILE_FALLING TILE_BREAKABLE

/* CONTENTS_EXTRA_LIFE adds one to the player's lives. */
#define WS_TILE_EXTRA_LIFE CONTENTS_EXTRA_LIFE

/* Contents, not a kind: TILE_BREAKABLE is the kind with the same value. */
#define WS_TILE_TRANSFORM CONTENTS_TRANSFORM
static inline bool ws_is_pickup(BYTE contents)
{
    return contents != 0 && contents != WS_TILE_TRANSFORM;
}
#define WS_TILE_SOFT_LAND TILE_JUMP_PAD

#define WS_DIR_MIN 1
#define WS_DIR_MAX 4
static const int WS_DIR_DU[5] = { 0,  0, +1,  0, -1 };
static const int WS_DIR_DV[5] = { 0, -1,  0, +1,  0 };
#define WS_MAX_ENT    500  // the capacity of both tables

/* One tile, decoded. */
struct WsTile {
    BYTE  kind;  // the object marker
    BYTE  param;
    BYTE  contents;  // 1 crystal, 7 extra life
    BYTE  occupant;  // nonzero blocks the cell: an object or a foe
    BYTE  height;
    BYTE  spawn;     // the snapshot's contents: the cell as the map gave it
    BYTE  spawn_a;   // the snapshot's height
    BYTE  spawn_b;   // the snapshot's param
    float height_f;  // a lift's live height
    DWORD spent;     // the busy flag: a spent glue pad, an armed breakable
};

/* One live foe or bomb. */
struct WsEntity {
    BYTE  slot;        // its index in the ID table
    BYTE  kind;        // the foe kind; 0 for bombs
    BYTE  facing;      // 1..4, see WS_DIR_*
    BYTE  moving;      // the move in progress; 0 idle
    BYTE  subtype;     // behaviour: 1 heads for the exit, 2 and 3 chase
    DWORD frozen;      // held: nonzero does not act this tick
    BYTE  category;    // foes only: the contents it drops
    BYTE  gu, gv, gh;  // its cell
    BYTE  su, sv, sh;  // foes only: its home cell
    float pos[3];      // (U, H, V)
    DWORD hidden;      // nonzero once it has started dying
};

/* One frame's observation: what the autoplayer reads. */
struct Observation {
    bool  valid;
    DWORD frame;
    unsigned short mode;  // 0: not in a level

    BYTE  cols, rows;    // U extent, V extent
    const WsTile *grid;  // v + u * WS_GRID_PITCH

    BYTE  player_facing;    // 1..4
    BYTE  player_moving;    // 0 idle
    float player_grid[3];   // (U, H, V)
    float player_world[3];  // the camera eye, (U, H, V)
    BYTE  player_cell[3];   // (U, V, H)
    BYTE  exit_cell[3];     // (U, V, H), the level exit

    DWORD freeze_timer;  // nonzero holds every foe
    unsigned n_foes;
    unsigned n_enemies;
    WsEntity foes[WS_MAX_ENT];
    WsEntity enemies[WS_MAX_ENT];

    // The same fields gamestate.cpp reads.
    int gems_collected, gems_required;
    BYTE foes_killed, lives;
    int level_complete;
    unsigned short crystals_in_level;  // the crystals in the level

    /* Fills obs from the live Game.  False when there is no Game or the grid's
     * extents are out of range (menus, teardown). */
    bool observe();

    /* Whether an entity on (fu, fv) can step to the adjacent (tu, tv).  The game's
     * movement cancels a step when:
     *   - the destination's occupant byte is set (an object or a foe);
     *   - it is higher, unless the tile being left is a ramp facing that way;
     *   - it is WS_MAX_SAFE_DROP or more steps lower, unless it is a jump pad;
     *   - it is kind 0x16, or 0x17 with its busy flag clear.
     * Ramps (kinds 5 to 8) allow the climb only in their own direction; that test
     * is not reproduced here, so this can propose a climb the game refuses.
     * Everywhere else it errs towards blocked: a refused legal step costs a
     * detour, but an accepted illegal one wedges the autoplayer against a wall. */
    bool passable(int fu, int fv, int tu, int tv) const;

    /* As ws_passable, with foes treated as absent.  A foe blocks its cell as an
     * object does, but it moves; this separates "blocked for now" from "blocked
     * for good", so the autoplayer can wait for a guarded pickup. */
    bool passableIgnoringFoes(int fu, int fv, int tu, int tv) const;

    /* Whether a live foe or bomb stands on this cell.
     *
     * Foes act only while the freeze timer is 0 and the mode is 1; otherwise, and
     * once the level is complete or the player dead, they are held.  The freeze
     * bonus is the window in which a guarded pickup is safe.  Behaviour 1 walks to
     * the exit, 2 and 3 chase the player, 5 hunts other foes. */
    bool foeOnCell(int u, int v) const;

    /* KAROO_WS_TRACE: logs the frame's player and entity lines. */
    void traceFrame() const;

private:
    bool wsPassableImpl(int fu, int fv, int tu, int tv, bool ignore_foes) const;
    void traceEntities(const char *tag, const WsEntity *ents, unsigned n, bool foe) const;
};

/* Called once a frame from clock_seconds(), after gamestate_tick().  Drives
 * KAROO_MAP_DUMP, KAROO_ENTITY_TRACE and KAROO_OBS_DUMP; does nothing when
 * none is set. */
void worldstate_tick(void);

/* The latest valid observation, or NULL.  The autoplayer reads this rather
 * than the Game. */
const Observation *worldstate_latest(void);
