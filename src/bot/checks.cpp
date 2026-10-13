#include "checks.h"
#include "binio.h"
#include "sysdev.h"

namespace bot {

Checks::Checks()
{
    const std::string actions = sysdev::getEnv("KAROO_BOT_ACTIONS");
    if (!actions.empty())
        actions_.open(sysdev::nativePath(actions));
    const std::string bombLog = sysdev::getEnv("KAROO_BOT_BOMB_LOG");
    if (!bombLog.empty())
        bombLog_.open(sysdev::nativePath(bombLog));
    const std::string foeLog = sysdev::getEnv("KAROO_BOT_FOE_LOG");
    if (!foeLog.empty())
        foeLog_.open(sysdev::nativePath(foeLog));
}

void Checks::observe(const BotView &view, unsigned long frame, unsigned held)
{
    frame_ = frame;
    held_  = held;
    logAction(view);
    checkFoes(view);
    checkBombs(view);
    if (actions_.is_open()) {
        last_     = view;
        haveLast_ = true;
    }
}

/* The action log: each move the player makes, from rest to rest, with
 * the keys that started it.  Measured from the views, so it holds for
 * any policy; tools/bot/calibrate.py reads it. */
void Checks::logAction(const BotView &view)
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
void Checks::checkFoes(const BotView &view)
{
    if (!foeLog_.is_open())
        return;
    static const int CHECK_TICKS = 6;
    static const int AHEAD[] = {6, 30, 60, 120};
    const unsigned long tick = frame_;
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
void Checks::checkBombs(const BotView &view)
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

void Checks::writeAction(const BotView &view, const PlayerView &to)
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

}  // namespace bot
