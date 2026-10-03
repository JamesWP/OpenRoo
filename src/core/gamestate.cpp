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

#include <stdio.h>
#include <stdint.h>
#include "gamestate.h"
#include "sysdev.h"
#include "logger.h"
#include "game.h"
#include "player.h"
#include <string.h>
#include <algorithm>
#include <fstream>
#include "binio.h"

struct GameState {
    /* Reads the live Game; false when there is none. */
    bool read();

    int   gems_collected;  // dword
    int   gems_required;   // dword
    uint8_t  foes_killed;     // byte
    int   time_limit_s;
    uint32_t elapsed_ms;
    uint8_t  lives;  // drops at the restart after a death
    int   total_score;
    int   level_score;
    uint8_t  vitality;       // unconfirmed; see above
    uint8_t  death_raw[4];   // unconfirmed: the move state and the low three bytes of falling
    int   complete_flag;  // 0 to 1 at the exit
    uint16_t  extra_count;    // items available
    uint16_t  extra_cap;      // items collected: bombs and gems both
    uint8_t  extra_block;    // restarts; any suppresses the items bonus
    float pos[3];         // unconfirmed
    unsigned short mode;  // the mode handed to the input dispatch
};

static int       g_on = -1;
static GameState g_prevClock;
static bool      g_have_prev;
static unsigned short g_mode;
static uint32_t     g_frame;

bool gamestate_enabled(void)
{
    if (g_on < 0) {
        char buf[16];
        g_on = 0;
        if (sysdev::getEnv("KAROO_STATE_LOG", buf, sizeof(buf)) && buf[0] && buf[0] != '0')
            g_on = 1;
        g_logger.write("gamestate: state log %s\n", g_on ? "enabled" : "disabled");
    }
    return g_on > 0;
}

void gamestate_note_mode(unsigned short mode) { g_mode = mode; }
unsigned short gamestate_mode(void) { return g_mode; }

bool GameState::read()
{
    const Game *g = Game::instance();
    if (!g) return false;

    const Player *pl = g->player();
    gems_collected = pl->gemsCollected();
    gems_required  = g->gemsRequired();
    foes_killed    = g->foesKilled();
    time_limit_s   = g->timeLimit();
    elapsed_ms     = g->timeElapsed();
    lives          = (uint8_t)pl->lives();
    total_score    = pl->score();
    level_score    = g->tally()->levelTotal;
    vitality       = g->vitalityPercent();
    {
        int f120 = pl->falling();
        death_raw[0] = pl->moveState();
        memcpy(death_raw + 1, &f120, 3);
    }
    complete_flag  = pl->held();
    extra_count    = g->itemTotal();
    extra_cap      = pl->itemsCollected();
    extra_block    = g->restartCount();
    pos[0]         = pl->posU();
    pos[1]         = pl->posY();
    pos[2]         = pl->posV();
    mode           = g_mode;
    return true;
}

/* The last state read while a level was running (mode not 0).  The dump cannot
 * read the live object: a recording usually ends by quitting, and by then the
 * level is torn down.  So each in-level frame is kept and the last is dumped:
 * the end of the gameplay. */
static GameState g_live;
static bool      g_have_live;
static uint32_t     g_live_frame;

/* The state when a level was completed.  A recording that carries on into the
 * next level ends in that level, so its last in-level frame says nothing about
 * the level it names.  This latch refreshes while the complete flag stays set,
 * because the score is written a frame or two after the flag, and locks when
 * the flag clears, so a later level cannot overwrite it. */
static GameState g_done;
static bool      g_have_done;
static uint32_t     g_done_frame;
static int       g_done_phase;  // 0 never seen, 1 in progress, 2 locked

