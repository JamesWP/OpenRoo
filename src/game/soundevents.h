/* SoundEvents: the game's PlayEvents (playevents.h), hooked straight to the
 * sound.  It holds each source's sounds, given it as the level's sounds are
 * loaded (levelsounds.cpp), and does what the rules ask of them. */
#pragma once

#include <map>
#include "playevents.h"
#include "themesounds.h"

class GameAudio;
class SoundLibrary;
class SoundVoice;

class SoundEvents : public PlayEvents {
public:
    explicit SoundEvents(GameAudio *audio);

    /* The voice key's sound plays on; released when its entity is destroyed. */
    void attach(const SoundKey &key, SoundVoice *voice);

    void soundPlay(const SoundKey &key, float x, float y, float z) override;
    void soundPlay(const SoundKey &key) override;
    void soundStart(const SoundKey &key, float x, float y, float z) override;
    void soundMove(const SoundKey &key, float x, float y, float z) override;
    void soundStop(const SoundKey &key) override;
    void entityCreated(SoundSource source, unsigned id) override;
    void entityDestroyed(SoundSource source, unsigned id) override;

    /* The voice key's sound plays on, or null. */
    SoundVoice *voice(const SoundKey &key) const;

private:
    void acquire(const SoundKey &key, ThemeSound sound);

    GameAudio                        *audio_;
    SoundLibrary                     *library_;
    std::map<SoundKey, SoundVoice *> voices_;
};
