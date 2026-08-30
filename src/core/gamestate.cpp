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
 * STILL UNRESOLVED (the death run should settle both):
 *   +0x175402 "lives"  — read 2 on the menu, then 0 for the whole level with
 *                        no deaths.  Width unknown; logged as 4 raw bytes.
 *   +0x1752E8 "death"  — pulsed to 0x00000100 six times during clean play with
 *                        no deaths, so the live byte is +0x1752E9 and the
 *                        plan's "death / time-out" label is at best misaligned.
 *                        Logged as 4 raw bytes.
 *
 * The gem reading itself (collected at +0x175406, required at +0x2AB723) is
 * consistent with the arithmetic — the score is min(collected, required) * 5 —
 * but that is still an inference from the shape of the code.  Everything here
 * is logged, nothing is asserted, until a play session shows the counter move
 * on an actual pickup.  Fields whose meaning is not yet confirmed in game are
 * marked (?) in the log so a reader cannot mistake a guess for a result.
 */
#include "gamestate.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

#define GAME_GLOBAL_PTR ((void **)0x0046c498)

struct GameState {
    int   gems_collected;   // +0x175406  dword
    int   gems_required;    // +0x2ab723  dword
    BYTE  foes_killed;      // +0x4224d   byte
    int   time_limit_s;     // +0x2ab591  dword
    DWORD elapsed_ms;       // +0x2ab595  dword
    BYTE  lives_raw[4];     // +0x175402  width unknown (?)
    int   total_score;      // +0x1753f5  dword
    int   level_score;      // +0x140536  dword
    BYTE  vitality;         // +0x170a64  byte (?) — see note above
    BYTE  death_raw[4];     // +0x1752e8  width unknown; live byte is +0x1752e9 (?)
    int   complete_flag;    // +0x1752b8  dword — CONFIRMED: 0 -> 1 on level exit
    WORD  extra_count;      // +0x42250   ushort
    WORD  extra_cap;        // +0x1753e3  ushort
    BYTE  extra_block;      // +0x4220b   byte
    float pos[3];           // +0x1751ee  float[3] (?)
    unsigned short mode;    // game_state passed to DispatchInputActions
};

/* ── Death diff (field finder) ─────────────────────────────────────────────
 *
 * Guessing field meanings one at a time is slow and, as +0x175402 showed,
 * wrong: it is INC'd on a "boommaker" name match at 0x0041B23D and DEC'd in
 * GameTick at 0x004160D6, yet it read 0 for a whole level in which bombs were
 * collected and spent.  So instead of naming candidates up front, snapshot the
 * whole Game object during play and diff it the moment a death registers.  A
 * lives counter is then simply a dword that dropped by exactly 1 across the
 * death, and it names itself.
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

/* Report dwords that differ between the snapshot and the live object.  Ones
 * that moved by exactly -1 or +1 are listed first: that is what a life, a
 * bomb count or an attempt counter looks like across a single death. */
