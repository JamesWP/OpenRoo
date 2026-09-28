/* The world-state reader (worldstate.h): the tile grid, the foe and bomb
 * tables and the player, copied out of the Game each frame into an
 * Observation, plus three optional dumps: the map (KAROO_MAP_DUMP), a
 * per-frame entity trace (KAROO_ENTITY_TRACE) and the observations
 * (KAROO_OBS_DUMP).
 *
 * The grid pitch is a fixed 100, not the level's column count: indexing by the
 * live count gives a map that is subtly sheared rather than obviously wrong.
 * Positions are (U, H, V); the map check and the entity trace test that
 * ordering every frame against the grid bytes.
 *
 * Foes and bombs are different classes; only the MovableEntity fields they
 * share are read for both.  Both tables are walked through their live-ID
 * lists, never by scanning the slots, since a dead slot keeps its stale
 * pointer. */

#include "worldstate.h"
#include "gamestate.h"
#include "clock.h"
#include "policy.h"
#include "log.h"
#include "game.h"
#include "player.h"
#include "foe.h"
#include "bomb.h"
#include <stdio.h>
#include <string.h>

static WsTile     g_grid[WS_GRID_PITCH * WS_GRID_PITCH];
static Observation g_obs;
static bool        g_obs_valid;

static bool env_flag(const char *name)
{
    char buf[16];
    return GetEnvironmentVariableA(name, buf, sizeof(buf)) && buf[0] && buf[0] != '0';
}

static bool env_path(const char *name, char *out, DWORD n)
{
    return GetEnvironmentVariableA(name, out, n) != 0 && out[0] != 0;
}

/* Reads one entity: the MovableEntity fields, and for a foe its own.  The cell
 * bytes are signed in the class and copied as raw bytes. */
static void read_entity(const MovableEntity *obj, BYTE slot, WsEntity *e)
{
    memset(e, 0, sizeof(*e));
    e->slot    = slot;
    e->facing  = obj->facing();
    e->moving  = (BYTE)obj->moveDir();
    e->gu      = (BYTE)obj->cellU();
    e->gv      = (BYTE)obj->cellV();
    e->gh      = (BYTE)obj->heightCell();
    e->pos[0]  = obj->posU();
    e->pos[1]  = obj->posY();
    e->pos[2]  = obj->posV();
    e->hidden  = (DWORD)obj->dyingStarted();
    e->subtype = obj->type();
    e->frozen  = (DWORD)obj->held();
}

static void read_foe(const Foe *foe, BYTE slot, WsEntity *e)
{
    read_entity(foe, slot, e);
    e->kind     = foe->kind();
    e->category = foe->dropContents();
    e->su       = foe->homeU();
    e->sv       = foe->homeV();
    e->sh       = foe->homeH();
}

static void read_bomb(const Bomb *bomb, BYTE slot, WsEntity *e)
{
    read_entity(bomb, slot, e);
}

/* One table: its count, its live-ID list and its slots, through Game's
 * accessors for whichever table slot_of names. */
template <typename T>
static unsigned read_table(const Game *g, unsigned char count_in,
                           unsigned char (Game::*id_of)(unsigned int) const,
                           T *(Game::*slot_of)(unsigned int) const,
                           void (*read)(const T *, BYTE, WsEntity *),
                           WsEntity *out)
{
    unsigned count = count_in;
    if (count > WS_MAX_ENT) {
        log_write("worldstate: entity count %u exceeds table capacity %u — clamped\n",
                  count, (unsigned)WS_MAX_ENT);
        count = WS_MAX_ENT;
    }

    unsigned n = 0;
    for (unsigned i = 0; i < count; i++) {
        BYTE slot = (g->*id_of)(i);
        // No range check: a byte ID cannot index past the table.
         (WS_MAX_ENT > 0xff, "a BYTE id could overrun the slots");
        const T *obj = (g->*slot_of)(slot);
        if (!obj) continue;  // a freed slot still in the list
        read(obj, slot, &out[n++]);
    }
    return n;
}

