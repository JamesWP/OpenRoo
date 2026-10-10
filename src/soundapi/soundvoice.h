/* The sound interfaces the game objects use: they hold and play sounds
 * through these, and know nothing of the audio layer that implements them. */
#pragma once

/* A sound a game object holds and plays: one buffer, or a pool of voices
 * that take turns.  audiodev::Buffer and VoicePool implement it. */
class SoundVoice {
public:
    virtual void setPosition(float x, float y, float z, bool immediate = true) = 0;
    virtual void play(bool loop) = 0;
    virtual void stop() = 0;
protected:
    virtual ~SoundVoice() = default;
};

/* Where game objects get and give back their sounds; SoundManager implements it. */
class SoundLibrary {
public:
    virtual bool        active() const = 0;          // sound is up (SoundManager::created())
    virtual SoundVoice *acquireVoice(const char *name, bool want3D) = 0;   // acquireStatic
    virtual void        releaseVoice(SoundVoice *v, bool destroyIfUnused) = 0;
protected:
    virtual ~SoundLibrary() = default;
};
