/* The game-state reader.  Each frame it reads the fields the replay tests
 * assert on (the ones the level score is built from, plus lives, position and
 * the game mode), logs them when they change (KAROO_STATE_LOG), and at the end
 * of a replay dumps them as JSON (KAROO_STATE_DUMP) for replaytest.py.
 *
 * What the fields mean was confirmed by watching them in play: gems and foes
 * step by one per pickup and kill, lives drops by one at the restart after a
 * death, the complete flag goes 0 to 1 at the exit, and the level score is
 * reproduced exactly from the scored terms.  Three are not confirmed and are
 * marked so in the log and the dump:
 *   - vitality: a byte added straight into the level score.  It varies with
 *     movement; it is not health, as the game has no damage, only deaths;
 *   - death_raw: four raw bytes from the player's move state;
 *   - pos: the player's position. */

#include "gamestate.h"
#include "log.h"
#include "game.h"
#include "player.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct GameState {
    int   gems_collected;  // dword
    int   gems_required;   // dword
    BYTE  foes_killed;     // byte
    int   time_limit_s;
    DWORD elapsed_ms;
    BYTE  lives;  // drops at the restart after a death
    int   total_score;
    int   level_score;
    BYTE  vitality;       // unconfirmed; see above
    BYTE  death_raw[4];   // unconfirmed: the move state and the low three bytes of falling
    int   complete_flag;  // 0 to 1 at the exit
    WORD  extra_count;    // items available
    WORD  extra_cap;      // items collected: bombs and gems both
    BYTE  extra_block;    // restarts; any suppresses the items bonus
    float pos[3];         // unconfirmed
    unsigned short mode;  // the mode handed to the input dispatch
};

/* The death diff (KAROO_DEATH_DIFF=1), a field finder: rather than guess a
 * field's meaning, snapshot the whole Game while alive and diff it across a
 * death, so that a counter names itself by stepping by one.
 *
 * Two reports are made per death: one at the death, and one REPORT_AFTER
 * frames after the death cause clears, against the same snapshot.  The second
 * is needed because lives is decremented during the restart, not at the death.
 * The snapshot refreshes every SNAP_EVERY frames while alive, which keeps the
 * window, and the noise, small.
 *
 * The scan is by byte, not by dword: lives is a byte, and as a dword its step
 * of one reads as a change of 65536, lost among the other large changes. */
#define GAME_SIZE   0x51790d
#define SNAP_EVERY   30
#define DIFF_MAX     120
#define REPORT_AFTER 45  // frames after the respawn for the second report

static BYTE *g_snap;
static int   g_diff_on = -1;
static BYTE  g_prev_death;
static DWORD g_respawn_at;  // the frame the death cause cleared; 0 is idle

static bool deathdiff_enabled(void)
{
    if (g_diff_on < 0) {
        char buf[16];
        g_diff_on = 0;
        if (GetEnvironmentVariableA("KAROO_DEATH_DIFF", buf, sizeof(buf)) && buf[0] && buf[0] != '0') {
            g_snap = (BYTE *)VirtualAlloc(NULL, GAME_SIZE, MEM_COMMIT, PAGE_READWRITE);
            g_diff_on = (g_snap != NULL);
        }
        log_write("gamestate: death diff %s\n", g_diff_on ? "enabled" : "disabled");
    }
    return g_diff_on > 0;
}

/* Reports what differs between the snapshot and the live object.  Bytes that
 * moved by exactly one come first: that is what a life, a bomb count or an
 * attempt counter looks like across a death.  The rest are listed as dwords,
 * which reads better for pointers and floats. */
