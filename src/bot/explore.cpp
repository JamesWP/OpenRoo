#include "explore.h"
#include <stdlib.h>
#include <string>
#include "binio.h"
#include "sysdev.h"
#include "tile.h"

namespace bot {

namespace {

/* The game's cell after a move must hold this many ticks at rest before the
 * move counts as over; a teleporter or a sticky pad holds longer. */
const int REST_TICKS = 2;
/* A press that has not moved the player after this many ticks was refused. */
const int REFUSED_TICKS = 3;
/* A move that has not come to rest after this many ticks is cut off. */
const int GIVE_UP_TICKS = 600;

const char *kind_name(Outcome::Kind k)
{
    return k == Outcome::Rest ? "rest" : k == Outcome::Death ? "death" : "blocked";
}

}  // namespace

Explore::Explore()
{
    const std::string seed = sysdev::getEnv("KAROO_SEED");
    seed_ = seed.empty() ? 1u : (unsigned)strtoul(seed.c_str(), nullptr, 10) * 2654435761u + 1u;
    const std::string path = sysdev::getEnv("KAROO_BOT_MODEL_LOG");
    if (!path.empty())
        log_.open(sysdev::nativePath(path));
}

unsigned Explore::random()
{
    seed_ = seed_ * 1103515245u + 12345u;
    return seed_ >> 16;
}

void Explore::finish(const BotView &view, const char *kind, const char *cause)
{
    pending_ = false;
    if (!log_.is_open())
        return;
    const PlayerView &p = view.player;
    const Outcome &o = predicted_;
    printTo(log_, "{\"act\":\"%s\",\"from\":[%d,%d,%d,%d],\"from_kind\":%d,"
            "\"pred\":{\"kind\":\"%s\",\"to\":[%d,%d,%d],\"ms\":%.1f,\"why\":\"%s\",\"cells\":%d},"
            "\"actual\":{\"kind\":\"%s\",\"to\":[%d,%d,%d,%d],\"ms\":%.1f,\"cause\":\"%s\","
            "\"to_kind\":%d}}\n",
            act_, from_.u, from_.v, from_.h, from_.facing, fromKind_,
            kind_name(o.kind), o.u, o.v, o.facing, o.ms, o.why, (int)o.cells.size(),
            kind, p.u, p.v, p.h, p.facing, view.now - startMs_, cause,
            view.inside(p.u, p.v) ? view.cell(p.u, p.v).kind : -1);
}

void Explore::end(const BotView &view)
{
    if (pending_)
        finish(view, view.player.moveState ? "death" : "cut",
               view.player.moveState ? view.deathCause() : "");
}

unsigned Explore::act(const BotView &view)
{
    const PlayerView &p = view.player;
    restTicks_ = p.atRest() ? restTicks_ + 1 : 0;

    if (pending_) {
        waited_++;
        if (!p.atRest())
            moved_ = true;
        const Cell &here = view.cell(p.u, p.v);
        const bool held = (here.kind == TILE_TELEPORTER) ||
                          (here.kind == TILE_STICKY && !here.busy) ||
                          (here.kind == TILE_JUMP_PAD);
        if (!moved_ && p.u == from_.u && p.v == from_.v && p.facing == from_.facing &&
            waited_ >= REFUSED_TICKS && restTicks_ >= REFUSED_TICKS)
            finish(view, "blocked", "");
        else if (moved_ && restTicks_ >= REST_TICKS && !held)
            finish(view, "rest", "");
        else if (waited_ > GIVE_UP_TICKS)
            finish(view, "cut", "");
        if (pending_)
            return 0;
    }
    if (restTicks_ < REST_TICKS)
        return 0;

    const NavModel nav(view);
    static const unsigned acts[4] = {ACT_FORWARD, ACT_BACK, ACT_RIGHT, ACT_LEFT};
    static const char *const names[4] = {"forward", "back", "right", "left"};
    // Now and then, with bombs to drop, a bomb (for the bomb check); the
    // move goes on as usual.
    const unsigned bomb = (p.bombs > 0 && random() % 8 == 0) ? (unsigned)ACT_BOMB : 0u;
    unsigned pick = 0;
    Outcome o;
    for (int attempt = 0; attempt < 8; attempt++) {
        pick = random() % 4;
        o = pick == 0 ? nav.step(p.u, p.v, p.facing, p.facing) :
            pick == 1 ? nav.step(p.u, p.v, p.facing, NavModel::opposite(p.facing)) :
            pick == 2 ? nav.turn(p.u, p.v, p.facing, +1) : nav.turn(p.u, p.v, p.facing, -1);
        if (o.kind != Outcome::Death || random() % 10 == 0)
            break;
    }
    pending_   = true;
    moved_     = false;
    waited_    = 0;
    act_       = names[pick];
    from_      = p;
    fromKind_  = view.cell(p.u, p.v).kind;
    predicted_ = o;
    startMs_   = view.now;
    const unsigned a = acts[pick];
    if (!p.reversed)
        return a | bomb;
    return (a == ACT_FORWARD ? ACT_BACK : a == ACT_BACK ? ACT_FORWARD :
            a == ACT_RIGHT ? ACT_LEFT : ACT_RIGHT) | bomb;
}

}  // namespace bot
