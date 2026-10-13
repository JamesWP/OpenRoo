#include "botview.h"
#include <stdarg.h>
#include <stdio.h>
#include "game.h"
#include "player.h"
#include "tile.h"

namespace bot {

bool Cell::operator==(const Cell &o) const
{
    return height == o.height && kind == o.kind && contents == o.contents &&
           hidden == o.hidden && param == o.param && occupant == o.occupant &&
           slideDir == o.slideDir && teleU == o.teleU && teleV == o.teleV &&
           blastHeight == o.blastHeight && busy == o.busy;
}

namespace {

template <class E>
void fill_mover(Mover &m, const E &e)
{
    m.u = e.cellU();
    m.v = e.cellV();
    m.h = e.heightCell();
    m.facing = e.facing();
    m.moving = e.moveDir() != 0;
}

void appendf(std::string &out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void appendf(std::string &out, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out += buf;
}

void cell_json(std::string &out, int u, int v, const Cell &c)
{
    appendf(out, "[%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d]", u, v, c.height, c.kind,
            c.contents, c.hidden, c.param, c.occupant, c.slideDir, c.teleU, c.teleV,
            c.blastHeight, c.busy ? 1 : 0);
}

}  // namespace

BotView BotView::capture(const Game &game)
{
    BotView view;
    const World    *w   = game.world();
    const LevelMap *map = w->map();
    const Session  *s   = game.session();

    view.extentU = map->extentU();
    view.extentV = map->extentV();
    view.cells.resize((size_t)view.extentU * view.extentV);
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const Tile *t = map->tile(u, v);
            Cell &c = view.cells[(size_t)v * view.extentU + u];
            c.height      = t->height();
            c.kind        = t->objectMarker();
            c.contents    = t->contents();
            c.hidden      = t->field202();
            c.param       = t->param();
            c.occupant    = t->occupant();
            c.slideDir    = t->slideDir();
            c.teleU       = t->teleportU();
            c.teleV       = t->teleportV();
            c.blastHeight = t->blastHeight();
            c.busy        = t->busy() != 0;
        }

    view.level         = s->levelNameTableEntry(s->levelIndex());
    view.now           = w->now();
    view.timeLimitS    = w->timeLimit();
    view.timeElapsedMs = w->timeElapsed();
    view.gemsRequired  = w->gemsRequired();

    const Player *pl = w->player();
    view.exitU = pl->markerCellU();
    view.exitV = pl->markerCellV();
    view.exitH = pl->markerCellH();
    fill_mover(view.player, *pl);
    view.player.bombs      = pl->bombsCarried();
    view.player.glides     = pl->glides();
    view.player.lives      = pl->lives();
    view.player.gems       = pl->gemsCollected();
    view.player.reversed   = pl->reversedActive() != 0;
    view.player.frozenFoes = pl->freezeActive() != 0;
    view.player.protection = pl->protectionActive() != 0;
    view.player.stepMs     = pl->stepDuration();
    view.player.moveState  = pl->moveState();

    for (const Foe &f : w->foes()) {
        FoeView fv;
        fill_mover(fv, f);
        fv.kind  = (int)f.foeKind();
        fv.type  = f.type();
        fv.held  = f.held() != 0;
        fv.dying = f.dyingStarted() != 0;
        view.foes.push_back(fv);
    }
    for (const FallingTile &ft : w->fallings()) {
        FallingView fv;
        fv.u = ft.cellU();
        fv.v = ft.cellV();
        fv.h = ft.heightCell();
        fv.armed    = ft.armed();
        fv.armedAt  = ft.armedAt();
        fv.fallen   = ft.respawnPending() || map->tile(fv.u, fv.v)->busy() != 0;
        fv.respawns = !ft.noRespawn();
        view.fallings.push_back(fv);
    }
    for (const Bomb &b : w->bombs()) {
        BombView bv;
        bv.u = b.cellU();
        bv.v = b.cellV();
        bv.h = b.heightCell();
        bv.droppedAt = b.droppedAt();
        view.bombs.push_back(bv);
    }
    return view;
}

std::string BotView::toJson(unsigned frame, const BotView *prev) const
{
    std::string out;
    appendf(out, "{\"frame\":%u,\"now\":%.1f,\"elapsed_ms\":%u", frame, now, timeElapsedMs);
    if (!prev || prev->extentU != extentU || prev->extentV != extentV) {
        std::string name;
        for (char c : level) {
            if (c == '"' || c == '\\')
                name += '\\';
            name += c;
        }
        appendf(out, ",\"level\":\"%s\",\"extent\":[%d,%d],\"time_limit_s\":%d,"
                "\"gems_required\":%d,\"exit\":[%d,%d,%d]",
                name.c_str(), extentU, extentV, timeLimitS, gemsRequired, exitU, exitV, exitH);
        out += ",\"cell_fields\":[\"u\",\"v\",\"height\",\"kind\",\"contents\",\"hidden\","
               "\"param\",\"occupant\",\"slide_dir\",\"tele_u\",\"tele_v\",\"blast_height\",\"busy\"]";
        out += ",\"cells\":[";
        bool first = true;
        for (int v = 0; v < extentV; v++)
            for (int u = 0; u < extentU; u++) {
                if (!first)
                    out += ',';
                first = false;
                cell_json(out, u, v, cell(u, v));
            }
        out += ']';
    } else {
        out += ",\"changed\":[";
        bool first = true;
        for (int v = 0; v < extentV; v++)
            for (int u = 0; u < extentU; u++)
                if (cell(u, v) != prev->cell(u, v)) {
                    if (!first)
                        out += ',';
                    first = false;
                    cell_json(out, u, v, cell(u, v));
                }
        out += ']';
    }
    const PlayerView &p = player;
    appendf(out, ",\"player\":{\"cell\":[%d,%d,%d],\"facing\":%d,\"moving\":%d,\"bombs\":%d,"
            "\"glides\":%d,\"lives\":%d,\"gems\":%d,\"reversed\":%d,\"freeze\":%d,"
            "\"protection\":%d,\"step_ms\":%.0f,\"move_state\":%d}",
            p.u, p.v, p.h, p.facing, p.moving, p.bombs, p.glides, p.lives, p.gems,
            p.reversed, p.frozenFoes, p.protection, p.stepMs, p.moveState);
    out += ",\"foes\":[";
    for (size_t i = 0; i < foes.size(); i++) {
        const FoeView &f = foes[i];
        appendf(out, "%s{\"cell\":[%d,%d,%d],\"facing\":%d,\"moving\":%d,\"kind\":%d,"
                "\"type\":%d,\"held\":%d,\"dying\":%d}", i ? "," : "",
                f.u, f.v, f.h, f.facing, f.moving, f.kind, f.type, f.held, f.dying);
    }
    out += "],\"fallings\":[";
    for (size_t i = 0; i < fallings.size(); i++) {
        const FallingView &f = fallings[i];
        appendf(out, "%s{\"cell\":[%d,%d,%d],\"armed\":%d,\"armed_at\":%.1f,\"fallen\":%d,"
                "\"respawns\":%d}", i ? "," : "",
                f.u, f.v, f.h, f.armed, f.armedAt, f.fallen, f.respawns);
    }
    out += "],\"bombs\":[";
    for (size_t i = 0; i < bombs.size(); i++) {
        const BombView &b = bombs[i];
        appendf(out, "%s{\"cell\":[%d,%d,%d],\"dropped_at\":%.1f}", i ? "," : "",
                b.u, b.v, b.h, b.droppedAt);
    }
    out += "]}";
    return out;
}

}  // namespace bot
