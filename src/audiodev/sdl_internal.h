/* The SDL_mixer half of the layer, private to src/audiodev. */
#pragma once
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <memory>
#include <string>
#include <vector>
#include "audiodev.h"

namespace audiodev {

extern LogFn g_log;
#define AD_LOG(...) do { if (::audiodev::g_log) ::audiodev::g_log(__VA_ARGS__); } while (0)

/* A mixer on the default output, with SDL and SDL_mixer brought up for it.
 * Mixers are shared: whoever holds one keeps it alive. */
struct MixerRef {
    MIX_Mixer *mixer = NULL;
    explicit MixerRef(const SDL_AudioSpec *spec);
    ~MixerRef();
    MixerRef(const MixerRef &) = delete;
    MixerRef &operator=(const MixerRef &) = delete;
};

/* The two volume groups, each its own mixer: effects (every sound but the
 * music) and music.  Created on first use; the volume set so far applies. */
std::shared_ptr<MixerRef> effectsMixer(const SDL_AudioSpec *spec);
std::shared_ptr<MixerRef> musicMixer();

float effectsVolume();

/* A loaded sound, destroyed only after the tracks that play it. */
struct AudioRef {
    std::shared_ptr<MixerRef> mixer;
    MIX_Audio                *audio = NULL;
    ~AudioRef() { if (audio) MIX_DestroyAudio(audio); }
};
std::shared_ptr<AudioRef> loadAudio(const std::shared_ptr<MixerRef> &mixer,
                                    const char *path, bool predecode);

/* Plays a track from the start, once or looping forever. */
void playTrack(MIX_Track *track, bool loop);

struct BufferState;

/* The listener, shared by the device and the 3D buffers it positions. */
struct Listener {
    float pos[3]   = {0, 0, 0};
    float front[3] = {0, 0, 1};
    float top[3]   = {0, 1, 0};
    float rolloff  = 1.0f;
    bool  enabled  = false;
    std::vector<BufferState *> buffers;  // those that are positional

    /* A point in the world as the mixer's fixed listener (at the origin,
     * facing -z, y up, x to the right) would see it. */
    MIX_Point3D relative(const float world[3]) const;
    void        reapply();
};

struct DeviceState {
    std::shared_ptr<MixerRef> mixer;
    std::shared_ptr<Listener> listener = std::make_shared<Listener>();
};

struct BufferState {
    std::string                filename;
    std::shared_ptr<AudioRef>  audio;
    MIX_Track                 *track = NULL;
    std::shared_ptr<Listener>  listener;
    bool                       threeD  = false;  // loaded as a 3D sound
    bool                       spatial = false;  // and currently positional
    float                      pos[3]  = {0, 0, 0};

    ~BufferState();
    void release();
    void applyPosition();
};

}  // namespace audiodev
