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

#define GAME_GLOBAL_PTR ((void **)0x0046c498)

struct GameState {
    int   gems_collected;   // +0x175406  dword
    int   gems_required;    // +0x2ab723  dword
    BYTE  foes_killed;      // +0x4224d   byte
    int   time_limit_s;     // +0x2ab591  dword
    DWORD elapsed_ms;       // +0x2ab595  dword
    int   lives;            // +0x175402  dword (?)
    int   total_score;      // +0x1753f5  dword
    int   level_score;      // +0x140536  dword
    BYTE  health;           // +0x170a64  byte
    int   death_flag;       // +0x1752e8  dword (?)
    int   complete_flag;    // +0x1752b8  dword (?)
    WORD  extra_count;      // +0x42250   ushort
    WORD  extra_cap;        // +0x1753e3  ushort
    BYTE  extra_block;      // +0x4220b   byte
    float pos[3];           // +0x1751ee  float[3] (?)
    unsigned short mode;    // game_state passed to DispatchInputActions
};

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
    s->lives          = *(const int   *)(g + 0x175402);
    s->total_score    = *(const int   *)(g + 0x1753f5);
    s->level_score    = *(const int   *)(g + 0x140536);
    s->health         = *(const BYTE  *)(g + 0x170a64);
    s->death_flag     = *(const int   *)(g + 0x1752e8);
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

    log_write("gamestate: f=%lu mode=%u gems=%d/%d(?) foes=%u lives=%d(?) "
              "hp=%u score=%d/%d t=%lu/%ds extra=%u/%u blk=%u "
              "death=%d(?) done=%d(?) pos=%.3f,%.3f,%.3f(?)\n",
              (unsigned long)g_frame, (unsigned)s.mode,
              s.gems_collected, s.gems_required, (unsigned)s.foes_killed,
              s.lives, (unsigned)s.health, s.level_score, s.total_score,
              (unsigned long)(s.elapsed_ms / 1000), s.time_limit_s,
              (unsigned)s.extra_count, (unsigned)s.extra_cap,
              (unsigned)s.extra_block,
              s.death_flag, s.complete_flag,
              s.pos[0], s.pos[1], s.pos[2]);

    g_prev      = s;
    g_have_prev = true;
}
