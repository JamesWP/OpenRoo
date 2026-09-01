/* World-state reader — AI_PLAN.md Stages 1-3.
 *
 * ── Where these offsets come from ────────────────────────────────────────
 *
 * The grid: `SetupLevelObjects` (the function containing 0x00417010) walks the
 * level as two nested loops.  The outer counter is bounded by Game+0x2ab727
 * and is *added*; the inner is bounded by Game+0x2ab728 and is *multiplied by
 * 100*; the tile address is `Game + idx*0x7f + 0x2ab72a`.  The 100 is a fixed
 * pitch, not the live column count — indexing by the live count gives a map
 * that is subtly sheared rather than obviously wrong, so it is spelled
 * WS_GRID_PITCH here and never confused with `cols`.
 *
 * The axis that is multiplied is called U (extent Game+0x2ab728), the one that
 * is added is V (extent Game+0x2ab727).  Which is X and which is Y on screen
 * is NOT confirmed and nothing here depends on it.
 *
 * The (U, H, V) float ordering is confirmed three independent ways, which is
 * why it is stated plainly rather than marked (?):
 *   - SetupLevelObjects seeds the player float triple at +0x1751ee from the
 *     start cell bytes as (0x17531c, 0x17531e, 0x17531d) — i.e. (U, H, V);
 *   - it seeds Game+0x2ab580 from the same three in the same order;
 *   - SpawnFoeObject writes foe+0x25/+0x29/+0x2d from (param_1, param_3,
 *     param_2) = (U, height, V).
 * All three agree, and the grid-byte cross-check below tests it every frame.
 *
 * The entity tables: `SpawnFoeObject` (0x004172d0) gives the foe table
 * (pointers at Game+0x174804, count Game+0x174fd4, live ids Game+0x174fd5) and
 * every field offset used here.  `RemoveEnemyObject` (0x00417a20) gives the
 * enemy table; its pointer base, Game+0x173e3f, was read off the disassembly
 * (`lea 0x173e3f(%esi,%eax,4),%edi`) rather than inferred by analogy, because
 * the fieldrefs scanner skips SIB forms and so reported nothing.
 *
 * Enemies are a *different* class from foes — RemoveEnemyObject dereferences
 * +0x15e, past the end of a foe's 0x15e-byte allocation.  So enemy fields are
 * not assumed to match foe fields; the three that are read here (+0x25/+0x29/
 * +0x2d position and +0x82) are confirmed from RenderGameFrame's own use:
 *
 *     pvVar7 = (&pGVar16->pEnemy_objects)[(&pGVar16->field_0x174610)[i]];
 *     local_944 = -*(float *)((int)pvVar7 + 0x2d);
 *     local_948 =  *(float *)((int)pvVar7 + 0x29);
 *     local_94c =  *(float *)((int)pvVar7 + 0x25);
 *
 * Note the negation of the V component on the way to world space — the same
 * convention BuildSceneObjectList uses for flPosZ.  It is left un-negated here
 * so that the raw field and the grid byte can be compared directly.
 *
 * Both tables are walked through the live-id list, never by scanning the
 * pointer table: a dead slot keeps its stale pointer.
 *
 * ── What is deliberately NOT here ────────────────────────────────────────
 *
 * Tile-kind semantics beyond placement (is this bridge extended? is this
 * switch thrown?) are not located yet, and pathfinding is not attempted.
 * See AI_PLAN.md § "Not in scope yet".
 */
#include "worldstate.h"
#include "gamestate.h"
#include "clock.h"
#include "policy.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

#define GAME_GLOBAL_PTR ((void **)0x0046c498)

/* Grid */
#define OFF_V_EXTENT   0x2ab727
#define OFF_U_EXTENT   0x2ab728
#define OFF_TILE_HGT   0x2ab729   /* tile base - 1 */
#define OFF_TILE_BASE  0x2ab72a
#define OFF_PLANE2     0x3e1819   /* second plane, same idx and stride */
#define TILE_STRIDE    0x7f

/* Entities */
#define OFF_FOE_PTRS   0x174804
#define OFF_FOE_COUNT  0x174fd4
#define OFF_FOE_IDS    0x174fd5
#define OFF_ENE_PTRS   0x173e3f
#define OFF_ENE_COUNT  0x17460f
#define OFF_ENE_IDS    0x174610

