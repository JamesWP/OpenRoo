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
#include <deque>
#include "botview.h"
#include "bombmodel.h"
#include "foemodel.h"
#include <map>
#include "policy.h"
#include "progctrl.h"
#include "sysdev.h"

namespace bot {

namespace {

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
        const std::string actions = sysdev::getEnv("KAROO_BOT_ACTIONS");
        if (!actions.empty())
            actions_.open(sysdev::nativePath(actions));
        const std::string bombLog = sysdev::getEnv("KAROO_BOT_BOMB_LOG");
        if (!bombLog.empty())
            bombLog_.open(sysdev::nativePath(bombLog));
        const std::string foeLog = sysdev::getEnv("KAROO_BOT_FOE_LOG");
        if (!foeLog.empty())
            foeLog_.open(sysdev::nativePath(foeLog));
        const std::string trace = sysdev::getEnv("KAROO_BOT_TRACE");
        if (!trace.empty())
            trace_.open(sysdev::nativePath(trace));
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
        BotView view = BotView::capture(*game_);
        if (trace_)
            trace_ << view.toJson(frames_, haveLast_ ? &last_ : nullptr) << '\n';
        logAction(view);
        checkFoes(view);
        checkBombs(view);
        const unsigned acts = policy_->act(view);
        held_     = acts;
        last_     = std::move(view);
        haveLast_ = true;
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
        // A level can end in the tick it is completed, leaving play for the
        // tally at once.
        if (playing_ && game_->state() == GameState::LevelComplete) {
            done_ = true;
            policy_->end(BotView::capture(*game_));
            trace_.close();
            write("complete", "");
            return true;
        }
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
            cause   = BotView::capture(*game_).deathCause();
        } else if (w->now() - lastGemMs_ > stuckMs_) {
            outcome = "stuck";
        } else if (w->now() - startMs_ > capMs_) {
            outcome = "cap";
        }
        if (!outcome)
            return false;
        done_ = true;
        policy_->end(BotView::capture(*game_));
        trace_.close();
        write(outcome, cause);
        return true;
    }