static void deathdiff_report(const BYTE *game, unsigned cause, const char *when)
{
    int shown = 0, delta1 = 0;
    log_write("deathdiff: === %s (cause=%u) — dwords changed vs pre-death snapshot ===\n",
              when, cause);

    // Byte steps of one: the counters.
    for (DWORD o = 0; o < GAME_SIZE && shown < DIFF_MAX; o++) {
        int a = g_snap[o], b = game[o];
        int d = b - a;
        if (d != 1 && d != -1) continue;
        log_write("deathdiff:   +0x%06lx  byte %d -> %d  (%+d)  <-- step\n",
                  (unsigned long)o, a, b, d);
        shown++; delta1++;
    }
    // Everything else, as dwords.
    for (DWORD o = 0; o + 4 <= GAME_SIZE && shown < DIFF_MAX; o += 4) {
        int a = *(const int *)(g_snap + o);
        int b = *(const int *)(game   + o);
        if (a == b) continue;
        bool bytestep = false;
        for (int i = 0; i < 4; i++) {
            int d = (int)game[o + i] - (int)g_snap[o + i];
            if (d == 1 || d == -1) bytestep = true;
        }
        if (bytestep) continue;  // reported above
        log_write("deathdiff:   +0x%06lx  %d -> %d  (%+d)\n",
                  (unsigned long)o, a, b, b - a);
        shown++;
    }
    log_write("deathdiff: === %d shown, %d of them byte +/-1 steps ===\n", shown, delta1);
}

static int       g_on = -1;
static GameState g_prevClock;
static bool      g_have_prev;
static unsigned short g_mode;
static DWORD     g_frame;

bool gamestate_enabled(void)
{
    if (g_on < 0) {
        char buf[16];
        g_on = 0;
        if (GetEnvironmentVariableA("KAROO_STATE_LOG", buf, sizeof(buf)) && buf[0] && buf[0] != '0')
            g_on = 1;
        log_write("gamestate: state log %s\n", g_on ? "enabled" : "disabled");
    }
    return g_on > 0;
}

void gamestate_note_mode(unsigned short mode) { g_mode = mode; }
unsigned short gamestate_mode(void) { return g_mode; }

static bool read_state(GameState *s)
{
    const Game *g = Game::instance();
    if (!g) return false;

    const Player *pl = g->player();
    s->gems_collected = pl->gemsCollected();
    s->gems_required  = g->gemsRequired();
    s->foes_killed    = g->foesKilled();
    s->time_limit_s   = g->timeLimit();
    s->elapsed_ms     = g->timeElapsed();
    s->lives          = (BYTE)pl->lives();
    s->total_score    = pl->score();
    s->level_score    = g->tally()->levelTotal;
    s->vitality       = g->vitalityPercent();
    {
        int f120 = pl->falling();
        s->death_raw[0] = pl->moveState();
        memcpy(s->death_raw + 1, &f120, 3);
    }
    s->complete_flag  = pl->held();
    s->extra_count    = g->itemTotal();
    s->extra_cap      = pl->itemsCollected();
    s->extra_block    = g->restartCount();
    s->pos[0]         = pl->posU();
    s->pos[1]         = pl->posY();
    s->pos[2]         = pl->posV();
    s->mode           = g_mode;
    return true;
}

/* The last state read while a level was running (mode not 0).  The dump cannot
 * read the live object: a recording usually ends by quitting, and by then the
 * level is torn down.  So each in-level frame is kept and the last is dumped:
 * the end of the gameplay. */
static GameState g_live;
static bool      g_have_live;
static DWORD     g_live_frame;

/* The state when a level was completed.  A recording that carries on into the
 * next level ends in that level, so its last in-level frame says nothing about
 * the level it names.  This latch refreshes while the complete flag stays set,
 * because the score is written a frame or two after the flag, and locks when
 * the flag clears, so a later level cannot overwrite it. */
static GameState g_done;
static bool      g_have_done;
static DWORD     g_done_frame;
static int       g_done_phase;  // 0 never seen, 1 in progress, 2 locked

