#include "sdl_internal.h"

namespace audiodev {

/* The whole file in one voice, played once. */
struct StreamState {
    std::unique_ptr<Voice> voice;
    bool                   started = false;
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
    return !state_->started || state_->voice->idle();
}

bool Stream::prepare(Device &dev, const char *path)
{
    AD_LOG("audiodev: Stream::prepare('%s')\n", path ? path : "<null>");

    release();

    if (!path || !dev.isUp())
        return false;

    auto wav = std::make_shared<Wav>();
    if (!loadWav(path, wav.get()))
        return false;
    state_->voice.reset(new Voice(dev.state()->id, wav));
    if (!state_->voice->ok()) {
        release();
        return false;
    }
    return true;
}

void Stream::play()
{
    if (!state_->voice) return;
    state_->voice->start(false);
    state_->started = true;
}

void Stream::stop()
{
    if (state_->voice)
        state_->voice->stop();
    state_->started = false;
}

void Stream::release()
{
    state_->voice.reset();
    state_->started = false;
}

}  // namespace audiodev
