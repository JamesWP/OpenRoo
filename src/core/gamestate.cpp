/* Game-state reader (REPLAY_PLAN.md Stage B).
 *
 * Field widths and arithmetic below come from decompiling
 * Game::CalculateLevelScore (0x0041A760), which reads every field the score
 * screen shows.  That decompile corrected three things in the plan's table:
 *
 *   - +0x4224D (foes killed) is a *byte*, not a dword — CalculateLevelScore
 *     reads it as `(uint)*(byte *)(this + 0x4224d) * 0x32`.  Same for
 *     +0x170A64 (health), also a byte.
 *   - +0x140522 is the gem *surplus*, not "gems missed": it is written as
 *     collected - required only on the branch where collected exceeds
 *     required, alongside a (collected - required) * 10 bonus at +0x140506.
 *   - There is a scored pair the plan's table omits entirely — a ushort at
 *     +0x42250 scored at x5 into +0x140512, capped by a ushort at +0x1753E3
 *     and suppressed by a byte flag at +0x4220B.
 *
 * +0x170A64 is NOT health.  The plan called it "health / energy, clamped to
 * 100", but the game has no damage model at all — you die outright on a fall
 * or on contact with a foe.  It is added straight into the level score as a
 * bonus term, which fits James's reading of it as "vitality", a speed-like
 * bonus for moving quickly.  That is a hypothesis, not a result: it is logged
 * as vit=(?) and confirmed by watching it rise while moving fast.
 *
 * CONFIRMED IN GAME (2026-08-30, Forest\BombStart, James playing):
 *   +0x175406 gems collected  — stepped 0..10, one per pickup
 *   +0x04224D foes killed     — stepped 0..4, one per kill (byte)
 *   +0x170A64 vitality        — varies continuously with movement, not health
 *   +0x1752B8 level complete  — 0 -> 1 on reaching the exit
 *   +0x1751EE player position — 1606 distinct values, smooth per-frame motion
 *   +0x42250 / +0x1753E3      — items available / items collected; the second
 *                               counts BOTH bombs and gems (3 + 10 = 13)
 * The level score reproduces exactly from these six terms, which is the real
 * proof — see the formula in REPLAY_PLAN.md.
 *
 * +0x175402 IS lives, confirmed 2026-08-30: it read 2 through a new game and
 * stepped to 1 at the respawn, matching DEC EAX / store at 0x004160D6.  It had
 * read 0 in two earlier runs simply because those saves had no lives left —
 * a reminder that "the field never moved" is not evidence when the value was
 * already at its floor.  The INC at 0x0041B23D is consistent with an
 * extra-life pickup granting one.
 *
 * STILL UNRESOLVED:
 *   +0x1752E8 "death"  — pulsed to 0x00000100 six times during clean play with
 *                        no deaths, so the live byte is +0x1752E9 and the
 *                        plan's "death / time-out" label is at best misaligned.
 *                        Logged as 4 raw bytes.
 *
 * The gem reading it(self) (collected at +0x175406, required at +0x2AB723) is
 * consistent with the arithmetic — the score is min(collected, required) * 5 —
 * but that is still an inference from the shape of the code.  Everything here
 * is logged, nothing is asserted, until a play session shows the counter move
 * on an actual pickup.  Fields whose meaning is not yet confirmed in game are
 * marked (?) in the log so a reader cannot mistake a guess for a result.
 */
#include "gamestate.h"
#include "log.h"
#include "game.h"
#include "player.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


struct GameState {
    int   gems_collected;   // Player +0x23d (Game+0x175406)  dword
    int   gems_required;    // +0x2ab723  dword
    BYTE  foes_killed;      // +0x4224d   byte
    int   time_limit_s;     // +0x2ab591  dword
    DWORD elapsed_ms;       // +0x2ab595  dword
    BYTE  lives;            // Player +0x239 (Game+0x175402)  CONFIRMED lives remaining
    int   total_score;      // Player +0x22c (Game+0x1753f5)  dword
    int   level_score;      // +0x140536  dword
    BYTE  vitality;         // +0x170a64  byte (?) — see note above
    BYTE  death_raw[4];     // Player +0x11f..+0x122 (Game+0x1752e8)  width unknown; live byte is +0x1752e9 (?)
    int   complete_flag;    // Player +0xef (Game+0x1752b8)  dword — CONFIRMED: 0 -> 1 on level exit
    WORD  extra_count;      // +0x42250   ushort
    WORD  extra_cap;        // Player +0x21a (Game+0x1753e3)  ushort
    BYTE  extra_block;      // +0x4220b   byte
    float pos[3];           // Player +0x25 (Game+0x1751ee)  float[3] (?)
    unsigned short mode;    // game_state passed to DispatchInputActions
};

