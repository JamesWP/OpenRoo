#include "sdl_internal.h"
#include <algorithm>

namespace audiodev {

BufferState::~BufferState()
{
    release();
}

void BufferState::release()
{
    if (listener) {
        auto &v = listener->buffers;
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
        listener.reset();
    }
    if (track) {
        MIX_DestroyTrack(track);
        track = NULL;
    }
    audio.reset();
    filename.clear();
    threeD = spatial = silentLoaded = false;
}

/* Puts the track where the listener hears it, or takes it out of 3D mode. */
void BufferState::applyPosition()
{
    if (!track) return;
    if (spatial && listener && listener->enabled) {
        MIX_Point3D p = listener->relative(pos);
        MIX_SetTrack3DPosition(track, &p);
    } else {
        MIX_SetTrack3DPosition(track, NULL);
    }
}

Buffer::Buffer() : state_(new BufferState())
{
}

Buffer::~Buffer()
{
    delete state_;
}

void Buffer::reset()                 { state_->release(); }
bool Buffer::isLoaded() const        { return state_->track != NULL || state_->silentLoaded; }
bool Buffer::is3D() const            { return state_->threeD; }
const char *Buffer::filename() const { return state_->filename.c_str(); }

/* Makes a track for audio on dev's mixer; false if that fails. */
static bool attach(BufferState *s, Device &dev, std::shared_ptr<AudioRef> audio,
                   bool want3D)
{
    s->track = MIX_CreateTrack(dev.state()->mixer->mixer);
    if (!s->track || !MIX_SetTrackAudio(s->track, audio->audio)) {
        AD_LOG("audiodev: can't make a track: %s\n", SDL_GetError());
        s->release();
        return false;
    }
    s->audio = std::move(audio);
    s->threeD = s->spatial = want3D;
    if (want3D) {
        s->listener = dev.state()->listener;
        s->listener->buffers.push_back(s);
        s->applyPosition();
    }
    return true;
}

bool Buffer::load(Device &dev, const char *path, bool want3D)
{
    reset();

    if (!dev.isUp() || !path) {
        AD_LOG("audiodev: load: no device or path\n");
        return false;
    }
    if (silent()) {
        state_->silentLoaded = true;
        state_->threeD = state_->spatial = want3D;
        state_->filename = path;
        return true;
    }
    std::shared_ptr<AudioRef> audio = loadAudio(dev.state()->mixer, path, true);
    if (!audio || !attach(state_, dev, audio, want3D))
        return false;
    state_->filename = path;
    return true;
}

bool Buffer::duplicate(Device &dev, const Buffer &src)
{
    reset();

    if (!src.isLoaded() || !dev.isUp()) return false;
    if (silent()) {
        state_->silentLoaded = true;
        state_->threeD = state_->spatial = src.state_->threeD;
        state_->filename = src.state_->filename;
        return true;
    }
    if (!attach(state_, dev, src.state_->audio, src.state_->threeD))
        return false;
    state_->filename = src.state_->filename;
    std::copy(src.state_->pos, src.state_->pos + 3, state_->pos);
    return true;
}

bool Buffer::reload3D(Device &dev, bool want3D)
{
    BufferState *s = state_;
    if (!isLoaded()) return false;
    if (want3D == s->threeD) return true;
    if (silent()) {
        s->threeD = s->spatial = want3D;
        return true;
    }

    // The sound is rebuilt for the new mode, so whatever was playing stops: the
    // game starts its looping sounds again afterwards and forgets these.
    MIX_StopTrack(s->track, 0);
    s->threeD = s->spatial = want3D;
    if (want3D) {
        s->listener = dev.state()->listener;
        s->listener->buffers.push_back(s);
    } else if (s->listener) {
        auto &v = s->listener->buffers;
        v.erase(std::remove(v.begin(), v.end(), s), v.end());
        s->listener.reset();
    }
    s->applyPosition();
    return true;
}

bool Buffer::set3DEnabled(bool enable)
{
    if (!state_->threeD) return false;
    state_->spatial = enable;
    state_->applyPosition();
    return true;
}

void Buffer::setPosition(float x, float y, float z, bool immediate)
{
    state_->pos[0] = x;
    state_->pos[1] = y;
    state_->pos[2] = z;
    if (immediate) state_->applyPosition();
}

void Buffer::play(bool loop)
{
    if (state_->track)
        playTrack(state_->track, loop);
}

void Buffer::stop()
{
    if (state_->track)
        MIX_StopTrack(state_->track, 0);
}

}  // namespace audiodev