/* Player */
/* The player is an entity of the same class as foes and enemies: its object
 * base is Game+0x1751c9, the context the input callbacks are registered with
 * in DirectInputSetup.  That is confirmed arithmetically — the position triple
 * this file already reads at Game+0x1751ee is exactly base+0x25, and the grid
 * bytes at Game+0x1751fa are base+0x31, matching the foe layout offset for
 * offset. */
#define OFF_PLAYER_OBJ 0x1751c9
#define OFF_PLR_FACING (OFF_PLAYER_OBJ + 0x14)    /* 0x1751dd */
#define OFF_PLR_MOVING (OFF_PLAYER_OBJ + 0x14e)   /* 0x175317 */
#define OFF_PLR_GRIDF  0x1751ee   /* float[3] (U, H, V) = base+0x25 */
#define OFF_PLR_CELL   0x1751fa   /* byte[3]  (U, V, H) */
#define OFF_PLR_WORLD  0x2ab580   /* float[3] (U, H, V) */

/* The level exit, as (U, V, H) bytes.  SetupLevelObjects finds it by searching
 * the grid for tile kind 4 (FUN_0041f430(..., 4, Game+0x17530b)) — and there
 * is exactly one such tile on every level dumped so far.  GameTick sets the
 * completion flag when the player's cell equals these three AND
 * gems_collected >= gems_required, so the exit only counts once the level's
 * crystals are done. */
#define OFF_EXIT_CELL  0x17530b

/* Scalars, same fields gamestate.cpp reads. */
#define OFF_GEMS_GOT   0x175406
#define OFF_GEMS_REQ   0x2ab723
#define OFF_FOES_KILL  0x04224d
#define OFF_LIVES      0x175402
#define OFF_COMPLETE   0x1752b8
#define OFF_CRYSTALS   0x042252

static WsTile     g_grid[WS_GRID_PITCH * WS_GRID_PITCH];
static Observation g_obs;
static bool        g_obs_valid;

/* ── env plumbing ───────────────────────────────────────────────────────── */

static bool env_flag(const char *name)
{
    char buf[16];
    return GetEnvironmentVariableA(name, buf, sizeof(buf)) && buf[0] && buf[0] != '0';
}

static bool env_path(const char *name, char *out, DWORD n)
{
    return GetEnvironmentVariableA(name, out, n) != 0 && out[0] != 0;
}

/* ── reading ────────────────────────────────────────────────────────────── */

static inline const BYTE *tile_at(const BYTE *g, unsigned u, unsigned v)
{
    return g + (v + u * WS_GRID_PITCH) * TILE_STRIDE + OFF_TILE_BASE;
}

static inline const BYTE *plane2_at(const BYTE *g, unsigned u, unsigned v)
{
    return g + (v + u * WS_GRID_PITCH) * TILE_STRIDE + OFF_PLANE2;
}

/* Read one entity through the live-id list.  `foe` selects the extra fields
 * that are only confirmed for the foe class. */
static void read_entity(const BYTE *obj, BYTE slot, bool foe, WsEntity *e)
{
    memset(e, 0, sizeof(*e));
    e->slot    = slot;
    e->facing  = obj[0x14];
    e->moving  = (BYTE)*(const DWORD *)(obj + 0x14e);
    e->gu      = obj[0x31];
    e->gv      = obj[0x32];
    e->gh      = obj[0x33];
    memcpy(e->pos, obj + 0x25, sizeof(e->pos));
    e->hidden  = *(const DWORD *)(obj + 0x82);
    if (foe) {
        e->kind     = obj[0x152];
        e->category = obj[0x15a];
        e->su       = obj[0x153];
        e->sv       = obj[0x154];
        e->sh       = obj[0x155];
    }
}

static unsigned read_table(const BYTE *g, unsigned off_ptrs, unsigned off_count,
                           unsigned off_ids, bool foe, WsEntity *out)
{
    unsigned count = g[off_count];
    if (count > WS_MAX_ENT) {
        log_write("worldstate: entity count %u exceeds table capacity %u — clamped\n",
                  count, (unsigned)WS_MAX_ENT);
        count = WS_MAX_ENT;
    }
    const BYTE   *ids  = g + off_ids;
    const BYTE  **ptrs = (const BYTE **)(g + off_ptrs);

    unsigned n = 0;
    for (unsigned i = 0; i < count; i++) {
        BYTE slot = ids[i];
        if (slot >= WS_MAX_ENT) continue;      /* corrupt id — skip, don't fault */
        const BYTE *obj = ptrs[slot];
        if (!obj) continue;                    /* freed slot still in the list */
        read_entity(obj, slot, foe, &out[n++]);
    }
    return n;
}

