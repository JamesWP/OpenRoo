#include "capture.h"
#include "game.h"
#include "player.h"
#include "bridgeobject.h"
#include "tile.h"

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
    m.launched = e.launched();
    m.launchedAt = e.fallStart();
    m.teleport = e.teleportPhase();
    m.teleportAt = e.teleportSince();
    m.falling = e.falling() != 0 || e.gliding() != 0 || m.launched || m.teleport != 0;
    m.gliding = e.gliding() != 0;
    m.turning = e.moveDir() > 10;
    m.moveDir = e.moveDir();
    m.animStart = e.animStart();
    m.animMs = e.animDuration();
    m.stepEnd = e.stepEnd();
    m.stuckSince = e.stuckSince();
    m.lastMoveDir = e.lastMoveDir();
    m.riding = e.ridingPlatform();
    m.onSwitch = e.onSwitch();
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
            if (t->objectMarker() == TILE_SWITCH)
                c.param = (unsigned char)(t->field1f3() + 1);  // the level file's
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
    view.player.speedUpEnds  = pl->effectAActive() ? pl->effectAStart() + 10000.0 : 0;  // K_EFFECT_MS
    view.player.slowDownEnds = pl->effectCActive() ? pl->effectCStart() + 10000.0 : 0;
    view.player.moveState  = pl->moveState();
    view.player.switchSlot = pl->switchSlot() == 0xff ? -1 : pl->switchSlot();
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
        fv.lastU = f.lastTargetU();
        fv.lastV = f.lastTargetV();
        fv.drops = f.dropContents();
        fv.lastDrop    = f.lastContact();
        fv.dropPending = f.bombDropRequest() != 0;
        fv.turned      = f.turnKind() != 0;
        fv.switchSlot  = f.switchSlot() == 0xff ? -1 : f.switchSlot();
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
    for (const BridgeObject &br : w->bridges()) {
        if (w->bridge(br.switchSlot()) != &br)
            continue;  // a switch works only the first of its slot
        const BridgeExtent e = br.extent();
        BridgeView bv;
        bv.slot = e.slot;
        bv.height = e.height;
        // The deck runs from the anchor along the step: a reversed bridge's
        // rest is a cell past its anchor (bridgeobject.cpp, spawn).
        const int u0 = (int)e.restU - (e.axis == 1 && e.step < 0 ? 1 : 0);
        const int v0 = (int)e.restV - (e.axis == 2 && e.step < 0 ? 1 : 0);
        for (int i = 0; i < e.span; i++)
            bv.deck.push_back({u0 + (e.axis == 1 ? e.step * i : 0),
                               v0 + (e.axis == 2 ? e.step * i : 0)});
        // Phase 0 extends next; a moving bridge is shown in the state it moves to.
        bv.moving = e.armed;
        bv.extended = e.armed ? e.phase == 0 : e.phase == 1;
        bv.phaseStart = e.armed ? br.phaseStart() : 0;  // unset until first armed
        view.bridges.push_back(bv);
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
