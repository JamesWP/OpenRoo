/* The game-state reader.  Each frame it reads the fields the replay tests
 * assert on (the ones the level score is built from, plus lives, position and
 * the game mode), logs them when they change (KAROO_STATE_LOG), and at the end
 * of a replay dumps a snapshot per level load, completion and death as JSON
 * (KAROO_STATE_DUMP) for replaytest.py.
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
#include <string>
#include <vector>
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
    uint8_t  bombs;  // bombs the player is carrying
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
    bombs          = pl->bombsCarried();
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

static std::string json_escape(const char *in)
{
    std::string out;
    for (; *in; in++) {
        if (*in == '"' || *in == '\\') out += '\\';
        out += *in;
    }
    return out;
}

/* Level events.  A recording crosses several levels and deaths, and the last
 * in-level frame describes only the final one, so a snapshot is taken at each
 * moment worth asserting on: a level loading (the first in-level frame), a
 * level completing, the player dying, and the run ending ("quit").  The dump lists them in order. */
struct Event {
    const char *kind;     // "load", "complete", "death" or "quit"
    uint32_t    frame;
    std::string level;
    GameState   state;
};
static std::vector<Event> g_events;
static const size_t MAX_EVENTS = 512;

static std::string current_level(void)
{
    const Game *g = Game::instance();
    return g && g->levelName() ? g->levelName() : "";
}

static void record_event(const char *kind, uint32_t frame, const GameState &s,
                         const std::string &level)
{
    if (g_events.size() >= MAX_EVENTS) return;
    Event e;
    e.kind  = kind;
    e.frame = frame;
    e.state = s;
    e.level = level;
    g_events.push_back(e);
    g_logger.write("gamestate: event %s at frame %lu level=%s (score=%d total=%d "
                   "gems=%d/%d lives=%u t=%lus)\n",
                   kind, (unsigned long)frame, e.level.c_str(), s.level_score,
                   s.total_score, s.gems_collected, s.gems_required,
                   (unsigned)s.lives, (unsigned long)(s.elapsed_ms / 1000));
}

/* Completion is recorded when the complete flag clears, or at the dump if the
 * recording ends first, because the score is written a frame or two after the
 * flag rises.  g_done refreshes while the flag stays set. */
static GameState g_done;
static uint32_t  g_done_frame;
static std::string g_done_level;  // the name moves on once the flag clears
static bool      g_done_pending;
static bool      g_have_last;
static GameState g_last;  // previous tick's state, for edge detection
static std::string g_last_level;  // the level of the last in-level frame

static void flush_completion(void)
{
    if (!g_done_pending) return;
    g_done_pending = false;
    record_event("complete", g_done_frame, g_done, g_done_level);
}

void gamestate_tick(void)
{
    g_frame++;

    GameState s;
    if (!s.read()) return;
    if (s.mode != 0) {
        if (!g_have_last || g_last.mode == 0)
            record_event("load", g_frame, s, current_level());
        else if (s.lives < g_last.lives)
            record_event("death", g_frame, s, current_level());
    }

    if (s.complete_flag != 0) {
        g_done         = s;
        g_done_frame   = g_frame;
        g_done_level   = current_level();
        g_done_pending = true;
    } else {
        flush_completion();
    }
    if (s.mode != 0) g_last_level = current_level();
    g_last      = s;
    g_have_last = true;

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

    std::ofstream fp(sysdev::nativePath(path));  // text mode
    if (!fp) {
        g_logger.write("gamestate: dump: cannot open %s\n", path.c_str());
        return;
    }

    flush_completion();
    if (g_have_last)
        record_event("quit", g_frame, g_last, g_last_level);

    printTo(fp, "{\n");
    printTo(fp, "  \"reason\": \"%s\",\n", reason);
    printTo(fp, "  \"frames_run\": %lu,\n", (unsigned long)g_frame);
    printTo(fp, "  \"_unconfirmed\": [\"vitality\", \"death_cause\", \"pos\"],\n");
    printTo(fp, "  \"events\": [");
    for (size_t i = 0; i < g_events.size(); i++) {
        const Event &e = g_events[i];
        const GameState &s = e.state;
        printTo(fp, "%s\n    {\n", i ? "," : "");
        printTo(fp, "      \"kind\": \"%s\",\n",           e.kind);
        printTo(fp, "      \"frame\": %lu,\n",              (unsigned long)e.frame);
        printTo(fp, "      \"level\": \"%s\",\n",          json_escape(e.level.c_str()).c_str());
        printTo(fp, "      \"mode\": %u,\n",                (unsigned)s.mode);
        printTo(fp, "      \"gems_collected\": %d,\n",      s.gems_collected);
        printTo(fp, "      \"gems_required\": %d,\n",       s.gems_required);
        printTo(fp, "      \"foes_killed\": %u,\n",         (unsigned)s.foes_killed);
        printTo(fp, "      \"items_collected\": %u,\n",     (unsigned)s.extra_cap);
        printTo(fp, "      \"items_available\": %u,\n",     (unsigned)s.extra_count);
        printTo(fp, "      \"items_bonus_blocked\": %u,\n", (unsigned)s.extra_block);
        printTo(fp, "      \"vitality\": %u,\n",            (unsigned)s.vitality);
        printTo(fp, "      \"lives\": %u,\n",               (unsigned)s.lives);
        printTo(fp, "      \"bombs\": %u,\n",               (unsigned)s.bombs);
        printTo(fp, "      \"level_score\": %d,\n",         s.level_score);
        printTo(fp, "      \"total_score\": %d,\n",         s.total_score);
        printTo(fp, "      \"time_limit_s\": %d,\n",        s.time_limit_s);
        printTo(fp, "      \"elapsed_ms\": %lu,\n",         (unsigned long)s.elapsed_ms);
        printTo(fp, "      \"level_complete\": %d,\n",      s.complete_flag);
        printTo(fp, "      \"death_cause\": %u,\n",         (unsigned)s.death_raw[0]);
        printTo(fp, "      \"pos\": [%.6f, %.6f, %.6f]\n",  s.pos[0], s.pos[1], s.pos[2]);
        printTo(fp, "    }");
    }
    printTo(fp, "%s]\n", g_events.empty() ? "" : "\n  ");
    printTo(fp, "}\n");
    fp.close();

    g_logger.write("gamestate: dumped %lu event(s) (%s, %lu frames) to %s\n",
              (unsigned long)g_events.size(), reason, (unsigned long)g_frame, path.c_str());
}
