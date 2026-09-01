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

/* Facing is a 4-way discrete direction, 1..4, held at entity+0x14.
 *
 * The mapping is read from the movement interpolation in FUN_00438770, which
 * switches on the in-progress direction (entity+0x14e) and interpolates the
 * float position from the neighbouring cell to the current one:
 *
 *   dir 1: V = (gv+1) - t   ->  V decreasing
 *   dir 2: U = (gu-1) + t   ->  U increasing
 *   dir 3: V = (gv-1) + t   ->  V increasing
 *   dir 4: U = (gu+1) - t   ->  U decreasing
 *
 * FUN_0043ad40 turns: d = d + delta, wrapping to 1..4 — right is +1, left +3.
 */
/* Falls are survivable only under three height steps.
 *
 * The landing branch of FUN_00438770 reads:
 *
 *   if (dest.kind == 0x0e || ((fall_start_height - landing_height) < 3
 *                             && landing_height > 1))
 *        entity+0x11f = 0;      // landed safely
 *   else entity+0x11f = 2;      // dead
 *
 * so a drop of 1 or 2 is fine and 3 or more is fatal, with tile kind 0x0e a
 * always-safe landing.  Falling below height 0 is separately fatal. */
#define WS_MAX_SAFE_DROP 2

/* Pickups worth walking to.
 *
 * Every nonzero tile contents byte is something to collect (crystals are 1),
 * with one exception: contents 0x0d sets the *player's* +0x152 to 3 and writes
 * 3 into the tile's occupant byte — +0x152 is the field SpawnFoeObject uses
 * for a foe's kind, so this transforms the player into something.  Whatever it
 * does, it is not obviously a prize, so it is left alone.
 *
 * NOTE: which value is specifically the extra life has NOT been identified.
 * Lives (Game+0x175402) are written only by level init, the death decrement,
 * save-slot restore, and the "mausuruh" cheat — no pickup writes them
 * directly, so a life must be granted indirectly.  Collecting every benign
 * pickup is a superset that includes it. */
#define WS_TILE_TRANSFORM 0x0d
static inline bool ws_is_pickup(BYTE contents)
{
    return contents != 0 && contents != WS_TILE_TRANSFORM;
}
#define WS_TILE_SOFT_LAND 0x0e

#define WS_DIR_MIN 1
#define WS_DIR_MAX 4
static const int WS_DIR_DU[5] = { 0,  0, +1,  0, -1 };
static const int WS_DIR_DV[5] = { 0, -1,  0, +1,  0 };
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
    BYTE  facing;      /* +0x14  1..4, see WS_DIR_* below */
    BYTE  moving;      /* +0x14e in-progress move direction, 0 = idle */
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

    BYTE  player_facing;       /* Game+0x1751dd  1..4 */
    BYTE  player_moving;       /* Game+0x175317  0 = idle */
    float player_grid[3];      /* Game+0x1751ee  (U, H, V) */
    float player_world[3];     /* Game+0x2ab580  (U, H, V) */
    BYTE  player_cell[3];      /* Game+0x1751fa  (U, V, H) */
    BYTE  exit_cell[3];        /* Game+0x17530b  (U, V, H) — the level exit */

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

/* Can an entity standing on (fu,fv) step to the adjacent cell (tu,tv)?
 *
 * A conservative reading of the destination checks in FUN_00438770, which
 * cancels a queued move (entity+0x14e = 0) when:
 *
 *   - the destination's occupant byte (tile+0x08) is nonzero and the step is a
 *     real one — this is what makes objects, and foes, block a cell;
 *   - the destination is UP and the tile being left is not a ramp facing that
 *     way — you can drop off a ledge you cannot climb back up;
 *   - the destination is WS_MAX_SAFE_DROP or more steps DOWN — the landing
 *     code kills the player unless the drop is under 3 (or the landing tile is
 *     kind 0x0e, which is always safe);
 *   - the destination kind is 0x16, or 0x17 with its tile+0x7a flag clear.
 *
 * Ramps are kinds 5..8 (`FUN_0041f8a0` returns true for 4 < k < 9), and on
 * those the climb is allowed only when the travel direction matches the ramp's
 * orientation (kind - 4).  That orientation test is NOT reproduced here: this
 * returns true for any ramp, which can propose a climb the game refuses.
 *
 * Erring towards "blocked" is deliberate everywhere else.  A refused legal
 * move costs a detour; an accepted illegal one wedges the policy against an
 * obstacle, which is the failure this function exists to stop. */
bool ws_passable(const Observation *o, int fu, int fv, int tu, int tv);

/* Called once per frame from clock_seconds(), after gamestate_tick().
 * Drives KAROO_MAP_DUMP (Stage 1), KAROO_ENTITY_TRACE (Stage 2) and
 * KAROO_OBS_DUMP (Stage 3).  A no-op when none of those are set. */
void worldstate_tick(void);

/* The most recent valid observation, or NULL.  Stage 4's policy reads this
 * rather than re-walking the Game object. */
const Observation *worldstate_latest(void);
