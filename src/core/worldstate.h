#pragma once
#include <windows.h>

/* World-state reader — AI_PLAN.md Stages 1-3.
 *
 * Reads the tile grid and the live entity tables out of the Game object.
 * Everything here is a plain read of already-allocated memory at the existing
 * per-frame hook point: no new patches, no new call sites, no writes.  That is
 * a hard requirement, not a style preference — a perception layer that
 * perturbs the simulation invalidates every recording in tests/.
 *
 * Axis naming.  The grid is indexed `idx = v + u * 100`, and the code below
 * calls the two axes U and V rather than X and Y because which one is which
 * on screen has not been confirmed.  What *is* confirmed (see worldstate.cpp)
 * is that U is the axis multiplied by 100, V the one added, and that the
 * float position triples everywhere in the game are ordered (U, H, V).
 */

#define WS_GRID_PITCH 100          /* fixed, NOT the live column count */
#define WS_MAX_ENT    500          /* pointer-table capacity, both tables */

/* One tile, decoded.  Field meanings are in AI_PLAN.md § Finding 1. */
struct WsTile {
    BYTE  kind;        /* +0x2ab72a */
    BYTE  param;       /* +0x2ab72b */
    BYTE  contents;    /* +0x2ab72c  1 = crystal */
    BYTE  occupant;    /* +0x2ab732  foe kind, written at spawn */
    BYTE  height;      /* +0x2ab729 */
    BYTE  spawn;       /* +0x3e181c  second plane: foe spawns, pickups */
    BYTE  spawn_a;     /* +0x3e1819 */
    BYTE  spawn_b;     /* +0x3e181b */
    float height_f;    /* +0x2ab733 */
};

/* One live foe or enemy. */
struct WsEntity {
    BYTE  slot;        /* index into the pointer table */
    BYTE  kind;        /* foe +0x152; 0 for enemies (field not confirmed) */
    BYTE  subtype;     /* +0x14 */
    BYTE  category;    /* +0x15a (foes only) */
    BYTE  gu, gv, gh;  /* +0x31 / +0x32 / +0x33 — current grid cell */
    BYTE  su, sv, sh;  /* +0x153..0x155 — spawn cell (foes only) */
    float pos[3];      /* +0x25 / +0x29 / +0x2d — (U, H, V) */
    DWORD hidden;      /* +0x82 — nonzero means not drawn this frame */
};

/* A single frame's observation: the seam an AI plugs into (Stage 3). */
struct Observation {
    bool  valid;
    DWORD frame;
    unsigned short mode;       /* 0 = not in a level */

    BYTE  cols, rows;          /* U extent, V extent */
    const WsTile *grid;        /* cols*rows entries, row-major in V */

    float player_grid[3];      /* Game+0x1751ee  (U, H, V) */
    float player_world[3];     /* Game+0x2ab580  (U, H, V) */
    BYTE  player_cell[3];      /* Game+0x1751fa  (U, V, H) */

    unsigned n_foes;
    unsigned n_enemies;
    WsEntity foes[WS_MAX_ENT];
    WsEntity enemies[WS_MAX_ENT];

    /* Scalars, mirrored from the same fields gamestate.cpp reads. */
    int gems_collected, gems_required;
    BYTE foes_killed, lives;
    int level_complete;
    unsigned short crystals_in_level;   /* Game+0x42252 */
};

/* Fill `obs` from the live Game object.  Returns false when there is no Game
 * or the grid dimensions are out of range (menus, teardown). */
bool worldstate_observe(Observation *obs);

/* Called once per frame from clock_seconds(), after gamestate_tick().
 * Drives KAROO_MAP_DUMP (Stage 1), KAROO_ENTITY_TRACE (Stage 2) and
 * KAROO_OBS_DUMP (Stage 3).  A no-op when none of those are set. */
void worldstate_tick(void);

/* The most recent valid observation, or NULL.  Stage 4's policy reads this
 * rather than re-walking the Game object. */
const Observation *worldstate_latest(void);