void gamestate_tick(void)
{
    g_frame++;

    GameState s;
    if (!read_state(&s)) return;
    if (s.mode != 0) {
        g_live       = s;
        g_have_live  = true;
        g_live_frame = g_frame;
    }

    if (s.complete_flag != 0) {
        if (g_done_phase != 2) {  // refresh until the flag clears
            g_done       = s;
            g_have_done  = true;
            g_done_frame = g_frame;
            g_done_phase = 1;
        }
    } else if (g_done_phase == 1) {
        g_done_phase = 2;  // lock: the first completion wins
        log_write("gamestate: level completed at frame %lu - latched "
                  "(score=%d total=%d gems=%d/%d t=%lus)\n",
                  (unsigned long)g_done_frame, g_done.level_score,
                  g_done.total_score, g_done.gems_collected, g_done.gems_required,
                  (unsigned long)(g_done.elapsed_ms / 1000));
    }

    if (!gamestate_enabled()) return;

    // Elapsed time moves every frame, so it is left out of the comparison; it
    // is still printed on each line.
    GameState a = s, b = g_prevClock;
    a.elapsed_ms = b.elapsed_ms = 0;
    if (g_have_prev && memcmp(&a, &b, sizeof(a)) == 0) return;

    log_write("gamestate: f=%lu mode=%u gems=%d/%d(?) foes=%u vit=%u "
              "score=%d/%d t=%lu/%ds items=%u/%u blk=%u done=%d "
              "lives=%u death[%02x %02x %02x %02x](?) "
              "pos=%.3f,%.3f,%.3f\n",
              (unsigned long)g_frame, (unsigned)s.mode,
              s.gems_collected, s.gems_required, (unsigned)s.foes_killed,
              (unsigned)s.vitality, s.level_score, s.total_score,
              (unsigned long)(s.elapsed_ms / 1000), s.time_limit_s,
              (unsigned)s.extra_cap, (unsigned)s.extra_count,
              (unsigned)s.extra_block, s.complete_flag,
              (unsigned)s.lives,
              s.death_raw[0], s.death_raw[1], s.death_raw[2], s.death_raw[3],
              s.pos[0], s.pos[1], s.pos[2]);

    g_prevClock      = s;
    g_have_prev = true;
}

void gamestate_deathdiff(void)
{
    if (!deathdiff_enabled()) return;
    const BYTE *game = (const BYTE *)Game::instance();
    if (!game) return;

    BYTE cause = ((const Game *)game)->player()->moveState();

    if (cause != 0 && g_prev_death == 0) {
        deathdiff_report(game, cause, "at death");
        g_respawn_at = 0;
    } else if (cause == 0 && g_prev_death != 0) {
        g_respawn_at = g_frame;  // the restart began; hold the snapshot
    } else if (g_respawn_at && g_frame - g_respawn_at >= REPORT_AFTER) {
        // Lives is decremented in the restart, not at the death.
        deathdiff_report(game, g_prev_death, "after respawn");
        g_respawn_at = 0;
    } else if (cause == 0 && !g_respawn_at && (g_frame % SNAP_EVERY) == 0) {
        memcpy(g_snap, game, GAME_SIZE);  // alive: refresh the snapshot
    }

    g_prev_death = cause;
}

/* Writes the dump, as JSON so that the harness can name the field that
 * differs.  The unconfirmed fields are dumped too, and listed under
 * "_unconfirmed", so a test asserting on one does so knowingly. */
