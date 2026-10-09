/* What the level's entities need of the game around them, handed to them at
 * spawn so they know nothing of Game: the shared clock, tick step, map and
 * sound manager, and a table per kind for the slots they live in. */
#pragma once

struct TickStep;
class  LevelMap;
class  SoundManager;
class  SwitchCells;

struct EntityContext {
    double       *clock;
    TickStep     *tickStep;
    LevelMap     *map;
    SoundManager *sound;
    /* The switch cells and the highest switch slot in use (a view of the
     * Game's, so it follows the level being built). */
    SwitchCells         *switches;
    const unsigned char *switchMax;
    /* The level's name (diagnostics) and the count of foes that drop a
     * crystal, which spawning a foe of that type raises. */
    const char     *levelName;
    unsigned short *crystalFoes;
};

/* One kind's slots, in use up to count; N is the most the game allows. */
template <class T, unsigned N>
struct EntitySlots {
    T            *slot[N] = {};
    unsigned char count   = 0;
};

/* A kind whose live entities are found by id: slot[id] holds one, and
 * ids[0..count) lists the ids in use. */
template <class T, unsigned N>
struct EntityIdTable {
    T            *slot[N] = {};
    unsigned char count   = 0;
    unsigned char ids[N]  = {};
};