private:
    /* The action log: each move the player makes, from rest to rest, with
     * the keys that started it.  Measured from the views, so it holds for
     * any policy; tools/bot/calibrate.py reads it. */
    void logAction(const BotView &view)
    {
        if (!actions_.is_open() || !haveLast_)
            return;
        const PlayerView &p = view.player, &q = last_.player;
        // A move ends at rest, or when the next one starts without a rest
        // between: the destination cell changes while still moving.
        const bool chained = p.moving && q.moving && (p.u != q.u || p.v != q.v);
        if (started_ && ((!p.moving && q.moving) || chained)) {
            started_ = false;
            writeAction(view, chained ? q : p);
        }
        if ((p.moving && !q.moving) || chained) {
            // held_ is still what the last tick held: the keys that started it.
            started_   = true;
            startKeys_ = held_;
            from_      = q;
            fromMs_    = p.animStart;
            moveMs_    = p.animMs;
            fromKind_  = last_.cell(from_.u, from_.v).kind;
        }
    }

    /* The foe check: every CHECK_TICKS, the foe model is run from the views
     * of each look-ahead ago, with the player where it really went since,
     * and its foes compared with where they are now. */
    void checkFoes(const BotView &view)
    {
        if (!foeLog_.is_open())
            return;
        static const int CHECK_TICKS = 6;
        static const int AHEAD[] = {6, 30, 60, 120};
        const unsigned long tick = frames_;
        track_.push_back({view.now, {view.player.u, view.player.v}});
        while (track_.size() > 200)
            track_.pop_front();
        if (tick % CHECK_TICKS != 0 || view.foes.empty())
            return;
        for (const int ahead : AHEAD) {
            for (const auto &[t, old] : past_) {
                if (t + (unsigned long)ahead != tick || old.foes.size() != view.foes.size())
                    continue;
                sim_.load(old);
                sim_.run(view.now, [this](double ms) {
                    Step at = track_.front().second;
                    for (const auto &[when, cell] : track_)
                        if (when <= ms + 1e-6)
                            at = cell;
                    return at;
                });
                if (sim_.foeCount() != view.foes.size())
                    continue;
                printTo(foeLog_, "{\"frame\":%lu,\"ahead_ms\":%.1f,\"foes\":[", tick,
                        view.now - old.now);
                for (size_t i = 0; i < view.foes.size(); i++) {
                    const Step p = sim_.foeCell(i);
                    const FoeView &f = view.foes[i], &was = old.foes[i];
                    printTo(foeLog_, "%s{\"pred\":[%d,%d],\"actual\":[%d,%d],\"was\":[%d,%d],"
                            "\"type\":%d,\"kind\":%d}", i ? "," : "", p.u, p.v, f.u, f.v,
                            was.u, was.v, f.type, f.kind);
                }
                printTo(foeLog_, "]}\n");
            }
        }
        past_.push_back({tick, view});
        while (!past_.empty() && past_.front().first + 120 < tick)
            past_.pop_front();
    }

    /* The bomb check: each bomb's blast as the model predicts it when the
     * bomb is first seen, against where it is when its fuse runs out. */
    void checkBombs(const BotView &view)
    {
        if (!bombLog_.is_open())
            return;
        for (const BombView &b : view.bombs) {
            auto it = bombs_.find(b.droppedAt);
            if (it == bombs_.end()) {
                bombs_[b.droppedAt] = {predictBomb(view, b), false, view.now - b.droppedAt};
                continue;
            }
            auto &[pred, done, seenAfter] = it->second;
            if (done || view.now < b.droppedAt + 2000.0)
                continue;
            done = true;
            printTo(bombLog_, "{\"dropped_at\":%.1f,\"seen_after_ms\":%.1f,\"pred\":[%d,%d,%d],"
                    "\"pred_lost\":%d,\"actual\":[%d,%d,%d]}\n", b.droppedAt, seenAfter,
                    pred.cell.u, pred.cell.v, pred.h, pred.lost ? 1 : 0, b.u, b.v, b.h);
        }
    }

    void writeAction(const BotView &view, const PlayerView &to)
    {
        static const char *const names[] = {"forward", "back", "left", "right", "bomb"};
        std::string keys;
        for (int i = 0; i < 5; i++)
            if (startKeys_ & (1u << i)) {
                if (!keys.empty())
                    keys += '+';
                keys += names[i];
            }
        printTo(actions_, "{\"keys\":\"%s\",\"from\":[%d,%d,%d],\"to\":[%d,%d,%d],"
                "\"facing\":[%d,%d],\"start_ms\":%.1f,\"ms\":%.1f,\"step_ms\":%.0f,"
                "\"reversed\":%d,\"kind_from\":%u,\"kind_to\":%u}\n",
                keys.empty() ? "none" : keys.c_str(), from_.u, from_.v, from_.h,
                to.u, to.v, to.h, from_.facing, to.facing, fromMs_, moveMs_,
                from_.stepMs, from_.reversed ? 1 : 0, fromKind_, view.cell(to.u, to.v).kind);
    }

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
        std::string note;
        for (char c : policy_->note()) {
            if (c == '"' || c == '\\')
                note += '\\';
            note += c;
        }
        printTo(fp, "  \"note\": \"%s\",\n", note.c_str());
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
    std::ofstream trace_;   // KAROO_BOT_TRACE: the view each tick it acts
    BotView     last_;
    std::ofstream actions_; // KAROO_BOT_ACTIONS: the action log
    std::ofstream bombLog_; // KAROO_BOT_BOMB_LOG: the bomb model against the game
    std::map<double, std::tuple<Blast, bool, double>> bombs_;  // by drop time
    std::ofstream foeLog_;  // KAROO_BOT_FOE_LOG: the foe model against the game
    FoeModel        sim_;
    std::deque<std::pair<double, Step>>         track_;  // the player's cell by clock
    std::deque<std::pair<unsigned long, BotView>> past_; // the views the check runs from
    unsigned    held_ = 0, startKeys_ = 0;
    bool        started_ = false;
    PlayerView  from_;
    double      fromMs_ = 0, moveMs_ = 0;
    unsigned    fromKind_ = 0;
    bool        haveLast_ = false;
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