bool worldstate_observe(Observation *obs)
{
    const BYTE *g = (const BYTE *)*GAME_GLOBAL_PTR;
    memset(obs, 0, sizeof(*obs));
    if (!g) return false;

    BYTE rows = g[OFF_V_EXTENT];
    BYTE cols = g[OFF_U_EXTENT];
    if (rows == 0 || cols == 0 || rows > WS_GRID_PITCH || cols > WS_GRID_PITCH)
        return false;                          /* menu, or level torn down */

    obs->valid = true;
    obs->frame = clock_frame();
    obs->rows  = rows;
    obs->cols  = cols;
    obs->grid  = g_grid;

    for (unsigned u = 0; u < cols; u++) {
        for (unsigned v = 0; v < rows; v++) {
            const BYTE *t  = tile_at(g, u, v);
            const BYTE *p2 = plane2_at(g, u, v);
            WsTile *o = &g_grid[v + u * WS_GRID_PITCH];
            o->kind     = t[0];
            o->param    = t[1];
            o->contents = t[2];
            o->occupant = t[8];
            o->height   = t[-1];
            memcpy(&o->height_f, t + 9, sizeof(float));
            o->spawn_a  = p2[0];
            o->spawn_b  = p2[2];
            o->spawn    = p2[3];
        }
    }

    obs->player_facing = g[OFF_PLR_FACING];
    obs->player_moving = (BYTE)*(const DWORD *)(g + OFF_PLR_MOVING);
    memcpy(obs->player_grid,  g + OFF_PLR_GRIDF, sizeof(obs->player_grid));
    memcpy(obs->player_world, g + OFF_PLR_WORLD, sizeof(obs->player_world));
    memcpy(obs->player_cell,  g + OFF_PLR_CELL,  sizeof(obs->player_cell));
    memcpy(obs->exit_cell,    g + OFF_EXIT_CELL, sizeof(obs->exit_cell));

    obs->n_foes    = read_table(g, OFF_FOE_PTRS, OFF_FOE_COUNT, OFF_FOE_IDS,
                                true,  obs->foes);
    obs->n_enemies = read_table(g, OFF_ENE_PTRS, OFF_ENE_COUNT, OFF_ENE_IDS,
                                false, obs->enemies);

    obs->gems_collected    = *(const int   *)(g + OFF_GEMS_GOT);
    obs->gems_required     = *(const int   *)(g + OFF_GEMS_REQ);
    obs->foes_killed       = g[OFF_FOES_KILL];
    obs->lives             = g[OFF_LIVES];
    obs->level_complete    = *(const int   *)(g + OFF_COMPLETE);
    obs->crystals_in_level = *(const WORD  *)(g + OFF_CRYSTALS);
    return true;
}

/* Tile kinds 5..8 are ramps: FUN_0041f8a0(k) == (4 < k && k < 9), and the
 * movement code uses that predicate to decide whether a height change is a
 * climb or a wall. */
static inline bool ws_is_ramp(BYTE kind) { return kind > 4 && kind < 9; }

