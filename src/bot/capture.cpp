#include "capture.h"
#include "game.h"
#include "player.h"

namespace bot {

namespace {

template <class E>
void fill_mover(Mover &m, const E &e)
{
    m.u = e.cellU();
    m.v = e.cellV();
    m.h = e.heightCell();
    m.facing = e.facing();
    m.moving = e.moveDir() != 0;
    m.falling = e.falling() != 0 || e.gliding() != 0;
    m.turning = e.moveDir() > 10;
    m.moveDir = e.moveDir();
    m.animStart = e.animStart();
    m.animMs = e.animDuration();
}

}  // namespace

BotView capture(const Game &game)
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
            c.jumpTop     = t->field1f1();
            c.track       = t->platformTrack() != 0;
            c.liftBottom  = (unsigned char)t->liftBottom();
            c.liftTop     = (unsigned char)t->liftTop();
            c.busy        = t->busy() != 0;
        }

    view.level         = s->levelNameTableEntry(s->levelIndex());
    view.now           = w->now();
    view.tickMs        = w->tickStep()->value;
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
    view.player.freezeUntil = pl->freezeActive() ? pl->freezeStart() + 5000.0 : 0;  // K_FREEZE_MS
    view.player.bombReadyAt = pl->lastBombDrop() + 2000.0;

    for (const Foe &f : w->foes()) {
        FoeView fv;
        fill_mover(fv, f);
        fv.kind  = (int)f.foeKind();
        fv.type  = f.type();
        fv.held  = f.held() != 0;
        fv.dying = f.dyingStarted() != 0;
        fv.homeU = f.homeU();
        fv.homeV = f.homeV();
        view.foes.push_back(fv);
    }
    for (const FallingTile &ft : w->fallings()) {
        FallingView fv;
        fv.u = ft.cellU();
        fv.v = ft.cellV();
        fv.h = ft.heightCell();
        fv.armed    = ft.armed();
        fv.armedAt  = ft.armed() || ft.respawnPending() ? ft.armedAt() : 0;  // unset until it first arms; a fallen tile is back 5 s after it
        fv.fallen   = ft.respawnPending() || map->tile(fv.u, fv.v)->busy() != 0;
        fv.respawns = !ft.noRespawn();
        view.fallings.push_back(fv);
    }
    for (const LiftObject &l : w->lifts()) {
        LiftView lv;
        lv.u = l.cellU();
        lv.v = l.cellV();
        lv.base = l.baseHeight();
        lv.top = l.topHeight();
        lv.state = l.state();
        lv.atTop = l.atTop();
        lv.phaseStart = l.phaseStart();
        view.lifts.push_back(lv);
    }
    for (const PlatformObject &pf : w->platforms()) {
        PlatformView pv;
        pv.alongU = pf.kind() == 0x0a;
        pv.originU = pf.originU();
        pv.originV = pf.originV();
        pv.limit = pf.limit();
        pv.height = pf.originHeight();
        pv.state = pf.state();
        pv.atLimit = pf.atLimit();
        pv.phaseStart = pf.phaseStart();
        view.platforms.push_back(pv);
    }
    for (const Bomb &b : w->bombs()) {
        BombView bv;
        fill_mover(bv, b);
        bv.droppedAt = b.droppedAt();
        view.bombs.push_back(bv);
    }
    return view;
}

}  // namespace bot