/* ── Death diff (field finder) ─────────────────────────────────────────────
 *
 * Guessing field meanings one at a time is slow and, as +0x175402 showed,
 * wrong: it is INC'd at 0x0041B23D and DEC'd in GameTick at 0x004160D6, yet it
 * read 0 for a whole level in which bombs were collected and spent.
 *
 * (That INC was attributed here to a "boommaker" name match, which is wrong.
 * 0x0041B23D is in the typed-cheat handler FUN_0041aca0, and the code that
 * reaches it is "mausuruh" — +1 life.  "boommaker" is the adjacent compare and
 * adds 10 to a different field, Game+0x1752b1.  No *pickup* writes 0x175402 at
 * all: its only writers are level init, this cheat, the death decrement and
 * save-slot restore.)
 *
 * So instead of naming candidates up front, snapshot the
 * whole Game object during play and diff it the moment a death registers.  A
 * lives counter is then simply a dword that dropped by exactly 1 across the
 * death, and it names it(self).
 *
 * Scan byte-wise, not dword-wise.  The first version compared aligned dwords
 * only, and that very nearly lost the answer: lives is a byte, so its 2 -> 1
 * step showed up as the dword at +0x175400 moving 147624 -> 82088, a delta of
 * -65536 buried among the large-delta noise instead of being flagged as a
 * step.  Byte granularity is what makes a counter announce it(self).
 *
 * KAROO_DEATH_DIFF=1 enables it.  The snapshot refreshes every SNAP_EVERY
 * frames while alive, so the diff window is short and the noise stays low.
 *
 * The first version diffed only at the moment of death, and that was not
 * enough: across two real deaths it found no dword stepping by 1, because the
 * game does not decrement lives when you die — it does it during the restart
 * (GameTick 0x004160D6 does DEC EAX / store / call 0x004184A0).  So there is a
 * second report REPORT_AFTER frames after the death cause clears, diffed
 * against the same pre-death snapshot, which brackets the whole death ->
 * restart cycle.
 */
#define GAME_SIZE   0x51790d
#define SNAP_EVERY   30
#define DIFF_MAX     120
#define REPORT_AFTER 45   /* frames after respawn for the second report */

static BYTE *g_snap;
static int   g_diff_on = -1;
static BYTE  g_prev_death;
static DWORD g_respawn_at;   /* frame the death cause cleared; 0 = idle */

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

/* Report what differs between the snapshot and the live object.  Bytes that
 * moved by exactly -1 or +1 are listed first: that is what a life, a bomb
 * count or an attempt counter looks like across a single death.  Everything
 * else is reported as dwords, which reads better for pointers and floats. */
static void deathdiff_report(const BYTE *game, unsigned cause, const char *when)
{
    int shown = 0, delta1 = 0;
    log_write("deathdiff: === %s (cause=%u) — dwords changed vs pre-death snapshot ===\n",
              when, cause);

    /* Pass 0: byte-granular +/-1 steps — the counters. */
    for (DWORD o = 0; o < GAME_SIZE && shown < DIFF_MAX; o++) {
        int a = g_snap[o], b = game[o];
        int d = b - a;
        if (d != 1 && d != -1) continue;
        log_write("deathdiff:   +0x%06lx  byte %d -> %d  (%+d)  <-- step\n",
                  (unsigned long)o, a, b, d);
        shown++; delta1++;
    }
    /* Pass 1: everything else, as dwords. */
    for (DWORD o = 0; o + 4 <= GAME_SIZE && shown < DIFF_MAX; o += 4) {
        int a = *(const int *)(g_snap + o);
        int b = *(const int *)(game   + o);
        if (a == b) continue;
        bool bytestep = false;
        for (int i = 0; i < 4; i++) {
            int d = (int)game[o + i] - (int)g_snap[o + i];
            if (d == 1 || d == -1) bytestep = true;
        }
        if (bytestep) continue;          /* already reported above */
        log_write("deathdiff:   +0x%06lx  %d -> %d  (%+d)\n",
                  (unsigned long)o, a, b, b - a);
        shown++;
    }
    log_write("deathdiff: === %d shown, %d of them byte +/-1 steps ===\n", shown, delta1);
}