bool ws_passable(const Observation *o, int fu, int fv, int tu, int tv)
{
    if (tu < 0 || tv < 0 || tu >= o->cols || tv >= o->rows) return false;
    if (fu < 0 || fv < 0 || fu >= o->cols || fv >= o->rows) return false;

    const WsTile *from = &o->grid[fv + fu * WS_GRID_PITCH];
    const WsTile *to   = &o->grid[tv + tu * WS_GRID_PITCH];

    if (to->kind == 0)      return false;   /* no floor — confirmed by falling into one */
    if (to->occupant != 0)  return false;   /* an object or a foe is standing there */
    if (to->kind == 0x16)   return false;
    if (to->kind == 0x17)   return false;   /* gated on tile+0x7a, which is not read */

    /* Climbing.
     *
     * Exempting a step whenever *either* tile was a ramp was far too loose,
     * and it killed a run: the policy routed up a ledge it could only ever
     * drop off.  The movement code is specific — it permits the climb only
     * when the tile being LEFT is a ramp whose orientation matches the
     * direction of travel:
     *
     *   if (move_dir == cur.kind - 4 || move_dir == turn(cur.kind - 4, 2))
     *       if (cur.height < dest.height) ... climb ...
     *   else if (dest.height == cur.height + 1) cancel;
     *
     * so the ramp's own facing matters, and it is the source tile that counts.
     */
    if (to->height > from->height) {
        if (!ws_is_ramp(from->kind)) return false;
        int dir = 0;
        for (int d = WS_DIR_MIN; d <= WS_DIR_MAX; d++)
            if (fu + WS_DIR_DU[d] == tu && fv + WS_DIR_DV[d] == tv) { dir = d; break; }
        int ramp = from->kind - 4;                 /* 1..4 */
        int back = ((ramp - 1 + 2) % 4) + 1;       /* its opposite */
        if (dir != ramp && dir != back) return false;
    }

    /* Falling: a drop of three or more kills.  Nothing in the search stopped
     * this before, so a route could walk the player off a lethal ledge and the
     * policy would cheerfully take it. */
    if (to->kind != WS_TILE_SOFT_LAND &&
        from->height > to->height + WS_MAX_SAFE_DROP)
        return false;

    return true;
}

const Observation *worldstate_latest(void) { return g_obs_valid ? &g_obs : NULL; }

/* ── Stage 1: map dump, with its own cross-check ────────────────────────── */

/* The check that makes this dump worth trusting.
 *
 * `Game+0x42252` is the game's own count of crystals in the level, accumulated
 * by SetupLevelObjects and — confirmed by scanning every write to the field —
 * never touched again during play.  Recomputing it from the grid is therefore
 * a second, independent reading of the same quantity, and the two disagreeing
 * means the index arithmetic is wrong.
 *
 * It is NOT simply the number of tiles with contents == 1.  SetupLevelObjects
 * zeroes a tile's contents byte when it spawns a foe there, and separately
 * increments the counter for a foe spawned with parameter 0x0b (a crystal
 * carrier).  So the grid-side total has to add those foes back.  The
 * distinguishing field is foe+0x62, which holds the spawn parameter verbatim;
 * foe+0x15a is *not* usable here because it reads 1 for both the 0x0b and the
 * 0x07 spawn, only one of which counts.
 *
 * foe+0x62 is not in WsEntity — it is read directly here rather than widening
 * the struct for one diagnostic.
 */
static void map_check(const BYTE *g, const Observation *obs)
{
    unsigned from_tiles = 0;
    for (unsigned u = 0; u < obs->cols; u++)
        for (unsigned v = 0; v < obs->rows; v++)
            if (g_grid[v + u * WS_GRID_PITCH].contents == 1) from_tiles++;

    unsigned from_foes = 0;
    const BYTE **ptrs = (const BYTE **)(g + OFF_FOE_PTRS);
    for (unsigned i = 0; i < obs->n_foes; i++) {
        const BYTE *obj = ptrs[obs->foes[i].slot];
        if (obj && obj[0x62] == 0x0b) from_foes++;
    }

    unsigned total = from_tiles + from_foes;
    bool ok = (total == obs->crystals_in_level);
    log_write("worldstate: map check: %u tile crystals + %u carrier foes = %u, "
              "game says %u — %s\n",
              from_tiles, from_foes, total, (unsigned)obs->crystals_in_level,
              ok ? "MATCH" : "*** MISMATCH: grid indexing is wrong ***");

    /* The player must be standing somewhere on the board. */
    if (obs->player_cell[0] >= obs->cols || obs->player_cell[1] >= obs->rows)
        log_write("worldstate: map check: *** player cell (%u,%u) outside "
                  "grid %ux%u ***\n",
                  obs->player_cell[0], obs->player_cell[1],
                  obs->cols, obs->rows);
}

