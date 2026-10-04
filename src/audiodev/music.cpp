#include "sdl_internal.h"

namespace audiodev {

/* The track plays on the music mixer, apart from the effects, so each has its
 * own volume; a repeating track loops inside the mixer. */
struct MusicState {
    std::shared_ptr<AudioRef> audio;
    MIX_Track                *track = NULL;
};

Music::Music() : state_(new MusicState())
{
}

Music::~Music()
{
    stop();
    delete state_;
}

void Music::setWindow(void *)
{
}

void Music::play(const char *path, bool repeat)
{
    stop();

    std::shared_ptr<MixerRef> mixer = musicMixer();
    if (!mixer)
        return;
    state_->audio = loadAudio(mixer, path, false);
    if (!state_->audio)
        return;
    state_->track = MIX_CreateTrack(mixer->mixer);
    if (!state_->track || !MIX_SetTrackAudio(state_->track, state_->audio->audio)) {
        AD_LOG("audiodev: Music open failed: %s\n", SDL_GetError());
        stop();
        return;
    }
    playTrack(state_->track, repeat);
}

void Music::stop()
{
    if (state_->track) {
        MIX_DestroyTrack(state_->track);
        state_->track = NULL;
    }
    state_->audio.reset();
}

bool Music::handleWindowMessage(unsigned, unsigned long, long)
{
    return false;
}

}  // namespace audiodev
