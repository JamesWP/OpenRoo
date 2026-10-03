#include "sdl_internal.h"

namespace audiodev {

Voice::Voice(SDL_AudioDeviceID device, std::shared_ptr<const Wav> wav)
    : wav_(std::move(wav)), loop_(false)
{
    SDL_AudioSpec spec;
    if (!wav_ || wav_->pcm.empty() || !wav_->spec(&spec)) {
        AD_LOG("audiodev: Voice: unusable wav\n");
        return;
    }
    if (device) {
        stream_ = SDL_CreateAudioStream(&spec, NULL);
        if (stream_ && !SDL_BindAudioStream(device, stream_)) {
            AD_LOG("audiodev: SDL_BindAudioStream failed: %s\n", SDL_GetError());
            SDL_DestroyAudioStream(stream_);
            stream_ = NULL;
        }
    } else {
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                            &spec, NULL, NULL);
        if (stream_) {
            ownsDevice_ = true;
            registerGainTarget(SDL_GetAudioStreamDevice(stream_));
            SDL_ResumeAudioStreamDevice(stream_);
        }
    }
    if (!stream_)
        AD_LOG("audiodev: no audio stream: %s\n", SDL_GetError());
    else
        SDL_SetAudioStreamGetCallback(stream_, refill, this);
}

Voice::~Voice()
{
    if (!stream_) return;
    if (ownsDevice_)
        unregisterGainTarget(SDL_GetAudioStreamDevice(stream_));
    SDL_DestroyAudioStream(stream_);
}

void Voice::start(bool loop)
{
    if (!stream_) return;
    loop_ = false;
    SDL_ClearAudioStream(stream_);
    loop_ = loop;
    SDL_PutAudioStreamData(stream_, wav_->pcm.data(), (int)wav_->pcm.size());
    SDL_FlushAudioStream(stream_);
}

void Voice::stop()
{
    if (!stream_) return;
    loop_ = false;
    SDL_ClearAudioStream(stream_);
}

bool Voice::idle() const
{
    return !stream_ || (SDL_GetAudioStreamQueued(stream_) == 0
                        && SDL_GetAudioStreamAvailable(stream_) == 0);
}

/* Runs on SDL's audio thread, with the stream locked, whenever the device
 * wants more than the stream holds. */
void Voice::refill(void *self, SDL_AudioStream *stream, int additional, int)
{
    Voice *v = static_cast<Voice *>(self);
    for (int i = 0; i < 8 && v->loop_
                    && SDL_GetAudioStreamAvailable(stream) < additional; i++)
        SDL_PutAudioStreamData(stream, v->wav_->pcm.data(),
                               (int)v->wav_->pcm.size());
}

}  // namespace audiodev
