/* PlayEvents: what the rules tell the world outside them, as it happens.
 *
 * Calls, not a list: nothing is kept.  The game hooks them straight to the
 * sound (soundevents.h); a caller that wants none of it, such as a look-ahead
 * run of the rules, passes a PlayEvents itself, whose calls do nothing.
 *
 * A sound is named by its source (which kind of object, and its id there,
 * entitylist.h) and which of the source's sounds it is.  A one-shot plays; a
 * loop starts, moves with its source, and stops.  Whatever is still playing
 * of an entity's when it is removed, the rules stop: a loop left running
 * would run on. */
#pragma once

enum class SoundSource : unsigned char { Lift, Platform, Bridge, Falling, Player, Catcher, Thrower, Bomb, Level };

/* Each source's sounds. */
enum class Sfx : unsigned char {
    LiftMoving,      // a lift's loop while it rises or falls
    PlatformMoving,  // a platform's while it runs along its track
    BridgeMoving,    // a bridge's while its deck goes out or in
    TileFalls,       // a falling tile's as it falls
    TileRespawns,    // and as it comes back
    // A mover's (player, foe, bomb; movableentity.h):
    Step,            // a step taken, or a landing
    Switch,          // coming onto a switch
    Glue,            // caught by a sticky pad
    Slide,           // the loop while on a slide
    Ice,             // the loop while on ice
    Teleport,        // a teleporter taking it
    Teleported,      // and setting it down
    Splat,           // killed by a drop
    Fall,            // a fall, following it down
    Glide,           // the loop while gliding
    GlideStart,      // the paraglider opening
    JumpPad,         // a jump pad's launch, following it up
    Crystal,         // a gem picked up
    BombRoll,        // a bomb's fuse while it rolls
    BombBlast,       // and its blast
    // The player's pickups, each in three variants:
    PickupFreeze,    // the foes frozen
    PickupLife,      // an extra life
    PickupTime,      // five more seconds
    PickupFast,      // speed up
    PickupSlow,      // speed down
    PickupGlider,    // a paraglider
    PickupBombs,     // three bombs
    PickupReversed,  // the controls reversed
    PickupShield,    // protection
    // The level's (id 0), heard everywhere:
    TimeOut,         // the time has run out
    GemsDone,        // the last gem needed, in three takes
    LevelComplete,   // the player home with the gems
};

struct SoundKey {
    SoundSource source;
    unsigned    id;
    Sfx         sfx;
    unsigned char variant = 0;  // which take, of a sound that has several

    bool operator<(const SoundKey &o) const
    {
        if (source != o.source) return source < o.source;
        if (id != o.id)         return id < o.id;
        if (sfx != o.sfx)       return sfx < o.sfx;
        return variant < o.variant;
    }
};

class PlayEvents {
public:
    virtual ~PlayEvents() = default;

    /* A one-shot plays at (x, y, z). */
    virtual void soundPlay(const SoundKey &, float, float, float) {}
    /* A one-shot with no place: heard the same everywhere. */
    virtual void soundPlay(const SoundKey &) {}
    /* A loop starts at (x, y, z), moves there, stops. */
    virtual void soundStart(const SoundKey &, float, float, float) {}
    virtual void soundMove(const SoundKey &, float, float, float) {}
    /* Stops the key's sound, a loop or a one-shot still playing; a move or a
     * stop of a sound not playing does nothing. */
    virtual void soundStop(const SoundKey &) {}
};