bool Observation::observe()
{
    const BYTE *g = (const BYTE *)Game::instance();
    memset(this, 0, sizeof(*this));
    if (!g) return false;

    const LevelMap *map = ((const Game *)g)->map();
    BYTE rows = map->extentV();
    BYTE cols = map->extentU();
    if (rows == 0 || cols == 0 || rows > WS_GRID_PITCH || cols > WS_GRID_PITCH)
        return false;  // a menu, or the level torn down

    valid = true;
    frame = clock_frame();
    this->rows  = rows;
    this->cols  = cols;
    grid  = g_grid;

    for (unsigned u = 0; u < cols; u++) {
        for (unsigned v = 0; v < rows; v++) {
            const Tile *t  = map->tile((int)u, (int)v);
            const Tile *p2 = map->snapshot((int)u, (int)v);
            WsTile *o = &g_grid[v + u * WS_GRID_PITCH];
            o->kind     = t->objectMarker();
            o->param    = t->param();
            o->contents = t->contents();
            o->occupant = t->occupant();
            o->height   = t->height();
            o->height_f = t->liftLiveHeight();
            o->spent    = (DWORD)t->busy();
            // The snapshot: the cell as the map gave it.
            o->spawn_a  = p2->height();
            o->spawn_b  = p2->param();
            o->spawn    = p2->contents();
        }
    }

    const Player *pl = ((const Game *)g)->player();
    player_facing  = pl->facing();
    player_moving  = (BYTE)pl->moveDir();
    player_grid[0] = pl->posU();
    player_grid[1] = pl->posY();
    player_grid[2] = pl->posV();
    for (int k = 0; k < 3; k++)  // the camera eye, (U, H, V)
        player_world[k] = ((const Game *)g)->cameraEye(k);
    player_cell[0] = (BYTE)pl->cellU();
    player_cell[1] = (BYTE)pl->cellV();
    player_cell[2] = (BYTE)pl->heightCell();
    // The exit: the level's one tile of kind 4.  The level completes when the
    // player stands on it with the gems collected at least the gems required.
    exit_cell[0]   = pl->markerCellU();
    exit_cell[1]   = pl->markerCellV();
    exit_cell[2]   = pl->markerCellH();

    const Game *game = (const Game *)g;
    n_foes    = read_table(game, game->foeCount(), &Game::foeId,
                                &Game::foeSlot, read_foe, foes);
    n_enemies = read_table(game, game->bombCount(), &Game::bombId,
                                &Game::bombSlot, read_bomb, enemies);

    gems_collected    = pl->gemsCollected();
    gems_required     = game->gemsRequired();
    foes_killed       = ((const Game *)g)->foesKilled();
    lives             = (BYTE)pl->lives();
    level_complete    = pl->held();
    crystals_in_level = ((const Game *)g)->field_42252();
    freeze_timer      = (DWORD)pl->effect8Active();
    return true;
}

/* Kinds 5 to 8 are ramps; the movement code uses this test to tell a climb
 * from a wall. */
static inline bool ws_is_ramp(BYTE kind) { return kind > 4 && kind < 9; }

bool Observation::foeOnCell(int u, int v) const
{
    for (unsigned pass = 0; pass < 2; pass++) {
        const WsEntity *e = pass ? enemies : foes;
        unsigned n        = pass ? n_enemies : n_foes;
        for (unsigned i = 0; i < n; i++)
            if (e[i].gu == u && e[i].gv == v) return true;
    }
    return false;
}