static void map_dump(const BYTE *g, const Observation *obs, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        log_write("worldstate: map dump: cannot open %s\n", path);
        return;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"frame\": %lu,\n", (unsigned long)obs->frame);
    fprintf(fp, "  \"cols_u\": %u,\n", obs->cols);
    fprintf(fp, "  \"rows_v\": %u,\n", obs->rows);
    fprintf(fp, "  \"pitch\": %u,\n", (unsigned)WS_GRID_PITCH);
    fprintf(fp, "  \"crystals_in_level\": %u,\n", (unsigned)obs->crystals_in_level);
    fprintf(fp, "  \"gems_required\": %d,\n", obs->gems_required);
    fprintf(fp, "  \"player_cell\": [%u, %u, %u],\n",
            obs->player_cell[0], obs->player_cell[1], obs->player_cell[2]);
    fprintf(fp, "  \"_axes\": \"index = v + u*pitch; float triples are (u, h, v)\",\n");
    fprintf(fp, "  \"_fields\": [\"kind\", \"param\", \"contents\", \"height\", "
                "\"spawn\", \"spawn_a\", \"spawn_b\"],\n");
    fprintf(fp, "  \"tiles\": [\n");
    for (unsigned u = 0; u < obs->cols; u++) {
        fprintf(fp, "    [");
        for (unsigned v = 0; v < obs->rows; v++) {
            const WsTile *t = &g_grid[v + u * WS_GRID_PITCH];
            fprintf(fp, "%s[%u,%u,%u,%u,%u,%u,%u]", v ? "," : "",
                    t->kind, t->param, t->contents, t->height,
                    t->spawn, t->spawn_a, t->spawn_b);
        }
        fprintf(fp, "]%s\n", u + 1 < obs->cols ? "," : "");
    }
    fprintf(fp, "  ]\n}\n");
    fclose(fp);

    log_write("worldstate: map dump: %ux%u grid written to %s\n",
              obs->cols, obs->rows, path);
    map_check(g, obs);
}

/* ── Stage 2: per-frame entity trace ────────────────────────────────────── */

/* The grid/float agreement test, run every frame on every entity.
 *
 * An entity's float position and its grid bytes are written from the same two
 * numbers at spawn and maintained separately afterwards, so they should track
 * each other to within a tile for the whole level.  If the (U, H, V) ordering
 * were wrong, this diverges immediately and by a lot — which is the point:
 * it settles the ordering in numbers rather than by eye, and it keeps
 * settling it on every level anyone runs.
 *
 * The tolerance is deliberately loose.  This is a "the axes are not
 * transposed" check, not a physics assertion.
 */
#define WS_AXIS_TOL 2.0f

static bool axis_ok(float f, BYTE cell)
{
    float d = f - (float)cell;
    if (d < 0) d = -d;
    return d <= WS_AXIS_TOL;
}

static void trace_entities(const Observation *obs, const char *tag,
                           const WsEntity *ents, unsigned n, bool foe)
{
    for (unsigned i = 0; i < n; i++) {
        const WsEntity *e = &ents[i];
        bool inb = (e->gu < obs->cols && e->gv < obs->rows);
        bool ax  = axis_ok(e->pos[0], e->gu) && axis_ok(e->pos[2], e->gv);
        log_write("entity: f=%lu %s[%u] slot=%u kind=%u face=%u cat=%u "
                  "cell=(%u,%u,%u) pos=(%.3f,%.3f,%.3f) hid=%lu%s%s\n",
                  (unsigned long)obs->frame, tag, i, e->slot,
                  e->kind, e->facing, e->category,
                  e->gu, e->gv, e->gh,
                  e->pos[0], e->pos[1], e->pos[2],
                  (unsigned long)e->hidden,
                  inb ? "" : "  *** CELL OUT OF BOUNDS ***",
                  ax  ? "" : "  *** POS/CELL DISAGREE (axis order?) ***");
        (void)foe;
    }
}

static void trace_frame(const Observation *obs)
{
    log_write("entity: f=%lu mode=%u grid=%ux%u player cell=(%u,%u,%u) face=%u "
              "gridf=(%.3f,%.3f,%.3f) world=(%.3f,%.3f,%.3f) "
              "foes=%u enemies=%u killed=%u gems=%d/%d\n",
              (unsigned long)obs->frame, (unsigned)obs->mode,
              obs->cols, obs->rows,
              obs->player_cell[0], obs->player_cell[1], obs->player_cell[2],
              obs->player_facing,
              obs->player_grid[0],  obs->player_grid[1],  obs->player_grid[2],
              obs->player_world[0], obs->player_world[1], obs->player_world[2],
              obs->n_foes, obs->n_enemies, (unsigned)obs->foes_killed,
              obs->gems_collected, obs->gems_required);
    trace_entities(obs, "foe",   obs->foes,    obs->n_foes,    true);
    trace_entities(obs, "enemy", obs->enemies, obs->n_enemies, false);
}