void gamestate_dump(const char *reason)
{
    static bool dumped = false;
    if (dumped) return;  // the first, most live, wins

    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("KAROO_STATE_DUMP", path, sizeof(path)) || !path[0])
        return;
    dumped = true;

    GameState s   = g_live;
    bool     have = g_have_live;

    FILE *fp = fopen(path, "w");
    if (!fp) {
        log_write("gamestate: dump: cannot open %s\n", path);
        return;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"reason\": \"%s\",\n", reason);
    fprintf(fp, "  \"frame\": %lu,\n", (unsigned long)g_live_frame);
    fprintf(fp, "  \"frames_run\": %lu,\n", (unsigned long)g_frame);
    fprintf(fp, "  \"game_live\": %s", have ? "true" : "false");
    if (have) {
        fprintf(fp, ",\n");
        fprintf(fp, "  \"mode\": %u,\n",            (unsigned)s.mode);
        fprintf(fp, "  \"gems_collected\": %d,\n",  s.gems_collected);
        fprintf(fp, "  \"gems_required\": %d,\n",   s.gems_required);
        fprintf(fp, "  \"foes_killed\": %u,\n",     (unsigned)s.foes_killed);
        fprintf(fp, "  \"items_collected\": %u,\n", (unsigned)s.extra_cap);
        fprintf(fp, "  \"items_available\": %u,\n", (unsigned)s.extra_count);
        fprintf(fp, "  \"items_bonus_blocked\": %u,\n", (unsigned)s.extra_block);
        fprintf(fp, "  \"vitality\": %u,\n",        (unsigned)s.vitality);
        fprintf(fp, "  \"lives\": %u,\n",           (unsigned)s.lives);
        fprintf(fp, "  \"level_score\": %d,\n",     s.level_score);
        fprintf(fp, "  \"total_score\": %d,\n",     s.total_score);
        fprintf(fp, "  \"time_limit_s\": %d,\n",    s.time_limit_s);
        fprintf(fp, "  \"elapsed_ms\": %lu,\n",     (unsigned long)s.elapsed_ms);
        fprintf(fp, "  \"level_complete\": %d,\n",  s.complete_flag);
        fprintf(fp, "  \"death_cause\": %u,\n",     (unsigned)s.death_raw[0]);
        fprintf(fp, "  \"pos\": [%.6f, %.6f, %.6f],\n", s.pos[0], s.pos[1], s.pos[2]);
        fprintf(fp, "  \"_unconfirmed\": [\"vitality\", \"death_cause\", \"pos\"],\n");
        fprintf(fp, "  \"completed_a_level\": %s,\n", g_have_done ? "true" : "false");
        if (g_have_done) {
            fprintf(fp, "  \"at_completion\": {\n");
            fprintf(fp, "    \"frame\": %lu,\n",           (unsigned long)g_done_frame);
            fprintf(fp, "    \"gems_collected\": %d,\n",   g_done.gems_collected);
            fprintf(fp, "    \"gems_required\": %d,\n",    g_done.gems_required);
            fprintf(fp, "    \"foes_killed\": %u,\n",      (unsigned)g_done.foes_killed);
            fprintf(fp, "    \"items_collected\": %u,\n",  (unsigned)g_done.extra_cap);
            fprintf(fp, "    \"items_available\": %u,\n",  (unsigned)g_done.extra_count);
            fprintf(fp, "    \"items_bonus_blocked\": %u,\n", (unsigned)g_done.extra_block);
            fprintf(fp, "    \"vitality\": %u,\n",         (unsigned)g_done.vitality);
            fprintf(fp, "    \"lives\": %u,\n",            (unsigned)g_done.lives);
            fprintf(fp, "    \"level_score\": %d,\n",      g_done.level_score);
            fprintf(fp, "    \"total_score\": %d,\n",      g_done.total_score);
            fprintf(fp, "    \"time_limit_s\": %d,\n",     g_done.time_limit_s);
            fprintf(fp, "    \"elapsed_ms\": %lu,\n",      (unsigned long)g_done.elapsed_ms);
            fprintf(fp, "    \"level_complete\": %d\n",    g_done.complete_flag);
            fprintf(fp, "  }\n");
        } else {
            fprintf(fp, "  \"at_completion\": null\n");
        }
    } else {
        fprintf(fp, "\n");
    }
    fprintf(fp, "}\n");
    fclose(fp);

    log_write("gamestate: dumped end state (%s, from frame %lu of %lu) to %s\n",
              reason, (unsigned long)g_live_frame, (unsigned long)g_frame, path);
}
