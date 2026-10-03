/* The SDL half of the layer, private to src/audiodev. */
#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "audiodev.h"

namespace audiodev {

struct DeviceState {
    SDL_AudioDeviceID id      = 0;
    bool              enable3D = false;  // asked for; SDL cannot spatialise
};

extern LogFn g_log;
#define AD_LOG(...) do { if (::audiodev::g_log) ::audiodev::g_log(__VA_ARGS__); } while (0)

/* A parsed PCM .wav file. */
struct Wav {
    int                  channels = 0;
    int                  sampleRate = 0;
    int                  bitsPerSample = 0;
    std::vector<uint8_t> pcm;

    /* False for a format SDL has no sample type for. */
    bool spec(SDL_AudioSpec *out) const;
};
bool loadWav(const char *path, Wav *out);

/* The master volume, 0..1, applied as the gain of every open device. */
void registerGainTarget(SDL_AudioDeviceID id);
void unregisterGainTarget(SDL_AudioDeviceID id);

/* One sound source: a stream of a wav's samples into a device, once or
 * looping.  The loop is refilled from the stream's own callback, so it needs
 * no thread of ours and no pumping. */
class Voice {
public:
    /* Fails (and is unusable) if the stream cannot be made.  A device of 0
     * opens its own on the default output (music). */
    Voice(SDL_AudioDeviceID device, std::shared_ptr<const Wav> wav);
    ~Voice();
    Voice(const Voice &) = delete;
    Voice &operator=(const Voice &) = delete;

    bool ok() const { return stream_ != NULL; }
    const std::shared_ptr<const Wav> &wav() const { return wav_; }

    /* Starts from the beginning; replaces whatever was queued. */
    void start(bool loop);
    void stop();

    /* True when nothing is left to play. */
    bool idle() const;

private:
    static void SDLCALL refill(void *self, SDL_AudioStream *stream,
                               int additional, int total);

    SDL_AudioStream            *stream_ = NULL;
    std::shared_ptr<const Wav>  wav_;
    std::atomic<bool>           loop_;
    bool                        ownsDevice_ = false;
};

}  // namespace audiodev