void gamestate_tick(void)
{
    g_frame++;

    GameState s;
    if (!s.read()) return;
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
        g_logger.write("gamestate: level completed at frame %lu - latched "
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

    g_logger.write("gamestate: f=%lu mode=%u gems=%d/%d(?) foes=%u vit=%u "
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

/* Writes the dump, as JSON so that the harness can name the field that
 * differs.  The unconfirmed fields are dumped too, and listed under
 * "_unconfirmed", so a test asserting on one does so knowingly. */
void gamestate_dump(const char *reason)
{
    static bool dumped = false;
    if (dumped) return;  // the first, most live, wins

    const std::string path = sysdev::getEnv("KAROO_STATE_DUMP");
    if (path.empty())
        return;
    dumped = true;

    GameState s   = g_live;
    bool     have = g_have_live;

    std::ofstream fp(path);  // text mode
    if (!fp) {
        g_logger.write("gamestate: dump: cannot open %s\n", path);
        return;
    }

    printTo(fp, "{\n");
    printTo(fp, "  \"reason\": \"%s\",\n", reason);
    printTo(fp, "  \"frame\": %lu,\n", (unsigned long)g_live_frame);
    printTo(fp, "  \"frames_run\": %lu,\n", (unsigned long)g_frame);
    printTo(fp, "  \"game_live\": %s", have ? "true" : "false");
    if (have) {
        printTo(fp, ",\n");
        printTo(fp, "  \"mode\": %u,\n",            (unsigned)s.mode);
        printTo(fp, "  \"gems_collected\": %d,\n",  s.gems_collected);
        printTo(fp, "  \"gems_required\": %d,\n",   s.gems_required);
        printTo(fp, "  \"foes_killed\": %u,\n",     (unsigned)s.foes_killed);
        printTo(fp, "  \"items_collected\": %u,\n", (unsigned)s.extra_cap);
        printTo(fp, "  \"items_available\": %u,\n", (unsigned)s.extra_count);
        printTo(fp, "  \"items_bonus_blocked\": %u,\n", (unsigned)s.extra_block);
        printTo(fp, "  \"vitality\": %u,\n",        (unsigned)s.vitality);
        printTo(fp, "  \"lives\": %u,\n",           (unsigned)s.lives);
        printTo(fp, "  \"level_score\": %d,\n",     s.level_score);
        printTo(fp, "  \"total_score\": %d,\n",     s.total_score);
        printTo(fp, "  \"time_limit_s\": %d,\n",    s.time_limit_s);
        printTo(fp, "  \"elapsed_ms\": %lu,\n",     (unsigned long)s.elapsed_ms);
        printTo(fp, "  \"level_complete\": %d,\n",  s.complete_flag);
        printTo(fp, "  \"death_cause\": %u,\n",     (unsigned)s.death_raw[0]);
        printTo(fp, "  \"pos\": [%.6f, %.6f, %.6f],\n", s.pos[0], s.pos[1], s.pos[2]);
        printTo(fp, "  \"_unconfirmed\": [\"vitality\", \"death_cause\", \"pos\"],\n");
        printTo(fp, "  \"completed_a_level\": %s,\n", g_have_done ? "true" : "false");
        if (g_have_done) {
            printTo(fp, "  \"at_completion\": {\n");
            printTo(fp, "    \"frame\": %lu,\n",           (unsigned long)g_done_frame);
            printTo(fp, "    \"gems_collected\": %d,\n",   g_done.gems_collected);
            printTo(fp, "    \"gems_required\": %d,\n",    g_done.gems_required);
            printTo(fp, "    \"foes_killed\": %u,\n",      (unsigned)g_done.foes_killed);
            printTo(fp, "    \"items_collected\": %u,\n",  (unsigned)g_done.extra_cap);
            printTo(fp, "    \"items_available\": %u,\n",  (unsigned)g_done.extra_count);
            printTo(fp, "    \"items_bonus_blocked\": %u,\n", (unsigned)g_done.extra_block);
            printTo(fp, "    \"vitality\": %u,\n",         (unsigned)g_done.vitality);
            printTo(fp, "    \"lives\": %u,\n",            (unsigned)g_done.lives);
            printTo(fp, "    \"level_score\": %d,\n",      g_done.level_score);
            printTo(fp, "    \"total_score\": %d,\n",      g_done.total_score);
            printTo(fp, "    \"time_limit_s\": %d,\n",     g_done.time_limit_s);
            printTo(fp, "    \"elapsed_ms\": %lu,\n",      (unsigned long)g_done.elapsed_ms);
            printTo(fp, "    \"level_complete\": %d\n",    g_done.complete_flag);
            printTo(fp, "  }\n");
        } else {
            printTo(fp, "  \"at_completion\": null\n");
        }
    } else {
        printTo(fp, "\n");
    }
    printTo(fp, "}\n");
    fp.close();

    g_logger.write("gamestate: dumped end state (%s, from frame %lu of %lu) to %s\n",
              reason, (unsigned long)g_live_frame, (unsigned long)g_frame, path);
}
