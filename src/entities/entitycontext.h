/* What the level's entities need of the game around them, handed to them at
 * spawn so they know nothing of Game: the shared clock, tick step, map and
 * sound manager, and a table per kind for the slots they live in. */
#pragma once

struct TickStep;
class  LevelMap;
class  SoundLibrary;
class  SwitchCells;

struct EntityContext {
    double       *clock;
    TickStep     *tickStep;
    LevelMap     *map;
    SoundLibrary *sound;
    /* The switch cells and the highest switch slot in use (a view of the
     * World's, so it follows the level being built). */
    SwitchCells         *switches;
    const unsigned char *switchMax;
    /* The level's name (diagnostics) and the level's crystal count (World's
     * crystalsInLevel), which spawning a foe of type 0x0b raises. */
    const char     *levelName;
    unsigned short *crystalsInLevel;
};

/* One kind's slots, in use up to count; N is the most the game allows.
 * begin()/end() walk slot[0..count).  end() reads count, and a range-for
 * takes end() once, so count is read once, at the start of the loop.  A loop
 * body that adds or removes entries of this table must not use the range. */
template <class T, unsigned N>
struct EntitySlots {
    T            *slot[N] = {};
    unsigned char count   = 0;

    T **begin() { return slot; }
    T **end()   { return slot + count; }
};

/* A kind whose live entities are found by id: slot[id] holds one, and
 * ids[0..count) lists the ids in use.  begin()/end() walk the live objects in
 * ids order, as slot[ids[i]] for i in [0, count).  As with EntitySlots, end()
 * reads count once, at the start of a range-for, and a loop body that adds or
 * removes entries of this table must not use the range.  A loop that needs
 * the id itself indexes ids[] instead. */
template <class T, unsigned N>
struct EntityIdTable {
    T            *slot[N] = {};
    unsigned char count   = 0;
    unsigned char ids[N]  = {};

    class Iterator {
    public:
        Iterator(EntityIdTable *t, unsigned i) : t_(t), i_(i) {}
        T    *operator*() const { return t_->slot[t_->ids[i_]]; }
        Iterator &operator++()  { ++i_; return *this; }
        bool  operator!=(const Iterator &o) const { return i_ != o.i_; }

    private:
        EntityIdTable *t_;
        unsigned       i_;
    };

    Iterator begin() { return Iterator(this, 0); }
    Iterator end()   { return Iterator(this, count); }
};
