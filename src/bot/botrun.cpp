#include "botrun.h"
#include <fstream>
#include <memory>
#include <stdlib.h>
#include <strings.h>
#include <string>
#include "binio.h"
#include "game.h"
#include "keycodes.h"
#include "logger.h"
#include "player.h"
#include "policy.h"
#include "progctrl.h"
#include "sysdev.h"
#include "tile.h"

namespace bot {

namespace {

/* Why the player died, from its move state (movableentity.cpp, foe.cpp,
 * playtick.cpp, player.cpp):
 *   foe       1, a foe touched it;
 *   drop      2, it landed from too high, or went below the floor;
 *   timeout   3, the time limit ran out;
 *   bomb      4 on a cell with a live blast, a bomb's blast caught it;
 *   void      4 on a cell with no live blast: it fell off the map.  The blast
 *             check matches any height within one of the cell's blast height,
 *             which is 0 while no blast is live, so a player falling through
 *             empty space is caught at height 1 with the bomb's state;
 *   harakiri  6, the player's own harakiri key. */
const char *death_cause(const Player &pl)
{
    switch (pl.moveState()) {
    case 1:  return "foe";
    case 2:  return "drop";
    case 3:  return "timeout";
    case 4:  return pl.curTile()->blastHeight() != 0 ? "bomb" : "void";
    case 6:  return "harakiri";
    default: return "unknown";
    }
}

double env_seconds(const char *name, double fallback)
{
    const std::string s = sysdev::getEnv(name);
    return s.empty() ? fallback : atof(s.c_str());
}

/* The first key bound to a mode-1 action, or KEY_NONE. */
int bound_key(const ProgableControl &pc, const char *action)
{
    for (const ActionEntry *a = pc.actionTable(1).first(); a; a = a->chain)
        if (strcasecmp(a->name, action) == 0)
            return a->kbd ? a->kbd->scancode : input::KEY_NONE;
    return input::KEY_NONE;
}

class Run : public input::KeySource {
public:
    Run(const Game *game, const ProgableControl &pc, std::unique_ptr<Policy> policy)
        : game_(game), policy_(std::move(policy))
    {
        keyFor_[0] = bound_key(pc, "John_Move_Forward");
        keyFor_[1] = bound_key(pc, "John_Move_Back");
        keyFor_[2] = bound_key(pc, "John_Turn_Left");
        keyFor_[3] = bound_key(pc, "John_Turn_Right");
        keyFor_[4] = bound_key(pc, "John_Release_Bomb");
        stuckMs_ = env_seconds("KAROO_BOT_STUCK_S", 60) * 1000.0;
        capMs_   = env_seconds("KAROO_BOT_MAX_S", 900) * 1000.0;
        summary_ = sysdev::getEnv("KAROO_BOT_SUMMARY");
    }

    bool exclusive() const override { return true; }

    void sample(unsigned char keys[input::KEY_COUNT]) override
    {
        if (done_)
            return;
        if (game_->state() == GameState::Flythrough) {
            // Enter is a press: down one tick, up the next.
            if ((enterTick_++ & 1) == 0)
                keys[input::KEY_RETURN] = 0x80;
            return;
        }
        if (game_->state() != GameState::Playing || game_->world()->player()->moveState() != 0)
            return;
        const unsigned acts = policy_->act(*game_);
        for (int i = 0; i < 5; i++)
            if ((acts & (1u << i)) && keyFor_[i] != input::KEY_NONE)
                keys[keyFor_[i]] = 0x80;
    }

    bool frame()
    {
        if (done_)
            return false;
        frames_++;
        const World  *w  = game_->world();
        const Player *pl = w->player();
        if (game_->state() != GameState::Playing)
            return false;
        if (!playing_) {
            playing_     = true;
            startMs_     = w->now();
            startFrame_  = frames_;
            lastGemMs_   = w->now();
        }
        if (pl->gemsCollected() != lastGems_) {
            lastGems_  = pl->gemsCollected();
            lastGemMs_ = w->now();
        }

        const char *outcome = nullptr;
        const char *cause   = "";
        if (pl->held() != 0) {
            outcome = "complete";
        } else if (pl->moveState() == 3) {
            outcome = "timeout";
        } else if (pl->moveState() != 0) {
            outcome = "death";
            cause   = death_cause(*pl);
        } else if (w->now() - lastGemMs_ > stuckMs_) {
            outcome = "stuck";
        } else if (w->now() - startMs_ > capMs_) {
            outcome = "cap";
        }
        if (!outcome)
            return false;
        done_ = true;
        write(outcome, cause);
        return true;
    }

private:
    void write(const char *outcome, const char *cause) const
    {
        const World   *w  = game_->world();
        const Player  *pl = w->player();
        const Session *s  = game_->session();
        g_logger.write("bot: %s%s%s after %lu frames, gems %d/%d\n", outcome,
                       *cause ? " by " : "", cause, (unsigned long)(frames_ - startFrame_),
                       pl->gemsCollected(), w->gemsRequired());
        if (summary_.empty())
            return;
        std::ofstream fp(sysdev::nativePath(summary_));
        if (!fp) {
            g_logger.write("bot: cannot write %s\n", summary_.c_str());
            return;
        }
        std::string name;
        for (const char *c = s->levelNameTableEntry(s->levelIndex()); *c; c++) {
            if (*c == '"' || *c == '\\')
                name += '\\';
            name += *c;
        }
        printTo(fp, "{\n");
        printTo(fp, "  \"outcome\": \"%s\",\n", outcome);
        printTo(fp, "  \"death_cause\": \"%s\",\n", cause);
        printTo(fp, "  \"level_index\": %u,\n", (unsigned)s->levelIndex());
        printTo(fp, "  \"level\": \"%s\",\n", name.c_str());
        printTo(fp, "  \"frames\": %lu,\n", (unsigned long)(frames_ - startFrame_));
        printTo(fp, "  \"game_ms\": %.0f,\n", w->now() - startMs_);
        printTo(fp, "  \"gems_collected\": %d,\n", pl->gemsCollected());
        printTo(fp, "  \"gems_required\": %d,\n", w->gemsRequired());
        printTo(fp, "  \"time_limit_s\": %d,\n", w->timeLimit());
        printTo(fp, "  \"cell\": [%d, %d, %d]\n", pl->cellU(), pl->cellV(), pl->heightCell());
        printTo(fp, "}\n");
    }

    const Game              *game_;
    std::unique_ptr<Policy>  policy_;
    int         keyFor_[5];
    double      stuckMs_, capMs_;
    std::string summary_;
    bool        done_    = false;
    bool        playing_ = false;
    unsigned    enterTick_ = 0;
    unsigned long frames_ = 0, startFrame_ = 0;
    double      startMs_ = 0, lastGemMs_ = 0;
    int         lastGems_ = 0;
};

std::unique_ptr<Run> s_run;

}  // namespace

int init(const Game *game, const ProgableControl *controls)
{
    const std::string name = sysdev::getEnv("KAROO_BOT");
    if (name.empty())
        return 0;
    std::unique_ptr<Policy> policy = makePolicy(name);
    if (!policy) {
        g_logger.logMessage(4, "BOT: KAROO_BOT=%s names no policy", name.c_str());
        return -1;
    }
    g_logger.logMessage(1, "BOT: playing with policy %s", name.c_str());
    s_run = std::make_unique<Run>(game, *controls, std::move(policy));
    return 1;
}

input::KeySource *source() { return s_run.get(); }

bool frame() { return s_run && s_run->frame(); }

}  // namespace bot
