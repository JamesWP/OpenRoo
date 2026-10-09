/* What the level's entities need of the game around them, handed to them at
 * spawn so they know nothing of Game: the shared clock, tick step, map and
 * sound manager, and a table per kind for the slots they live in. */
#pragma once

struct TickStep;
class  LevelMap;
class  SoundManager;

struct EntityContext {
    double       *clock;
    TickStep     *tickStep;
    LevelMap     *map;
    SoundManager *sound;
};

/* One kind's slots, in use up to count; N is the most the game allows. */
template <class T, unsigned N>
struct EntitySlots {
    T            *slot[N] = {};
    unsigned char count   = 0;
};