/* ── Stage 3: observation dump ──────────────────────────────────────────── */

static void obs_dump_line(FILE *fp, const Observation *obs)
{
    fprintf(fp, "{\"frame\":%lu,\"cols\":%u,\"rows\":%u,"
                "\"player_cell\":[%u,%u,%u],"
                "\"player_grid\":[%.6f,%.6f,%.6f],"
                "\"player_world\":[%.6f,%.6f,%.6f],"
                "\"gems\":[%d,%d],\"foes_killed\":%u,\"lives\":%u,"
                "\"complete\":%d,\"entities\":[",
            (unsigned long)obs->frame, obs->cols, obs->rows,
            obs->player_cell[0], obs->player_cell[1], obs->player_cell[2],
            obs->player_grid[0],  obs->player_grid[1],  obs->player_grid[2],
            obs->player_world[0], obs->player_world[1], obs->player_world[2],
            obs->gems_collected, obs->gems_required,
            (unsigned)obs->foes_killed, (unsigned)obs->lives,
            obs->level_complete);

    bool first = true;
    for (unsigned pass = 0; pass < 2; pass++) {
        const WsEntity *v = pass ? obs->enemies : obs->foes;
        unsigned n        = pass ? obs->n_enemies : obs->n_foes;
        for (unsigned i = 0; i < n; i++) {
            fprintf(fp, "%s{\"t\":\"%s\",\"slot\":%u,\"kind\":%u,"
                        "\"cell\":[%u,%u,%u],\"pos\":[%.6f,%.6f,%.6f]}",
                    first ? "" : ",", pass ? "enemy" : "foe",
                    v[i].slot, v[i].kind, v[i].gu, v[i].gv, v[i].gh,
                    v[i].pos[0], v[i].pos[1], v[i].pos[2]);
            first = false;
        }
    }
    fprintf(fp, "]}\n");
}

/* ── frame hook ─────────────────────────────────────────────────────────── */

static int   g_trace = -1, g_obsdump = -1;
static char  g_map_path[MAX_PATH], g_obs_path[MAX_PATH];
static bool  g_map_wanted, g_map_done;
static FILE *g_obs_fp;

static void worldstate_init(void)
{
    g_trace   = env_flag("KAROO_ENTITY_TRACE");
    g_map_wanted = env_path("KAROO_MAP_DUMP", g_map_path, sizeof(g_map_path));
    g_obsdump = env_path("KAROO_OBS_DUMP", g_obs_path, sizeof(g_obs_path));
    log_write("worldstate: trace=%s map_dump=%s obs_dump=%s\n",
              g_trace ? "on" : "off",
              g_map_wanted ? g_map_path : "off",
              g_obsdump ? g_obs_path : "off");
}

void worldstate_tick(void)
{
    if (g_trace < 0) worldstate_init();
    /* The policy is a consumer too — without this it silently gets no
     * observation at all and quietly does nothing, which is exactly how the
     * first Stage 4 run failed: the policy loaded, logged that it was active,
     * and never pressed a key. */
    if (!g_trace && !g_map_wanted && !g_obsdump && !policy_active()) return;

    /* mode is the confirmed "in a level" signal (gamestate.cpp Stage B): it
     * goes 0 -> 1 on level start and back at the end.  Everything below is
     * gated on it so menus do not produce a map dump of a stale grid. */
    Observation *obs = &g_obs;
    unsigned short mode = gamestate_mode();
    if (mode == 0) {
        g_map_done = false;            /* re-arm for the next level */
        return;
    }
    if (!worldstate_observe(obs)) return;
    obs->mode   = mode;
    g_obs_valid = true;

    if (g_map_wanted && !g_map_done) {
        g_map_done = true;
        map_dump((const BYTE *)*GAME_GLOBAL_PTR, obs, g_map_path);
    }
    if (g_trace) trace_frame(obs);
    if (g_obsdump) {
        if (!g_obs_fp) g_obs_fp = fopen(g_obs_path, "w");
        if (g_obs_fp) { obs_dump_line(g_obs_fp, obs); fflush(g_obs_fp); }
    }
}
