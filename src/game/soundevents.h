/* SoundEvents: the game's PlayEvents (playevents.h), hooked straight to the
 * sound.
 *
 * Its voices belong to sounds, not to entities.  Each sound -- a source kind's
 * sfx and variant, say a catcher's step -- is defined once from the theme as
 * the level's sounds are loaded (levelsounds.cpp, fixedsounds.cpp), with a few
 * voices loaded up front.  A one-shot plays on the sound's next voice that no
 * loop holds, cutting off the oldest one-shot if it must; a loop holds its
 * voice until it stops, and the sound loads another voice if all are held.
 * Which key last played on a voice is kept, so a move or a stop reaches it
 * until another key takes the voice over. */
#pragma once

#include <map>
#include <string>
#include <vector>
#include "playevents.h"

class SoundLibrary;
class SoundVoice;

class SoundEvents : public PlayEvents {
public:
    explicit SoundEvents(SoundLibrary *library);
    ~SoundEvents() override;
    SoundEvents(const SoundEvents &) = delete;
    SoundEvents &operator=(const SoundEvents &) = delete;

    /* The sound a source kind's sfx (and variant) plays: the file, whether it
     * is heard from a place, and how many voices to load now.  Replaces what
     * was defined there before. */
    void define(SoundSource source, Sfx sfx, unsigned char variant, const char *name,
                bool positional, unsigned voices);
    /* No sound there any more: its voices are released. */
    void undefine(SoundSource source, Sfx sfx, unsigned char variant = 0);
    /* Every sound released. */
    void clear();

    void soundPlay(const SoundKey &key, float x, float y, float z) override;
    void soundPlay(const SoundKey &key) override;
    void soundStart(const SoundKey &key, float x, float y, float z) override;
    void soundMove(const SoundKey &key, float x, float y, float z) override;
    void soundStop(const SoundKey &key) override;

private:
    struct Voice {
        SoundVoice *voice;
        SoundKey    owner{};       // the key that last played on it
        bool        owned = false;
        bool        held = false;  // a loop is on it
    };
    struct Sound {
        std::string        name;
        bool               positional = true;
        std::vector<Voice> voices;
        size_t             next = 0;  // where the next one-shot looks from
    };
    struct SoundId {
        SoundSource source;
        Sfx sfx;
        unsigned char variant;
        bool operator<(const SoundId &o) const
        {
            if (source != o.source) return source < o.source;
            if (sfx != o.sfx)       return sfx < o.sfx;
            return variant < o.variant;
        }
    };

    Sound *sound(const SoundKey &key);
    Voice *owned(const SoundKey &key);
    /* A voice for key: the one it has, else one no loop holds, else a new one. */
    Voice *take(Sound &s, const SoundKey &key);
    bool load(Sound &s);
    void release(Sound &s);

    SoundLibrary               *library_;
    std::map<SoundId, Sound>    sounds_;
};
