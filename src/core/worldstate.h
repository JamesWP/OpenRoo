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
/* Kind 2 is the glue pad (the "Glue&Enemy" levels are full of them: 16 on
 * Forest\Glue&EnemyStart).  Standing on one whose spent flag is clear cancels
 * the entity's queued move for a fixed time, then sets the flag and the pad
 * goes inert:
 *
 *   if (height == tile.height && tile.kind == 2 && tile+0x7a == 0) {
 *       if (now - stuck_since <= DURATION) entity+0x145 = 0;   // frozen
 *       else { stuck_since = 0; tile+0x7a = 1; }               // pad spent
 *   }
 *
 * Foes run the same entity code, so a foe crossing one is frozen too — which
 * is the basis for luring them onto a pad, not implemented here. */
#define WS_TILE_GLUE 0x02

/* Kind 0x0d is the falling tile — SetupLevelObjects hands each one to
 * SpawnBreakableObject.  Stand on it and a few ticks later it drops away and
 * takes you with it; cross it without stopping and it is harmless.  Forest
 * level 9 ("DestrStart", 25 of them) is the first to use them, and they are
 * laid out as corridors — (3,10)..(3,13), (6,6)..(6,9), (11,7)..(11,9) — so
 * treating them as impassable would make the level unsolvable.  They are
 * avoided softly instead: routed around when there is an alternative, crossed
 * when there is not.
 *
 * They come back.  UpdateBreakableTile (0x00403d40) arms the tile when someone
 * steps on it, drops it after a delay (kind -> 0, which is the death), and
 * then respawns it (kind -> 0x0d) — but only when the object's +0x59 is zero,
 * and that field is the TILE'S PARAM BYTE passed through SpawnBreakableObject.
 * So param 0 respawns and a nonzero param falls once and stays gone, which is
 * the "does not respawn" variant in later levels.  All 25 on DestrStart have
 * param 0.
 *
 * The practical consequence for planning is that a severed route is usually
 * temporary, so a plan must be allowed to recover rather than being computed
 * once — see plan.cpp. */
#define WS_TILE_FALLING 0x0d

/* Contents 7 is the extra life.  UpdatePlayerTileEffects does
 * `entity+0x239 += 1` for it, and the player entity is Game+0x1751c9, so that
 * write lands on Game+0x175402 — the lives counter.  (The sibling case,
 * contents 1, does entity+0x23d, which is Game+0x175406, the gem count.)
 *
 * This is why a scan for writes to the absolute address 0x175402 came up with
 * only level init, the "mausuruh" cheat, the death decrement and save-restore:
 * the pickup writes it through the entity pointer. */
#define WS_TILE_EXTRA_LIFE 0x07

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
    BYTE  contents;    /* +0x2ab72c  1 = crystal, 7 = extra life */
    BYTE  occupant;    /* +0x2ab732  foe kind, written at spawn */
    BYTE  height;      /* +0x2ab729 */
    BYTE  spawn;       /* +0x3e181c  second plane: foe spawns, pickups */
    BYTE  spawn_a;     /* +0x3e1819 */
    BYTE  spawn_b;     /* +0x3e181b */
    float height_f;    /* +0x2ab733 */
    DWORD spent;       /* +0x2ab7a4 (tile+0x7a) — "this tile has been used" */
};

/* One live foe or enemy. */
struct WsEntity {
    BYTE  slot;        /* index into the pointer table */
    BYTE  kind;        /* foe +0x152; 0 for enemies (field not confirmed) */
    BYTE  facing;      /* +0x14  1..4, see WS_DIR_* below */
    BYTE  moving;      /* +0x14e in-progress move direction, 0 = idle */
    BYTE  subtype;     /* +0x62  behaviour: 1 heads for the exit, 2/3 chase */
    DWORD frozen;      /* +0xef  nonzero = this foe will not act this tick */
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

    DWORD freeze_timer;        /* Game+0x1753af — nonzero freezes every foe */
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

/* As ws_passable, but with foes treated as if they were not there.
 *
 * A foe standing on a cell blocks it through the occupant byte, exactly like a
 * crate does — but a foe moves and a crate does not.  Asking "would this be
 * reachable if the foes stepped aside?" separates a route that is blocked
 * for now from one that is blocked for good, which is what lets the policy
 * wait for a guarded pickup instead of giving up on it. */
bool ws_passable_ignoring_foes(const Observation *o, int fu, int fv,
                               int tu, int tv);

/* Foe behaviour, from GameTick's foe loop:
 *
 *   if (Game+0x1753af == 0 && bGame_state == 1) foe+0xef = 0;   // acts
 *   else                                        foe+0xef = 1;   // frozen
 *
 * and foe+0xef is also forced to 1 once the level is completed or the player
 * is dead.  Game+0x1753af is the freeze bonus: while it is set every foe is
 * frozen, which is the window in which a guarded pickup can be taken safely.
 *
 * foe+0x62 selects the behaviour: 1 walks to the level EXIT rather than
 * chasing, 2 and 3 chase the player through different searches, 5 hunts other
 * foes.  So not every foe is coming for you.
 */

/* Is a live foe or enemy standing on this cell? */
bool ws_foe_on_cell(const Observation *o, int u, int v);

/* Called once per frame from clock_seconds(), after gamestate_tick().
 * Drives KAROO_MAP_DUMP (Stage 1), KAROO_ENTITY_TRACE (Stage 2) and
 * KAROO_OBS_DUMP (Stage 3).  A no-op when none of those are set. */
void worldstate_tick(void);

/* The most recent valid observation, or NULL.  Stage 4's policy reads this
 * rather than re-walking the Game object. */
const Observation *worldstate_latest(void);