static void deathdiff_report(const BYTE *game, unsigned cause, const char *when)
{
    int shown = 0, delta1 = 0;
    log_write("deathdiff: === %s (cause=%u) — dwords changed vs pre-death snapshot ===\n",
              when, cause);

    for (int pass = 0; pass < 2 && shown < DIFF_MAX; pass++) {
        for (DWORD o = 0; o + 4 <= GAME_SIZE && shown < DIFF_MAX; o += 4) {
            int a = *(const int *)(g_snap + o);
            int b = *(const int *)(game   + o);
            if (a == b) continue;
            bool step = (b - a == -1) || (b - a == 1);
            if (pass == 0 && !step) continue;
            if (pass == 1 && step)  continue;
            if (pass == 0) delta1++;
            log_write("deathdiff:   +0x%06lx  %d -> %d  (%+d)%s\n",
                      (unsigned long)o, a, b, b - a, step ? "  <-- step" : "");
            shown++;
        }
    }
    log_write("deathdiff: === %d shown, %d of them +/-1 steps ===\n", shown, delta1);
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

static bool read_state(GameState *s)
{
    const unsigned char *g = (const unsigned char *)*GAME_GLOBAL_PTR;
    if (!g) return false;

    s->gems_collected = *(const int   *)(g + 0x175406);
    s->gems_required  = *(const int   *)(g + 0x2ab723);
    s->foes_killed    = *(const BYTE  *)(g + 0x04224d);
    s->time_limit_s   = *(const int   *)(g + 0x2ab591);
    s->elapsed_ms     = *(const DWORD *)(g + 0x2ab595);
    memcpy(s->lives_raw, g + 0x175402, 4);
    s->total_score    = *(const int   *)(g + 0x1753f5);
    s->level_score    = *(const int   *)(g + 0x140536);
    s->vitality       = *(const BYTE  *)(g + 0x170a64);
    memcpy(s->death_raw, g + 0x1752e8, 4);
    s->complete_flag  = *(const int   *)(g + 0x1752b8);
    s->extra_count    = *(const WORD  *)(g + 0x042250);
    s->extra_cap      = *(const WORD  *)(g + 0x1753e3);
    s->extra_block    = *(const BYTE  *)(g + 0x04220b);
    memcpy(s->pos, g + 0x1751ee, sizeof(s->pos));
    s->mode           = g_mode;
    return true;
}

void gamestate_tick(void)
{
    g_frame++;
    if (!gamestate_enabled()) return;

    GameState s;
    if (!read_state(&s)) return;

    /* Elapsed time moves every frame; comparing it would log every frame and
     * bury the events worth seeing.  It is still printed on each line. */
    GameState a = s, b = g_prev;
    a.elapsed_ms = b.elapsed_ms = 0;
    if (g_have_prev && memcmp(&a, &b, sizeof(a)) == 0) return;

    log_write("gamestate: f=%lu mode=%u gems=%d/%d(?) foes=%u vit=%u "
              "score=%d/%d t=%lu/%ds items=%u/%u blk=%u done=%d "
              "lives[%02x %02x %02x %02x](?) death[%02x %02x %02x %02x](?) "
              "pos=%.3f,%.3f,%.3f\n",
              (unsigned long)g_frame, (unsigned)s.mode,
              s.gems_collected, s.gems_required, (unsigned)s.foes_killed,
              (unsigned)s.vitality, s.level_score, s.total_score,
              (unsigned long)(s.elapsed_ms / 1000), s.time_limit_s,
              (unsigned)s.extra_cap, (unsigned)s.extra_count,
              (unsigned)s.extra_block, s.complete_flag,
              s.lives_raw[0], s.lives_raw[1], s.lives_raw[2], s.lives_raw[3],
              s.death_raw[0], s.death_raw[1], s.death_raw[2], s.death_raw[3],
              s.pos[0], s.pos[1], s.pos[2]);

    g_prev      = s;
    g_have_prev = true;
}

void gamestate_deathdiff(void)
{
    if (!deathdiff_enabled()) return;
    const BYTE *game = (const BYTE *)*GAME_GLOBAL_PTR;
    if (!game) return;

    BYTE cause = *(const BYTE *)(game + 0x1752e8);

    if (cause != 0 && g_prev_death == 0) {
        deathdiff_report(game, cause, "at death");
        g_respawn_at = 0;
    } else if (cause == 0 && g_prev_death != 0) {
        g_respawn_at = g_frame;                 /* restart began — hold the snapshot */
    } else if (g_respawn_at && g_frame - g_respawn_at >= REPORT_AFTER) {
        /* The decrement happens in here, not at the death itself. */
        deathdiff_report(game, g_prev_death, "after respawn");
        g_respawn_at = 0;
    } else if (cause == 0 && !g_respawn_at && (g_frame % SNAP_EVERY) == 0) {
        memcpy(g_snap, game, GAME_SIZE);        /* alive — refresh window */
    }

    g_prev_death = cause;
}