static int       g_on = -1;
static GameState g_prev;
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
    {   /* four bytes from +0x11f: the move state and the low three of +0x120 */
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

/* Last state seen while a level was actually running.
 *
 * The end-of-run dump cannot simply read the live object: a recording usually
 * ends with the player quitting, and by the time the process is shutting down
 * the level is torn down and the score fields are gone.  Nor can it wait for
 * the recording to be exhausted — the first real recording ends by quitting
 * the game, ~110 frames before its own last record.  So cache every in-level
 * frame and dump the last one, which is the end state of the gameplay
 * segment, which is what a test wants to assert on.
 *
 * mode != 0 is the "in a level" test: Stage B confirmed mode goes 0 -> 1 on
 * level start and 1 -> 0 at the end. */
static GameState g_live;
static bool      g_have_live;
static DWORD     g_live_frame;

/* State at the moment a level was completed.
 *
 * The last in-level frame is NOT the end of the level under test as soon as a
 * recording carries on into the next one -- the water01 recording completes
 * Water01 and then launches the following level, so its last in-level frame
 * reads gems 0/12 and complete=0, which is the *next* level starting.  A test
 * that blessed that would assert nothing about the level it named.
 *
 * So latch separately on the completion flag.  The latch refreshes while the
 * flag stays set rather than freezing on the first frame of it, because
 * CalculateLevelScore writes level_score and the new running total a frame or
 * two after the flag flips -- latching the leading edge would capture a score
 * that had not been computed yet.  It locks when the flag clears, so a later
 * level's completion cannot overwrite the first one. */
static GameState g_done;
static bool      g_have_done;
static DWORD     g_done_frame;
static int       g_done_phase;   /* 0 never seen, 1 in progress, 2 locked */

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
        if (g_done_phase != 2) {          /* refresh until the flag clears */
            g_done       = s;
            g_have_done  = true;
            g_done_frame = g_frame;
            g_done_phase = 1;
        }
    } else if (g_done_phase == 1) {
        g_done_phase = 2;                 /* lock: first completion wins */
        log_write("gamestate: level completed at frame %lu - latched "
                  "(score=%d total=%d gems=%d/%d t=%lus)\n",
                  (unsigned long)g_done_frame, g_done.level_score,
                  g_done.total_score, g_done.gems_collected, g_done.gems_required,
                  (unsigned long)(g_done.elapsed_ms / 1000));
    }

    if (!gamestate_enabled()) return;

    /* Elapsed time moves every frame; comparing it would log every frame and
     * bury the events worth seeing.  It is still printed on each line. */
    GameState a = s, b = g_prev;
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

    g_prev      = s;
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
        g_respawn_at = g_frame;                 /* restart began — hold the snapshot */
    } else if (g_respawn_at && g_frame - g_respawn_at >= REPORT_AFTER) {
        /* The decrement happens in here, not at the death it(self). */
        deathdiff_report(game, g_prev_death, "after respawn");
        g_respawn_at = 0;
    } else if (cause == 0 && !g_respawn_at && (g_frame % SNAP_EVERY) == 0) {
        memcpy(g_snap, game, GAME_SIZE);        /* alive — refresh window */
    }

    g_prev_death = cause;
}


/* ── Stage E: end-of-run state dump ───────────────────────────────────────
 *
 * Written at the end of a replay, while the level is still live — after
 * teardown the score fields are gone.  Emitted as JSON so the harness can diff
 * it field by field and name the field that moved, rather than reporting only
 * that the run differed.
 *
 * Every field here is one from the table above.  The ones still marked (?) in
 * the log are dumped too, with an "_unconfirmed" list naming them, so a test
 * that asserts on one is doing so knowingly.
 */
void gamestate_dump(const char *reason)
{
    static bool dumped = false;
    if (dumped) return;              /* the first (earliest, most live) wins */

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
