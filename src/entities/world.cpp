/* World: the whole-table operations on the level's objects.  Each walks one
 * or more of the entity tables, in the order the game always has. */

#include "world.h"
#include "prof.h"
#include "themedraw.h"

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

/* Releases each object's sounds, then destroys them all, first to last. */
template <class T>
static void purge(EntityList<T> &list, SoundLibrary *sound)
{
    for (T &obj : list)
        obj.releaseSounds(sound);
    list.clear();
}

void World::clearObjects(PlayEvents &events)
{
    for (const LiftObject &lift : lifts_)
        events.entityDestroyed(SoundSource::Lift, lift.id());
    lifts_.clear();
    for (const PlatformObject &platform : platforms_)
        events.entityDestroyed(SoundSource::Platform, platform.id());
    platforms_.clear();
    purge(fallings_, sound_);
    for (const BridgeObject &bridge : bridges_)
        events.entityDestroyed(SoundSource::Bridge, bridge.id());
    bridges_.clear();

    // The 256 switch counts.
    switchCells_.clearCounts();

    // A foe frees its tile claims as it goes.
    purge(foes_, sound_);
    purge(bombs_, sound_);
}

/* Ticks every object in a list, in order; a tick does not add or remove. */
template <class T>
static void tickAll(EntityList<T> &list, const char *name)
{
    if (list.empty())
        return;
    PROF_SCOPE(name);
    for (T &obj : list)
        obj.tick();
}

void World::tickLifts(PlayEvents &events)
{
    if (lifts_.empty())
        return;
    PROF_SCOPE("lifts");
    for (LiftObject &lift : lifts_)
        lift.tick(events);
}
void World::tickPlatforms(PlayEvents &events)
{
    if (platforms_.empty())
        return;
    PROF_SCOPE("platforms");
    for (PlatformObject &platform : platforms_)
        platform.tick(events);
}
void World::tickFallings()  { tickAll(fallings_, "falling tiles"); }

void World::tickBridges(PlayEvents &events)
{
    if (bridges_.empty())
        return;
    PROF_SCOPE("bridges");
    for (BridgeObject &b : bridges_) {
        b.tick(events);
        b.debugDraw(switchCells_);
    }
}

void World::tickBombs()
{
    if (bombs_.empty())
        return;
    PROF_SCOPE("bombs");
    for (std::size_t i = 0; i < bombs_.size();) {
        Bomb &b = bombs_[i];
        b.tick();
        b.debugDraw((unsigned)i);
        if (b.removeRequested() != 0)
            Bomb::remove(entityContext(), bombs_, i);  // the next moves into i
        else
            ++i;
    }
}

void World::draw(draw::Frame &f, draw::Pass pass, unsigned deathsDrawn) const
{
    for (const LiftObject &l : lifts_)
        l.draw(f, pass);
    for (const PlatformObject &p : platforms_)
        p.draw(f, pass);
    for (const BridgeObject &b : bridges_)
        b.draw(f, pass);
    for (const FallingTile &t : fallings_)
        t.draw(f, pass);
    player_.draw(f, pass);
    for (const Bomb &b : bombs_)
        b.draw(f, pass, deathsDrawn);
    for (const Foe &e : foes_)
        e.draw(f, pass, deathsDrawn);

    if (pass == draw::Pass::Particles) {
        if (f.opts.particles > 0)
            draw::bursts(f, THEME_OBJ_FALLING_DUST);
        if (f.opts.particles > 1) {
            const BlendState additive = BlendState::additive();
            draw::bursts(f, THEME_OBJ_CRYSTAL_PICKUP, &additive);
            draw::bursts(f, THEME_OBJ_ITEM_PICKUP, &additive);
        }
    }
}
