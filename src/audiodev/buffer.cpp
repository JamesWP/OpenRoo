#include "sdl_internal.h"

namespace audiodev {

struct BufferState {
    std::string             filename;  // owned copy of the path
    std::unique_ptr<Voice>  voice;
    bool                    threeD = false;  // asked for; played as 2D
};

Buffer::Buffer() : state_(new BufferState())
{
}

Buffer::~Buffer()
{
    reset();
    delete state_;
}

void Buffer::reset()
{
    state_->voice.reset();
    state_->filename.clear();
    state_->threeD = false;
}

bool Buffer::isLoaded() const        { return state_->voice != NULL; }
bool Buffer::is3D() const            { return state_->threeD; }
const char *Buffer::filename() const { return state_->filename.c_str(); }

bool Buffer::load(Device &dev, const char *path, bool want3D)
{
    reset();

    if (!dev.isUp() || !path) {
        AD_LOG("audiodev: load: no device or path\n");
        return false;
    }

    auto wav = std::make_shared<Wav>();
    if (!loadWav(path, wav.get())) {
        AD_LOG("audiodev: load: can't parse '%s'\n", path);
        return false;
    }
    std::unique_ptr<Voice> voice(new Voice(dev.state()->id, wav));
    if (!voice->ok()) {
        AD_LOG("audiodev: load: can't create a voice for '%s'\n", path);
        return false;
    }
    state_->voice = std::move(voice);
    state_->filename = path;
    state_->threeD = want3D;
    return true;
}

bool Buffer::duplicate(Device &dev, const Buffer &src)
{
    reset();

    if (!src.state_->voice) return false;

    std::unique_ptr<Voice> voice(new Voice(dev.state()->id, src.state_->voice->wav()));
    if (!voice->ok()) return false;
    state_->voice = std::move(voice);
    state_->filename = src.state_->filename;
    state_->threeD = src.state_->threeD;
    return true;
}

bool Buffer::reload3D(Device &, bool want3D)
{
    if (!state_->voice) return false;
    state_->threeD = want3D;  // nothing to rebuild: it is only a flag here
    return true;
}

bool Buffer::set3DEnabled(bool)
{
    return state_->threeD;
}

void Buffer::setPosition(float, float, float, bool)
{
}

void Buffer::play(bool loop)
{
    if (state_->voice)
        state_->voice->start(loop);
}

void Buffer::stop()
{
    if (state_->voice)
        state_->voice->stop();
}

}  // namespace audiodev
