#include "sdl_internal.h"
#include <algorithm>
#include <math.h>

namespace audiodev {

LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }

static float g_effectsGain = 1.0f;
static float g_musicGain   = 1.0f;
static std::weak_ptr<MixerRef> g_effects, g_music;

MixerRef::MixerRef(const SDL_AudioSpec *spec)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        AD_LOG("audiodev: SDL_INIT_AUDIO failed: %s\n", SDL_GetError());
        return;
    }
    if (!MIX_Init()) {
        AD_LOG("audiodev: MIX_Init failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, spec);
    if (!mixer) {
        AD_LOG("audiodev: MIX_CreateMixerDevice failed: %s\n", SDL_GetError());
        MIX_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

MixerRef::~MixerRef()
{
    if (!mixer) return;
    MIX_DestroyMixer(mixer);
    MIX_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

static std::shared_ptr<MixerRef> shared_mixer(std::weak_ptr<MixerRef> &slot,
                                              const SDL_AudioSpec *spec,
                                              float gain)
{
    std::shared_ptr<MixerRef> m = slot.lock();
    if (m) return m;
    m = std::make_shared<MixerRef>(spec);
    if (!m->mixer) return NULL;
    MIX_SetMixerGain(m->mixer, gain);
    slot = m;
    return m;
}

std::shared_ptr<MixerRef> effectsMixer(const SDL_AudioSpec *spec)
{
    return shared_mixer(g_effects, spec, g_effectsGain);
}

std::shared_ptr<MixerRef> musicMixer()
{
    return shared_mixer(g_music, NULL, g_musicGain);
}

float effectsVolume() { return g_effectsGain; }

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void setEffectsVolume(float gain)
{
    g_effectsGain = clamp01(gain);
    if (std::shared_ptr<MixerRef> m = g_effects.lock())
        MIX_SetMixerGain(m->mixer, g_effectsGain);
}

void setMusicVolume(float gain)
{
    g_musicGain = clamp01(gain);
    if (std::shared_ptr<MixerRef> m = g_music.lock())
        MIX_SetMixerGain(m->mixer, g_musicGain);
}

std::shared_ptr<AudioRef> loadAudio(const std::shared_ptr<MixerRef> &mixer,
                                    const char *path, bool predecode)
{
    std::shared_ptr<AudioRef> a = std::make_shared<AudioRef>();
    a->mixer = mixer;
    a->audio = MIX_LoadAudio(mixer->mixer, path, predecode);
    if (!a->audio) {
        AD_LOG("audiodev: can't load '%s': %s\n", path, SDL_GetError());
        return NULL;
    }
    return a;
}

void playTrack(MIX_Track *track, bool loop)
{
    MIX_StopTrack(track, 0);
    SDL_PropertiesID opts = SDL_CreateProperties();
    SDL_SetNumberProperty(opts, MIX_PROP_PLAY_LOOPS_NUMBER, loop ? -1 : 0);
    if (!MIX_PlayTrack(track, opts))
        AD_LOG("audiodev: play failed: %s\n", SDL_GetError());
    SDL_DestroyProperties(opts);
}

/* The game's listener is in a left-handed world (+z ahead, x = up x front);
 * the mixer's is right-handed with the listener fixed, so a source is moved
 * into the listener's frame.  The rolloff factor scales distance, as the
 * mixer has no rolloff of its own. */
MIX_Point3D Listener::relative(const float w[3]) const
{
    float d[3] = { w[0] - pos[0], w[1] - pos[1], w[2] - pos[2] };
    float right[3] = {
        top[1] * front[2] - top[2] * front[1],
        top[2] * front[0] - top[0] * front[2],
        top[0] * front[1] - top[1] * front[0],
    };
    auto dot = [&](const float *v) { return d[0] * v[0] + d[1] * v[1] + d[2] * v[2]; };
    MIX_Point3D p;
    p.x =  dot(right) * rolloff;
    p.y =  dot(top) * rolloff;
    p.z = -dot(front) * rolloff;
    return p;
}

void Listener::reapply()
{
    for (BufferState *b : buffers)
        b->applyPosition();
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
    return state_->mixer != NULL;
}

void Device::destroy()
{
    state_->mixer.reset();
}

bool Device::create(const DeviceConfig &config)
{
    AD_LOG("audiodev: create(window=%p 3d=%d ch=%d rate=%d bits=%d)\n",
           config.window, (int)config.enable3D, config.channels,
           config.sampleRate, config.bitsPerSample);

    destroy();

    SDL_AudioSpec spec = {};
    spec.format   = config.bitsPerSample == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16;
    spec.channels = config.channels;
    spec.freq     = config.sampleRate;
    state_->mixer = effectsMixer(&spec);
    if (!state_->mixer)
        return false;
    state_->listener->enabled = config.enable3D;
    AD_LOG("audiodev: create OK\n");
    return true;
}

bool Device::set3DEnabled(bool enable)
{
    state_->listener->enabled = enable;
    return isUp();
}

void Device::setListenerPosition(const float pos[3], bool immediate)
{
    std::copy(pos, pos + 3, state_->listener->pos);
    if (immediate) state_->listener->reapply();
}

void Device::setListenerOrientation(const float front[3], const float top[3],
                                    bool immediate)
{
    Listener &l = *state_->listener;
    std::copy(front, front + 3, l.front);
    std::copy(top, top + 3, l.top);
    if (immediate) l.reapply();
}

void Device::setListenerRolloff(float rolloff, bool immediate)
{
    state_->listener->rolloff = rolloff;
    if (immediate) state_->listener->reapply();
}

void Device::commit()
{
    state_->listener->reapply();
}

}  // namespace audiodev
