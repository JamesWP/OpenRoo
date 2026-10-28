/* PlayEvents: what the rules tell the world outside them, as it happens.
 *
 * Calls, not a list: nothing is kept.  The game hooks them straight to the
 * sound (soundevents.h); a caller that wants none of it, such as a look-ahead
 * run of the rules, passes a PlayEvents itself, whose calls do nothing.
 *
 * A sound is named by its source (which kind of object, and its id there,
 * entitylist.h) and which of the source's sounds it is.  A one-shot plays; a
 * loop starts, moves with its source, and stops.  When an entity is destroyed,
 * entityDestroyed() reports it, and its sounds are released. */
#pragma once

enum class SoundSource : unsigned char { Lift, Platform, Bridge, Falling };

/* Each source's sounds. */
enum class Sfx : unsigned char {
    LiftMoving,      // a lift's loop while it rises or falls
    PlatformMoving,  // a platform's while it runs along its track
    BridgeMoving,    // a bridge's while its deck goes out or in
    TileFalls,       // a falling tile's as it falls
    TileRespawns,    // and as it comes back
};

struct SoundKey {
    SoundSource source;
    unsigned    id;
    Sfx         sfx;

    bool operator<(const SoundKey &o) const
    {
        if (source != o.source) return source < o.source;
        if (id != o.id)         return id < o.id;
        return sfx < o.sfx;
    }
};

class PlayEvents {
public:
    virtual ~PlayEvents() = default;

    /* A one-shot plays at (x, y, z). */
    virtual void soundPlay(const SoundKey &, float, float, float) {}
    /* A loop starts at (x, y, z), moves there, stops. */
    virtual void soundStart(const SoundKey &, float, float, float) {}
    virtual void soundMove(const SoundKey &, float, float, float) {}
    virtual void soundStop(const SoundKey &) {}
    /* An entity was destroyed; its sounds are released. */
    virtual void entityDestroyed(SoundSource, unsigned) {}
};
