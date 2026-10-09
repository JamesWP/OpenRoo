#include "sdl_internal.h"

namespace audiodev {

/* The whole file on a track of its own, played once. */
struct StreamState {
    std::shared_ptr<AudioRef> audio;
    MIX_Track                *track = NULL;
};

Stream::Stream() : state_(new StreamState())
{
}

Stream::~Stream()
{
    release();
    delete state_;
}

bool Stream::done() const
{
    return !state_->track || !MIX_TrackPlaying(state_->track);
}

bool Stream::prepare(Device &dev, const char *path)
{
    AD_LOG("audiodev: Stream::prepare('%s')\n", path ? path : "<null>");

    release();

    if (!path || !dev.isUp())
        return false;
    if (silent())
        return true;

    state_->audio = loadAudio(dev.state()->mixer, path, true);
    if (!state_->audio)
        return false;
    state_->track = MIX_CreateTrack(dev.state()->mixer->mixer);
    if (!state_->track || !MIX_SetTrackAudio(state_->track, state_->audio->audio)) {
        release();
        return false;
    }
    return true;
}

void Stream::play()
{
    if (state_->track)
        playTrack(state_->track, false);
}

void Stream::stop()
{
    if (state_->track)
        MIX_StopTrack(state_->track, 0);
}

void Stream::release()
{
    if (state_->track) {
        MIX_DestroyTrack(state_->track);
        state_->track = NULL;
    }
    state_->audio.reset();
}

}  // namespace audiodev