bool Observation::wsPassableImpl(int fu, int fv,
                             int tu, int tv, bool ignore_foes) const
{
    if (tu < 0 || tv < 0 || tu >= cols || tv >= rows) return false;
    if (fu < 0 || fv < 0 || fu >= cols || fv >= rows) return false;

    const WsTile *from = &grid[fv + fu * WS_GRID_PITCH];
    const WsTile *to   = &grid[tv + tu * WS_GRID_PITCH];

    if (to->kind == TILE_EMPTY)      return false;  // no floor
    // The occupant byte is set by an entity arriving (its kind) and cleared
    // when it leaves; it is not used for scenery.  PRESERVED: it leaks.  A foe
    // removed on death is not cleared from its tile, so the byte stays set for
    // the rest of the level (and a breakable tile re-arms on it).  A cell is
    // therefore treated as occupied only if a foe or bomb is actually reported
    // there.
    if (to->occupant != 0 && !ignore_foes && foeOnCell(tu, tv))
        return false;
    if (to->kind == TILE_IMPASSABLE)   return false;
    if (to->kind == TILE_DESTRUCTIBLE && to->spent == 0) return false;

    // An unspent glue pad holds whoever stands on it, which with foes about is
    // how the player gets caught.  A spent pad is safe.
    if (to->kind == WS_TILE_GLUE && to->spent == 0) return false;

    // A climb is allowed only when the tile being left is a ramp facing the
    // direction of travel, or facing directly away; it is the source tile that
    // counts.  Otherwise a step one higher is refused.
    if (to->height > from->height) {
        if (!ws_is_ramp(from->kind)) return false;
        int dir = 0;
        for (int d = WS_DIR_MIN; d <= WS_DIR_MAX; d++)
            if (fu + WS_DIR_DU[d] == tu && fv + WS_DIR_DV[d] == tv) { dir = d; break; }
        int ramp = from->kind - 4;            // 1..4
        int back = ((ramp - 1 + 2) % 4) + 1;  // its opposite
        if (dir != ramp && dir != back) return false;
    }

    // A drop of three or more kills, unless onto a jump pad.
    if (to->kind != WS_TILE_SOFT_LAND &&
        from->height > to->height + WS_MAX_SAFE_DROP)
        return false;

    return true;
}

bool Observation::passable(int fu, int fv, int tu, int tv) const
{
    return wsPassableImpl(fu, fv, tu, tv, false);
}

bool Observation::passableIgnoringFoes(int fu, int fv,
                                       int tu, int tv) const
{
    return wsPassableImpl(fu, fv, tu, tv, true);
}

const Observation *worldstate_latest(void) { return g_obs_valid ? &g_obs : NULL; }

/* The map check.  The Game's count of crystals in the level is set by the
 * level builder and never written in play, so recounting it from the grid is
 * an independent reading: if the two differ, the index arithmetic is wrong.
 * It is not just the tiles with crystal contents: the builder clears the
 * contents of a tile where it spawns a foe, and counts a foe spawned with
 * contents 0x0b, a crystal carrier, so those foes are added back.  Their
 * behaviour field holds the spawn contents; the drop-contents field cannot
 * tell 0x0b from 0x07. */
