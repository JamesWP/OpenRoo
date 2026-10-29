/* World: the whole-table operations on the level's objects.  Each walks one
 * or more of the entity tables, in the order the game always has. */

#include "world.h"
#include "prof.h"
#include "tile.h"
#include "entitymath.h"

BridgeObject *World::bridge(unsigned char switchSlot)
{
    for (BridgeObject &b : bridges_)
        if (b.switchSlot() == switchSlot)
            return &b;
    return nullptr;
}

const BridgeObject *World::bridge(unsigned char switchSlot) const
{
    return const_cast<World *>(this)->bridge(switchSlot);
}

/* Stops each object's loops, then destroys them all, first to last. */
void World::clearObjects(PlayEvents &events)
{
    effects_.clear();
    playerPath_.clear();
    effectsAt_ = -1.0;
    for (const LiftObject &lift : lifts_)
        events.soundStop({SoundSource::Lift, lift.id(), Sfx::LiftMoving});
    lifts_.clear();
    for (const PlatformObject &platform : platforms_)
        events.soundStop({SoundSource::Platform, platform.id(), Sfx::PlatformMoving});
    platforms_.clear();
    fallings_.clear();
    for (const BridgeObject &bridge : bridges_)
        events.soundStop({SoundSource::Bridge, bridge.id(), Sfx::BridgeMoving});
    bridges_.clear();

    // The 256 switch counts.
    switchCells_.clearCounts();

    // A foe frees its tile claims as it goes.
    for (Foe &foe : foes_)
        foe.despawn(events);
    foes_.clear();
    for (const Bomb &bomb : bombs_)
        events.soundStop(bomb.sound(Sfx::BombRoll));
    bombs_.clear();
}

/* Ticks every object in a list, in order; a tick does not add or remove. */
template <class T>
static void tickAll(EntityList<T> &list, const char *name, PlayEvents &events)
{
    if (list.empty())
        return;
    PROF_SCOPE(name);
    for (T &obj : list)
        obj.tick(events);
}

void World::tickLifts(PlayEvents &events) { tickAll(lifts_, "lifts", events); }
void World::tickPlatforms(PlayEvents &events) { tickAll(platforms_, "platforms", events); }
void World::tickFallings(PlayEvents &events) { tickAll(fallings_, "falling tiles", events); }

void World::tickBridges(PlayEvents &events)
{
    if (bridges_.empty())
        return;
    PROF_SCOPE("bridges");
    for (BridgeObject &b : bridges_)
        b.tick(events);
}

void World::tickBombs(PlayEvents &events)
{
    if (bombs_.empty())
        return;
    PROF_SCOPE("bombs");
    for (std::size_t i = 0; i < bombs_.size();) {
        Bomb &b = bombs_[i];
        b.tick(events);
        if (b.removeRequested() != 0)
            Bomb::remove(bombs_, i, events);  // the next moves into i
        else
            ++i;
    }
}


void World::recordEffects()
{
    if (clock_ == effectsAt_)
        return;
    effectsAt_ = clock_;

    while (!effects_.empty() && clock_ - effects_.front().startMs > EFFECT_MS)
        effects_.erase(effects_.begin());
    auto add = [this](EffectKind kind, float u, float y, float v) {
        effects_.push_back({ kind, u, y, v, clock_, effectSeed_++ });
    };
    if (player_.pickedUp() == CONTENTS_CRYSTAL)
        add(EffectKind::CrystalPickup, player_.posU(), player_.posY(), player_.posV());
    else if (player_.pickedUp() != 0)
        add(EffectKind::ItemPickup, player_.posU(), player_.posY(), player_.posV());
    for (const Foe &foe : foes_)
        if (foe.pickedUp() == CONTENTS_CRYSTAL)
            add(EffectKind::CrystalPickup, foe.posU(), foe.posY(), foe.posV());
    for (const FallingTile &t : fallings_)
        if (t.justFell() != 0)
            add(EffectKind::FallingDust, (float)t.cellU(), (float)t.heightCell(), (float)t.cellV());

    while (!playerPath_.empty() && clock_ - playerPath_.front().timeMs > PATH_MS)
        playerPath_.pop_front();
    if (playerPath_.empty() || clock_ - playerPath_.back().timeMs >= PATH_STEP_MS) {
        // Sped up, and moving along (or straight against) its facing.
        const bool trail = player_.effectAActive() != 0 &&
            ((unsigned)player_.moveDir() == player_.facing() ||
             (unsigned)player_.moveDir() == Sim_GetTurnedDirection(player_.facing(), 2));
        playerPath_.push_back({ clock_, player_.posU(), player_.posY(), player_.posV(),
                                player_.facing(), trail });
    }
}
