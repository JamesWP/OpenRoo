#include "sdl_internal.h"
#include <algorithm>

namespace audiodev {

static std::vector<SDL_AudioDeviceID> g_gainTargets;
static float g_gain = 1.0f;

void registerGainTarget(SDL_AudioDeviceID id)
{
    g_gainTargets.push_back(id);
    SDL_SetAudioDeviceGain(id, g_gain);
}

void unregisterGainTarget(SDL_AudioDeviceID id)
{
    g_gainTargets.erase(std::remove(g_gainTargets.begin(), g_gainTargets.end(), id),
                        g_gainTargets.end());
}

/* The old system volume was two 16-bit channels packed in a word; SDL has no
 * system volume, so this is this program's own, and balance is dropped. */
unsigned masterVolume()
{
    unsigned v = (unsigned)(g_gain * 0xffff + 0.5f);
    return v | (v << 16);
}

void setMasterVolume(unsigned packed)
{
    g_gain = (float)(((packed & 0xffff) + (packed >> 16)) / 2) / 0xffff;
    for (SDL_AudioDeviceID id : g_gainTargets)
        SDL_SetAudioDeviceGain(id, g_gain);
}

Device::Device() : state_(new DeviceState())
{
}

Device::~Device()
{
    destroy();
    delete state_;
}

bool Device::isUp() const
{
    return state_->id != 0;
}

void Device::destroy()
{
    if (state_->id) {
        unregisterGainTarget(state_->id);
        SDL_CloseAudioDevice(state_->id);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        state_->id = 0;
    }
}

bool Device::create(const DeviceConfig &config)
{
    AD_LOG("audiodev: create(window=%p 3d=%d ch=%d rate=%d bits=%d)\n",
           config.window, (int)config.enable3D, config.channels,
           config.sampleRate, config.bitsPerSample);

    destroy();

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        AD_LOG("audiodev: SDL_INIT_AUDIO failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec = {};
    spec.format   = config.bitsPerSample == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16;
    spec.channels = config.channels;
    spec.freq     = config.sampleRate;
    state_->id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!state_->id) {
        AD_LOG("audiodev: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_ResumeAudioDevice(state_->id);
    registerGainTarget(state_->id);
    state_->enable3D = config.enable3D;
    AD_LOG("audiodev: create OK\n");
    return true;
}

/* SDL has no spatial audio: the listener is accepted and ignored, and every
 * sound plays as a 2D one.  See docs/SDL_PLATFORM.md. */
bool Device::set3DEnabled(bool enable)
{
    state_->enable3D = enable;
    return isUp();
}

void Device::commit() {}
void Device::setListenerPosition(const float *, bool) {}
void Device::setListenerOrientation(const float *, const float *, bool) {}
void Device::setListenerRolloff(float, bool) {}

}  // namespace audiodev