static void map_check(const BYTE *g, const Observation *obs)
{
    unsigned from_tiles = 0;
    for (unsigned u = 0; u < obs->cols; u++)
        for (unsigned v = 0; v < obs->rows; v++)
            if (g_grid[v + u * WS_GRID_PITCH].contents == CONTENTS_CRYSTAL) from_tiles++;

    unsigned from_foes = 0;
    const Game *game = (const Game *)g;
    for (unsigned i = 0; i < obs->n_foes; i++) {
        const Foe *foe = game->foeSlot(obs->foes[i].slot);
        if (foe && foe->type() == 0x0b) from_foes++;
    }

    unsigned total = from_tiles + from_foes;
    bool ok = (total == obs->crystals_in_level);
    log_write("worldstate: map check: %u tile crystals + %u carrier foes = %u, "
              "game says %u — %s\n",
              from_tiles, from_foes, total, (unsigned)obs->crystals_in_level,
              ok ? "MATCH" : "*** MISMATCH: grid indexing is wrong ***");

    // The player must be on the board.
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
    fprintf(fp, "  \"level_index\": %u,\n", (unsigned)((const Game *)g)->levelIndex());
    // Level names contain backslashes, which JSON strings must escape.
    fputs("  \"level_name\": \"", fp);
    for (const char *n = (const char *)(g + 0x173483); *n && n < (const char *)g + 0x173483 + 96; n++) {
        if (*n == '\\' || *n == '"') fputc('\\', fp);
        fputc(*n, fp);
    }
    fputs("\",\n", fp);
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

/* The entity trace's axis check.  An entity's float position and its grid
 * bytes start from the same numbers and are kept separately, so they should
 * agree to within a tile all level; if the (U, H, V) order were wrong they
 * would diverge at once.  The tolerance is loose: this checks the axes are not
 * transposed, not the physics. */
#define WS_AXIS_TOL 2.0f

static bool axis_ok(float f, BYTE cell)
{
    float d = f - (float)cell;
    if (d < 0) d = -d;
    return d <= WS_AXIS_TOL;
}

void Observation::traceEntities(const char *tag,
                           const WsEntity *ents, unsigned n, bool foe) const
{
    for (unsigned i = 0; i < n; i++) {
        const WsEntity *e = &ents[i];
        bool inb = (e->gu < cols && e->gv < rows);
        bool ax  = axis_ok(e->pos[0], e->gu) && axis_ok(e->pos[2], e->gv);
        log_write("entity: f=%lu %s[%u] slot=%u kind=%u face=%u cat=%u "
                  "cell=(%u,%u,%u) pos=(%.3f,%.3f,%.3f) hid=%lu%s%s\n",
                  (unsigned long)frame, tag, i, e->slot,
                  e->kind, e->facing, e->category,
                  e->gu, e->gv, e->gh,
                  e->pos[0], e->pos[1], e->pos[2],
                  (unsigned long)e->hidden,
                  inb ? "" : "  *** CELL OUT OF BOUNDS ***",
                  ax  ? "" : "  *** POS/CELL DISAGREE (axis order?) ***");
        (void)foe;
    }
}

void Observation::traceFrame() const
{
    log_write("entity: f=%lu mode=%u grid=%ux%u player cell=(%u,%u,%u) face=%u "
              "gridf=(%.3f,%.3f,%.3f) world=(%.3f,%.3f,%.3f) "
              "foes=%u enemies=%u killed=%u gems=%d/%d\n",
              (unsigned long)frame, (unsigned)mode,
              cols, rows,
              player_cell[0], player_cell[1], player_cell[2],
              player_facing,
              player_grid[0],  player_grid[1],  player_grid[2],
              player_world[0], player_world[1], player_world[2],
              n_foes, n_enemies, (unsigned)foes_killed,
              gems_collected, gems_required);
    traceEntities("foe",   foes,    n_foes,    true);
    traceEntities("enemy", enemies, n_enemies, false);
}

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

static int   g_trace = -1, g_obsdump = -1;
static char  g_map_path[MAX_PATH], g_obs_path[MAX_PATH];

/* One map dump per run, carrying the level index and name.  Re-dumping on a
 * later level would overwrite it with a different map. */
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
    // The autoplayer needs observations too; without it here it gets none.
    if (!g_trace && !g_map_wanted && !g_obsdump && !policy_active()) return;

    // Only in a level (mode not 0): menus must not dump a stale grid.
    Observation *obs = &g_obs;
    unsigned short mode = gamestate_mode();
    if (mode == 0) return;
    if (!obs->observe()) return;
    obs->mode   = mode;
    g_obs_valid = true;

    if (g_map_wanted && !g_map_done) {
        g_map_done = true;
        map_dump((const BYTE *)Game::instance(), obs, g_map_path);
    }
    if (g_trace) obs->traceFrame();
    if (g_obsdump) {
        if (!g_obs_fp) g_obs_fp = fopen(g_obs_path, "w");
        if (g_obs_fp) { obs_dump_line(g_obs_fp, obs); fflush(g_obs_fp); }
    }
}
